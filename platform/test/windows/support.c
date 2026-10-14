/*
 * support.c: test_support.h and windows_support.h for Windows (MinGW-w64
 * and msvcrt): the temporary directory from TMPDIR or GetTempPathA, _mkdir
 * and _open with _O_EXCL for the scratch names, FindFirstFileA for a
 * directory's entries, _stat64, _putenv_s, _dup and _dup2, Sleep and
 * GetTickCount64, SHFileOperationA for a tree; CreateProcessA for a child. Narrow paths, in the ANSI code
 * page, as the C runtime and the platform take them.
 */
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <windows.h>

#include <shellapi.h>

#include "test_support.h"
#include "windows_support.h"

/* How many names a scratch file or directory tries before giving up. */
#define SCRATCH_ATTEMPTS 100u

/* The temporary directory: $TMPDIR if set (Git Bash sets TMP and TEMP, which
 * GetTempPathA reads), else the system's; without its trailing separator. */
static bool temp_directory(char *path, size_t size)
{
    const char *base = getenv("TMPDIR");
    if (base != NULL && base[0] != '\0') {
        const int length = snprintf(path, size, "%s", base);
        return length > 0 && (size_t)length < size;
    }
    const DWORD length = GetTempPathA((DWORD)(size > MAXDWORD ? MAXDWORD : size), path);
    if (length == 0 || length >= size)
        return false;
    if (path[length - 1] == '\\' || path[length - 1] == '/')
        path[length - 1] = '\0';
    return true;
}

/* "<temporary directory>/<prefix>.<process>.<attempt>": unique to this
 * process, and to each attempt in it. */
static bool scratch_name(char *path, size_t size, const char *prefix, unsigned attempt)
{
    static unsigned counter;
    char base[TSUP_PATH_SIZE];
    if (!temp_directory(base, sizeof base))
        return false;
    const int length = snprintf(path, size, "%s/%s.%lu.%u.%u", base, prefix, (unsigned long)GetCurrentProcessId(),
                                counter++, attempt);
    return length > 0 && (size_t)length < size;
}

bool tsup_make_temp_directory(char *path, size_t size, const char *prefix)
{
    for (unsigned attempt = 0; attempt < SCRATCH_ATTEMPTS; attempt++) {
        if (!scratch_name(path, size, prefix, attempt))
            return false;
        if (_mkdir(path) == 0)
            return true;
        if (errno != EEXIST)
            return false;
    }
    return false;
}

bool tsup_make_temp_file(char *path, size_t size, const char *prefix)
{
    for (unsigned attempt = 0; attempt < SCRATCH_ATTEMPTS; attempt++) {
        if (!scratch_name(path, size, prefix, attempt))
            return false;
        const int descriptor = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (descriptor >= 0) {
            if (_close(descriptor) == 0)
                return true;
            (void)remove(path); /* best-effort: the test fails anyway */
            return false;
        }
        if (errno != EEXIST)
            return false;
    }
    return false;
}

/* Windows directories take their permissions from their parent's; the user's
 * temporary directory is the user's alone. */
bool tsup_make_directory(const char *path)
{
    return _mkdir(path) == 0;
}

bool tsup_remove_directory(const char *path)
{
    return _rmdir(path) == 0;
}

bool tsup_remove_file(const char *path)
{
    return remove(path) == 0 || errno == ENOENT;
}

/* The pattern FindFirstFileA lists a directory by: the path, a slash, a star. */
static bool directory_pattern(const char *path, char *pattern, size_t size)
{
    const int length = snprintf(pattern, size, "%s/*", path);
    return length > 0 && (size_t)length < size;
}

