/*
 * test_arm_division_darwin.c: a SIGFPE that kill sends while the program
 * counter is at a division by zero (arm_division.h), with macOS's system call
 * numbers. Where Linux's si_code says SI_USER (../linux), macOS's says
 * FPE_INTDIV for a SIGFPE that kill or raise sends, as for a divide error
 * (measured on macOS 15; docs/DECISIONS.md, "On macOS a SIGFPE sent by kill
 * looks like a divide error"), so a fault
 * handler cannot tell them apart, and arm_division completes the division at
 * the program counter as the processor would have faulted on it: with
 * libgcc's result, here 0 / 0, which is 0, and the program goes on past it.
 * The previous handler sees nothing. The rest is in
 * ../posix/test_arm_division_posix.c.
 */
// deps: arm_division.c
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "arm_division_fixture.h"
#include "ptest.h"

/* macOS's kill, BSD system call 37 (<sys/syscall.h>) in the BSD class
 * 0x2000000 that syscall takes on x86-64; the kernel returns 0 in EAX on
 * success, which the division then divides. */
#define DARWIN_SYSCALL_KILL 0x2000025

static uint32_t quotient_after_kill = UINT32_MAX;

static void kill_self_with_sigfpe_before_a_division_by_zero(void)
{
    static const uint32_t zero;
    const long pid = (long)getpid();
    uint32_t eax = DARWIN_SYSCALL_KILL;
    __asm__ volatile("xorl %%edx, %%edx\n\tsyscall\n\tdivl (%[divisor])"
                     : "+a"(eax)
                     : "D"(pid), "S"((long)SIGFPE), [divisor] "r"(&zero)
                     : "rcx", "rdx", "r11", "memory");
    quotient_after_kill = eax;
}

PTEST(a_signal_sent_by_kill_at_a_division_by_zero_completes_that_division)
{
    PT_ASSERT_EQ_INT(calls_after(kill_self_with_sigfpe_before_a_division_by_zero), 0);
    PT_ASSERT_EQ_U32(quotient_after_kill, 0);
}

PTEST_MAIN()
