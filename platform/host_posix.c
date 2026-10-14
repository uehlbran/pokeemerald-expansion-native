/*
 * host_posix.c: the host layer where every POSIX system agrees (Linux today,
 * macOS next): the fault handlers' installation (host_fault.h), the restart
 * of the executable and its end (host_process.h), the files (host_file.h) and the deadline
 * thread (host_thread.h). What each system does its own way is in its own
 * source, host_linux.c, reached through host_internal.h.
 *
 * Followed: POSIX (sigaction, sigaltstack, sigprocmask, execve, _exit, open,
 * write, fsync, rename, mkdir, localtime_r, pthread_create and
 * pthread_cond_timedwait), and signal-safety(7) for what a signal handler may
 * call: everything on the handlers' path here (sigaction, raise, execve,
 * _exit, write) is async-signal-safe.
 */
#define _FILE_OFFSET_BITS 64 /* with _TIME_BITS: a 64-bit time_t on 32-bit builds, */
#define _TIME_BITS 64        /* so host_file_local_time converts dates past 2038 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_file.h"
#include "host_internal.h"
#include "host_process.h"
#include "host_thread.h"

_Static_assert(sizeof(time_t) >= 8, "host_posix.c needs a 64-bit time_t (_TIME_BITS=64)");

/* POSIX: the environment, which the application declares. */
extern char **environ;

/* --- host_fault: the divide error ------------------------------------------ */

static struct {
    bool installed;
    host_fault_divide_handler handler;
    struct sigaction previous; /* what a fault the handler passes on goes to */
} divide;

/* Passes the signal to the handler installed before, or lets it kill the
 * process: with the default action restored, the instruction faults again on
 * return. */
static void pass_on(int signal_number, siginfo_t *info, void *context)
{
    const struct sigaction *previous = &divide.previous;
    if ((previous->sa_flags & SA_SIGINFO) != 0 && previous->sa_sigaction != NULL) {
        previous->sa_sigaction(signal_number, info, context);
        return;
    }
    if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN) {
        previous->sa_handler(signal_number);
        return;
    }
    struct sigaction fallback;
    memset(&fallback, 0, sizeof fallback);
    fallback.sa_handler = SIG_DFL;
    (void)sigemptyset(&fallback.sa_mask);
    (void)sigaction(signal_number, &fallback, NULL);
}

static void on_divide_signal(int signal_number, siginfo_t *info, void *context)
{
    struct host_fault fault = {.signal_number = signal_number, .info = info, .context = context};
    if (divide.handler(&fault))
        return;
    pass_on(signal_number, info, context);
}

int host_fault_install_divide_error(host_fault_divide_handler handler)
{
    host_fault_uninstall_divide_error();
    divide.handler = handler;
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_divide_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGFPE, &action, &divide.previous) != 0) {
        perror("host_fault: sigaction(SIGFPE)");
        return -1;
    }
    divide.installed = true;
    return 0;
}

void host_fault_uninstall_divide_error(void)
{
    if (!divide.installed)
        return;
    (void)sigaction(SIGFPE, &divide.previous, NULL); /* restoring what sigaction gave back cannot fail */
    divide.installed = false;
}

/* --- host_fault: crashes ----------------------------------------------------- */

/* The signals that mean the program crashed: a bad memory access (SIGSEGV,
 * SIGBUS), an illegal instruction (SIGILL), an arithmetic fault no handler
 * completed (SIGFPE), and abort() (SIGABRT, the C library's stack-protector
 * and fortify checks). */
static const int crash_signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#define CRASH_SIGNAL_COUNT (sizeof crash_signals / sizeof crash_signals[0])

static struct {
    bool installed;
    host_fault_crash_handler handler;
} crash;

static _Alignas(16) unsigned char alternate_stack[HOST_FAULT_ALTERNATE_STACK_SIZE];

/* Lets the signal do what it does without the handler: kill the process.
 * Relies on the handler being installed without SA_NODEFER: the signal is
 * blocked while the handler runs, so the raise stays pending and is
 * delivered, with the default action, when the handler returns. */
static void die_of(int signal_number)
{
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(signal_number, &action, NULL);
    (void)raise(signal_number); /* pending until the handler returns; a fault would recur anyway */
}

static void on_crash_signal(int signal_number, siginfo_t *info, void *context)
{
    const struct host_fault fault = {.signal_number = signal_number, .info = info, .context = context};
    crash.handler(&fault);
    die_of(signal_number);
}

