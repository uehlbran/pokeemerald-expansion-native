/*
 * host_sleep.h: a sleep as long as asked, give or take a fraction of a
 * millisecond, whatever the system's timer resolution.
 *
 * Stands in for nothing on the GBA: the frame pacer (frame_pacer.h) sleeps
 * for most of a frame period, 16.7 ms at the GBA's speed and 4.2 ms at four
 * times it. SDL_Delay sleeps in the system's timer ticks, which SDL asks
 * Windows to make 1 ms (timeBeginPeriod) and which Windows 11 may leave at
 * 15.6 ms (for a process it deems in the background, or to save power); a
 * high-resolution waitable timer does not depend on them. Measured on a
 * Windows 11 laptop where the request was honoured: twenty sleeps of 2 ms
 * took 58 ms with SDL_Delay and 44 to 49 ms with the timer
 * (docs/DECISIONS.md, 2026-11-17).
 * Wraps, on POSIX (host_posix.c): nanosleep, again for what is left after
 * a signal. On Windows (host_windows_sleep.c): a waitable timer made with
 * CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 version 1803 and later,
 * below the 1903 the build needs), made once on the first call and kept
 * for the process's life; where it cannot be made, Sleep, in milliseconds.
 * Only the main thread calls it.
 */
#ifndef PLATFORM_HOST_SLEEP_H
#define PLATFORM_HOST_SLEEP_H

#include <stdint.h>

/* The longest sleep host_sleep_us takes in one call: a second. */
#define HOST_SLEEP_MAX_US UINT64_C(1000000)

/* Sleeps for microseconds (capped at HOST_SLEEP_MAX_US); 0 returns at once.
 * It may return a little late, never early by more than the system's
 * clock rounding. */
void host_sleep_us(uint64_t microseconds);

#endif /* PLATFORM_HOST_SLEEP_H */
