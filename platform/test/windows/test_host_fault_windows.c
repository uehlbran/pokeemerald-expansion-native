/*
 * test_host_fault_windows.c: where a fault goes on Windows when the installed
 * handler does not complete it (host_fault.h, host_windows_fault.c), in child
 * processes (windows_support.h), which end by the crash handler's code or by
 * the fault itself: a passed-on divide error reaches the crash filter, an
 * uninstalled handler is not called again, a divide error nobody completes
 * ends the process with its exception code, the crash handler gets every kind
 * of fault (a stack overflow included, on the stack the guarantee keeps), a
 * crash handler that returns lets the fault end the process, the fault
 * address is the access's, and the exceptions that are not divide errors
 * pass the vectored handler untouched. The handler that completes a division
 * is tested in ../test_host_fault.c.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

#include "host_fault.h"
#include "host_fault_fixture.h"
#include "ptest.h"
#include "windows_support.h"

/* Codes the children end with. */
#define CHILD_DIVIDE_HANDLER 4u    /* the divide handler ran, though uninstalled or not a divide error */
#define CHILD_FAULT_ADDRESS_OK 5u  /* the crash handler saw the expected fault address */
#define CHILD_FAULT_ADDRESS_BAD 6u /* ... and another one */
#define CHILD_CRASH_DIVIDE 10u     /* the crash handler got a divide error */
#define CHILD_RIGHT_CODE 11u       /* the crash handler got the exception the child raised */
#define CHILD_WRONG_CODE 12u       /* ... and another one */
#define CHILD_PASSED 13u           /* the body ran to its end */

/* An address no one maps: in the first page. */
#define UNMAPPED_ADDRESS 0x10u

/* The exception code the child's fault should arrive with. */
static DWORD expected_code;

static void end_with_divide_or_code(const struct host_fault *fault)
{
    if (host_fault_is_divide_error(fault))
        wsup_end(CHILD_CRASH_DIVIDE);
    wsup_end((DWORD)host_fault_number(fault) == expected_code ? CHILD_RIGHT_CODE : CHILD_WRONG_CODE);
}

static void return_from_the_crash_handler(const struct host_fault *fault)
{
    (void)fault;
}

static void end_by_fault_address(const struct host_fault *fault)
{
    wsup_end(host_fault_address(fault) == UNMAPPED_ADDRESS ? CHILD_FAULT_ADDRESS_OK : CHILD_FAULT_ADDRESS_BAD);
}

static bool pass_everything_on(struct host_fault *fault)
{
    (void)fault;
    return false;
}

static bool end_from_the_divide_handler(struct host_fault *fault)
{
    (void)fault;
    wsup_end(CHILD_DIVIDE_HANDLER);
}

/* --- the faults the children raise ----------------------------------------------------- */

static void divide_by_zero_here(void)
{
    uint32_t quotient, remainder;
    divide_by_zero(&quotient, &remainder);
}

static void store_to_an_unmapped_address(void)
{
    volatile uintptr_t where = UNMAPPED_ADDRESS; /* volatile: no constant address for the compiler to warn about */
    *(volatile uint8_t *)where = 1;
}

static void execute_an_illegal_instruction(void)
{
    __asm__ volatile("ud2");
}

static void execute_a_privileged_instruction(void)
{
    __asm__ volatile("hlt");
}

static void execute_a_breakpoint(void)
{
    __asm__ volatile("int3");
}

/* Deep enough to overflow the 2 MiB main stack; the flag keeps the
 * compiler from proving that it never ends. */
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

/* --- the children -------------------------------------------------------------------- */

static void pass_on_to_the_crash_handler(void)
{
    if (host_fault_install_crash(end_with_divide_or_code) != 0
        || host_fault_install_divide_error(pass_everything_on) != 0)
        return;
    divide_by_zero_here();
}

static void divide_after_uninstalling(void)
{
    if (host_fault_install_crash(end_with_divide_or_code) != 0
        || host_fault_install_divide_error(end_from_the_divide_handler) != 0)
        return;
    host_fault_uninstall_divide_error();
    divide_by_zero_here();
}

static void pass_on_with_no_crash_handler(void)
{
    if (host_fault_install_divide_error(pass_everything_on) != 0)
        return;
    divide_by_zero_here();
}

/* Installs the crash handler that compares codes, then faults. */
static void crash_with(DWORD code, void (*fault)(void))
{
    expected_code = code;
    if (host_fault_install_crash(end_with_divide_or_code) != 0)
        return;
    fault();
}

static void crash_by_access(void)
{
    crash_with(EXCEPTION_ACCESS_VIOLATION, store_to_an_unmapped_address);
}

static void crash_by_illegal_instruction(void)
{
    crash_with(EXCEPTION_ILLEGAL_INSTRUCTION, execute_an_illegal_instruction);
}

static void crash_by_privileged_instruction(void)
{
    crash_with(EXCEPTION_PRIV_INSTRUCTION, execute_a_privileged_instruction);
}

static void crash_by_breakpoint(void)
{
    crash_with(EXCEPTION_BREAKPOINT, execute_a_breakpoint);
}

