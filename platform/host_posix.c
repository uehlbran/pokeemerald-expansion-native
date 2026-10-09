/*
 * host_posix.c: the host layer where every POSIX system agrees (Linux today,
 * macOS next): the fault handlers' installation (host_fault.h), the restart
 * of the executable (host_process.h) and the files (host_file.h). What each
 * system does its own way is in its own source, host_linux.c, reached
 * through host_internal.h.
 *
 * Followed: POSIX (sigaction, sigaltstack, sigprocmask, execve, fsync,
 * rename, mkdir, localtime_r), and signal-safety(7) for what a signal
 * handler may call: everything on the handlers' path here (sigaction, raise,
 * execve) is async-signal-safe.
 */
#define _FILE_OFFSET_BITS 64 /* with _TIME_BITS: a 64-bit time_t on 32-bit builds, */
#define _TIME_BITS 64        /* so host_file_local_time converts dates past 2038 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_file.h"
#include "host_internal.h"
#include "host_process.h"

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

/* --- host_process ---------------------------------------------------------------- */

int host_process_restart(char *const argv[])
{
    (void)execve(host_self_executable, argv, environ); /* returns only on failure */
    return -1;
}

/* --- host_file --------------------------------------------------------------------- */

/* mkdir's mode for a new directory: every permission, which the umask
 * narrows (mkdir(2)). */
#define HOST_FILE_DIRECTORY_MODE 0777

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