static bool is_dot_entry(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

/* The shell's delete of a whole tree, without a dialog or the recycle bin.
 * It takes backslashes only, and a list ended by two NULs. */
bool tsup_remove_tree(const char *path)
{
    if (!tsup_exists(path))
        return true;
    char from[TSUP_PATH_SIZE + 1u];
    const size_t length = strlen(path);
    if (length >= TSUP_PATH_SIZE)
        return false;
    memcpy(from, path, length);
    for (size_t i = 0; i < length; i++) {
        if (from[i] == '/')
            from[i] = '\\';
    }
    from[length] = '\0';
    from[length + 1u] = '\0';
    SHFILEOPSTRUCTA operation;
    memset(&operation, 0, sizeof operation);
    operation.wFunc = FO_DELETE;
    operation.pFrom = from;
    operation.fFlags = FOF_NO_UI;
    return SHFileOperationA(&operation) == 0 && !operation.fAnyOperationsAborted;
}

bool tsup_exists(const char *path)
{
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

intmax_t tsup_file_size(const char *path)
{
    struct _stat64 info;
    return _stat64(path, &info) == 0 ? (intmax_t)info.st_size : -1;
}

bool tsup_directory_is_empty(const char *path)
{
    char pattern[TSUP_PATH_SIZE];
    if (!directory_pattern(path, pattern, sizeof pattern))
        return false;
    WIN32_FIND_DATAA entry;
    HANDLE search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE)
        return false;
    size_t entries = 0;
    do {
        if (!is_dot_entry(entry.cFileName))
            entries++;
    } while (FindNextFileA(search, &entry));
    return FindClose(search) && entries == 0;
}

bool tsup_in_directory(const char *dir, void (*body)(void *context), void *context)
{
    char previous[TSUP_PATH_SIZE];
    if (_getcwd(previous, (int)sizeof previous) == NULL)
        return false;
    bool ok = _chdir(dir) == 0;
    if (ok)
        body(context);
    return _chdir(previous) == 0 && ok;
}

/* _putenv_s changes the C runtime's environment and the process's, which a
 * started process inherits; an empty value removes the variable. */
bool tsup_set_environment(const char *name, const char *value)
{
    return _putenv_s(name, value) == 0;
}

bool tsup_unset_environment(const char *name)
{
    return _putenv_s(name, "") == 0;
}

void tsup_sleep_milliseconds(unsigned milliseconds)
{
    Sleep(milliseconds);
}

uint64_t tsup_monotonic_milliseconds(void)
{
    return GetTickCount64();
}

const char *tsup_null_device(void)
{
    return "NUL";
}

#define STDERR_DESCRIPTOR 2

bool tsup_redirect_stderr(struct tsup_stderr *saved, FILE *to)
{
    saved->saved = -1;
    if (fflush(stderr) != 0)
        return false;
    const int copy = _dup(STDERR_DESCRIPTOR);
    if (copy < 0)
        return false;
    if (_dup2(_fileno(to), STDERR_DESCRIPTOR) != 0) {
        (void)_close(copy); /* nothing redirected: the caller sees the failure */
        return false;
    }
    saved->saved = copy;
    return true;
}

bool tsup_restore_stderr(struct tsup_stderr *saved)
{
    if (saved->saved < 0)
        return false;
    bool restored = fflush(stderr) == 0; /* put back whether or not the flush worked */
    restored = _dup2(saved->saved, STDERR_DESCRIPTOR) == 0 && restored;
    restored = _close(saved->saved) == 0 && restored;
    saved->saved = -1;
    return restored;
}

/* --- windows_support.h ------------------------------------------------------- */

const char *wsup_child_name(void)
{
    const char *name = getenv(WSUP_CHILD_VARIABLE);
    return name != NULL && name[0] != '\0' ? name : NULL;
}

void wsup_end(uint32_t code)
{
    (void)TerminateProcess(GetCurrentProcess(), code);
    for (;;) /* not reached: TerminateProcess ends the calling thread with its process */
        Sleep(INFINITE);
}

/* The child is named through the environment it inherits, set only while it
 * is created. */
bool wsup_run_child(const char *name, uint32_t *code)
{
    char path[TSUP_PATH_SIZE];
    const DWORD length = GetModuleFileNameA(NULL, path, sizeof path);
    if (length == 0 || length >= sizeof path || fflush(stdout) != 0 || fflush(stderr) != 0)
        return false;
    char command_line[TSUP_PATH_SIZE + 2];
    if (snprintf(command_line, sizeof command_line, "\"%s\"", path) >= (int)sizeof command_line)
        return false;
    if (_putenv_s(WSUP_CHILD_VARIABLE, name) != 0)
        return false;
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child;
    const bool started = CreateProcessA(path, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &child);
    const bool unset = _putenv_s(WSUP_CHILD_VARIABLE, "") == 0;
    if (!started)
        return false;
    (void)CloseHandle(child.hThread);
    bool ended = WaitForSingleObject(child.hProcess, WSUP_CHILD_MILLISECONDS) == WAIT_OBJECT_0;
    if (!ended) {
        (void)TerminateProcess(child.hProcess, WSUP_CHILD_RETURNED);
        (void)WaitForSingleObject(child.hProcess, INFINITE);
    }
    DWORD exit_code = 0;
    ended = GetExitCodeProcess(child.hProcess, &exit_code) && ended;
    (void)CloseHandle(child.hProcess);
    *code = exit_code;
    return ended && unset;
}