static void crash_by_stack_overflow(void)
{
    crash_with(EXCEPTION_STACK_OVERFLOW, overflow_the_stack);
}

static void return_after_an_access(void)
{
    if (host_fault_install_crash(return_from_the_crash_handler) == 0)
        store_to_an_unmapped_address();
}

static void return_after_an_illegal_instruction(void)
{
    if (host_fault_install_crash(return_from_the_crash_handler) == 0)
        execute_an_illegal_instruction();
}

static void report_the_fault_address(void)
{
    if (host_fault_install_crash(end_by_fault_address) == 0)
        store_to_an_unmapped_address();
}

/* OutputDebugStringA raises an exception (DBG_PRINTEXCEPTION_C) and handles
 * it itself when no debugger is attached: the vectored handler sees it first
 * and must leave it alone. */
static void send_a_debug_string(void)
{
    if (host_fault_install_divide_error(end_from_the_divide_handler) != 0)
        return;
    OutputDebugStringA("test_host_fault_windows: not a divide error\n");
    wsup_end(CHILD_PASSED);
}

static const struct {
    const char *name;
    void (*body)(void);
} children[] = {
    {"pass_on", pass_on_to_the_crash_handler},
    {"uninstalled", divide_after_uninstalling},
    {"no_crash_handler", pass_on_with_no_crash_handler},
    {"access", crash_by_access},
    {"illegal", crash_by_illegal_instruction},
    {"privileged", crash_by_privileged_instruction},
    {"breakpoint", crash_by_breakpoint},
    {"overflow", crash_by_stack_overflow},
    {"return_access", return_after_an_access},
    {"return_illegal", return_after_an_illegal_instruction},
    {"fault_address", report_the_fault_address},
    {"debug_string", send_a_debug_string},
};

/* The code the named child ended with, or UINT32_MAX if it did not run. */
static uint32_t child_code(const char *name)
{
    uint32_t code = 0;
    return wsup_run_child(name, &code) ? code : UINT32_MAX;
}

/* --- the tests ------------------------------------------------------------------------ */

PTEST(a_divide_error_passed_on_reaches_the_crash_handler)
{
    PT_ASSERT_EQ_U32(child_code("pass_on"), CHILD_CRASH_DIVIDE);
}

PTEST(uninstall_gives_divide_errors_back_to_the_crash_handler)
{
    PT_ASSERT_EQ_U32(child_code("uninstalled"), CHILD_CRASH_DIVIDE);
}

PTEST(a_divide_error_no_handler_completes_ends_the_process_with_its_code)
{
    PT_ASSERT_EQ_U32(child_code("no_crash_handler"), EXCEPTION_INT_DIVIDE_BY_ZERO);
}

PTEST(the_crash_handler_gets_every_kind_of_fault)
{
    PT_ASSERT_EQ_U32(child_code("access"), CHILD_RIGHT_CODE);
    PT_ASSERT_EQ_U32(child_code("illegal"), CHILD_RIGHT_CODE);
    PT_ASSERT_EQ_U32(child_code("privileged"), CHILD_RIGHT_CODE);
    PT_ASSERT_EQ_U32(child_code("breakpoint"), CHILD_RIGHT_CODE);
}

PTEST(the_crash_handler_runs_after_a_stack_overflow)
{
    PT_ASSERT_EQ_U32(child_code("overflow"), CHILD_RIGHT_CODE);
}

PTEST(a_crash_handler_that_returns_lets_the_fault_end_the_process)
{
    PT_ASSERT_EQ_U32(child_code("return_access"), EXCEPTION_ACCESS_VIOLATION);
    PT_ASSERT_EQ_U32(child_code("return_illegal"), EXCEPTION_ILLEGAL_INSTRUCTION);
}

PTEST(the_fault_address_is_the_access_that_faulted)
{
    PT_ASSERT_EQ_U32(child_code("fault_address"), CHILD_FAULT_ADDRESS_OK);
}

PTEST(exceptions_that_are_not_divide_errors_pass_the_divide_handler)
{
    PT_ASSERT_EQ_U32(child_code("debug_string"), CHILD_PASSED);
}

/* DIV EDI (host_fault_fixture.h): two bytes. */
#define DIV_EDI_LENGTH 2u

static char seen_name[HOST_FAULT_NAME_SIZE];

static bool name_and_complete(struct host_fault *fault)
{
    host_fault_name(fault, seen_name);
    host_fault_set_register(fault, HOST_FAULT_PC, host_fault_register(fault, HOST_FAULT_PC) + DIV_EDI_LENGTH);
    return true;
}

PTEST(a_fault_is_named_by_its_exception_code_in_hexadecimal)
{
    uint32_t quotient, remainder;
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(name_and_complete), 0);
    divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT_EQ_STR(seen_name, "exception 0xC0000094"); /* EXCEPTION_INT_DIVIDE_BY_ZERO */
}

PTEST(installing_turns_the_error_dialogs_off)
{
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(pass_everything_on), 0);
    const UINT mode = GetErrorMode();
    host_fault_uninstall_divide_error();
    PT_ASSERT((mode & SEM_NOGPFAULTERRORBOX) != 0);
    PT_ASSERT((mode & SEM_FAILCRITICALERRORS) != 0);
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
