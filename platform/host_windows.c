/*
 * host_windows.c: the host layer on Windows (x86-64, MinGW-w64), the calls
 * made in the ordinary course of a run: the pages mapped at fixed addresses
 * (host_memory.h), the image's and the main stack's bounds (host_image.h),
 * the files (host_file.h), and the executable's path, its restart and its
 * end (host_process.h). The faults are host_windows_fault.c's, the stack switch
 * host_windows_stack.c's, where the link put the regions host_linked.c's.
 *
 * Followed: Microsoft's documentation of VirtualAlloc, VirtualFree and
 * VirtualQuery (memoryapi.h), GetCurrentThreadStackLimits,
 * CreateProcessW and TerminateProcess (processthreadsapi.h), WriteFile,
 * FlushFileBuffers, MoveFileExW, LockFileEx, UnlockFileEx and
 * CreateDirectoryW (fileapi.h, winbase.h),
 * MultiByteToWideChar and WideCharToMultiByte (stringapiset.h),
 * GetModuleFileNameW, and FormatMessage for the reports; msvcrt's _open,
 * _get_osfhandle and _localtime64_s; the PE format's section flags through
 * gba_game.pe.ld's layout; "File path formats on Windows systems" for a
 * path's syntax.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <io.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>

#include <windows.h>

#include "host_file.h"
#include "host_image.h"
#include "host_memory.h"
#include "host_process.h"

/* --- reports ---------------------------------------------------------------------- */

/* Room for the system's text of an error code. */
#define WINDOWS_ERROR_TEXT_SIZE 256u

/* The system's text for a GetLastError code, without its line break, or
 * "unknown error" if it has none. */
static const char *windows_error_text(DWORD error, char *text, DWORD size)
{
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0,
                                        text, size, NULL);
    DWORD end = length;
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' || text[end - 1] == '.'))
        end--;
    if (end == 0)
        return "unknown error";
    text[end] = '\0';
    return text;
}

/* "<subsystem>: <call> at 0x<address> failed: <the system's text> (Windows
 * error <code>)", for the call that just failed. */
static void report_memory_failure(const char *call, uintptr_t address)
{
    const DWORD error = GetLastError();
    char text[WINDOWS_ERROR_TEXT_SIZE];
    (void)fprintf(stderr, "host_memory: %s at 0x%08" PRIXPTR " failed: %s (Windows error %lu)\n", call, address,
                  windows_error_text(error, text, sizeof text), (unsigned long)error);
}

/* --- host_memory: pages mapped at run time ---------------------------------------- */

/* VirtualAlloc reserves address space in units of the allocation granularity,
 * 64 KiB on every Windows so far (GetSystemInfo's dwAllocationGranularity),
 * and rounds a reservation's address down to a multiple of it: an address
 * that is not a multiple of 64 KiB comes back elsewhere and is refused, and a
 * mapping keeps the rest of its 64 KiB unit from any other. MEM_RESERVE
 * fails if any page of the range is reserved already, which is the refusal
 * to map over a mapping (Linux: MAP_FIXED_NOREPLACE). */
