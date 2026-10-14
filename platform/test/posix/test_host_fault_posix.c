/*
 * test_host_fault_posix.c: where a fault goes when the installed handler does
 * not complete it (host_fault.h), in forked children, which end by the crash
 * handler's _exit code or by the fault itself: a passed-on divide error
 * reaches the crash handler installed before, an uninstalled handler is not
 * called again, a crash handler gets every crash fault, and a fault whose
 * handler returns ends the process. The handlers here call only _exit,
 * sigaction and raise, which signal-safety(7) allows. The handler that
 * completes a division is tested in ../test_host_fault.c.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_fault_fixture.h"
#include "host_image.h"
#include "posix_child.h"
#include "posix_support.h"
#include "ptest.h"

/* --- Where faults end, in children ----------------------------------------------- */

/* Exit codes of the children. */
#define CHILD_DIVIDE_HANDLER 4     /* the divide handler ran after it was uninstalled */
#define CHILD_FAULT_ADDRESS_OK 5   /* the crash handler saw the expected fault address */
#define CHILD_FAULT_ADDRESS_BAD 6  /* ... and another one */
#define CHILD_PLAIN_PREVIOUS 7     /* a previous handler without SA_SIGINFO got the fault */
#define CHILD_ON_ALTERNATE_STACK 8 /* the divide handler ran off the main stack */
#define CHILD_ON_MAIN_STACK 9      /* ... and on it */
#define CHILD_CRASH_DIVIDE 10      /* the crash handler got a divide error */
#define CHILD_CRASH_NUMBER_BASE 64 /* the crash handler got fault number n: 64 + n */

/* A child's body, as psup_run_child takes it. */
struct child_body {
    void (*run)(void);
};

static void call_body(void *context)
{
    const struct child_body *const body = context;
    body->run();
}

/* Runs body in a forked child (no core file when a fault ends it, an alarm
 * for a handler that resumed at the faulting instruction again and would loop
 * forever); its wait status, or -1. */
static int in_child(void (*body)(void))
{
    struct child_body child = {body};
    return psup_run_child(call_body, &child);
}

static void exit_with_divide_or_number(const struct host_fault *fault)
{
    _exit(host_fault_is_divide_error(fault) ? CHILD_CRASH_DIVIDE : CHILD_CRASH_NUMBER_BASE + host_fault_number(fault));
}

static bool pass_everything_on(struct host_fault *fault)
{
    (void)fault;
    return false;
}

static bool exit_from_the_divide_handler(struct host_fault *fault)
{
    (void)fault;
    _exit(CHILD_DIVIDE_HANDLER);
}

static void divide_by_zero_in_child(void)
{
    uint32_t quotient, remainder;
    divide_by_zero(&quotient, &remainder);
}

static void pass_on_to_the_crash_handler(void)
{
    if (host_fault_install_crash(exit_with_divide_or_number) != 0
        || host_fault_install_divide_error(pass_everything_on) != 0)
        return;
    divide_by_zero_in_child();
}

PTEST(a_divide_error_passed_on_reaches_the_handler_installed_before)
{
    PT_ASSERT_EQ_INT(psup_exit_code(in_child(pass_on_to_the_crash_handler)), CHILD_CRASH_DIVIDE);
}

static void divide_after_uninstalling(void)
{
    if (host_fault_install_crash(exit_with_divide_or_number) != 0
        || host_fault_install_divide_error(exit_from_the_divide_handler) != 0)
        return;
    host_fault_uninstall_divide_error();
    divide_by_zero_in_child();
}

PTEST(uninstall_gives_divide_errors_back_to_the_handler_installed_before)
{
    PT_ASSERT_EQ_INT(psup_exit_code(in_child(divide_after_uninstalling)), CHILD_CRASH_DIVIDE);
}

/* A previous handler installed without SA_SIGINFO, a plain one. */
static void exit_from_a_plain_handler(int signal_number)
{
    (void)signal_number;
    _exit(CHILD_PLAIN_PREVIOUS);
}

static void pass_on_to_a_plain_handler(void)
{
    struct sigaction plain;
    memset(&plain, 0, sizeof plain);
    plain.sa_handler = exit_from_a_plain_handler;
    if (sigemptyset(&plain.sa_mask) != 0 || sigaction(SIGFPE, &plain, NULL) != 0
        || host_fault_install_divide_error(pass_everything_on) != 0)
        return;
    divide_by_zero_in_child();
}

PTEST(a_divide_error_passed_on_reaches_a_previous_handler_without_siginfo)
{
    PT_ASSERT_EQ_INT(psup_exit_code(in_child(pass_on_to_a_plain_handler)), CHILD_PLAIN_PREVIOUS);
}

/* The main thread's stack, taken before the fault (host_image). */
static struct host_span main_stack;

