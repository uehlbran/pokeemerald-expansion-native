/*
 * test_host_fault.c: the faults the host hands the platform (host_fault.h).
 *
 * A division by zero, inline assembly with its operands in known registers,
 * completes through the installed handler, which reads every register
 * through the accessors and writes results the program then sees. Where a
 * fault goes when a handler does not complete it is tested in forked
 * children, which end by the crash handler's _exit code or by the fault
 * itself: a passed-on divide error reaches the crash handler installed
 * before, an uninstalled handler is not called again, a crash handler gets
 * every crash fault, and a fault whose handler returns ends the process.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host_fault.h"
#include "ptest.h"

/* DIV ECX: F7 /6 with ModRM 11 110 001 (Intel SDM vol. 2), the same bytes
 * on i386 and x86-64 (no REX). */
static const uint8_t div_ecx[] = {0xF7, 0xF1};

/* ModRM register numbers (Intel SDM vol. 2, table 2-2). */
#define MODRM_EAX 0u
#define MODRM_ECX 1u
#define MODRM_EDX 2u
#define MODRM_ESI 6u
#define MODRM_R9 9u
/* A multiple of the register count at both widths (8 and 16). */
#define REGISTER_COUNT_MULTIPLE 16u

#define DIVIDEND_LOW 0x12345678u
#define ESI_MARK 0x5E5E5E5Eu
#define R9_MARK 0x0909090909090909u
#define QUOTIENT_MARK 0xAAAA5555u
#define REMAINDER_MARK 0x5555AAAAu

/* What the divide handler saw, read after the fault. */
static struct {
    bool called;
    bool divide_error;
    int number;
    uintptr_t pc, accumulator, data;
    uintptr_t eax, ecx, edx, esi, esi_wrapped, r9;
} seen;

static bool complete_with_marks(struct host_fault *fault)
{
    seen.called = true;
    seen.divide_error = host_fault_is_divide_error(fault);
    seen.number = host_fault_number(fault);
    seen.pc = host_fault_register(fault, HOST_FAULT_PC);
    seen.accumulator = host_fault_register(fault, HOST_FAULT_ACCUMULATOR);
    seen.data = host_fault_register(fault, HOST_FAULT_DATA);
    seen.eax = host_fault_general_register(fault, MODRM_EAX);
    seen.ecx = host_fault_general_register(fault, MODRM_ECX);
    seen.edx = host_fault_general_register(fault, MODRM_EDX);
    seen.esi = host_fault_general_register(fault, MODRM_ESI);
    seen.esi_wrapped = host_fault_general_register(fault, MODRM_ESI + REGISTER_COUNT_MULTIPLE);
    seen.r9 = host_fault_general_register(fault, MODRM_R9);
    if (memcmp((const void *)seen.pc, div_ecx, sizeof div_ecx) != 0)
        return false; /* not the division below: pass it on */
    host_fault_set_register(fault, HOST_FAULT_ACCUMULATOR, QUOTIENT_MARK);
    host_fault_set_register(fault, HOST_FAULT_DATA, REMAINDER_MARK);
    host_fault_set_register(fault, HOST_FAULT_PC, seen.pc + sizeof div_ecx);
    return true;
}

/* DIVIDEND_LOW / 0 with ESI (and on x86-64 R9) marked; *quotient and
 * *remainder are EAX and EDX after it. */
static void divide_by_zero(uint32_t *quotient, uint32_t *remainder)
{
    uint32_t eax = DIVIDEND_LOW;
    uint32_t edx = 0;
    const uint32_t zero = 0;
#if defined(__x86_64__)
    register uint64_t r9 __asm__("r9") = R9_MARK;
    __asm__ volatile("divl %[divisor]"
                     : "+a"(eax), "+d"(edx)
                     : [divisor] "c"(zero), "S"(ESI_MARK), "r"(r9)
                     : "cc", "memory");
#else
    __asm__ volatile("divl %[divisor]" : "+a"(eax), "+d"(edx) : [divisor] "c"(zero), "S"(ESI_MARK) : "cc", "memory");
#endif
    *quotient = eax;
    *remainder = edx;
}

