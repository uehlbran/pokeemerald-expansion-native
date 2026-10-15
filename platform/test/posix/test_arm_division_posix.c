/*
 * test_arm_division_posix.c: where a divide error goes that arm_division.h
 * does not complete (a prefix or operand form the decoder refuses, a SIGFPE
 * that raise sent, a wide dividend, a quotient that does not fit):
 * to the handler installed before arm_division_init, which is restored at
 * shutdown, or, with none, the process ends with SIGFPE after one line on
 * stderr. The handlers are sigaction handlers that long-jump out
 * (siglongjmp is async-signal-safe) and count in a volatile sig_atomic_t
 * (arm_division_fixture.h); the last test runs in a forked child. What the
 * decoder does complete is tested in ../test_arm_division.c; a SIGFPE that
 * kill sends, whose si_code differs by OS, in ../linux and ../darwin.
 */
// deps: arm_division.c
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "arm_division.h"
#include "arm_division_fixture.h"
#include "posix_child.h"
#include "posix_support.h"
#include "ptest.h"

PTEST(shutdown_twice_is_safe_and_restores_the_previous_handler)
{
    struct sigaction before;
    PT_ASSERT_EQ_INT(sigaction(SIGFPE, NULL, &before), 0);
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK); /* again: restores, then installs */
    arm_division_shutdown();
    arm_division_shutdown();
    struct sigaction now;
    PT_ASSERT_EQ_INT(sigaction(SIGFPE, NULL, &now), 0);
    PT_ASSERT(now.sa_sigaction == before.sa_sigaction);
    PT_ASSERT_EQ_INT(now.sa_flags & SA_SIGINFO, before.sa_flags & SA_SIGINFO);
}

/* ---- the divide errors the decoder refuses, the same at both widths ---- */

/* Each is a divide error the processor raises and the decoder does not
 * complete, so it must reach the handler installed before (the crash reboot
 * in test-runner mode). The registers are named by their 32-bit names, which
 * GCC accepts as clobbers at both widths. */
/* divb %cl by zero (F6 F1) of a dividend wider than a byte (AH not 0):
 * the 8-bit forms are a division of two bytes, whose dividend is zero- or
 * sign-extended into AX (the 8-bit IDIV's twin is below). */
static void byte_division_by_zero(void)
{
    __asm__ volatile("movw $0x105, %%ax\n\tmovb $0, %%cl\n\tdivb %%cl" : : : "eax", "ecx");
}

/* idivb %cl by zero (F6 F9) with AH not AL's sign: 5 with AH 1. */
static void unextended_signed_byte_division_by_zero(void)
{
    __asm__ volatile("movw $0x105, %%ax\n\tmovb $0, %%cl\n\tidivb %%cl" : : : "eax", "ecx");
}

/* divw %cx by zero (66 F7 F1) with DX not 0: a 32-bit dividend in DX:AX. */
static void wide_dividend_by_zero_16(void)
{
    __asm__ volatile("movl $1, %%edx\n\tmovl $5, %%eax\n\txorl %%ecx, %%ecx\n\tdivw %%cx" : : : "eax", "ecx", "edx");
}

/* divb %cl by 1 of AX 0x200: a quotient wider than AL. */
static void wide_quotient_8(void)
{
    __asm__ volatile("movw $0x200, %%ax\n\tmovb $1, %%cl\n\tdivb %%cl" : : : "eax", "ecx");
}

/* The operand-size prefix before an 8-bit division (66 F6 F1), which the
 * processor ignores and no compiler writes. */
static void operand_size_prefixed_byte_division_by_zero(void)
{
    __asm__ volatile("movw $5, %%ax\n\tmovb $0, %%cl\n\t.byte 0x66, 0xF6, 0xF1" : : : "eax", "ecx");
}

/* divl %ecx by zero with a prefix byte in front (prefix F7 F1): the
 * processor ignores or applies the prefix; the decoder refuses it. 67h, FS
 * and GS are meaningless with a register operand and REP and REPNE with a
 * non-string instruction (66h, the operand size, makes a 16-bit division,
 * which the decoder completes: ../test_arm_division.c).
 * LOCK cannot be tested here: LOCK DIV raises #UD (SIGILL), not a divide
 * error. */
