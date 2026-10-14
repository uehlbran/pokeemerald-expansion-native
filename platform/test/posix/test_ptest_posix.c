/*
 * test_ptest_posix.c: what the per-test time limit must leave alone on a
 * POSIX system: a test may use SIGALRM and alarm(2) itself, which the runner
 * neither takes nor resets (test_crash_reboot's children do).
 *
 * The handler only stores into a volatile sig_atomic_t, as signal-safety(7)
 * allows. It runs on the test's thread, which the alarm's end of the sleep
 * shows: the runner's watchdog thread blocks every signal (host_thread.h).
 */
#include <signal.h>
#include <unistd.h>

#include "ptest.h"

#define ALARM_SECONDS 1u
#define WAIT_SECONDS 3u

static volatile sig_atomic_t alarm_rang;

static void on_alarm(int signal_number)
{
    (void)signal_number;
    alarm_rang = 1;
}

PTEST(a_test_may_use_its_own_alarm_under_the_runner)
{
    struct sigaction mine = {.sa_handler = on_alarm}, previous;
    PT_ASSERT_EQ_INT(sigemptyset(&mine.sa_mask), 0);
    PT_ASSERT_EQ_INT(sigaction(SIGALRM, &mine, &previous), 0);
    alarm_rang = 0;
    (void)alarm(ALARM_SECONDS);
    unsigned left = WAIT_SECONDS;
    while (!alarm_rang && left > 0)
        left = sleep(left); /* an alarm ends the sleep early */
    const bool rang = alarm_rang != 0;
    PT_ASSERT_EQ_INT(sigaction(SIGALRM, &previous, NULL), 0);
    PT_ASSERT(rang);
    PT_ASSERT(left > 0); /* the sleep was cut short: the handler ran on this thread */
}

PTEST_MAIN()
