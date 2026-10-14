/*
 * host_thread.h: a one-shot deadline that sets a flag.
 *
 * Stands in for nothing on the GBA: the unit tests' per-test time limit
 * (test/ptest.h) needs a clock that runs while the test does, and the one
 * way to have it on every target is a thread (Windows has no SIGALRM or
 * alarm(2), and a signal would interrupt game state at any instruction).
 * The platform is single-threaded by design (docs/C-RULES.md, "Concurrency");
 * this thread is the single exception, justified in docs/DECISIONS.md ("The
 * test time limit is kept by a flag-setting thread"), and it does nothing
 * but set a flag.
 *
 * Ownership model. The caller's thread owns all game and test state, and the
 * flag's meaning; the watchdog's thread owns the watchdog's own state (the
 * deadline and the stop request: behind its mutex on POSIX; on Windows in
 * relaxed C11 atomics whose changes an event announces) and writes exactly one
 * thing outside it: the caller's atomic flag, once. It calls no callback,
 * reads no caller state, prints nothing and allocates nothing. The flag is
 * a C11 atomic_bool: the watchdog stores true (relaxed: the flag carries no
 * data that needs publishing, the checking thread only wants to know that
 * the time has passed, and the join in host_watchdog_stop orders everything
 * else); the caller loads it (relaxed, for the same reason) and clears it
 * only while no watchdog runs on it.
 *
 * Wraps, on Linux and macOS (host_posix.c): a pthread that waits on a
 * condition variable until the deadline, against the wall clock
 * (pthread_cond_timedwait takes CLOCK_REALTIME on both systems; a clock set
 * during a test moves the deadline with it). On Windows
 * (host_windows_thread.c): a thread that waits on an auto-reset event with a
 * timeout against GetTickCount64 (CreateThread, WaitForSingleObject), the
 * deadline and the stop request in relaxed C11 atomics that SetEvent
 * announces; stop joins the thread by waiting on its handle.
 */
#ifndef PLATFORM_HOST_THREAD_H
#define PLATFORM_HOST_THREAD_H

#include <stdatomic.h>

struct host_watchdog;

/* Starts a watchdog that stores true in *expired once, seconds (1 to 2^31 - 1)
 * from now, unless it is stopped first. *expired must outlive the watchdog.
 * Returns it, or NULL after a report on stderr if the thread could not be
 * started. */
struct host_watchdog *host_watchdog_start(unsigned seconds, atomic_bool *expired);

/* Moves the deadline to seconds from now (1 to 2^31 - 1), longer or shorter. A
 * deadline that has passed has already set the flag; this does not clear it.
 * Returns 0, or -1 after a report on stderr (NULL: -1 without one); the old
 * deadline then stands. */
int host_watchdog_restart(struct host_watchdog *watchdog, unsigned seconds);

/* Cancels the deadline, waits for the thread to end and releases the
 * watchdog: no thread of it is left. NULL is accepted and does nothing. */
void host_watchdog_stop(struct host_watchdog *watchdog);

#endif /* PLATFORM_HOST_THREAD_H */
