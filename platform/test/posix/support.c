#define _XOPEN_SOURCE 700 /* nftw */
/*
 * support.c: test_support.h and posix_support.h for the POSIX systems (Linux
 * today, macOS next): mkdtemp and mkstemp, stat, setenv, dup and dup2, fork
 * with waitpid, poll.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <ftw.h>

#include "posix_support.h"
#include "test_support.h"

#define DEFAULT_TEMP_DIRECTORY "/tmp"
#define SCRATCH_DIRECTORY_MODE 0700
#define MAX_OPEN_DESCRIPTORS 16 /* nftw's open directories: the depth of a scratch tree is a few */

/* Writes "<temporary directory>/<prefix>.XXXXXX" to path, for mkdtemp or mkstemp to fill. */
static bool temp_template(char *path, size_t size, const char *prefix)
{
    const char *base = getenv("TMPDIR");
    if (base == NULL || base[0] == '\0')
        base = DEFAULT_TEMP_DIRECTORY;
    const int length = snprintf(path, size, "%s/%s.XXXXXX", base, prefix);
    return length > 0 && (size_t)length < size;
}

bool tsup_make_temp_directory(char *path, size_t size, const char *prefix)
{
    return temp_template(path, size, prefix) && mkdtemp(path) != NULL;
}

bool tsup_make_temp_file(char *path, size_t size, const char *prefix)
{
    if (!temp_template(path, size, prefix))
        return false;
    const int descriptor = mkstemp(path);
    if (descriptor < 0)
        return false;
    if (close(descriptor) == 0)
        return true;
    (void)unlink(path); /* best-effort: the test fails anyway */
    return false;
}

FILE *tsup_open_temp_stream(void)
{
    return tmpfile();
}

bool tsup_make_directory(const char *path)
{
    return mkdir(path, SCRATCH_DIRECTORY_MODE) == 0;
}

bool tsup_remove_directory(const char *path)
{
    return rmdir(path) == 0;
}

bool tsup_remove_file(const char *path)
{
    return unlink(path) == 0 || errno == ENOENT;
}

static int remove_entry(const char *path, const struct stat *info, int type, struct FTW *position)
{
    (void)info;
    (void)type;
    (void)position;
    return remove(path);
}

bool tsup_remove_tree(const char *path)
{
    /* Depth first, so a directory is empty when its turn comes; no links followed. */
    return nftw(path, remove_entry, MAX_OPEN_DESCRIPTORS, FTW_DEPTH | FTW_PHYS) == 0 || errno == ENOENT;
}

bool tsup_exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

intmax_t tsup_file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 ? (intmax_t)info.st_size : -1;
}

bool tsup_directory_is_empty(const char *path)
{
    DIR *const directory = opendir(path);
    if (directory == NULL)
        return false;
    size_t entries = 0;
    for (const struct dirent *entry = readdir(directory); entry != NULL; entry = readdir(directory))
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            entries++;
    return closedir(directory) == 0 && entries == 0;
}

bool tsup_in_directory(const char *dir, void (*body)(void *context), void *context)
{
    const int previous = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (previous < 0)
        return false;
    bool ok = chdir(dir) == 0;
    if (ok)
        body(context);
    ok = fchdir(previous) == 0 && ok;
    return close(previous) == 0 && ok;
}

bool tsup_set_environment(const char *name, const char *value)
{
    return setenv(name, value, 1) == 0;
}

bool tsup_unset_environment(const char *name)
{
    return unsetenv(name) == 0;
}

#define MILLISECONDS_PER_SECOND 1000u
#define NANOSECONDS_PER_MILLISECOND 1000000u

void tsup_sleep_milliseconds(unsigned milliseconds)
{
    struct timespec left = {.tv_sec = (time_t)(milliseconds / MILLISECONDS_PER_SECOND),
                            .tv_nsec = (long)(milliseconds % MILLISECONDS_PER_SECOND)
                                       * (long)NANOSECONDS_PER_MILLISECOND};
    while (nanosleep(&left, &left) != 0 && errno == EINTR)
        ; /* a signal ends the sleep early: finish it */
}

uint64_t tsup_monotonic_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * MILLISECONDS_PER_SECOND + (uint64_t)now.tv_nsec / NANOSECONDS_PER_MILLISECOND;
}

const char *tsup_executable_suffix(void)
{
    return "";
}

int tsup_divide_error_number(void)
{
    return SIGFPE;
}

const char *tsup_null_device(void)
{
    return "/dev/null";
}

bool tsup_redirect_stderr(struct tsup_stderr *saved, FILE *to)
{
    saved->saved = -1;
    if (fflush(stderr) != 0)
        return false;
    const int copy = dup(STDERR_FILENO);
    if (copy < 0)
        return false;
    if (dup2(fileno(to), STDERR_FILENO) < 0) {
        (void)close(copy); /* nothing redirected: the caller sees the failure */
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
    restored = dup2(saved->saved, STDERR_FILENO) >= 0 && restored;
    restored = close(saved->saved) == 0 && restored;
    saved->saved = -1;
    return restored;
}

pid_t psup_start_child(void (*body)(void *context), void *context)
{
    if (fflush(stdout) != 0 || fflush(stderr) != 0)
        return -1;
    const pid_t pid = fork();
    if (pid != 0)
        return pid; /* the parent's, or -1 */
    const struct rlimit no_core = {0, 0};
    if (setrlimit(RLIMIT_CORE, &no_core) != 0)
        _exit(PSUP_CHILD_RETURNED);
    (void)alarm(PSUP_CHILD_SECONDS); /* the previous remainder is the parent's, not ours */
    body(context);
    _exit(PSUP_CHILD_RETURNED);
}

int psup_wait_child(pid_t pid)
{
    int status;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return status;
}

int psup_fork_child(void (*body)(void *context), void *context)
{
    const pid_t pid = psup_start_child(body, context);
    return pid < 0 ? -1 : psup_wait_child(pid);
}

int psup_exit_code(int status)
{
    return status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int psup_terminating_signal(int status)
{
    return status != -1 && WIFSIGNALED(status) ? WTERMSIG(status) : -1;
}

ssize_t psup_read_within(int descriptor, void *buffer, size_t size, int milliseconds)
{
    struct pollfd wait = {.fd = descriptor, .events = POLLIN};
    const int ready = poll(&wait, 1, milliseconds);
    if (ready <= 0)
        return -1;
    return read(descriptor, buffer, size);
}