int host_memory_map_fixed(uintptr_t address, size_t size)
{
    void *const wanted = (void *)address;
    void *const got = VirtualAlloc(wanted, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (got == NULL) {
        report_memory_failure("VirtualAlloc", address);
        return -1;
    }
    if (got != wanted) {
        (void)fprintf(stderr,
                      "host_memory: VirtualAlloc at 0x%08" PRIXPTR " returned %p (not a multiple of the 64 KiB "
                      "allocation granularity)\n",
                      address, got);
        if (!VirtualFree(got, 0, MEM_RELEASE))
            report_memory_failure("VirtualFree", (uintptr_t)got);
        return -1;
    }
    return 0;
}

/* VirtualFree releases a whole reservation, never part of one, so the size
 * must be the one host_memory_map_fixed was given (whole pages, as both
 * calls take it); anything else is refused rather than half done. */
int host_memory_unmap_fixed(uintptr_t address, size_t size)
{
    void *const base = (void *)address;
    MEMORY_BASIC_INFORMATION mapping;
    if (VirtualQuery(base, &mapping, sizeof mapping) != sizeof mapping) {
        report_memory_failure("VirtualQuery", address);
        return -1;
    }
    if (mapping.AllocationBase != base || mapping.RegionSize != size) {
        (void)fprintf(stderr,
                      "host_memory: 0x%08" PRIXPTR " is not a mapping of %zu bytes made by host_memory_map_fixed\n",
                      address, size);
        return -1;
    }
    if (!VirtualFree(base, 0, MEM_RELEASE)) {
        report_memory_failure("VirtualFree", address);
        return -1;
    }
    return 0;
}

/* --- host_image ------------------------------------------------------------------------ */

/* gba_game.pe.ld's start of the code, where the program's image begins above
 * the GBA layout; MinGW's default script's end of the zero-initialised data;
 * and its start of the initialised data, which gba_game.pe.ld moves after
 * every read-only section (check_pe_layout.py checks the order on each link),
 * so that from it to the end everything is writable. */
extern const unsigned char __text_start__[], __data_start__[], __bss_end__[];

struct host_span host_image_whole(void)
{
    return host_span_of(__text_start__, __bss_end__);
}

struct host_span host_image_writable(void)
{
    return host_span_of(__data_start__, __bss_end__);
}

/* The low byte of a page's protection is its access; the bits above it
 * (PAGE_GUARD, PAGE_NOCACHE, PAGE_WRITECOMBINE) modify it (winnt.h). */
#define PAGE_ACCESS_MASK 0xFFu

/* The page's own protection, which the loader set from its section's flags:
 * the game's initialised data (.gamedat) lies below __data_start__ and is
 * writable, so the bounds alone cannot say. Copy-on-write pages, which the
 * image's writable sections are until first written, count as writable. */
bool host_image_is_read_only(uintptr_t address)
{
    const struct host_span image = host_image_whole();
    MEMORY_BASIC_INFORMATION page;
    if (address < image.start || address >= image.end
        || VirtualQuery((const void *)address, &page, sizeof page) != sizeof page)
        return false;
    const DWORD access = page.Protect & PAGE_ACCESS_MASK;
    return page.State == MEM_COMMIT && (page.Protect & PAGE_GUARD) == 0
           && (access == PAGE_READONLY || access == PAGE_EXECUTE_READ);
}

/* The thread's whole stack reservation, from the thread's environment block
 * (Windows 8 and later), not just its committed depth: 2 MiB for the main
 * thread, the image's SizeOfStackReserve (MinGW's default), against Linux's
 * 8 MiB. */
int host_image_main_stack(struct host_span *out)
{
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    if (low == 0 || high <= low) {
        (void)fprintf(stderr, "host_image: GetCurrentThreadStackLimits returned an unusable stack\n");
        return -1;
    }
    *out = (struct host_span){.start = (uintptr_t)low, .end = (uintptr_t)high};
    return 0;
}

/* --- errno for what the system says ---------------------------------------------- */

/* The errno a POSIX call would set for a GetLastError code, for callers that
 * report strerror(errno) (host_file.h): the codes the file and process calls
 * give for causes a caller can act on; anything else is an input/output
 * error. */
static int errno_of(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return EACCES;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_INVALID_HANDLE:
        return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return ENOMEM;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        return ENOSPC;
    case ERROR_FILENAME_EXCED_RANGE:
        return ENAMETOOLONG;
    case ERROR_NOT_SAME_DEVICE:
        return EXDEV;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
        return EPIPE;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
        return EINVAL;
    case ERROR_BAD_EXE_FORMAT:
        return ENOEXEC;
    default:
        return EIO;
    }
}

/* --- text: the C runtime's narrow strings and the system's wide ones ----------------- */