PTEST(a_division_by_zero_completes_through_the_installed_handler)
{
    memset(&seen, 0, sizeof seen);
    uint32_t quotient = 0, remainder = 0;
    const int installed = host_fault_install_divide_error(complete_with_marks);
    if (installed == 0)
        divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT_EQ_INT(installed, 0);
    PT_ASSERT(seen.called);
    PT_ASSERT(seen.divide_error);
    PT_ASSERT_EQ_INT(seen.number, SIGFPE);
    PT_ASSERT_EQ_U32(quotient, QUOTIENT_MARK);
    PT_ASSERT_EQ_U32(remainder, REMAINDER_MARK);
}

PTEST(the_register_accessors_return_what_the_context_holds)
{
    memset(&seen, 0, sizeof seen);
    uint32_t quotient = 0, remainder = 0;
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(complete_with_marks), 0);
    divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT(seen.accumulator == DIVIDEND_LOW);
    PT_ASSERT(seen.data == 0);
    PT_ASSERT(seen.eax == DIVIDEND_LOW);
    PT_ASSERT(seen.ecx == 0);
    PT_ASSERT(seen.edx == 0);
    PT_ASSERT(seen.esi == ESI_MARK);
    PT_ASSERT(seen.esi_wrapped == ESI_MARK);
#if defined(__x86_64__)
    PT_ASSERT(seen.r9 == R9_MARK);
#else
    PT_ASSERT(seen.r9 == seen.ecx); /* 9 is ECX, modulo i386's 8 registers */
#endif
}

PTEST(uninstall_is_safe_twice_and_without_install)
{
    host_fault_uninstall_divide_error();
    host_fault_uninstall_divide_error();
    host_fault_uninstall_crash();
    host_fault_uninstall_crash();
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(complete_with_marks), 0);
    host_fault_uninstall_divide_error();
    host_fault_uninstall_divide_error();
}

/* --- Where faults end, in children ----------------------------------------------- */

/* Exit codes of the children. */
#define CHILD_RETURNED 3           /* the body ran to its end: no fault, or a fault ignored */
#define CHILD_DIVIDE_HANDLER 4     /* the divide handler ran after it was uninstalled */
#define CHILD_FAULT_ADDRESS_OK 5   /* the crash handler saw the expected fault address */
#define CHILD_FAULT_ADDRESS_BAD 6  /* ... and another one */
#define CHILD_CRASH_DIVIDE 10      /* the crash handler got a divide error */
#define CHILD_CRASH_NUMBER_BASE 64 /* the crash handler got fault number n: 64 + n */

/* Seconds a child may take before SIGALRM ends it: a handler that resumed
 * at the faulting instruction again would loop forever. */
#define CHILD_TIME_LIMIT_SECONDS 10u

/* Runs body in a forked child; its wait status, or -1. */
static int in_child(void (*body)(void))
{
    (void)fflush(stdout);
    const pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        (void)signal(SIGALRM, SIG_DFL); /* not ptest's timeout: this child just ends */
        (void)alarm(CHILD_TIME_LIMIT_SECONDS);
        body();
        _exit(CHILD_RETURNED);
    }
    int status;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return status;
}

static int exit_code(int status)
{
    return status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int terminating_signal(int status)
{
    return status != -1 && WIFSIGNALED(status) ? WTERMSIG(status) : -1;
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
    PT_ASSERT_EQ_INT(exit_code(in_child(pass_on_to_the_crash_handler)), CHILD_CRASH_DIVIDE);
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
    PT_ASSERT_EQ_INT(exit_code(in_child(divide_after_uninstalling)), CHILD_CRASH_DIVIDE);
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
    PT_ASSERT_EQ_INT(terminating_signal(in_child(pass_on_with_nothing_before)), SIGFPE);
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
        PT_ASSERT_EQ_INT(exit_code(in_child(raise_under_the_crash_handler)), CHILD_CRASH_NUMBER_BASE + crashes[i]);
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
    PT_ASSERT_EQ_INT(terminating_signal(in_child(raise_under_a_returning_crash_handler)), SIGABRT);
    raised_signal = SIGILL;
    PT_ASSERT_EQ_INT(terminating_signal(in_child(raise_under_a_returning_crash_handler)), SIGILL);
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
    PT_ASSERT_EQ_INT(exit_code(in_child(store_to_an_unmapped_address)), CHILD_FAULT_ADDRESS_OK);
}

PTEST_MAIN()
