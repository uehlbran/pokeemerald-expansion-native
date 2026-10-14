/*
 * test_host_thread.c: the deadline thread (host_thread.h): it sets the
 * caller's flag once the time has passed and not before, a stop before the
 * deadline leaves the flag alone, a restart moves the deadline either way,
 * and bad arguments are refused. Deadlines are whole seconds, so these tests
 * wait one or two. That no thread is left behind is counted in
 * linux/test_host_thread_linux.c.
 *
 * That the deadline follows the monotonic clock, not the wall clock, is covered
 * by inspection (WATCHDOG_CLOCK in host_posix.c, the condition variable's
 * attribute set with pthread_condattr_setclock and checked): a test would have
 * to step the system's wall clock, which needs privileges, and reading the
 * attribute back would need a test-only accessor in host_thread.h, which this
 * module does not carry.
 *
 * Also by inspection, for the same reason (no fault can be injected into the
 * pthread calls or the clock without such an accessor): that a failure on the
 * watchdog's thread (its lock, its wait) is not printed there but recorded,
 * and reported by host_watchdog_stop after the join (watchdog_thread and
 * host_watchdog_stop in host_posix.c; no call on the thread's path prints);
 * and that a failed clock in the start's handshake ends the wait without
 * reading the time it did not get (watchdog_await_start).
 */
#include <stdatomic.h>

#include "host_thread.h"
#include "ptest.h"
#include "test_support.h"

#define MILLISECONDS_PER_SECOND 1000u
#define SETTLE_MILLISECONDS 300u /* well inside a one-second deadline, well past a stopped one */
#define PAST_ONE_SECOND_MILLISECONDS 1500u

static bool flag_set(atomic_bool *flag)
{
    return atomic_load_explicit(flag, memory_order_relaxed);
}

PTEST(the_flag_is_set_after_the_deadline_and_not_before)
{
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(1, &expired);
    PT_ASSERT(watchdog != NULL);
    const uint64_t started = tsup_monotonic_milliseconds();
    tsup_sleep_milliseconds(SETTLE_MILLISECONDS);
    const bool early = flag_set(&expired);
    while (!flag_set(&expired) && tsup_monotonic_milliseconds() - started < 3 * MILLISECONDS_PER_SECOND)
        tsup_sleep_milliseconds(10);
    const uint64_t elapsed = tsup_monotonic_milliseconds() - started;
    host_watchdog_stop(watchdog);
    PT_ASSERT(!early);
    PT_ASSERT(flag_set(&expired));
    PT_ASSERT(elapsed >= MILLISECONDS_PER_SECOND - 50u); /* the sleeps and the deadline round differently */
    PT_ASSERT(elapsed < 2 * MILLISECONDS_PER_SECOND);
}

PTEST(a_stop_before_the_deadline_leaves_the_flag_clear)
{
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(1, &expired);
    PT_ASSERT(watchdog != NULL);
    host_watchdog_stop(watchdog);
    tsup_sleep_milliseconds(PAST_ONE_SECOND_MILLISECONDS);
    PT_ASSERT(!flag_set(&expired));
}

PTEST(a_restart_to_a_later_deadline_holds_the_flag_back)
{
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(1, &expired);
    PT_ASSERT(watchdog != NULL);
    const int restarted = host_watchdog_restart(watchdog, 30);
    tsup_sleep_milliseconds(PAST_ONE_SECOND_MILLISECONDS);
    const bool set = flag_set(&expired);
    host_watchdog_stop(watchdog);
    PT_ASSERT_EQ_INT(restarted, 0);
    PT_ASSERT(!set);
}

PTEST(a_restart_to_an_earlier_deadline_brings_the_flag_forward)
{
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(30, &expired);
    PT_ASSERT(watchdog != NULL);
    const int restarted = host_watchdog_restart(watchdog, 1);
    tsup_sleep_milliseconds(PAST_ONE_SECOND_MILLISECONDS);
    const bool set = flag_set(&expired);
    host_watchdog_stop(watchdog);
    PT_ASSERT_EQ_INT(restarted, 0);
    PT_ASSERT(set);
}

PTEST(a_zero_or_oversized_deadline_and_a_missing_flag_are_refused)
{
    atomic_bool expired = false;
    PT_ASSERT(host_watchdog_start(0, &expired) == NULL);
    PT_ASSERT(host_watchdog_start(UINT32_MAX, &expired) == NULL);
    PT_ASSERT(host_watchdog_start(1, NULL) == NULL);
    struct host_watchdog *const watchdog = host_watchdog_start(30, &expired);
    PT_ASSERT(watchdog != NULL);
    const int zero = host_watchdog_restart(watchdog, 0);
    const int huge = host_watchdog_restart(watchdog, UINT32_MAX);
    host_watchdog_stop(watchdog);
    PT_ASSERT_EQ_INT(zero, -1);
    PT_ASSERT_EQ_INT(huge, -1);
    PT_ASSERT(!flag_set(&expired));
}

PTEST(a_watchdog_with_a_long_deadline_stops_at_once)
{
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(30, &expired);
    PT_ASSERT(watchdog != NULL);
    const uint64_t started = tsup_monotonic_milliseconds();
    host_watchdog_stop(watchdog); /* a stop that does not wake the thread waits out its 30 seconds */
    const uint64_t elapsed = tsup_monotonic_milliseconds() - started;
    PT_ASSERT(elapsed < MILLISECONDS_PER_SECOND);
    PT_ASSERT(!flag_set(&expired));
}

PTEST(restarting_or_stopping_no_watchdog_is_safe)
{
    PT_ASSERT_EQ_INT(host_watchdog_restart(NULL, 1), -1);
    host_watchdog_stop(NULL);
}

PTEST_MAIN()
