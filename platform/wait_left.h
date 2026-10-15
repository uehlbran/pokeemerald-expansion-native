/*
 * wait_left.h: the relative wait from now to an absolute deadline, as
 * macOS's condition variable takes it (host_macos.c,
 * host_condition_wait_until, with pthread_cond_timedwait_relative_np).
 *
 * The wait is capped: Apple converts a relative wait to Mach time units, and a
 * far deadline (the watchdog allows UINT32_MAX / 2 seconds, about 68 years)
 * could overflow that conversion where the timebase is not 1:1. The caller
 * loops, and a capped wait that ends early is a wake-up like any other. It is
 * a pure function of the two times, so the Linux gate tests it
 * (test/test_wait_left.c) although only the macOS build calls it.
 */
#ifndef PLATFORM_WAIT_LEFT_H
#define PLATFORM_WAIT_LEFT_H

#include <stdbool.h>
#include <time.h>

/* The longest single wait: an hour. */
#define WAIT_LEFT_MAX_SECONDS 3600

/* Writes to left the time from now to deadline, both normalised timespecs on
 * the same clock, at most WAIT_LEFT_MAX_SECONDS; returns false, writing
 * nothing, when the deadline is not after now. */
bool wait_left(const struct timespec *deadline, const struct timespec *now, struct timespec *left);

#endif /* PLATFORM_WAIT_LEFT_H */