static bool install_crash_handlers(void)
{
    stack_t stack = {.ss_sp = alternate_stack, .ss_size = sizeof alternate_stack, .ss_flags = 0};
    if (sigaltstack(&stack, NULL) != 0) {
        perror("host_fault: sigaltstack");
        return false;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_crash_signal;
    action.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigset_t crashes;
    if (sigemptyset(&action.sa_mask) != 0 || sigemptyset(&crashes) != 0) {
        perror("host_fault: sigemptyset");
        return false;
    }
    for (size_t i = 0; i < CRASH_SIGNAL_COUNT; i++) {
        if (sigaction(crash_signals[i], &action, NULL) != 0 || sigaddset(&crashes, crash_signals[i]) != 0) {
            perror("host_fault: sigaction");
            return false;
        }
    }
    if (sigprocmask(SIG_UNBLOCK, &crashes, NULL) != 0) {
        perror("host_fault: sigprocmask");
        return false;
    }
    return true;
}

int host_fault_install_crash(host_fault_crash_handler handler)
{
    host_fault_uninstall_crash();
    crash.handler = handler;
    crash.installed = true; /* so that uninstall undoes a partial install */
    if (!install_crash_handlers()) {
        host_fault_uninstall_crash();
        return -1;
    }
    return 0;
}

void host_fault_uninstall_crash(void)
{
    if (!crash.installed)
        return;
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < CRASH_SIGNAL_COUNT; i++)
        (void)sigaction(crash_signals[i], &action, NULL); /* restoring a default cannot fail for these */
    stack_t no_stack = {.ss_sp = NULL, .ss_size = 0, .ss_flags = SS_DISABLE};
    (void)sigaltstack(&no_stack, NULL); /* fails only while running on it, which uninstall never does */
    crash.installed = false;
}

/* --- host_fault: what a handler reads ------------------------------------------ */

bool host_fault_is_divide_error(const struct host_fault *fault)
{
    return fault->signal_number == SIGFPE && fault->info->si_code == FPE_INTDIV;
}

int host_fault_number(const struct host_fault *fault)
{
    return fault->signal_number;
}

uintptr_t host_fault_address(const struct host_fault *fault)
{
    return (uintptr_t)fault->info->si_addr;
}

#define SIGNAL_NAME_PREFIX "signal "
#define DECIMAL_BASE 10u

/* Written by hand, not with snprintf, which is not async-signal-safe. */
void host_fault_name(const struct host_fault *fault, char name[HOST_FAULT_NAME_SIZE])
{
    char digits[HOST_FAULT_NAME_SIZE];
    size_t count = 0;
    unsigned number = (unsigned)fault->signal_number;
    do {
        digits[count++] = (char)('0' + number % DECIMAL_BASE);
        number /= DECIMAL_BASE;
    } while (number > 0 && count < sizeof digits);
    size_t length = sizeof SIGNAL_NAME_PREFIX - 1u;
    memcpy(name, SIGNAL_NAME_PREFIX, length);
    while (count > 0 && length < HOST_FAULT_NAME_SIZE - 1u)
        name[length++] = digits[--count];
    name[length] = '\0';
}

/* --- host_process ---------------------------------------------------------------- */

int host_process_restart(char *const argv[])
{
    (void)execve(host_self_executable, argv, environ); /* returns only on failure */
    return -1;
}

bool host_process_restart_is_handler_safe(void)
{
    return true;
}

void host_process_end(unsigned status)
{
    _exit((int)status);
}

/* --- host_file --------------------------------------------------------------------- */

/* mkdir's mode for a new directory: every permission, which the umask
 * narrows (mkdir(2)). */
#define HOST_FILE_DIRECTORY_MODE 0777

/* open's mode for a new file: readable by everyone, writable by the owner,
 * which the umask may narrow further. */
#define HOST_FILE_CREATED_MODE 0644

int host_file_open(const char *path, int flags)
{
    return open(path, flags | O_CLOEXEC, HOST_FILE_CREATED_MODE);
}

/* write(2) is async-signal-safe (signal-safety(7)). It returns 0 for a
 * non-empty buffer only on a device that takes nothing, which would loop
 * forever: that is an error here. */
int host_file_write_all(int descriptor, const void *bytes, size_t size)
{
    const unsigned char *next = bytes;
    while (size > 0) {
        const ssize_t written = write(descriptor, next, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            if (written == 0)
                errno = EIO;
            return -1;
        }
        next += written;
        size -= (size_t)written;
    }
    return 0;
}

/* errno is saved and put back: a handler that changed it would change what
 * the interrupted code reads after a failed call of its own. */
