/*
 * test_crash_reboot_windows.c: the crash reboot on Windows (crash_reboot.h),
 * for real: each test starts a child (windows_support.h) that sends its
 * stderr to a file the test names, installs the reboot and crashes. The
 * unhandled-exception filter starts no process: it writes the report, which
 * carries the persistent state, and ends the child with
 * CRASH_REBOOT_EXIT_CODE, for the parent to start the program again. The
 * test plays that parent: it checks the code and the report, and starts a
 * child the way the parent would, with the state from the report, which must
 * find it restored. Behaviour as in ../test_crash_reboot.c and
 * posix/test_crash_reboot_posix.c, but for who reboots.
 */
// deps: gba_memmap.c crash_reboot.c
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "crash_reboot.h"
#include "crash_reboot_fixture.h"
#include "gba_memmap.h"
#include "host_fault_fixture.h"
#include "ptest.h"
#include "test_support.h"
#include "windows_support.h"

/* Codes a child ends with when something before the crash goes wrong. */
#define CHILD_WRONG_STATE 30u
#define CHILD_SETUP_FAILED 32u
#define CHILD_NO_CRASH 34u

/* What the test passes its children: the file for their stderr, and the
 * state a restarted child is given, as the parent passes it. */
#define REPORT_VARIABLE "CRASH_REBOOT_WINDOWS_TEST_REPORT"
#define STATE_VARIABLE "CRASH_REBOOT_WINDOWS_TEST_STATE"

#define REPORT_TEXT_SIZE 2048u

/* The report's text around the parts that vary: the exception code, the
 * two addresses (16 hex digits each) and the state. */
#define HEAD "crash_reboot: exception 0x"
#define ADDRESS_DIGITS 16u
#define REBOOT_TAIL ": ending with exit code 75 for the parent to start the program again with --persistent-state "
#define NO_REBOOT_TAIL                                                                                         \
    ": the test runner's persistent state is as the process started with; a reboot would crash again, so the " \
    "process ends here\n"

/* Sends this child's stderr to the file the test named. */
static void report_to_the_test(void)
{
    const char *const path = getenv(REPORT_VARIABLE);
    FILE *const file = path == NULL ? NULL : fopen(path, "wb");
    static struct tsup_stderr saved; /* kept: the child ends with stderr redirected */
    if (file == NULL || !tsup_redirect_stderr(&saved, file))
        wsup_end(CHILD_SETUP_FAILED);
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

/* Deep enough to overflow the 2 MiB main stack; the flag keeps the compiler
 * from proving that it never ends. The filter then runs on what
 * SetThreadStackGuarantee kept at the stack's end (host_fault.h). */
static volatile bool keep_recursing = true;

static unsigned recurse(unsigned depth) // NOLINT(misc-no-recursion): overflowing the stack is the point
{
    volatile unsigned char page[4096];
    page[0] = (unsigned char)depth;
    if (keep_recursing)
        return recurse(depth + 1u) + page[0];
    return page[0];
}

static void overflow_the_stack(void)
{
    (void)recurse(0);
}

/* The crashing child: installs the reboot, puts pattern A in the region
 * (unless progress is false) and crashes. */
static void crash_in_child(bool progress, void (*crash)(void))
{
    char *argv[] = {(char *)self_name, NULL};
    report_to_the_test();
    if (gba_memmap_init() != GBA_MEMMAP_OK)
        wsup_end(CHILD_SETUP_FAILED);
    clear_region();
    if (crash_reboot_init(1, argv) != CRASH_REBOOT_OK)
        wsup_end(CHILD_SETUP_FAILED);
    if (progress)
        fill(pattern_a);
    crash();
    wsup_end(CHILD_NO_CRASH);
}

/* The child the parent starts again: given the state as the parent passes
 * it, it must find pattern A restored; it then progresses to pattern B and
 * crashes again. */
static void restarted_child(void)
{
    char *const state = getenv(STATE_VARIABLE);
    char *argv[] = {(char *)self_name, CRASH_REBOOT_OPTION, state, NULL};
    report_to_the_test();
    if (state == NULL || gba_memmap_init() != GBA_MEMMAP_OK || crash_reboot_init(3, argv) != CRASH_REBOOT_OK
        || crash_reboot_restore(state) != CRASH_REBOOT_OK)
        wsup_end(CHILD_SETUP_FAILED);
    if (!holds(pattern_a))
        wsup_end(CHILD_WRONG_STATE);
    fill(pattern_b);
    crash_by_null_call();
    wsup_end(CHILD_NO_CRASH);
}

static void crash_with_progress_by_null_call(void)
{
    crash_in_child(true, crash_by_null_call);
}
static void crash_with_progress_by_illegal_instruction(void)
{
    crash_in_child(true, execute_an_illegal_instruction);
}
static void crash_with_progress_by_division(void)
{
    crash_in_child(true, divide_with_no_handler);
}
static void crash_with_progress_by_stack_overflow(void)
{
    crash_in_child(true, overflow_the_stack);
}
static void crash_without_progress(void)
{
    crash_in_child(false, crash_by_null_call);
}

static const struct {
    const char *name;
    void (*body)(void);
} children[] = {
    {"null_call", crash_with_progress_by_null_call}, {"illegal", crash_with_progress_by_illegal_instruction},
    {"divide", crash_with_progress_by_division},     {"overflow", crash_with_progress_by_stack_overflow},
    {"no_progress", crash_without_progress},         {"restarted", restarted_child},
};

/* Runs child name with its stderr in a scratch file, and reads the file into
 * text. The child's code, or UINT32_MAX if it could not be run or read. */
static uint32_t run_child(const char *name, char text[REPORT_TEXT_SIZE])
{
    char path[MAX_PATH];
    text[0] = '\0';
    if (!tsup_make_temp_file(path, sizeof path, "crash_reboot_report"))
        return UINT32_MAX;
    uint32_t code = UINT32_MAX;
    const bool ran = tsup_set_environment(REPORT_VARIABLE, path) && wsup_run_child(name, &code);
    (void)tsup_unset_environment(REPORT_VARIABLE);
    FILE *const file = fopen(path, "rb");
    const size_t length = file == NULL ? 0 : fread(text, 1, REPORT_TEXT_SIZE - 1, file);
    text[length] = '\0';
    const bool read = file != NULL && fclose(file) == 0 && length < REPORT_TEXT_SIZE - 1;
    (void)tsup_remove_file(path);
    return ran && read ? code : UINT32_MAX;
}

static bool hex_digits_at(const char *text, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (text[i] == '\0' || strchr("0123456789abcdef", text[i]) == NULL)
            return false;
    }
    return true;
}

