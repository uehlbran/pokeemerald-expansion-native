/*
 * host_windows.c: the host layer on Windows (x86-64, MinGW-w64), the calls
 * made in the ordinary course of a run: the pages mapped at fixed addresses
 * (host_memory.h), the image's and the main stack's bounds (host_image.h),
 * the files (host_file.h), and the executable's path and its restart
 * (host_process.h). The faults are host_windows_fault.c's, where the link put
 * the regions host_linked.c's. The stack switch (host_stack.h) is a stub
 * until task W7 writes it: it returns -1 and prints nothing.
 *
 * Followed: Microsoft's documentation of VirtualAlloc, VirtualFree and
 * VirtualQuery (memoryapi.h), GetCurrentThreadStackLimits,
 * CreateProcessW and TerminateProcess (processthreadsapi.h), WriteFile,
 * FlushFileBuffers, MoveFileExW and CreateDirectoryW (fileapi.h, winbase.h),
 * MultiByteToWideChar and WideCharToMultiByte (stringapiset.h),
 * GetModuleFileNameW, and FormatMessage for the reports; msvcrt's _open,
 * _get_osfhandle and _localtime64_s; the PE format's section flags through
 * gba_game.pe.ld's layout.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <io.h>
#include <limits.h>
#include <stdatomic.h>
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
#include "host_stack.h"

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

/* Pointers to integers: the image's bounds are compared with addresses. */
static struct host_span span_of(const unsigned char *start, const unsigned char *end)
{
    return (struct host_span){.start = (uintptr_t)start, .end = (uintptr_t)end};
}

struct host_span host_image_whole(void)
{
    return span_of(__text_start__, __bss_end__);
}

struct host_span host_image_writable(void)
{
    return span_of(__data_start__, __bss_end__);
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

/* The C runtime's narrow strings, main's argv and the paths fopen and _open
 * take, are in the ANSI code page: msvcrt converts them with it, as the
 * system's "A" calls do. The wide calls take UTF-16. A string the code page
 * cannot carry either way is refused, never altered. */

/* text in UTF-16 in out (capacity characters, its NUL included). False with
 * errno set: ENAMETOOLONG if it does not fit, EILSEQ if it is not text in the
 * code page. No allocation. */
static bool widen(const char *text, wchar_t *out, int capacity)
{
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, out, capacity) > 0)
        return true;
    errno = GetLastError() == ERROR_INSUFFICIENT_BUFFER ? ENAMETOOLONG : EILSEQ;
    return false;
}

/* text in the ANSI code page in out (size bytes, its NUL included). False with
 * errno set as widen's. The flags that refuse a lossy conversion are not
 * allowed when the code page is UTF-8 (Windows' optional "beta: use Unicode
 * UTF-8" setting), where invalid input is the only loss. */
static bool narrow(const wchar_t *text, char *out, size_t size)
{
    const bool utf8 = GetACP() == CP_UTF8;
    const int capacity = size > INT_MAX ? INT_MAX : (int)size;
    BOOL lossy = FALSE;
    const int written = WideCharToMultiByte(CP_ACP, utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS, text, -1, out,
                                            capacity, NULL, utf8 ? NULL : &lossy);
    if (written > 0 && !lossy)
        return true;
    errno = written == 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER ? ENAMETOOLONG : EILSEQ;
    return false;
}

/* --- host_file --------------------------------------------------------------------- */

int host_file_open(const char *path, int flags)
{
    return _open(path, flags | _O_NOINHERIT | _O_BINARY, _S_IREAD | _S_IWRITE);
}

/* The system's handle behind a C runtime descriptor. _get_osfhandle reads the
 * runtime's table without its lock (_write would take the descriptor's), and
 * gives the handle as an integer, which is turned back into one here. */
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
    const HANDLE handle = handle_of(descriptor);
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

