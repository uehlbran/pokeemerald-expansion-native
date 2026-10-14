/*
 * test_crash_reboot_posix.c: the reboot after a crash (crash_reboot.h), for
 * real: each test forks a child that installs the handlers and crashes, the
 * handler execs this very binary (/proc/self/exe) with the child's arguments
 * plus the state, and this file's main, seeing a child mode in argv[1],
 * plays the rebooted program: it restores the state, checks it and exits with
 * a code the parent waits for. Behaviour as in ../test_crash_reboot.c.
 */
// deps: gba_memmap.c crash_reboot.c
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "crash_reboot.h"
#include "crash_reboot_fixture.h"
#include "gba_memmap.h"
#include "posix_support.h"
#include "ptest.h"

/* Exit codes of the rebooted child. */
#define CHILD_OK 42
#define CHILD_WRONG_STATE 3
#define CHILD_WRONG_ARGS 4
#define CHILD_SETUP_FAILED 5
#define CHILD_UNEXPECTED_REBOOT 6
#define CHILD_NO_CRASH 7

#define MODE_VERIFY "--child-verify"
#define MODE_CRASH_AGAIN "--child-crash-again"
#define MODE_NO_PROGRESS "--child-no-progress"

/* The value after CRASH_REBOOT_OPTION, and how often the option appears. */
static const char *state_argument(int argc, char **argv, int *occurrences)
{
    const char *value = NULL;
    *occurrences = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], CRASH_REBOOT_OPTION) == 0) {
            value = argv[i + 1];
            (*occurrences)++;
        }
    }
    return value;
}

/* main, for a process the handler started. */
static int rebooted_child(int argc, char **argv)
{
    int occurrences;
    const char *state = state_argument(argc, argv, &occurrences);
    if (occurrences != 1 || argc != 4)
        return CHILD_WRONG_ARGS;
    if (gba_memmap_init() != GBA_MEMMAP_OK || crash_reboot_restore(state) != CRASH_REBOOT_OK)
        return CHILD_SETUP_FAILED;
    if (strcmp(argv[1], MODE_VERIFY) == 0)
        return holds(pattern_a) ? CHILD_OK : CHILD_WRONG_STATE;
    if (strcmp(argv[1], MODE_CRASH_AGAIN) == 0) {
        if (holds(pattern_b))
            return CHILD_OK;
        if (!holds(pattern_a))
            return CHILD_WRONG_STATE;
        /* Rebooted once: install again, progress, crash again. */
        if (crash_reboot_init(argc, argv) != CRASH_REBOOT_OK)
            return CHILD_SETUP_FAILED;
        fill(pattern_b);
        crash_by_null_call();
        return CHILD_NO_CRASH;
    }
    return CHILD_UNEXPECTED_REBOOT;
}

struct crashing_child {
    const char *mode;
    int signal_number;
};

/* The forked child (psup_run_child: no core file, an alarm that survives every
 * reboot, so that a regressed loop guard fails the test instead of rebooting
 * forever): installs the reboot with mode as its argument, puts pattern A in
 * the region (unless mode is MODE_NO_PROGRESS) and crashes by the signal. */
static void crash_in_child(void *context)
{
    const struct crashing_child *const child = context;
    char *argv[] = {(char *)self_name, (char *)child->mode, NULL};
    if (gba_memmap_init() != GBA_MEMMAP_OK)
        _exit(CHILD_SETUP_FAILED);
    clear_region();
    if (crash_reboot_init(2, argv) != CRASH_REBOOT_OK)
        _exit(CHILD_SETUP_FAILED);
    if (strcmp(child->mode, MODE_NO_PROGRESS) != 0)
        fill(pattern_a);
    if (child->signal_number == SIGSEGV)
        crash_by_null_call();
    else
        (void)raise(child->signal_number);
    _exit(CHILD_NO_CRASH);
}

/* Runs the child; its wait status, or -1. */
static int run_crashing_child(const char *mode, int signal_number)
{
    struct crashing_child child = {mode, signal_number};
    return psup_run_child(crash_in_child, &child);
}

static bool exited_with(int status, int code)
{
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == code;
}

PTEST(a_null_call_reboots_with_the_persistent_state)
{
    int status = run_crashing_child(MODE_VERIFY, SIGSEGV);
    PT_ASSERT(status != -1);
    PT_ASSERT_EQ_INT(WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status), CHILD_OK);
}

PTEST(every_crash_signal_reboots)
{
    static const int signals[] = {SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGSEGV};
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++) {
        int status = run_crashing_child(MODE_VERIFY, signals[i]);
        PT_ASSERT(status != -1);
        PT_ASSERT_EQ_INT(WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status), CHILD_OK);
    }
}

PTEST(a_rebooted_process_reboots_again_with_one_state_option)
{
    int status = run_crashing_child(MODE_CRASH_AGAIN, SIGSEGV);
    PT_ASSERT(exited_with(status, CHILD_OK));
}

PTEST(a_crash_without_progress_ends_the_process)
{
    /* The region still holds what the process started with: a reboot would
     * loop, so the signal kills the process. */
    int status = run_crashing_child(MODE_NO_PROGRESS, SIGSEGV);
    PT_ASSERT(status != -1);
    PT_ASSERT(WIFSIGNALED(status));
    PT_ASSERT_EQ_INT(WTERMSIG(status), SIGSEGV);
}

PTEST(shutdown_restores_the_default_actions)
{
    char *argv[] = {(char *)self_name, NULL};
    struct sigaction installed, restored;
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(crash_reboot_init(1, argv), CRASH_REBOOT_OK);
    PT_ASSERT_EQ_INT(sigaction(SIGILL, NULL, &installed), 0);
    crash_reboot_shutdown();
    crash_reboot_shutdown(); /* twice is safe */
    PT_ASSERT_EQ_INT(sigaction(SIGILL, NULL, &restored), 0);
    PT_ASSERT(installed.sa_flags & SA_SIGINFO);
    PT_ASSERT(installed.sa_sigaction != NULL);
    PT_ASSERT(installed.sa_flags & SA_ONSTACK);
    PT_ASSERT(restored.sa_handler == SIG_DFL);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        return rebooted_child(argc, argv);
    return ptest_main_for(&ptest_tests, stdout);
}