/* The platform's narrow strings are UTF-8, as on Linux. The executable's
 * manifest makes UTF-8 the process's code page (build_glue/
 * windows_manifest.xml; docs/DECISIONS.md, "Narrow strings on Windows are
 * UTF-8"), so the C runtime's narrow strings, main's argv and the paths
 * fopen and _open take, are UTF-8 too, as are the system's "A" calls'. The
 * wide calls take UTF-16, converted here with CP_UTF8 named outright, not
 * CP_ACP, as Microsoft's "Use UTF-8 code pages in Windows apps" recommends
 * (CP_ACP is UTF-8 only where the manifest is honoured, Windows 10 version
 * 1903 on). Invalid input either way (bytes that are not UTF-8, a lone
 * surrogate) is refused rather than altered. */

/* text in UTF-16 in out (capacity characters, its NUL included). False with
 * errno set: ENAMETOOLONG if it does not fit, EILSEQ if it is not UTF-8. No
 * allocation. */
static bool widen(const char *text, wchar_t *out, int capacity)
{
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, capacity) > 0)
        return true;
    errno = GetLastError() == ERROR_INSUFFICIENT_BUFFER ? ENAMETOOLONG : EILSEQ;
    return false;
}

/* text in UTF-8 in out (size bytes, its NUL included). False with errno set
 * as widen's (EILSEQ: a lone surrogate). For CP_UTF8 the default-character
 * arguments must be NULL (WideCharToMultiByte). */
static bool narrow(const wchar_t *text, char *out, size_t size)
{
    const int capacity = size > INT_MAX ? INT_MAX : (int)size;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, out, capacity, NULL, NULL) > 0)
        return true;
    errno = GetLastError() == ERROR_INSUFFICIENT_BUFFER ? ENAMETOOLONG : EILSEQ;
    return false;
}

/* --- host_file --------------------------------------------------------------------- */

int host_file_open(const char *path, int flags)
{
    return _open(path, flags | _O_NOINHERIT | _O_BINARY, _S_IREAD | _S_IWRITE);
}

/* The system's handle behind a C runtime descriptor. _get_osfhandle reads the
 * runtime's table without its lock (_write would take the descriptor's), and
 * gives the handle as an integer, which is turned back into one here. It sets
 * errno for a descriptor that is not open, so a handler never calls it: see
 * host_file_for_handler. */
static HANDLE handle_of(int descriptor)
{
    const intptr_t handle = _get_osfhandle(descriptor);
    return handle == -1 ? INVALID_HANDLE_VALUE : (HANDLE)handle;
}

/* WriteFile on the handle: the C runtime's text-mode newline translation is
 * skipped too, which the crash reports' "\n" does not need and the platform's
 * files (binary, host_file_open) do not have. */