int host_file_sync(int descriptor)
{
    const HANDLE handle = handle_of(descriptor);
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
 * with MOVEFILE_REPLACE_EXISTING renames over it in one step on a volume
 * (NTFS journals the rename), so path names the old file or the new one;
 * MOVEFILE_WRITE_THROUGH returns only once the rename is on the disk. */
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
    if (length >= HOST_PROCESS_PATH_SIZE) { /* GetModuleFileNameW truncates silently */
        (void)fprintf(stderr, "host_process: the executable's path is %u characters or longer\n",
                      HOST_PROCESS_PATH_SIZE);
        return -1;
    }
    if (!narrow(path, out, size)) {
        (void)fprintf(stderr, "host_process: the executable's path %s\n",
                      errno == ENAMETOOLONG ? "does not fit the buffer"
                                            : "has characters the ANSI code page cannot hold");
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

/* The restart's state. Static, not on the stack: host_process_restart may run
 * in the crash handler on the little stack a stack overflow leaves
 * (host_windows_fault.c). `running` lets one thread restart; the buffers are
 * that thread's while it is set. */
static struct {
    atomic_flag running;
    wchar_t executable[HOST_PROCESS_PATH_SIZE];
    wchar_t argument[COMMAND_LINE_SIZE];
    wchar_t command_line[COMMAND_LINE_SIZE];
    size_t length;
} restart = {.running = ATOMIC_FLAG_INIT};

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

/* Windows has no exec: the same program is started as a child with the same
 * arguments, standard handles, environment and working directory, and this
 * process waits for it and ends with its status, so that whoever waits for
 * this process (the test runner, the shell) sees the child's end and its
 * output on the same handles. Unlike execve, the child has a process id of
 * its own and this process stays until it ends.
 *
 * Called from the crash handler too (crash_reboot), where CreateProcessW is
 * the one call on the path that allocates and takes locks (the process heap's,
 * the loader's): Windows has no other way to start a process, and the reason
 * it is accepted is in docs/DECISIONS.md ("A crash on Windows restarts through
 * CreateProcessW in the unhandled-exception filter"). Everything else here
 * writes static memory or is a system call that does neither. The process
 * ends with TerminateProcess, not ExitProcess: no DLL is told to detach and no
 * C runtime buffer is flushed (execve, too, drops what was not flushed).
 *
 * A second call while one runs (a fault on another thread, which only SDL's
 * could be) waits forever: the first call's end ends the process. */
int host_process_restart(char *const argv[])
{
    /* acquire: if an earlier call failed and let go (release below), its
     * writes to the buffers are seen before this one writes them. */
    if (atomic_flag_test_and_set_explicit(&restart.running, memory_order_acquire)) {
        for (;;)
            Sleep(INFINITE);
    }
    const DWORD length = GetModuleFileNameW(NULL, restart.executable, HOST_PROCESS_PATH_SIZE);
    bool started = false;
    PROCESS_INFORMATION child;
    if (length == 0 || length >= HOST_PROCESS_PATH_SIZE) {
        errno = length == 0 ? errno_of(GetLastError()) : ENAMETOOLONG;
    } else if (build_command_line(argv)) {
        STARTUPINFOW startup;
        memset(&startup, 0, sizeof startup);
        startup.cb = sizeof startup;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        started = CreateProcessW(restart.executable, restart.command_line, NULL, NULL, TRUE, 0, NULL, NULL, &startup,
                                 &child);
        if (!started)
            errno = errno_of(GetLastError());
    }
    if (!started) {
        /* release: pairs with the acquire above, for a later call. */
        atomic_flag_clear_explicit(&restart.running, memory_order_release);
        return -1;
    }
    (void)CloseHandle(child.hThread);
    DWORD status = RESTART_STATUS_LOST;
    if (WaitForSingleObject(child.hProcess, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(child.hProcess, &status))
        status = RESTART_STATUS_LOST;
    (void)TerminateProcess(GetCurrentProcess(), status);
    return -1; /* not reached: TerminateProcess ends the calling thread with its process */
}

/* --- not written yet (task W7) ------------------------------------------------------- */

int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context)
{
    (void)stack;
    (void)size;
    (void)entry;
    (void)context;
    return -1;
}

void host_stack_leave(void)
{
}