#define REFUSED_PREFIX_DIVISION(name, prefix)                                                                       \
    static void name(void)                                                                                          \
    {                                                                                                               \
        __asm__ volatile("movl $5, %%eax\n\txorl %%edx, %%edx\n\txorl %%ecx, %%ecx\n\t.byte " prefix ", 0xF7, 0xF1" \
                         :                                                                                          \
                         :                                                                                          \
                         : "eax", "ecx", "edx");                                                                    \
    }
REFUSED_PREFIX_DIVISION(address_size_prefixed_division_by_zero, "0x67")
REFUSED_PREFIX_DIVISION(fs_prefixed_division_by_zero, "0x64")
REFUSED_PREFIX_DIVISION(gs_prefixed_division_by_zero, "0x65")
REFUSED_PREFIX_DIVISION(rep_prefixed_division_by_zero, "0xF3")
REFUSED_PREFIX_DIVISION(repne_prefixed_division_by_zero, "0xF2")
/* The decoder skips at most MAX_PREFIXES (4) of ES, CS, SS and DS, one per
 * group by the encoding but any number by the processor: five ES prefixes
 * put the opcode where the decoder does not look. */
REFUSED_PREFIX_DIVISION(five_segment_prefixes_division_by_zero, "0x26, 0x26, 0x26, 0x26, 0x26")

/* EDX:EAX = 2^33 divided by 1: the quotient does not fit 32 bits. */
static void wide_quotient_32(void)
{
    __asm__ volatile("movl $2, %%edx\n\txorl %%eax, %%eax\n\tmovl $1, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx");
}

/* An unsigned division by zero of a 64-bit dividend (EDX non-zero): not one
 * of libgcc's 32-bit routines' cases. */
static void wide_dividend_by_zero_32(void)
{
    __asm__ volatile("movl $1, %%edx\n\tmovl $5, %%eax\n\txorl %%ecx, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx");
}

/* A signed 32-bit division by zero whose EDX is not EAX's sign
 * (EDX:EAX = 2^32 - 1). */
static void unextended_signed_dividend_by_zero_32(void)
{
    __asm__ volatile("xorl %%edx, %%edx\n\tmovl $-1, %%eax\n\txorl %%ecx, %%ecx\n\tidivl %%ecx"
                     :
                     :
                     : "eax", "ecx", "edx");
}

/* Not a divide error at all: SIGFPE sent by raise. Linux says so in si_code
 * (SI_TKILL); macOS gives it FPE_INTDIV, as a divide error (probe M2), and
 * host_fault_is_divide_error then finds no division at the program counter,
 * which is in raise's own code. */
static void sigfpe_from_raise(void)
{
    (void)raise(SIGFPE);
}

