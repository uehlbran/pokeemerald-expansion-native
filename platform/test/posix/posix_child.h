/*
 * posix_child.h: psup_run_child, the forked child the POSIX tests run bodies
 * in (posix_support.h has the fork itself, psup_fork_child). A process that
 * forks has no thread alive in the child, and the child must not inherit a
 * handle to one it cannot join, so the runner's watchdog thread (host_thread.h)
 * is stopped before the fork and started again after it, in the parent. The
 * limit is counted again from the restart: the child has its own alarm. The
 * hook is the pair ptest_pause_watchdog and ptest_resume_watchdog of ptest.h,
 * called from this header because the watchdog's handle is the test binary's
 * own static state, out of reach of support.c, which is shared and has no
 * runner of its own.
 */
#ifndef PLATFORM_TEST_POSIX_CHILD_H
#define PLATFORM_TEST_POSIX_CHILD_H

#include "posix_support.h"
#include "ptest.h"

/* psup_fork_child with no watchdog thread alive across the fork. */
static inline int psup_run_child(void (*body)(void *context), void *context)
{
    ptest_pause_watchdog();
    const int status = psup_fork_child(body, context);
    ptest_resume_watchdog();
    return status;
}

#endif /* PLATFORM_TEST_POSIX_CHILD_H */