int host_file_write_all(int descriptor, const void *bytes, size_t size)
{
    HANDLE handle = handle_of(descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    const unsigned char *next = bytes;
    while (size > 0) {
        const DWORD chunk = size > MAXDWORD ? MAXDWORD : (DWORD)size;
        DWORD written = 0;
        if (!WriteFile(handle, next, chunk, &written, NULL)) {
            errno = errno_of(GetLastError());
            return -1;
        }
        if (written == 0) { /* a device that takes nothing: an error, not a loop */
            errno = EIO;
            return -1;
        }
        next += written;
        size -= written;
    }
    return 0;
}

host_handler_file host_file_for_handler(int descriptor)
{
    return (host_handler_file)handle_of(descriptor);
}

/* The handle was resolved at install time, so nothing of the C runtime runs
 * here: no _get_osfhandle, whose error path sets errno through msvcrt's
 * _errno, and no GetLastError. */
bool host_file_write_from_handler(host_handler_file file, const void *bytes, size_t size)
{
    HANDLE handle = (HANDLE)file;
    const unsigned char *next = bytes;
    while (handle != INVALID_HANDLE_VALUE && size > 0) {
        const DWORD chunk = size > MAXDWORD ? MAXDWORD : (DWORD)size;
        DWORD written = 0;
        if (!WriteFile(handle, next, chunk, &written, NULL) || written == 0)
            break;
        next += written;
        size -= written;
    }
    return size == 0;
}

int host_file_sync(int descriptor)
{
    HANDLE handle = handle_of(descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    if (!FlushFileBuffers(handle)) {
        errno = errno_of(GetLastError());
        return -1;
    }
    return 0;
}

/* The C runtime's rename refuses an existing target on Windows. MoveFileExW
 * with MOVEFILE_REPLACE_EXISTING renames over it in one step on a volume; on
 * NTFS, which journals the rename, path then names the old file or the new
 * one, though the documentation does not promise it for every file system (a
 * FAT32 or exFAT volume can lose the target in a crash). MOVEFILE_WRITE_THROUGH
 * returns only once the rename is on the disk. */
int host_file_replace(const char *from, const char *path)
{
    wchar_t wide_from[HOST_FILE_PATH_SIZE];
    wchar_t wide_path[HOST_FILE_PATH_SIZE];
    if (!widen(from, wide_from, HOST_FILE_PATH_SIZE) || !widen(path, wide_path, HOST_FILE_PATH_SIZE))
        return -1;
    if (!MoveFileExW(wide_from, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        errno = errno_of(GetLastError());
        return -1;
    }
    return 0;
}

/* No directory sync on Windows: no call is documented to write a directory's
 * entries through (FlushFileBuffers takes a file, a volume or a pipe), and the
 * rename that needs it is written through by host_file_replace itself. The
 * length is still checked, as the header promises. */
int host_file_sync_directory_of(const char *path)
{
    if (strlen(path) >= HOST_FILE_PATH_SIZE) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

/* LockFileEx and UnlockFileEx take a range: this one is every byte a file
 * could have, from offset 0 (the OVERLAPPED's, zeroed) for 2^64 - 1 bytes,
 * the length split into its low and high halves. A range may run past the
 * end of the file. */
#define WHOLE_FILE_LOW MAXDWORD
#define WHOLE_FILE_HIGH MAXDWORD

int host_file_take_lock(int descriptor)
{
    HANDLE handle = handle_of(descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        return -1;
    }
    OVERLAPPED from_the_start;
    memset(&from_the_start, 0, sizeof from_the_start);
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, WHOLE_FILE_LOW, WHOLE_FILE_HIGH,
                    &from_the_start)) {
        const DWORD error = GetLastError();
        errno = error == ERROR_LOCK_VIOLATION ? EWOULDBLOCK : errno_of(error);
        return -1;
    }
    return 0;
}

int host_file_release_lock(int descriptor)
{
    HANDLE handle = handle_of(descriptor);
    OVERLAPPED from_the_start;
    memset(&from_the_start, 0, sizeof from_the_start);
    int unlock_error = 0;
    if (handle == INVALID_HANDLE_VALUE)
        unlock_error = EBADF;
    else if (!UnlockFileEx(handle, 0, WHOLE_FILE_LOW, WHOLE_FILE_HIGH, &from_the_start))
        unlock_error = errno_of(GetLastError());
    if (_close(descriptor) != 0) /* closing drops the lock too, if more slowly: done whether or not the unlock was */
        return -1;
    if (unlock_error != 0) {
        errno = unlock_error;
        return -1;
    }
    return 0;
}

int host_file_make_directory(const char *path)
{
    wchar_t wide_path[HOST_FILE_PATH_SIZE];
    if (!widen(path, wide_path, HOST_FILE_PATH_SIZE))
        return -1;
    if (!CreateDirectoryW(wide_path, NULL)) {
        errno = errno_of(GetLastError());
        return -1;
    }
    return 0;
}

/* msvcrt's 64-bit conversion, which follows TZ (_tzset) or else the system's
 * time zone, and takes 1970-01-01 to 3000-12-31 only. */
int host_file_local_time(int64_t seconds, struct tm *out)
{
    const __time64_t when = seconds;
    const errno_t error = _localtime64_s(out, &when);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return 0;
}

/* Microsoft, "File path formats on Windows systems": either slash separates,
 * and a path may start with a drive, a letter and a colon. */
#define DRIVE_PREFIX_LENGTH 2u

static bool is_path_separator(char c)
{
    return c == '/' || c == '\\';
}

static bool starts_with_a_drive(const char *path)
{
    return ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':';
}

size_t host_file_name_offset(const char *path)
{
    size_t offset = starts_with_a_drive(path) ? DRIVE_PREFIX_LENGTH : 0u;
    for (size_t i = offset; path[i] != '\0'; i++) {
        if (is_path_separator(path[i]))
            offset = i + 1u;
    }
    return offset;
}

bool host_file_path_is_absolute(const char *path)
{
    return is_path_separator(path[0]) || (starts_with_a_drive(path) && is_path_separator(path[DRIVE_PREFIX_LENGTH]));
}

/* --- host_process ------------------------------------------------------------------ */

int host_process_executable_path(char *out, size_t size)
{
    wchar_t path[HOST_PROCESS_PATH_SIZE];
    const DWORD length = GetModuleFileNameW(NULL, path, HOST_PROCESS_PATH_SIZE);
    if (length == 0) {
        const DWORD error = GetLastError();
        char text[WINDOWS_ERROR_TEXT_SIZE];
        (void)fprintf(stderr, "host_process: GetModuleFileNameW failed: %s (Windows error %lu)\n",
                      windows_error_text(error, text, sizeof text), (unsigned long)error);
        return -1;
    }
    if (length >= HOST_PROCESS_PATH_SIZE) { /* a full buffer is a path cut short (ERROR_INSUFFICIENT_BUFFER) */
        (void)fprintf(stderr, "host_process: the executable's path is %u characters or longer\n",
                      HOST_PROCESS_PATH_SIZE);
        return -1;
    }
    if (!narrow(path, out, size)) {
        (void)fprintf(stderr, "host_process: the executable's path %s\n",
                      errno == ENAMETOOLONG ? "does not fit the buffer" : "is not valid UTF-16");
        return -1;
    }
    return 0;
}

/* CreateProcessW's command line, at most 32,767 characters with its NUL
 * (Microsoft, CreateProcessW). */
#define COMMAND_LINE_SIZE 32767u

/* What the restarted process ends this one with if its status cannot be read
 * (EXIT_FAILURE, which the test runner reads as a failed run). */
#define RESTART_STATUS_LOST 1u

/* The restart's state, static for its size (two command lines of 64 KiB).
 * The main thread's: the replay's restart is its only caller. */
static struct {
    wchar_t executable[HOST_PROCESS_PATH_SIZE];
    wchar_t argument[COMMAND_LINE_SIZE];
    wchar_t command_line[COMMAND_LINE_SIZE];
    size_t length;
} restart;

/* Appends count copies of c to the command line. False when full. */
static bool append(wchar_t c, size_t count)
{
    if (count >= COMMAND_LINE_SIZE - restart.length)
        return false;
    for (size_t i = 0; i < count; i++)
        restart.command_line[restart.length++] = c;
    restart.command_line[restart.length] = L'\0';
    return true;
}

/* Appends one argument as the C runtime's parser (and CommandLineToArgvW)
 * reads it back (Microsoft, "Parsing C command-line arguments"): bare if it
 * is not empty and holds no space, tab, newline, vertical tab or quote;
 * otherwise in quotes, where a quote is escaped by a backslash, the
 * backslashes before it doubled, as are those before the closing quote. */
static bool append_argument(const wchar_t *argument)
{
    if (argument[0] != L'\0' && wcspbrk(argument, L" \t\n\v\"") == NULL) {
        for (const wchar_t *c = argument; *c != L'\0'; c++) {
            if (!append(*c, 1))
                return false;
        }
        return true;
    }
    if (!append(L'"', 1))
        return false;
    for (const wchar_t *c = argument;; c++) {
        size_t backslashes = 0;
        while (*c == L'\\') {
            backslashes++;
            c++;
        }
        if (*c == L'\0')
            return append(L'\\', 2 * backslashes) && append(L'"', 1);
        const bool escaped = *c == L'"';
        if (!append(L'\\', escaped ? 2 * backslashes + 1 : backslashes) || !append(*c, 1))
            return false;
    }
}

/* argv, converted and quoted, separated by spaces. False with errno set. */
static bool build_command_line(char *const argv[])
{
    restart.length = 0;
    restart.command_line[0] = L'\0';
    for (size_t i = 0; argv[i] != NULL; i++) {
        if (!widen(argv[i], restart.argument, COMMAND_LINE_SIZE))
            return false;
        if ((i > 0 && !append(L' ', 1)) || !append_argument(restart.argument)) {
            errno = E2BIG;
            return false;
        }
    }
    return true;
}

/* A job that ends its processes when its last handle closes, which this
 * process holds alone (not inherited) and the system closes when this process
 * ends, however it ends: so that a restarted program never outlives the
 * process waiting for it (a time limit that kills that process kills the
 * whole chain), as an exec'd program cannot outlive itself on Linux. NULL
 * with errno set if it cannot be made. */
static HANDLE make_kill_on_close_job(void)
{
    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) {
        errno = errno_of(GetLastError());
        return NULL;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    memset(&limits, 0, sizeof limits);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        errno = errno_of(GetLastError());
        (void)CloseHandle(job);
        return NULL;
    }
    return job;
}

/* The standard handles the child is given, made inheritable: the only
 * handles it inherits (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), so that a file
 * this process has open (the C runtime's fopen makes inheritable handles,
 * without FILE_SHARE_DELETE) is not held open in the child, where it would
 * refuse the child's own replacement of that file. of_kind holds each
 * kind's handle, or NULL where this process has none or it could not be made
 * inheritable (the child goes without it); list holds the distinct ones,
 * count of them, as the attribute takes them. */
enum { STANDARD_INPUT, STANDARD_OUTPUT, STANDARD_ERROR, STANDARD_HANDLE_COUNT };

struct standard_handles {
    HANDLE of_kind[STANDARD_HANDLE_COUNT];
    HANDLE list[STANDARD_HANDLE_COUNT];
    size_t count;
};

static void find_standard_handles(struct standard_handles *found)
{
    static const DWORD kinds[STANDARD_HANDLE_COUNT] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    found->count = 0;
    for (size_t i = 0; i < STANDARD_HANDLE_COUNT; i++) {
        HANDLE handle = GetStdHandle(kinds[i]);
        found->of_kind[i] = NULL;
        if (handle == NULL || handle == INVALID_HANDLE_VALUE)
            continue;
        bool listed = false;
        for (size_t j = 0; j < found->count && !listed; j++)
            listed = found->list[j] == handle;
        if (!listed && !SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            continue;
        if (!listed)
            found->list[found->count++] = handle;
        found->of_kind[i] = handle;
    }
}

/* Room for a process attribute list of one attribute (about 48 bytes on
 * x86-64; InitializeProcThreadAttributeList says how much it needs). */
#define ATTRIBUTE_LIST_SIZE 256u

/* Starts the command line built in `restart` as a suspended child that
 * inherits the standard handles only. The extended STARTUPINFOEXW, its size
 * in cb and EXTENDED_STARTUPINFO_PRESENT go together, with the handle list;
 * with no standard handle to give, the plain STARTUPINFOW and no
 * inheritance. False with errno set. */
static bool create_child(PROCESS_INFORMATION *child)
{
    static _Alignas(16) unsigned char attribute_list[ATTRIBUTE_LIST_SIZE];
    struct standard_handles handles;
    find_standard_handles(&handles);
    const bool inherits = handles.count > 0;
    STARTUPINFOEXW startup;
    memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = inherits ? sizeof startup : sizeof startup.StartupInfo;
    if (inherits) {
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = handles.of_kind[STANDARD_INPUT];
        startup.StartupInfo.hStdOutput = handles.of_kind[STANDARD_OUTPUT];
        startup.StartupInfo.hStdError = handles.of_kind[STANDARD_ERROR];
        startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)attribute_list; /* an opaque buffer the calls fill */
        SIZE_T list_size = sizeof attribute_list;
        if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &list_size)) {
            errno = errno_of(GetLastError());
            return false;
        }
        if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.list,
                                       handles.count * sizeof handles.list[0], NULL, NULL)) {
            errno = errno_of(GetLastError());
            DeleteProcThreadAttributeList(startup.lpAttributeList);
            return false;
        }
    }
    const DWORD flags = CREATE_SUSPENDED | (inherits ? EXTENDED_STARTUPINFO_PRESENT : 0u);
    const bool created = CreateProcessW(restart.executable, restart.command_line, NULL, NULL, inherits, flags, NULL,
                                        NULL, &startup.StartupInfo, child);
    if (!created)
        errno = errno_of(GetLastError());
    if (inherits)
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    return created;
}

