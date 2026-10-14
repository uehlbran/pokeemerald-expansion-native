/*
 * posix_support.h: what the tests in test/posix/ share, on top of
 * test_support.h: a bounded forked child, the way its end is read, and a
 * read from a pipe that gives up. Implemented in support.c, beside this
 * file. Tests of any other family do not include it.
 */
#ifndef PLATFORM_TEST_POSIX_SUPPORT_H
#define PLATFORM_TEST_POSIX_SUPPORT_H

#include <stddef.h>
#include <sys/types.h>

/* Seconds a child, and the process it restarts into (alarm(2) survives
 * execve), may take before SIGALRM ends it, so that a loop that would never
 * end fails the test instead of hanging it. */
#define PSUP_CHILD_SECONDS 10u

/* The exit code of a child whose body returned instead of ending itself. */
#define PSUP_CHILD_RETURNED 3

/* Runs body(context) in a forked child that cannot write a core file, with an
 * alarm of PSUP_CHILD_SECONDS; if body returns, the child exits with
 * PSUP_CHILD_RETURNED. Standard output and error are flushed before the fork,
 * so that the child does not repeat what the parent printed. Returns the
 * child's wait status once it has ended, or -1 if it could not be started or
 * waited for. This is the fork alone: a test calls psup_run_child
 * (posix_child.h), which first stops the runner's watchdog thread. */
int psup_fork_child(void (*body)(void *context), void *context);

/* psup_fork_child's two halves, for a child that runs while the test goes on:
 * the start, which returns the child's process id (or -1) without waiting,
 * and the wait, which returns its wait status once it has ended (or -1). A
 * test starts one with psup_start_child_running (posix_child.h). */
pid_t psup_start_child(void (*body)(void *context), void *context);
int psup_wait_child(pid_t pid);

/* The exit code in a wait status of a child that exited, else -1. */
int psup_exit_code(int status);

/* The signal that ended the child in a wait status, else -1. */
int psup_terminating_signal(int status);

/* Reads what is there from descriptor, waiting at most milliseconds for the
 * first byte. Returns the count read (0 at end of file), or -1 on error or
 * when nothing came in time. */
ssize_t psup_read_within(int descriptor, void *buffer, size_t size, int milliseconds);

#endif /* PLATFORM_TEST_POSIX_SUPPORT_H */
