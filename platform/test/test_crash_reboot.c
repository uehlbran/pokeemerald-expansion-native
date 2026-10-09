/*
 * test_crash_reboot.c: the reboot after a crash (crash_reboot.h).
 *
 * Expected behaviour from expansion's test runner (test/test_runner.c: the
 * 100h bytes at 03007F00h must come back after the reboot, and a reboot
 * that makes no progress would loop) and GBATEK "BIOS Reset Functions"
 * (RegisterRamReset keeps the last 200h bytes of IWRAM).
 *
 * The reboots are real: each test forks a child that installs the handlers
 * and crashes, the handler execs this very binary (/proc/self/exe) with the
 * child's arguments plus the state, and this file's main, seeing a child
 * mode in argv[1], plays the rebooted program: it restores the state, checks
 * it and exits with a code the parent waits for.
 */
// deps: gba_memmap.c
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "crash_reboot.h"
#include "gba_memmap.h"
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

static volatile uint8_t *region(void)
{
    return (volatile uint8_t *)(uintptr_t)CRASH_REBOOT_PERSISTENT_ADDRESS;
}

static uint8_t pattern_a(size_t i)
{
    return (uint8_t)(i ^ 0x5Au);
}
static uint8_t pattern_b(size_t i)
{
    return (uint8_t)(i * 7u + 1u);
}

static void fill(uint8_t (*pattern)(size_t))
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        region()[i] = pattern(i);
}

static bool holds(uint8_t (*pattern)(size_t))
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        if (region()[i] != pattern(i))
            return false;
    }
    return true;
}

static void clear_region(void)
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        region()[i] = 0;
}

/* A call through a NULL function pointer, as "Tests resume after CRASH". */
static void (*volatile null_function)(void);

static void crash_by_null_call(void)
{
    null_function();
}

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

static const char *self_name = "test_crash_reboot";

/* Seconds a forked child (and every reboot of it: alarm(2) survives execve)
 * may take before SIGALRM ends it, so that a regressed loop guard fails the
 * test instead of rebooting forever. */
#define CHILD_TIME_LIMIT_SECONDS 10u

/* Forks a child that installs the reboot with mode as its argument, puts
 * pattern A in the region (unless mode is MODE_NO_PROGRESS) and crashes by
 * crash(signal). Returns the child's wait status, or -1. */
static int run_crashing_child(const char *mode, int signal_number)
{
    (void)fflush(stdout);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        char *argv[] = {(char *)self_name, (char *)mode, NULL};
        (void)alarm(CHILD_TIME_LIMIT_SECONDS);
        if (gba_memmap_init() != GBA_MEMMAP_OK)
            _exit(CHILD_SETUP_FAILED);
        clear_region();
        if (crash_reboot_init(2, argv) != CRASH_REBOOT_OK)
            _exit(CHILD_SETUP_FAILED);
        if (strcmp(mode, MODE_NO_PROGRESS) != 0)
            fill(pattern_a);
        if (signal_number == SIGSEGV)
            crash_by_null_call();
        else
            (void)raise(signal_number);
        _exit(CHILD_NO_CRASH);
    }
    int status;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return status;
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

static char state_text[CRASH_REBOOT_STATE_HEX_LENGTH + 1];

static const char *hex_of(uint8_t (*pattern)(size_t))
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        state_text[2 * i] = digits[pattern(i) >> 4];
        state_text[2 * i + 1] = digits[pattern(i) & 0x0Fu];
    }
    state_text[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
    return state_text;
}

PTEST(restore_puts_the_bytes_back)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    clear_region();
    int result = crash_reboot_restore(hex_of(pattern_b));
    bool restored = holds(pattern_b);
    clear_region();
    PT_ASSERT_EQ_INT(result, CRASH_REBOOT_OK);
    PT_ASSERT(restored);
}

PTEST(restore_rejects_a_wrong_length_or_a_non_hex_digit)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    clear_region();
    char text[CRASH_REBOOT_STATE_HEX_LENGTH + 2];
    memcpy(text, hex_of(pattern_a), sizeof state_text);
    int too_short = crash_reboot_restore("00ff");
    text[CRASH_REBOOT_STATE_HEX_LENGTH] = '0';
    text[CRASH_REBOOT_STATE_HEX_LENGTH + 1] = '\0';
    int too_long = crash_reboot_restore(text);
    text[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
    text[CRASH_REBOOT_STATE_HEX_LENGTH - 1] = 'g';
    int not_hex = crash_reboot_restore(text);
    text[CRASH_REBOOT_STATE_HEX_LENGTH - 1] = 'A';
    int upper_case = crash_reboot_restore(text);
    int null_text = crash_reboot_restore(NULL);
    bool untouched = true;
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        untouched = untouched && region()[i] == 0;
    PT_ASSERT_EQ_INT(too_short, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(too_long, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(not_hex, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(upper_case, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(null_text, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT(untouched);
}

PTEST(init_rejects_no_arguments_and_too_many)
{
    static char *many[CRASH_REBOOT_MAX_ARGS];
    for (size_t i = 0; i < CRASH_REBOOT_MAX_ARGS; i++)
        many[i] = (char *)self_name;
    char *one[] = {(char *)self_name, NULL};
    PT_ASSERT_EQ_INT(crash_reboot_init(0, one), CRASH_REBOOT_ERR_ARGS);
    PT_ASSERT_EQ_INT(crash_reboot_init(1, NULL), CRASH_REBOOT_ERR_ARGS);
    /* The reboot adds two: CRASH_REBOOT_MAX_ARGS - 2 of the caller's fit. */
    PT_ASSERT_EQ_INT(crash_reboot_init((int)CRASH_REBOOT_MAX_ARGS - 1, many), CRASH_REBOOT_ERR_ARGS);
    PT_ASSERT_EQ_INT(crash_reboot_init((int)CRASH_REBOOT_MAX_ARGS - 2, many), CRASH_REBOOT_OK);
    crash_reboot_shutdown();
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