bool host_file_write_from_handler(int descriptor, const void *bytes, size_t size)
{
    const int saved_errno = errno;
    const unsigned char *next = bytes;
    while (size > 0) {
        const ssize_t written = write(descriptor, next, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        next += written;
        size -= (size_t)written;
    }
    errno = saved_errno;
    return size == 0;
}

int host_file_sync(int descriptor)
{
    return fsync(descriptor);
}

int host_file_replace(const char *from, const char *path)
{
    return rename(from, path); /* atomic over an existing file (POSIX rename) */
}

int host_file_sync_directory_of(const char *path)
{
    char directory[HOST_FILE_PATH_SIZE];
    const size_t length = strlen(path);
    if (length >= sizeof directory) {
        errno = ENAMETOOLONG;
        return -1;
    }
    const char *const last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        memcpy(directory, ".", sizeof ".");
    } else {
        /* "/x" is in "/"; "a/b/x" is in "a/b". */
        const size_t directory_length = last_slash == path ? 1 : (size_t)(last_slash - path);
        memcpy(directory, path, directory_length);
        directory[directory_length] = '\0';
    }
    const int descriptor = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
        return -1;
    if (fsync(descriptor) != 0) {
        const int error = errno;
        (void)close(descriptor); /* already failing */
        errno = error;
        return -1;
    }
    return close(descriptor);
}

int host_file_make_directory(const char *path)
{
    return mkdir(path, HOST_FILE_DIRECTORY_MODE);
}

int host_file_local_time(int64_t seconds, struct tm *out)
{
    const time_t when = (time_t)seconds;
    return localtime_r(&when, out) == NULL ? -1 : 0;
}

size_t host_file_name_offset(const char *path)
{
    const char *const last_slash = strrchr(path, '/');
    return last_slash == NULL ? 0 : (size_t)(last_slash - path) + 1u;
}

bool host_file_path_is_absolute(const char *path)
{
    return path[0] == '/';
}

/* --- host_thread: the deadline thread ---------------------------------------- */

/* The largest deadline host_watchdog_start takes: half of the unsigned range,
 * so that adding it to the clock cannot overflow a 64-bit time_t. */
#define WATCHDOG_MAX_SECONDS (UINT32_MAX / 2u)

/* How long host_watchdog_start waits for its thread to begin waiting. */
#define WATCHDOG_START_SECONDS 10

struct host_watchdog {
    pthread_t thread;
    pthread_mutex_t lock; /* guards deadline and stopping, nothing else */
    pthread_cond_t changed;
    struct timespec deadline;
    bool stopping;
    bool waiting;         /* the thread has started and is about to wait: host_watchdog_start returns only then */
    atomic_bool *expired; /* the caller's flag: the only thing written outside the lock */
};

/* Reports a failed pthread call (they return the error number, not errno). */
static void watchdog_report(const char *call, int error)
{
    (void)fprintf(stderr, "host_thread: %s failed: %s\n", call, strerror(error)); /* best-effort */
}

static int watchdog_set_deadline(struct host_watchdog *watchdog, unsigned seconds)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        watchdog_report("clock_gettime", errno);
        return -1;
    }
    watchdog->deadline = now;
    watchdog->deadline.tv_sec += (time_t)seconds;
    return 0;
}

/* True once the wall clock is at or past the deadline (a failing clock counts
 * as not yet: the wait then returns at once and tries again). */
static bool watchdog_deadline_passed(const struct host_watchdog *watchdog)
{
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
        return false;
    return now.tv_sec > watchdog->deadline.tv_sec
           || (now.tv_sec == watchdog->deadline.tv_sec && now.tv_nsec >= watchdog->deadline.tv_nsec);
}

static void *watchdog_thread(void *argument)
{
    struct host_watchdog *const watchdog = argument;
    const int locked = pthread_mutex_lock(&watchdog->lock);
    if (locked != 0) {
        watchdog_report("pthread_mutex_lock", locked); /* host_watchdog_start times out */
        return NULL;
    }
    watchdog->waiting = true;
    (void)pthread_cond_broadcast(&watchdog->changed); /* wakes host_watchdog_start */
    while (!watchdog->stopping) {
        if (watchdog_deadline_passed(watchdog)) {
            atomic_store_explicit(watchdog->expired, true, memory_order_relaxed); /* see host_thread.h */
            break;
        }
        /* Woken by a change, by the deadline, or for nothing; the loop decides which. */
        const int waited = pthread_cond_timedwait(&watchdog->changed, &watchdog->lock, &watchdog->deadline);
        if (waited != 0 && waited != ETIMEDOUT) {
            watchdog_report("pthread_cond_timedwait", waited);
            break;
        }
    }
    (void)pthread_mutex_unlock(&watchdog->lock);
    return NULL;
}