/* Puts the suspended child in the job and lets it run; it never runs outside
 * the job. On a failure, ends it. False with errno set. */
static bool run_in_job(HANDLE job, const PROCESS_INFORMATION *child)
{
    if (AssignProcessToJobObject(job, child->hProcess) && ResumeThread(child->hThread) != (DWORD)-1)
        return true;
    errno = errno_of(GetLastError());
    (void)TerminateProcess(child->hProcess, RESTART_STATUS_LOST);
    (void)CloseHandle(child->hThread);
    (void)CloseHandle(child->hProcess);
    return false;
}

/* Windows has no exec: the same program is started as a child with the same
 * arguments, standard handles, environment and working directory, and this
 * process waits for it and ends with its status, so that whoever waits for
 * this process (the test runner, the shell) sees the child's end and its
 * output on the same handles. Unlike execve, the child has a process id of
 * its own and this process stays until it ends; the child is in a job that
 * ends it if this process is ended first (make_kill_on_close_job), and it
 * inherits the standard handles and nothing else (create_child).
 *
 * The process ends with TerminateProcess, not ExitProcess: no DLL is told to
 * detach and no C runtime buffer is flushed (execve, too, drops what was not
 * flushed). No crash handler calls this: CreateProcessW allocates, from a
 * heap a crash may have corrupted (host_process_restart_is_handler_safe;
 * crash_reboot ends the process for its parent to start again instead). */