static bool exit_by_where_the_handler_runs(struct host_fault *fault)
{
    (void)fault;
    const uintptr_t frame = (uintptr_t)__builtin_frame_address(0);
    _exit(frame >= main_stack.start && frame < main_stack.end ? CHILD_ON_MAIN_STACK : CHILD_ON_ALTERNATE_STACK);
}

static void divide_with_an_alternate_stack(void)
{
    if (host_image_main_stack(&main_stack) != 0 || host_fault_install_crash(exit_with_divide_or_number) != 0
        || host_fault_install_divide_error(exit_by_where_the_handler_runs) != 0)
        return;
    divide_by_zero_in_child();
}

/* The crash handler's alternate stack is there for a stack overflow; a
 * divide error on an exhausted stack needs its handler there too. */
PTEST(the_divide_handler_runs_on_the_alternate_stack)
{
    PT_ASSERT_EQ_INT(psup_exit_code(in_child(divide_with_an_alternate_stack)), CHILD_ON_ALTERNATE_STACK);
}

/* The default actions, as host_fault_uninstall_crash leaves them (and not
 * the sanitizer's own handlers, which would report the fault). */
static bool with_default_actions(void)
{
    if (host_fault_install_crash(exit_with_divide_or_number) != 0)
        return false;
    host_fault_uninstall_crash();
    return true;
}

static void pass_on_with_nothing_before(void)
{
    if (!with_default_actions() || host_fault_install_divide_error(pass_everything_on) != 0)
        return;
    divide_by_zero_in_child();
}

PTEST(a_divide_error_passed_on_with_no_handler_before_ends_the_process)
{
    PT_ASSERT_EQ_INT(psup_terminating_signal(in_child(pass_on_with_nothing_before)), SIGFPE);
}

/* The crash faults, raised in turn: the handler exits with the number. */
static int raised_signal;

static void raise_under_the_crash_handler(void)
{
    if (host_fault_install_crash(exit_with_divide_or_number) != 0)
        return;
    (void)raise(raised_signal);
}

PTEST(the_crash_handler_gets_every_crash_fault)
{
    static const int crashes[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (size_t i = 0; i < sizeof crashes / sizeof crashes[0]; i++) {
        raised_signal = crashes[i];
        PT_ASSERT_EQ_INT(psup_exit_code(in_child(raise_under_the_crash_handler)), CHILD_CRASH_NUMBER_BASE + crashes[i]);
    }
}

static void return_from_the_crash_handler(const struct host_fault *fault)
{
    (void)fault;
}

static void raise_under_a_returning_crash_handler(void)
{
    if (host_fault_install_crash(return_from_the_crash_handler) != 0)
        return;
    (void)raise(raised_signal);
}

PTEST(a_crash_handler_that_returns_lets_the_fault_end_the_process)
{
    /* raised, so nothing would fault again: the end is the handler's */
    raised_signal = SIGABRT;
    PT_ASSERT_EQ_INT(psup_terminating_signal(in_child(raise_under_a_returning_crash_handler)), SIGABRT);
    raised_signal = SIGILL;
    PT_ASSERT_EQ_INT(psup_terminating_signal(in_child(raise_under_a_returning_crash_handler)), SIGILL);
}

/* An address no one maps: the first page. */
#define UNMAPPED_ADDRESS 0x10u

static void exit_by_fault_address(const struct host_fault *fault)
{
    _exit(host_fault_address(fault) == UNMAPPED_ADDRESS ? CHILD_FAULT_ADDRESS_OK : CHILD_FAULT_ADDRESS_BAD);
}

static void store_to_an_unmapped_address(void)
{
    if (host_fault_install_crash(exit_by_fault_address) != 0)
        return;
    volatile uintptr_t where = UNMAPPED_ADDRESS; /* volatile: no constant address for the compiler to warn about */
    *(volatile uint8_t *)where = 1;
}

PTEST(the_fault_address_is_the_access_that_faulted)
{
    PT_ASSERT_EQ_INT(psup_exit_code(in_child(store_to_an_unmapped_address)), CHILD_FAULT_ADDRESS_OK);
}

/* --- the fault's name -------------------------------------------------------------- */

/* DIV EDI (host_fault_fixture.h): two bytes. */
#define DIV_EDI_LENGTH 2u

static char seen_name[HOST_FAULT_NAME_SIZE];

static bool name_and_complete(struct host_fault *fault)
{
    host_fault_name(fault, seen_name);
    host_fault_set_register(fault, HOST_FAULT_PC, host_fault_register(fault, HOST_FAULT_PC) + DIV_EDI_LENGTH);
    return true;
}

PTEST(a_fault_is_named_by_its_signal_in_decimal)
{
    uint32_t quotient, remainder;
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(name_and_complete), 0);
    divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT_EQ_STR(seen_name, "signal 8"); /* SIGFPE */
}

PTEST_MAIN()
