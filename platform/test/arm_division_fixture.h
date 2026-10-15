/*
 * arm_division_fixture.h: what the POSIX tests of a divide error that
 * arm_division.h passes on share (posix/test_arm_division_posix.c and the
 * per-OS linux/test_arm_division_linux.c and darwin/test_arm_division_darwin.c):
 * a handler of the test's own installed before arm_division_init, which counts
 * the faults that reach it and long-jumps out (siglongjmp is
 * async-signal-safe), and the count after one division.
 */
#ifndef PLATFORM_TEST_ARM_DIVISION_FIXTURE_H
#define PLATFORM_TEST_ARM_DIVISION_FIXTURE_H

#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>

#include "arm_division.h"

/* The handler installed before arm_division_init gets what it cannot do. */
static sigjmp_buf previous_handler_target;
static volatile sig_atomic_t previous_handler_calls;

static inline void previous_handler(int signal_number, siginfo_t *info, void *context)
{
    (void)signal_number;
    (void)info;
    (void)context;
    previous_handler_calls++;
    siglongjmp(previous_handler_target, 1);
}

/* The same, installed without SA_SIGINFO: a plain signal handler. */
static inline void previous_plain_handler(int signal_number)
{
    (void)signal_number;
    previous_handler_calls++;
    siglongjmp(previous_handler_target, 1);
}

/* How many times division's divide error reached the handler that was
 * installed (mine) before arm_division_init, or a negative number if the
 * harness failed (-1) or the module did not restore mine at shutdown (-2). */
static inline int calls_after_with(const struct sigaction *mine, void (*division)(void))
{
    struct sigaction saved;
    if (sigaction(SIGFPE, mine, &saved) != 0)
        return -1;
    previous_handler_calls = 0;
    if (arm_division_init() != ARM_DIVISION_OK)
        return -1;
    if (sigsetjmp(previous_handler_target, 1) == 0)
        division();
    arm_division_shutdown();
    struct sigaction now;
    if (sigaction(SIGFPE, &saved, &now) != 0)
        return -1;
    const bool restored = now.sa_sigaction == mine->sa_sigaction;
    return restored ? (int)previous_handler_calls : -2;
}

static inline int calls_after(void (*division)(void))
{
    struct sigaction mine;
    memset(&mine, 0, sizeof mine);
    mine.sa_sigaction = previous_handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);
    return calls_after_with(&mine, division);
}

static inline int calls_after_plain(void (*division)(void))
{
    struct sigaction mine;
    memset(&mine, 0, sizeof mine);
    mine.sa_handler = previous_plain_handler;
    sigemptyset(&mine.sa_mask);
    return calls_after_with(&mine, division);
}

#endif /* PLATFORM_TEST_ARM_DIVISION_FIXTURE_H */
