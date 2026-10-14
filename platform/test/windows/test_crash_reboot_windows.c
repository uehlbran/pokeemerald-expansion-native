/*
 * test_crash_reboot_windows.c: the reboot after a crash on Windows
 * (crash_reboot.h), for real: each test starts a child (windows_support.h)
 * that installs the reboot and crashes; the unhandled-exception filter starts
 * this very executable again with the child's arguments plus the state
 * (host_process_restart) and waits for it; this file's main, seeing the state
 * option, plays the rebooted program: it restores the state, checks it and
 * ends with a code, which the child, waiting, ends with too. Behaviour as in
 * ../test_crash_reboot.c and posix/test_crash_reboot_posix.c.
 */
// deps: gba_memmap.c crash_reboot.c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

#include "crash_reboot.h"
#include "crash_reboot_fixture.h"
#include "gba_memmap.h"
#include "host_fault_fixture.h"
#include "ptest.h"
#include "windows_support.h"

/* Codes of the rebooted process, which the crashed child ends with. */
#define CHILD_OK 42u
#define CHILD_WRONG_STATE 30u
#define CHILD_WRONG_ARGS 31u
#define CHILD_SETUP_FAILED 32u
#define CHILD_UNEXPECTED_REBOOT 33u
#define CHILD_NO_CRASH 34u

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

/* main, for a process the crash filter started. */
static uint32_t rebooted(int argc, char **argv)
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

/* The ways the children crash. */
static void execute_an_illegal_instruction(void)
{
    __asm__ volatile("ud2");
}

/* host_fault_fixture.h's division by zero, which no handler completes here. */
static void divide_with_no_handler(void)
{
    uint32_t quotient, remainder;
    divide_by_zero(&quotient, &remainder);
}

/* The crashing child: installs the reboot with mode as its argument, puts
 * pattern A in the region (unless mode is MODE_NO_PROGRESS) and crashes. */
static void crash_in_child(const char *mode, void (*crash)(void))
{
    char *argv[] = {(char *)self_name, (char *)mode, NULL};
    if (gba_memmap_init() != GBA_MEMMAP_OK)
        wsup_end(CHILD_SETUP_FAILED);
    clear_region();
    if (crash_reboot_init(2, argv) != CRASH_REBOOT_OK)
        wsup_end(CHILD_SETUP_FAILED);
    if (strcmp(mode, MODE_NO_PROGRESS) != 0)
        fill(pattern_a);
    crash();
    wsup_end(CHILD_NO_CRASH);
}

static const struct {
    const char *name;
    const char *mode;
    void (*crash)(void);
} children[] = {
    {"null_call", MODE_VERIFY, crash_by_null_call},        {"illegal", MODE_VERIFY, execute_an_illegal_instruction},
    {"divide", MODE_VERIFY, divide_with_no_handler},       {"crash_again", MODE_CRASH_AGAIN, crash_by_null_call},
    {"no_progress", MODE_NO_PROGRESS, crash_by_null_call},
};

static uint32_t child_code(const char *name)
{
    uint32_t code = 0;
    return wsup_run_child(name, &code) ? code : UINT32_MAX;
}

PTEST(a_null_call_reboots_with_the_persistent_state)
{
    PT_ASSERT_EQ_U32(child_code("null_call"), CHILD_OK);
}

PTEST(an_illegal_instruction_and_a_divide_error_reboot_too)
{
    PT_ASSERT_EQ_U32(child_code("illegal"), CHILD_OK);
    PT_ASSERT_EQ_U32(child_code("divide"), CHILD_OK);
}

PTEST(a_rebooted_process_reboots_again_with_one_state_option)
{
    PT_ASSERT_EQ_U32(child_code("crash_again"), CHILD_OK);
}

PTEST(a_crash_without_progress_ends_the_process)
{
    /* The region holds what the process started with: a reboot would loop,
     * so the fault ends the process with its code. */
    PT_ASSERT_EQ_U32(child_code("no_progress"), EXCEPTION_ACCESS_VIOLATION);
}

/* The process's unhandled-exception filter, read by setting it back. */
static LPTOP_LEVEL_EXCEPTION_FILTER current_filter(void)
{
    const LPTOP_LEVEL_EXCEPTION_FILTER filter = SetUnhandledExceptionFilter(NULL);
    (void)SetUnhandledExceptionFilter(filter);
    return filter;
}

PTEST(shutdown_puts_the_filter_back)
{
    char *argv[] = {(char *)self_name, NULL};
    const LPTOP_LEVEL_EXCEPTION_FILTER before = current_filter();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(crash_reboot_init(1, argv), CRASH_REBOOT_OK);
    const LPTOP_LEVEL_EXCEPTION_FILTER installed = current_filter();
    crash_reboot_shutdown();
    crash_reboot_shutdown(); /* twice is safe */
    PT_ASSERT(installed != before);
    PT_ASSERT(current_filter() == before);
}

int main(int argc, char **argv)
{
    int occurrences;
    if (state_argument(argc, argv, &occurrences) != NULL)
        wsup_end(rebooted(argc, argv));
    const char *const name = wsup_child_name();
    if (name == NULL)
        return ptest_main_for(&ptest_tests, stdout);
    for (size_t i = 0; i < sizeof children / sizeof children[0]; i++) {
        if (strcmp(children[i].name, name) == 0)
            crash_in_child(children[i].mode, children[i].crash);
    }
    return (int)WSUP_CHILD_RETURNED;
}