/* Whether text is the report of exception `exception` (8 upper-case hex
 * digits) followed by tail; returns where tail ends, or NULL. */
static const char *report_up_to(const char *text, const char *exception, const char *tail)
{
    static const char before_address[] = ", fault address 0x";
    const size_t head_length = sizeof HEAD - 1;
    if (strncmp(text, HEAD, head_length) != 0 || strncmp(text + head_length, exception, 8) != 0)
        return NULL;
    text += head_length + 8;
    if (strncmp(text, " at 0x", 6) != 0 || !hex_digits_at(text + 6, ADDRESS_DIGITS))
        return NULL;
    text += 6 + ADDRESS_DIGITS;
    if (strncmp(text, before_address, sizeof before_address - 1) != 0
        || !hex_digits_at(text + sizeof before_address - 1, ADDRESS_DIGITS))
        return NULL;
    text += sizeof before_address - 1 + ADDRESS_DIGITS;
    return strncmp(text, tail, strlen(tail)) == 0 ? text + strlen(tail) : NULL;
}

/* Whether text is exactly the reboot report of exception `exception` with
 * pattern's state, one line. */
static bool is_reboot_report(const char *text, const char *exception, uint8_t (*pattern)(size_t))
{
    const char *const state = report_up_to(text, exception, REBOOT_TAIL);
    return state != NULL && strncmp(state, hex_of(pattern), CRASH_REBOOT_STATE_HEX_LENGTH) == 0
           && strcmp(state + CRASH_REBOOT_STATE_HEX_LENGTH, "\n") == 0;
}

PTEST(a_null_call_ends_with_the_reboot_code_and_reports_the_state)
{
    char text[REPORT_TEXT_SIZE];
    PT_ASSERT_EQ_U32(run_child("null_call", text), CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C0000005", pattern_a));
}

PTEST(an_illegal_instruction_and_a_divide_error_do_too)
{
    char text[REPORT_TEXT_SIZE];
    PT_ASSERT_EQ_U32(run_child("illegal", text), CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C000001D", pattern_a));
    PT_ASSERT_EQ_U32(run_child("divide", text), CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C0000094", pattern_a));
}

PTEST(a_stack_overflow_does_too_on_the_stack_kept_for_the_handler)
{
    char text[REPORT_TEXT_SIZE];
    PT_ASSERT_EQ_U32(run_child("overflow", text), CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C00000FD", pattern_a)); /* EXCEPTION_STACK_OVERFLOW */
}

PTEST(the_program_started_again_with_the_reported_state_finds_it_restored)
{
    char text[REPORT_TEXT_SIZE];
    PT_ASSERT_EQ_U32(run_child("null_call", text), CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C0000005", pattern_a));
    /* As the parent does: the digits the last report ends with. */
    static char state[CRASH_REBOOT_STATE_HEX_LENGTH + 1];
    memcpy(state, text + strlen(text) - 1 - CRASH_REBOOT_STATE_HEX_LENGTH, CRASH_REBOOT_STATE_HEX_LENGTH);
    PT_ASSERT(tsup_set_environment(STATE_VARIABLE, state));
    const uint32_t code = run_child("restarted", text);
    (void)tsup_unset_environment(STATE_VARIABLE);
    PT_ASSERT_EQ_U32(code, CRASH_REBOOT_EXIT_CODE);
    PT_ASSERT(is_reboot_report(text, "C0000005", pattern_b));
}

PTEST(a_crash_without_progress_ends_the_process_with_the_exception_code)
{
    /* The region holds what the process started with: a reboot would loop,
     * so the fault ends the process with its code, which no parent takes
     * for a reboot. */
    char text[REPORT_TEXT_SIZE];
    PT_ASSERT_EQ_U32(run_child("no_progress", text), EXCEPTION_ACCESS_VIOLATION);
    const char *const end = report_up_to(text, "C0000005", NO_REBOOT_TAIL);
    PT_ASSERT(end != NULL && *end == '\0');
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

int main(void)
{
    const char *const name = wsup_child_name();
    if (name == NULL)
        return ptest_main_for(&ptest_tests, stdout);
    for (size_t i = 0; i < sizeof children / sizeof children[0]; i++) {
        if (strcmp(children[i].name, name) == 0)
            children[i].body();
    }
    return (int)WSUP_CHILD_RETURNED;
}
