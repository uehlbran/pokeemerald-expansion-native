/*
 * test_wait_left.c: the relative wait wait_left gives (wait_left.h): the
 * time left, with the borrow from the seconds when the nanoseconds go
 * negative, none once the deadline has come, and at most an hour, so that a
 * far deadline is waited for in hours.
 */
#include <stdbool.h>
#include <time.h>

#include "ptest.h"
#include "wait_left.h"

static struct timespec time_at(time_t seconds, long nanoseconds)
{
    return (struct timespec){.tv_sec = seconds, .tv_nsec = nanoseconds};
}

static void expect_left(struct timespec deadline, struct timespec now, time_t seconds, long nanoseconds)
{
    struct timespec left = time_at(-1, -1);
    PT_ASSERT(wait_left(&deadline, &now, &left));
    PT_ASSERT_EQ_INT((long)left.tv_sec, (long)seconds);
    PT_ASSERT_EQ_INT(left.tv_nsec, nanoseconds);
}

PTEST(the_time_left_is_the_difference)
{
    expect_left(time_at(100, 500), time_at(90, 200), 10, 300);
    expect_left(time_at(100, 1), time_at(100, 0), 0, 1);
}

PTEST(the_nanoseconds_borrow_from_the_seconds)
{
    expect_left(time_at(100, 100), time_at(98, 900000000), 1, 100000100);
    expect_left(time_at(100, 0), time_at(99, 999999999), 0, 1);
}

PTEST(a_deadline_that_has_come_leaves_no_wait)
{
    struct timespec left = time_at(7, 7);
    const struct timespec now = time_at(100, 500);
    const struct timespec equal = time_at(100, 500);
    const struct timespec past = time_at(100, 499);
    const struct timespec long_past = time_at(3, 0);
    PT_ASSERT(!wait_left(&equal, &now, &left));
    PT_ASSERT(!wait_left(&past, &now, &left));
    PT_ASSERT(!wait_left(&long_past, &now, &left));
    PT_ASSERT_EQ_INT((long)left.tv_sec, 7L); /* untouched */
    PT_ASSERT_EQ_INT(left.tv_nsec, 7L);
}

PTEST(a_far_deadline_is_waited_for_an_hour_at_a_time)
{
    const time_t far = (time_t)(UINT32_MAX / 2u); /* the watchdog's longest deadline */
    expect_left(time_at(far, 0), time_at(0, 0), WAIT_LEFT_MAX_SECONDS, 0);
    expect_left(time_at(5 + WAIT_LEFT_MAX_SECONDS, 1), time_at(5, 0), WAIT_LEFT_MAX_SECONDS, 0);
    expect_left(time_at(5 + WAIT_LEFT_MAX_SECONDS, 0), time_at(5, 0), WAIT_LEFT_MAX_SECONDS, 0);
    expect_left(time_at(5 + WAIT_LEFT_MAX_SECONDS - 1, 999999999), time_at(5, 0), WAIT_LEFT_MAX_SECONDS - 1, 999999999);
}

PTEST_MAIN()
