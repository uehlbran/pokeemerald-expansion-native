/*
 * test_host_thread_posix.c: that no signal handler ever runs on a watchdog's
 * thread (host_thread.h), whose ownership model lets it touch nothing of the
 * test's. A signal sent to the process goes to any one thread that does not
 * block it (signal(7)). Linux gives it to the sending thread whenever that
 * thread can take it, so here the test thread blocks the signal while it
 * sends: the kernel must then look for another thread. The watchdog's thread
 * blocks every signal, so there is none, and the signal waits, pending, until
 * the test thread unblocks it and handles it itself.
 *
 * The handler only stores into a volatile sig_atomic_t, as signal-safety(7)
 * allows; the variable is thread-local, so it says which thread ran the
 * handler.
 */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <unistd.h>

#include "host_thread.h"
#include "ptest.h"
#include "test_support.h"

#define ROUNDS 200u
#define WATCHDOG_SECONDS 30u             /* far past the test: the watchdog only has to be there */
#define TAKE_MILLISECONDS 2u             /* how long another thread has to take the signal, if one could */
#define HANDLED_LIMIT_MILLISECONDS 1000u /* how long the handler may take to run after the unblock */

static _Thread_local volatile sig_atomic_t handled_on_this_thread;

static void on_user_signal(int signal_number)
{
    (void)signal_number;
    handled_on_this_thread = 1;
}

/* Sends the signal to the process while this thread blocks it, gives another
 * thread the time to take it, and unblocks it. True if the handler then ran
 * on this thread. pthread_sigmask delivers a pending signal it unblocks before
 * it returns; the bounded wait after it is for a runtime that defers the
 * handler (ThreadSanitizer runs it at its next interceptor). */
static bool handled_here_once_unblocked(const sigset_t *user)
{
    handled_on_this_thread = 0;
    if (pthread_sigmask(SIG_BLOCK, user, NULL) != 0 || kill(getpid(), SIGUSR1) != 0)
        return false;
    tsup_sleep_milliseconds(TAKE_MILLISECONDS);
    if (pthread_sigmask(SIG_UNBLOCK, user, NULL) != 0)
        return false;
    const uint64_t unblocked = tsup_monotonic_milliseconds();
    while (handled_on_this_thread == 0 && tsup_monotonic_milliseconds() - unblocked < HANDLED_LIMIT_MILLISECONDS)
        tsup_sleep_milliseconds(1);
    return handled_on_this_thread != 0;
}

PTEST(a_signal_sent_to_the_process_is_never_handled_on_the_watchdog_thread)
{
    sigset_t user;
    PT_ASSERT_EQ_INT(sigemptyset(&user), 0);
    PT_ASSERT_EQ_INT(sigaddset(&user, SIGUSR1), 0);
    struct sigaction mine = {.sa_handler = on_user_signal}, previous;
    PT_ASSERT_EQ_INT(sigemptyset(&mine.sa_mask), 0);
    PT_ASSERT_EQ_INT(sigaction(SIGUSR1, &mine, &previous), 0);
    atomic_bool expired = false;
    struct host_watchdog *const watchdog = host_watchdog_start(WATCHDOG_SECONDS, &expired);
    unsigned rounds = 0;
    while (watchdog != NULL && rounds < ROUNDS && handled_here_once_unblocked(&user))
        rounds++;
    host_watchdog_stop(watchdog);
    PT_ASSERT_EQ_INT(sigaction(SIGUSR1, &previous, NULL), 0);
    PT_ASSERT(watchdog != NULL);
    PT_ASSERT_EQ_INT(rounds, ROUNDS);
}

PTEST_MAIN()
