/*
 * host_windows_sleep.c: host_sleep_us on Windows (host_sleep.h), on a
 * high-resolution waitable timer (Microsoft, "CreateWaitableTimerExW",
 * CREATE_WAITABLE_TIMER_HIGH_RESOLUTION), which does not depend on the
 * system's timer resolution.
 */
#include "host_sleep.h"

#include <stdbool.h>

#include <windows.h>

/* MinGW-w64's headers may predate the flag (winbase.h, Windows 10 1803). */
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

/* A waitable timer's due time is in 100 ns units, negative for "from now". */
#define HUNDRED_NS_PER_US 10
#define US_PER_MS UINT64_C(1000)

static struct {
    bool tried; /* the timer was asked for once; timer is NULL if it was refused */
    HANDLE timer;
} sleeper;

/* Sleeps on the timer; false if the timer is not there or a call failed. */
static bool sleep_on_timer(uint64_t microseconds)
{
    if (!sleeper.tried) {
        sleeper.tried = true;
        sleeper.timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (sleeper.timer == NULL)
        return false;
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)microseconds * HUNDRED_NS_PER_US;
    if (!SetWaitableTimer(sleeper.timer, &due, 0, NULL, NULL, FALSE))
        return false;
    return WaitForSingleObject(sleeper.timer, INFINITE) == WAIT_OBJECT_0;
}

void host_sleep_us(uint64_t microseconds)
{
    if (microseconds == 0)
        return;
    if (microseconds > HOST_SLEEP_MAX_US)
        microseconds = HOST_SLEEP_MAX_US;
    if (!sleep_on_timer(microseconds))
        Sleep((DWORD)(microseconds / US_PER_MS));
}