int host_process_restart(char *const argv[])
{
    const DWORD length = GetModuleFileNameW(NULL, restart.executable, HOST_PROCESS_PATH_SIZE);
    const bool found = length > 0 && length < HOST_PROCESS_PATH_SIZE;
    if (!found)
        errno = length == 0 ? errno_of(GetLastError()) : ENAMETOOLONG;
    HANDLE job = found && build_command_line(argv) ? make_kill_on_close_job() : NULL;
    PROCESS_INFORMATION child;
    const bool started = job != NULL && create_child(&child) && run_in_job(job, &child);
    if (!started) {
        if (job != NULL)
            (void)CloseHandle(job);
        return -1;
    }
    (void)CloseHandle(child.hThread);
    DWORD status = RESTART_STATUS_LOST;
    if (WaitForSingleObject(child.hProcess, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(child.hProcess, &status))
        status = RESTART_STATUS_LOST;
    host_process_end(status); /* the job's handle closes with the process */
}

bool host_process_restart_is_handler_safe(void)
{
    return false;
}

/* TerminateProcess, a system call that neither allocates nor takes a lock of
 * this process's. */
void host_process_end(unsigned status)
{
    (void)TerminateProcess(GetCurrentProcess(), status);
    for (;;) /* not reached: TerminateProcess ends the calling thread with its process */
        Sleep(INFINITE);
}