PTEST(divide_errors_the_decoder_refuses_go_to_the_previous_handler)
{
    PT_ASSERT_EQ_INT(calls_after(byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(unextended_signed_byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_dividend_by_zero_16), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_quotient_8), 1);
    PT_ASSERT_EQ_INT(calls_after(operand_size_prefixed_byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(address_size_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(fs_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(gs_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(rep_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(repne_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(five_segment_prefixes_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_quotient_32), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_dividend_by_zero_32), 1);
    PT_ASSERT_EQ_INT(calls_after(unextended_signed_dividend_by_zero_32), 1);
}

PTEST(a_sigfpe_that_is_not_a_divide_error_goes_to_the_previous_handler)
{
    PT_ASSERT_EQ_INT(calls_after(sigfpe_from_raise), 1);
}

PTEST(a_previous_handler_without_siginfo_is_called_too)
{
    PT_ASSERT_EQ_INT(calls_after_plain(byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after_plain(sigfpe_from_raise), 1);
}

/* With the default action before it, a divide error nobody can complete
 * ends the process with SIGFPE, after one line on stderr naming the program
 * counter. Run in a child (psup_run_child: no core dump, an alarm that kills
 * it whatever happened, so a failure cannot hang the run), its stderr read
 * through a pipe once it has ended. */
#define PIPE_WAIT_MILLISECONDS 5000

/* The child's body: stderr into the pipe, the default action for SIGFPE, then the divide error. */
static void divide_with_default_action(void *context)
{
    const int *const fds = context;
    struct sigaction defaults;
    memset(&defaults, 0, sizeof defaults);
    defaults.sa_handler = SIG_DFL;
    sigemptyset(&defaults.sa_mask);
    if (close(fds[0]) != 0 || dup2(fds[1], STDERR_FILENO) < 0 || sigaction(SIGFPE, &defaults, NULL) != 0
        || arm_division_init() != ARM_DIVISION_OK)
        _exit(2);
    byte_division_by_zero();
    _exit(3); /* not reached: the divide error repeats and kills the process */
}

PTEST(without_a_previous_handler_an_undecodable_divide_error_reports_and_kills_the_process)
{
    int fds[2];
    PT_ASSERT_EQ_INT(pipe(fds), 0);
    const int status = psup_run_child(divide_with_default_action, fds);
    (void)close(fds[1]);
    char line[256] = {0};
    const ssize_t got = psup_read_within(fds[0], line, sizeof line - 1, PIPE_WAIT_MILLISECONDS);
    (void)close(fds[0]);

    PT_ASSERT(status != -1);
    PT_ASSERT_EQ_INT(psup_terminating_signal(status), SIGFPE);

    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on (not a DIV or IDIV by zero, nor INT_MIN / -1)\n";
    const size_t digits = sizeof(uintptr_t) * 2;
    PT_ASSERT_EQ_INT(got, (ssize_t)(sizeof head - 1 + digits + sizeof tail - 1)); /* one whole line */
    PT_ASSERT(strncmp(line, head, sizeof head - 1) == 0);
    for (size_t i = 0; i < digits; i++)
        PT_ASSERT(strchr("0123456789abcdef", line[sizeof head - 1 + i]) != NULL);
    PT_ASSERT_EQ_STR(line + sizeof head - 1 + digits, tail);
}

#if defined(__x86_64__)
/* The prefix bytes the decoder refuses that only mean something with REX: 48 3E F7 F1
 * is a REX prefix that is not the last (the processor ignores it: divl
 * %ecx); 40 48 F7 F1 is two REX prefixes (divq %rcx); 3E 48 26 F7 F1 puts a
 * segment prefix between the REX prefix and the opcode, where the
 * decoder does not look for one. */
REFUSED_PREFIX_DIVISION(rex_before_segment_prefix_division_by_zero, "0x48, 0x3E")
REFUSED_PREFIX_DIVISION(two_rex_prefixes_division_by_zero, "0x40, 0x48")
REFUSED_PREFIX_DIVISION(segment_prefix_after_rex_division_by_zero, "0x3E, 0x48, 0x26")

static void wide_quotient_64(void)
{
    /* RDX:RAX = 2^64 divided by 1: the quotient does not fit 64 bits. */
    __asm__ volatile("movl $1, %%edx\n\txorl %%eax, %%eax\n\tmovl $1, %%ecx\n\tdivq %%rcx" : : : "rax", "rcx", "rdx");
}

/* An unsigned 64-bit division by zero of a 128-bit dividend (RDX non-zero). */
static void wide_dividend_by_zero_64(void)
{
    __asm__ volatile("movl $1, %%edx\n\tmovl $5, %%eax\n\txorl %%ecx, %%ecx\n\tdivq %%rcx" : : : "rax", "rcx", "rdx");
}

/* A signed one whose RDX is not RAX's sign (RDX:RAX = 2^64 - 1). */
static void unextended_signed_dividend_by_zero_64(void)
{
    __asm__ volatile("xorl %%edx, %%edx\n\tmovq $-1, %%rax\n\txorl %%ecx, %%ecx\n\tidivq %%rcx"
                     :
                     :
                     : "rax", "rcx", "rdx");
}

PTEST(rex_divide_errors_the_decoder_refuses_go_to_the_previous_handler)
{
    PT_ASSERT_EQ_INT(calls_after(rex_before_segment_prefix_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(two_rex_prefixes_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(segment_prefix_after_rex_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_quotient_64), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_dividend_by_zero_64), 1);
    PT_ASSERT_EQ_INT(calls_after(unextended_signed_dividend_by_zero_64), 1);
}
#endif

PTEST_MAIN()
