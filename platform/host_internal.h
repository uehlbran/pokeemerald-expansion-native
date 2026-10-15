/*
 * host_internal.h: what host_posix.c, the host layer's source for every
 * POSIX system, takes from the source of the system it runs on
 * (host_linux.c, host_macos.c), and the stack switch's sanitizer
 * annotations, which host_posix.c gives both. Not an interface: modules
 * include the host_*.h headers.
 */
#ifndef PLATFORM_HOST_INTERNAL_H
#define PLATFORM_HOST_INTERNAL_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/ucontext.h>
#include <time.h>

/* host_fault.h's fault: the signal, what the kernel says about it, and the
 * interrupted context, whose saved registers the handler may edit. */
struct host_fault {
    int signal_number;
    siginfo_t *info;
    ucontext_t *context;
};

/* The path execve runs this executable by, ready before any handler can run,
 * so that a signal handler may use it: Linux's /proc/self/exe; on macOS the
 * path dyld recorded at start-up (host_macos.c). Async-signal-safe. */
const char *host_process_exec_path(void);

/* Writes what the open descriptor holds through to the disk: fsync on Linux;
 * on macOS F_FULLFSYNC, which empties the drive's own cache as well
 * (host_macos.c says why). Returns 0, or -1 with errno set. */
int host_file_flush(int descriptor);

/* Whether F_FULLFSYNC's error, error, says that the file system has no such
 * flush, so that macOS's host_file_flush falls back to fsync: ENOTSUP (as on
 * some network volumes), ENOTTY and EINVAL (a descriptor or file system the
 * call does not apply to). Any other error (EIO from the drive, EBADF, EINTR)
 * is reported, not hidden behind an fsync that may succeed. In host_posix.c,
 * so that the Linux gate tests it (test/posix/test_host_file_posix.c). */
bool host_file_flush_falls_back(int error);

/* Initialises the watchdog's condition variable (host_thread.h) so that its
 * timed waits are measured on the monotonic clock, which a step of the wall
 * clock does not move. Returns 0 or the pthread error, unreported. */
int host_condition_init(pthread_cond_t *condition);

/* Waits on condition, lock held, until it is signalled or CLOCK_MONOTONIC
 * reaches deadline. Returns 0 when woken (or woken for nothing), ETIMEDOUT
 * at the deadline (at once if it has passed), or another pthread error. */
int host_condition_wait_until(pthread_cond_t *condition, pthread_mutex_t *lock, const struct timespec *deadline);

/* AddressSanitizer's fiber annotations (sanitizer/common_interface_defs.h)
 * around a switch of stacks, so that it knows which stack is current; they do
 * nothing in a build without it. Start before the switch, naming the stack
 * switched to (a NULL fake_stack_save when the stack switched from is left
 * for good); finish first thing on the stack switched to. */
void host_stack_start_switch(void **fake_stack_save, const void *bottom, size_t size);
void host_stack_finish_switch(void *fake_stack_save, const void **old_bottom, size_t *old_size);

#endif /* PLATFORM_HOST_INTERNAL_H */
