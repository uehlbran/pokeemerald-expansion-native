/*
 * test_arm_division_linux.c: a SIGFPE that kill sends while the program
 * counter is at a division by zero (arm_division.h), with Linux's system call
 * numbers. Linux tells it from a divide error by si_code (SI_USER, not
 * FPE_INTDIV), so arm_division passes it on although a division by zero is
 * where the program counter points. macOS cannot tell them apart:
 * ../darwin/test_arm_division_darwin.c. The rest is in
 * ../posix/test_arm_division_posix.c.
 */
// deps: arm_division.c
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "arm_division_fixture.h"
#include "ptest.h"

/* Linux's kill: 62 on x86-64 (syscall), 37 on i386 (int 0x80). The signal is
 * delivered on the return from the system call, and the divide that follows
 * it is never reached. */
static void kill_self_with_sigfpe_before_a_division_by_zero(void)
{
    static const uint32_t zero;
    const long pid = (long)getpid();
#if defined(__x86_64__)
    __asm__ volatile("xorl %%edx, %%edx\n\tmovl $62, %%eax\n\tsyscall\n\tdivl (%[divisor])"
                     :
                     : "D"(pid), "S"((long)SIGFPE), [divisor] "r"(&zero)
                     : "rax", "rcx", "rdx", "r11", "memory");
#else
    __asm__ volatile("xorl %%edx, %%edx\n\tmovl $37, %%eax\n\tint $0x80\n\tdivl (%[divisor])"
                     :
                     : "b"(pid), "c"((long)SIGFPE), [divisor] "r"(&zero)
                     : "eax", "edx", "memory");
#endif
}

PTEST(a_signal_sent_by_kill_is_not_completed_even_where_the_pc_is_at_a_division_by_zero)
{
    PT_ASSERT_EQ_INT(calls_after(kill_self_with_sigfpe_before_a_division_by_zero), 1);
}

PTEST_MAIN()
