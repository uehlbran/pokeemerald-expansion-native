/*
 * host_windows_thread.c: the test time limit's watchdog on Windows
 * (host_thread.h, which has the contract and the ownership model): a thread
 * that waits on an event with a timeout and, when the deadline passes,
 * stores true in the caller's flag, once.
 *
 * Followed: Microsoft's documentation of CreateThread, CreateEventW, SetEvent,
 * WaitForSingleObject and GetTickCount64, and "Thread Stack Size".
 *
 * The deadline is on GetTickCount64's clock, milliseconds since boot, which
 * no one can set (the POSIX source follows the wall clock). What the two
 * threads share besides the caller's flag, the deadline and the stop request,
 * are C11 atomics accessed relaxed: they publish nothing but themselves, and
 * each write is followed by SetEvent on the auto-reset event the thread waits
 * on; a write the thread misses between its reads and its wait leaves the
 * event set, so the wait returns at once and the loop reads again. The
 * thread prints nothing and allocates nothing.
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>

#include "host_thread.h"

/* The largest deadline host_watchdog_start takes, as on POSIX. */
#define WATCHDOG_MAX_SECONDS (UINT32_MAX / 2u)

/* How long host_watchdog_start waits for its thread to begin waiting. */
#define WATCHDOG_START_MILLISECONDS 10000u

#define MILLISECONDS_PER_SECOND 1000u

/* The address space the watchdog's thread reserves for its stack, instead of
 * the image's 2 MiB (SizeOfStackReserve), which it would hold for nothing:
 * the thread only waits, reads the tick count and stores atomics, a few
 * frames deep. Windows rounds a reserve up to the 64 KiB allocation
 * granularity (Microsoft, "Thread Stack Size"). */
#define WATCHDOG_STACK_RESERVE ((SIZE_T)64 * 1024u)

struct host_watchdog {
    HANDLE thread;
    HANDLE changed;         /* auto-reset: a new deadline, or the stop */
    HANDLE waiting;         /* manual-reset: the thread is about to wait; start returns only then */
    atomic_ullong deadline; /* GetTickCount64's milliseconds */
    atomic_bool stopping;
    atomic_bool *expired; /* the caller's flag: the only thing written outside the watchdog */
};

static void watchdog_report(const char *call)
{
    (void)fprintf(stderr, "host_thread: %s failed (Windows error %lu)\n", call, (unsigned long)GetLastError());
}

static void set_deadline(struct host_watchdog *watchdog, unsigned seconds)
{
    const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)seconds * MILLISECONDS_PER_SECOND;
    atomic_store_explicit(&watchdog->deadline, deadline, memory_order_relaxed);
}

static DWORD WINAPI watchdog_thread(void *argument)
{
    struct host_watchdog *const watchdog = argument;
    (void)SetEvent(watchdog->waiting); /* a failure leaves start to time out and stop this thread */
    while (!atomic_load_explicit(&watchdog->stopping, memory_order_relaxed)) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG deadline = atomic_load_explicit(&watchdog->deadline, memory_order_relaxed);
        if (now >= deadline) {
            atomic_store_explicit(watchdog->expired, true, memory_order_relaxed); /* see host_thread.h */
            break;
        }
        /* INFINITE is the largest DWORD: a longer wait is cut short and the loop waits again. */
        const ULONGLONG left = deadline - now;
        const DWORD timeout = left < INFINITE ? (DWORD)left : INFINITE - 1u;
        if (WaitForSingleObject(watchdog->changed, timeout) == WAIT_FAILED)
            break;
    }
    return 0;
}

/* Closes what host_watchdog_start made, the thread's handle included if
 * there is one, and frees the watchdog. */
static void release(struct host_watchdog *watchdog)
{
    if (watchdog->thread != NULL)
        (void)CloseHandle(watchdog->thread);
    if (watchdog->waiting != NULL)
        (void)CloseHandle(watchdog->waiting);
    if (watchdog->changed != NULL)
        (void)CloseHandle(watchdog->changed);
    free(watchdog);
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
        (void)fprintf(stderr, "host_thread: calloc failed\n");
        return NULL;
    }
    watchdog->expired = expired;
    atomic_init(&watchdog->stopping, false);
    set_deadline(watchdog, seconds);
    watchdog->changed = CreateEventW(NULL, FALSE, FALSE, NULL);
    watchdog->waiting = watchdog->changed == NULL ? NULL : CreateEventW(NULL, TRUE, FALSE, NULL);
    if (watchdog->waiting == NULL) {
        watchdog_report("CreateEventW");
        release(watchdog);
        return NULL;
    }
    watchdog->thread = CreateThread(NULL, WATCHDOG_STACK_RESERVE, watchdog_thread, watchdog,
                                    STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (watchdog->thread == NULL) {
        watchdog_report("CreateThread");
        release(watchdog);
        return NULL;
    }
    if (WaitForSingleObject(watchdog->waiting, WATCHDOG_START_MILLISECONDS) != WAIT_OBJECT_0) {
        (void)fprintf(stderr, "host_thread: the watchdog's thread did not start\n");
        host_watchdog_stop(watchdog); /* the thread exists: end it before freeing */
        return NULL;
    }
    return watchdog;
}

int host_watchdog_restart(struct host_watchdog *watchdog, unsigned seconds)
{
    if (watchdog == NULL)
        return -1;
    if (seconds == 0 || seconds > WATCHDOG_MAX_SECONDS) {
        (void)fprintf(stderr, "host_thread: a watchdog needs a deadline of 1 to %u seconds\n", WATCHDOG_MAX_SECONDS);
        return -1;
    }
    set_deadline(watchdog, seconds);
    if (!SetEvent(watchdog->changed)) {
        watchdog_report("SetEvent");
        return -1;
    }
    return 0;
}

void host_watchdog_stop(struct host_watchdog *watchdog)
{
    if (watchdog == NULL)
        return;
    atomic_store_explicit(&watchdog->stopping, true, memory_order_relaxed);
    (void)SetEvent(watchdog->changed); /* a missed wake-up is caught by the loop's test before its next wait */
    if (WaitForSingleObject(watchdog->thread, INFINITE) != WAIT_OBJECT_0) {
        watchdog_report("WaitForSingleObject on the watchdog's thread");
        return; /* the thread may still use the watchdog: leak it rather than free it */
    }
    release(watchdog);
}