/* Waits until the thread is in its loop. A caller that forks next (the POSIX
 * tests do) then finds the thread idle in a condition wait, not in the middle
 * of its start, where the sanitizers' thread registry would be locked in the
 * child for good. */
static int watchdog_await_start(struct host_watchdog *watchdog)
{
    int error = pthread_mutex_lock(&watchdog->lock);
    if (error != 0)
        return error;
    /* A thread that failed to take the lock never says it is waiting: give up
     * after WATCHDOG_START_SECONDS rather than wait for ever. */
    struct timespec give_up;
    if (clock_gettime(CLOCK_REALTIME, &give_up) != 0)
        error = errno;
    give_up.tv_sec += WATCHDOG_START_SECONDS;
    while (!watchdog->waiting && error == 0)
        error = pthread_cond_timedwait(&watchdog->changed, &watchdog->lock, &give_up);
    (void)pthread_mutex_unlock(&watchdog->lock);
    return error;
}

struct host_watchdog *host_watchdog_start(unsigned seconds, atomic_bool *expired)
{
    if (seconds == 0 || seconds > WATCHDOG_MAX_SECONDS || expired == NULL) {
        (void)fprintf(stderr, "host_thread: a watchdog needs a deadline of 1 to %u seconds and a flag\n",
                      WATCHDOG_MAX_SECONDS);
        return NULL;
    }
    struct host_watchdog *const watchdog = calloc(1, sizeof *watchdog);
    if (watchdog == NULL) {
        watchdog_report("calloc", ENOMEM);
        return NULL;
    }
    watchdog->expired = expired;
    int error = pthread_mutex_init(&watchdog->lock, NULL);
    if (error != 0) {
        watchdog_report("pthread_mutex_init", error);
        free(watchdog);
        return NULL;
    }
    error = pthread_cond_init(&watchdog->changed, NULL);
    if (error != 0) {
        watchdog_report("pthread_cond_init", error);
        (void)pthread_mutex_destroy(&watchdog->lock); /* already failing */
        free(watchdog);
        return NULL;
    }
    error = watchdog_set_deadline(watchdog, seconds) == 0 ? 0 : EINVAL; /* reported by itself */
    if (error == 0) {
        error = pthread_create(&watchdog->thread, NULL, watchdog_thread, watchdog);
        if (error != 0)
            watchdog_report("pthread_create", error);
        else if (watchdog_await_start(watchdog) != 0) {
            host_watchdog_stop(watchdog); /* the thread exists: end it before freeing */
            return NULL;
        }
        if (error == 0)
            return watchdog;
    }
    (void)pthread_cond_destroy(&watchdog->changed); /* already failing */
    (void)pthread_mutex_destroy(&watchdog->lock);
    free(watchdog);
    return NULL;
}

int host_watchdog_restart(struct host_watchdog *watchdog, unsigned seconds)
{
    if (watchdog == NULL)
        return -1;
    if (seconds == 0 || seconds > WATCHDOG_MAX_SECONDS) {
        (void)fprintf(stderr, "host_thread: a watchdog needs a deadline of 1 to %u seconds\n", WATCHDOG_MAX_SECONDS);
        return -1;
    }
    const int locked = pthread_mutex_lock(&watchdog->lock);
    if (locked != 0) {
        watchdog_report("pthread_mutex_lock", locked);
        return -1;
    }
    const int status = watchdog_set_deadline(watchdog, seconds);
    const int signalled = pthread_cond_signal(&watchdog->changed);
    (void)pthread_mutex_unlock(&watchdog->lock); /* cannot fail on a mutex this thread holds */
    if (signalled != 0) {
        watchdog_report("pthread_cond_signal", signalled);
        return -1;
    }
    return status;
}

void host_watchdog_stop(struct host_watchdog *watchdog)
{
    if (watchdog == NULL)
        return;
    if (pthread_mutex_lock(&watchdog->lock) == 0) {
        watchdog->stopping = true;
        (void)pthread_cond_signal(&watchdog->changed); /* a missed signal is caught by the loop's test */
        (void)pthread_mutex_unlock(&watchdog->lock);
    }
    const int joined = pthread_join(watchdog->thread, NULL);
    if (joined != 0) {
        watchdog_report("pthread_join", joined);
        return; /* the thread may still use the watchdog: leak it rather than free it */
    }
    (void)pthread_cond_destroy(&watchdog->changed); /* nothing waits on it any more */
    (void)pthread_mutex_destroy(&watchdog->lock);
    free(watchdog);
}
