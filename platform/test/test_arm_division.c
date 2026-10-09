/*
 * test_arm_division.c: x86 division by zero completed as the GBA's libgcc
 * completes it (arm_division.h), on i386 and on x86-64.
 *
 * Expected results from libgcc for the GBA (gcc-arm-none-eabi 13.2.1, run
 * under qemu-arm by platform/arm_division_libgcc; arm_division.h quotes
 * them): 32-bit unsigned n/0 is 0xFFFFFFFF unless n is 0; signed n/0 is
 * INT32_MAX, INT32_MIN or 0 by the sign of n; the same for %; INT32_MIN / -1
 * is INT32_MIN rem 0. 64-bit (x86-64's REX.W forms) follows __udivdi3 and
 * __divdi3, whose results the tables below copy from that record. The
 * divisions are inline assembly, so that each operand encoding of DIV and
 * IDIV (Intel SDM vol. 2: register, [reg], [reg+disp8], [reg+disp32],
 * [disp32], SIB with and without base, segment prefixes; on x86-64 also REX
 * and RIP-relative operands) is the one tested, and so that
 * UndefinedBehaviorSanitizer does not stop the C division first.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "arm_division.h"
#include "ptest.h"

/* The handler installed before arm_division_init gets what it cannot do. */
static sigjmp_buf previous_handler_target;
static volatile int previous_handler_calls;

static void previous_handler(int signal_number, siginfo_t *info, void *context)
{
    (void)signal_number;
    (void)info;
    (void)context;
    previous_handler_calls++;
    siglongjmp(previous_handler_target, 1);
}

/* The same, installed without SA_SIGINFO: a plain signal handler. */
static void previous_plain_handler(int signal_number)
{
    (void)signal_number;
    previous_handler_calls++;
    siglongjmp(previous_handler_target, 1);
}

/* How many times division's divide error reached the handler that was
 * installed (mine) before arm_division_init, or a negative number if the
 * harness failed (-1) or the module did not restore mine at shutdown (-2). */
static int calls_after_with(const struct sigaction *mine, void (*division)(void))
{
    struct sigaction saved;
    if (sigaction(SIGFPE, mine, &saved) != 0)
        return -1;
    previous_handler_calls = 0;
    if (arm_division_init() != ARM_DIVISION_OK)
        return -1;
    if (sigsetjmp(previous_handler_target, 1) == 0)
        division();
    arm_division_shutdown();
    struct sigaction now;
    if (sigaction(SIGFPE, &saved, &now) != 0)
        return -1;
    const bool restored = now.sa_sigaction == mine->sa_sigaction;
    return restored ? previous_handler_calls : -2;
}

static int calls_after(void (*division)(void))
{
    struct sigaction mine;
    memset(&mine, 0, sizeof mine);
    mine.sa_sigaction = previous_handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);
    return calls_after_with(&mine, division);
}

static int calls_after_plain(void (*division)(void))
{
    struct sigaction mine;
    memset(&mine, 0, sizeof mine);
    mine.sa_handler = previous_plain_handler;
    sigemptyset(&mine.sa_mask);
    return calls_after_with(&mine, division);
}

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
/* divb %cl by zero (F6 F1): 8-bit forms are not the compiler's division of
 * an int or wider. */
static void byte_division_by_zero(void)
{
    __asm__ volatile("movw $5, %%ax\n\tmovb $0, %%cl\n\tdivb %%cl" : : : "eax", "ecx");
}

/* divl %ecx by zero with a prefix byte in front (prefix F7 F1): the
 * processor ignores or applies the prefix; the decoder refuses it. 66h
 * makes it a 16-bit division (divw %cx); 67h, FS and GS are meaningless with
 * a register operand and REP and REPNE with a non-string instruction.
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
REFUSED_PREFIX_DIVISION(operand_size_prefixed_division_by_zero, "0x66")
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

/* Not a divide error at all: SIGFPE sent by raise (si_code SI_TKILL). */
static void sigfpe_from_raise(void)
{
    (void)raise(SIGFPE);
}

PTEST(divide_errors_the_decoder_refuses_go_to_the_previous_handler)
{
    PT_ASSERT_EQ_INT(calls_after(byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(operand_size_prefixed_division_by_zero), 1);
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
 * counter. Run in a child, its stderr read through a pipe. */
PTEST(without_a_previous_handler_an_undecodable_divide_error_reports_and_kills_the_process)
{
    int fds[2];
    PT_ASSERT_EQ_INT(pipe(fds), 0);
    (void)fflush(NULL);
    const pid_t child = fork();
    PT_ASSERT(child >= 0);
    if (child == 0) {
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
    PT_ASSERT_EQ_INT(close(fds[1]), 0);
    char line[256] = {0};
    const ssize_t got = read(fds[0], line, sizeof line - 1);
    PT_ASSERT_EQ_INT(close(fds[0]), 0);
    int status = 0;
    PT_ASSERT_EQ_INT(waitpid(child, &status, 0), child);
    PT_ASSERT(WIFSIGNALED(status));
    PT_ASSERT_EQ_INT(WTERMSIG(status), SIGFPE);

    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on";
    const size_t digits = sizeof(uintptr_t) * 2;
    PT_ASSERT(got > 0);
    PT_ASSERT(strncmp(line, head, sizeof head - 1) == 0);
    for (size_t i = 0; i < digits; i++)
        PT_ASSERT(strchr("0123456789abcdef", line[sizeof head - 1 + i]) != NULL);
    PT_ASSERT(strncmp(line + sizeof head - 1 + digits, tail, sizeof tail - 1) == 0);
}

#if defined(__i386__)

struct result {
    uint32_t quotient;
    uint32_t remainder;
};

static struct result div_register(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %%ecx"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor));
    return r;
}

static struct result idiv_register(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("cltd\n\tidivl %%ecx" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor));
    return r;
}

static uint32_t divisor_in_memory[4];
static uint32_t global_divisor;

/* divl (%ecx): ModRM mod 0. */
static struct result div_base(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%ecx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* divl 4(%ecx): mod 1, an 8-bit displacement. */
static struct result div_disp8(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 4(%%ecx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor - 1)
                     : "memory");
    return r;
}

/* divl 0x100(%ecx): mod 2, a 32-bit displacement. */
static struct result div_disp32(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 0x100(%%ecx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"((uintptr_t)divisor - 0x100)
                     : "memory");
    return r;
}

/* divl global_divisor: mod 0, rm 5, an absolute address. */
static struct result div_absolute(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %2"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "m"(global_divisor), "a"(dividend)
                     : "memory");
    return r;
}

/* divl 8(%ecx,%ebx,4): a SIB byte and a displacement. */
static struct result div_sib(uint32_t dividend, const uint32_t *array, uint32_t index)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 8(%%ecx,%%ebx,4)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(array - 2), "b"(index)
                     : "memory");
    return r;
}

/* divl global_divisor(,%ebx,4): SIB with no base, a 32-bit displacement. */
static struct result div_sib_no_base(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %c2(,%%ebx,4)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&global_divisor), "a"(dividend), "b"(0)
                     : "memory");
    return r;
}

/* idivl (%esp): SIB with ESP as base, after a push. */
static struct result idiv_stack(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("pushl %%ecx\n\tcltd\n\tidivl (%%esp)\n\taddl $4, %%esp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* ds; divl (%ecx): a segment override prefix. */
static struct result div_prefixed(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x3e\n\tdivl (%%ecx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* idivl -4(%ebp): mod 1 with EBP as the base and a negative disp8; the
 * divisor is 4 bytes below the base. EBP is saved and restored around it. */
static struct result idiv_ebp_negative_disp8(int32_t dividend, const int32_t *divisor)
{
    struct result r;
    __asm__ volatile("pushl %%ebp\n\tleal 4(%%ecx), %%ebp\n\tcltd\n\tidivl -4(%%ebp)\n\tpopl %%ebp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* divl 8(%esp): a SIB byte with ESP as the base and no index, mod 1. */
static struct result div_esp_disp8(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("subl $12, %%esp\n\tmovl %%ecx, 8(%%esp)\n\txorl %%edx, %%edx\n\tdivl 8(%%esp)\n\taddl $12, %%esp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* Sixteen divisors for the memory-operand tests: every one non-zero and
 * different from the rest, except the one aim() zeroes. A decoder that finds
 * the divisor at the wrong address reads a non-zero value, completes nothing
 * and the divide error is passed on (the process dies), instead of reading
 * a zero by luck. */
#define SLOT_COUNT 16u
#define SLOT_SENTINEL_BASE 0x100u
static uint32_t slots[SLOT_COUNT];

static void aim(unsigned zero_slot)
{
    for (unsigned i = 0; i < SLOT_COUNT; i++)
        slots[i] = SLOT_SENTINEL_BASE + i;
    slots[zero_slot] = 0;
}

/* divl (%ecx,%ebx,1): a SIB byte, scale 1 (shift 0), byte offset in EBX. */
static struct result div_sib_scale1(uint32_t dividend, uintptr_t byte_offset)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%ecx,%%ebx,1)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(slots), "b"(byte_offset)
                     : "memory");
    return r;
}

/* divl (%ecx,%ebx,2): scale 2 (shift 1). */
static struct result div_sib_scale2(uint32_t dividend, uintptr_t index)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%ecx,%%ebx,2)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(slots), "b"(index)
                     : "memory");
    return r;
}

/* divl (%ecx,%ebx,8): scale 8 (shift 3). */
static struct result div_sib_scale8(uint32_t dividend, uintptr_t index)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%ecx,%%ebx,8)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(slots), "b"(index)
                     : "memory");
    return r;
}

/* divl 8(%ebp,%ebx,1): SIB with EBP as the base and mod 1: the base is used
 * (only mod 0 means "no base, disp32"). EBP is saved around it. */
static struct result div_sib_ebp_disp8(uint32_t dividend, const uint32_t *target)
{
    struct result r;
    __asm__ volatile("pushl %%ebp\n\tmovl %%ecx, %%ebp\n\txorl %%edx, %%edx\n\tdivl 8(%%ebp,%%ebx,1)\n\tpopl %%ebp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"((uintptr_t)target - 8u), "b"(0)
                     : "memory");
    return r;
}

/* divl 0x100(%ebp,%ebx,1): the same with mod 2. */
static struct result div_sib_ebp_disp32(uint32_t dividend, const uint32_t *target)
{
    struct result r;
    __asm__ volatile("pushl %%ebp\n\tmovl %%ecx, %%ebp\n\txorl %%edx, %%edx\n\tdivl 0x100(%%ebp,%%ebx,1)\n\tpopl %%ebp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"((uintptr_t)target - 0x100u), "b"(0)
                     : "memory");
    return r;
}

/* divl 0x100(%esp): SIB with ESP as the base, no index, mod 2. */
static struct result div_esp_disp32(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile(
        "subl $0x104, %%esp\n\tmovl %%ecx, 0x100(%%esp)\n\txorl %%edx, %%edx\n\tdivl 0x100(%%esp)\n\taddl $0x104, %%esp"
        : "=a"(r.quotient), "=&d"(r.remainder)
        : "a"(dividend), "c"(divisor)
        : "memory");
    return r;
}

/* divl 0x100(%esp) written out with scale bits 3 in its SIB byte: with no
 * index the scale means nothing (Intel SDM vol. 2, table 2-3), so the
 * address is still ESP + 0x100. F7 /6 mod 2 rm 4, SIB 11 100 100, disp32. */
static struct result div_esp_disp32_scale_ignored(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("subl $0x104, %%esp\n\tmovl %%ecx, 0x100(%%esp)\n\txorl %%edx, %%edx\n\t"
                     ".byte 0xf7, 0xb4, 0xe4, 0x00, 0x01, 0x00, 0x00\n\taddl $0x104, %%esp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* ds; ds; divl (%ecx): two prefixes stacked. */
static struct result div_two_prefixes(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x3e, 0x2e\n\tdivl (%%ecx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

static void expect(struct result r, uint32_t quotient, uint32_t remainder)
{
    PT_ASSERT_EQ_U32(r.quotient, quotient);
    PT_ASSERT_EQ_U32(r.remainder, remainder);
}

PTEST(unsigned_division_by_zero_gives_all_ones_or_zero)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result nonzero = div_register(16711680u, 0);
    struct result one = div_register(1, 0);
    struct result zero = div_register(0, 0);
    arm_division_shutdown();
    expect(nonzero, UINT32_MAX, UINT32_MAX);
    expect(one, UINT32_MAX, UINT32_MAX);
    expect(zero, 0, 0);
}

PTEST(signed_division_by_zero_saturates_by_the_sign)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result positive = idiv_register(255, 0);
    struct result negative = idiv_register(-3, 0);
    struct result zero = idiv_register(0, 0);
    arm_division_shutdown();
    expect(positive, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX);
    expect(negative, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN);
    expect(zero, 0, 0);
}

PTEST(int_min_by_minus_one_is_int_min_remainder_zero)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result r = idiv_register(INT32_MIN, -1);
    arm_division_shutdown();
    expect(r, (uint32_t)INT32_MIN, 0);
}

PTEST(ordinary_division_is_untouched)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result u = div_register(7, 2);
    struct result s = idiv_register(-7, 2);
    arm_division_shutdown();
    expect(u, 3, 1);
    expect(s, (uint32_t)-3, (uint32_t)-1);
}

PTEST(every_memory_operand_form_is_decoded)
{
    /* Each form is aimed at its own slot; the other fifteen divisors are
     * non-zero, so only a decoder that finds the right address completes it. */
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    aim(1);
    struct result base = div_base(5, &slots[1]);
    aim(2);
    struct result disp8 = div_disp8(5, &slots[2]);
    aim(5);
    struct result disp32 = div_disp32(5, &slots[5]);
    aim(3);
    struct result sib = div_sib(5, slots, 3);
    aim(6);
    struct result sib_scale1 = div_sib_scale1(5, 6 * sizeof slots[0]);
    aim(7);
    struct result sib_scale2 = div_sib_scale2(5, 7 * sizeof slots[0] / 2);
    aim(8);
    struct result sib_scale8 = div_sib_scale8(5, 8 * sizeof slots[0] / 8);
    aim(9);
    struct result sib_ebp_disp8 = div_sib_ebp_disp8(5, &slots[9]);
    aim(10);
    struct result sib_ebp_disp32 = div_sib_ebp_disp32(5, &slots[10]);
    aim(11);
    struct result prefixed = div_prefixed(5, &slots[11]);
    aim(12);
    struct result two_prefixes = div_two_prefixes(5, &slots[12]);
    global_divisor = 0;
    struct result absolute = div_absolute(5);
    struct result sib_no_base = div_sib_no_base(5);
    struct result stack = idiv_stack(-5, 0);
    const int32_t below_ebp = 0;
    struct result ebp_negative = idiv_ebp_negative_disp8(7, &below_ebp);
    struct result esp_disp8 = div_esp_disp8(5, 0);
    struct result esp_disp32 = div_esp_disp32(5, 0);
    struct result esp_scale_ignored = div_esp_disp32_scale_ignored(5, 0);
    arm_division_shutdown();
    expect(base, UINT32_MAX, UINT32_MAX);
    expect(disp8, UINT32_MAX, UINT32_MAX);
    expect(disp32, UINT32_MAX, UINT32_MAX);
    expect(absolute, UINT32_MAX, UINT32_MAX);
    expect(sib, UINT32_MAX, UINT32_MAX);
    expect(sib_scale1, UINT32_MAX, UINT32_MAX);
    expect(sib_scale2, UINT32_MAX, UINT32_MAX);
    expect(sib_scale8, UINT32_MAX, UINT32_MAX);
    expect(sib_ebp_disp8, UINT32_MAX, UINT32_MAX);
    expect(sib_ebp_disp32, UINT32_MAX, UINT32_MAX);
    expect(sib_no_base, UINT32_MAX, UINT32_MAX);
    expect(stack, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN);
    expect(prefixed, UINT32_MAX, UINT32_MAX);
    expect(two_prefixes, UINT32_MAX, UINT32_MAX);
    expect(ebp_negative, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX);
    expect(esp_disp8, UINT32_MAX, UINT32_MAX);
    expect(esp_disp32, UINT32_MAX, UINT32_MAX);
    expect(esp_scale_ignored, UINT32_MAX, UINT32_MAX);
}

PTEST(ebp_and_esp_based_operands_are_read_at_their_offsets)
{
    /* Non-zero divisors at the same addressing forms divide normally, so the
     * decoder found the divisor where the CPU did. */
    const int32_t below_ebp = -3;
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result ebp_negative = idiv_ebp_negative_disp8(10, &below_ebp);
    struct result esp_disp8 = div_esp_disp8(10, 4);
    arm_division_shutdown();
    expect(ebp_negative, (uint32_t)-3, 1);
    expect(esp_disp8, 2, 2);
}

PTEST(memory_operands_are_read_not_assumed_zero)
{
    /* A non-zero divisor in memory divides normally. */
    divisor_in_memory[0] = 3;
    global_divisor = 4;
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result base = div_base(10, &divisor_in_memory[0]);
    struct result absolute = div_absolute(10);
    arm_division_shutdown();
    divisor_in_memory[0] = 0;
    global_divisor = 0;
    expect(base, 3, 1);
    expect(absolute, 2, 2);
}

#elif defined(__x86_64__)

/* RAX and RDX, whole, after the division. */
struct result {
    uint64_t quotient;
    uint64_t remainder;
};

#define HALF_SHIFT 32u

/* Each 64-bit value as its two halves: ptest.h compares at most 32 bits. */
static void expect(struct result r, uint64_t quotient, uint64_t remainder)
{
    PT_ASSERT_EQ_U32(r.quotient >> HALF_SHIFT, quotient >> HALF_SHIFT);
    PT_ASSERT_EQ_U32(r.quotient, quotient);
    PT_ASSERT_EQ_U32(r.remainder >> HALF_SHIFT, remainder >> HALF_SHIFT);
    PT_ASSERT_EQ_U32(r.remainder, remainder);
}

/* divl %ecx (F7 F1). */
static struct result divl_register(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %%ecx"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"(divisor));
    return r;
}

/* idivl %ecx (F7 F9). */
static struct result idivl_register(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("cltd\n\tidivl %%ecx"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)(uint32_t)dividend), "c"(divisor));
    return r;
}

/* divq %rcx (48 F7 F1): REX.W. */
static struct result divq_register(uint64_t dividend, uint64_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivq %%rcx"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor));
    return r;
}

/* idivq %rcx (48 F7 F9). */
static struct result idivq_register(int64_t dividend, int64_t divisor)
{
    struct result r;
    __asm__ volatile("cqto\n\tidivq %%rcx" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor));
    return r;
}

/* divl %ecx with other bits in the upper halves of RAX and RDX: a 32-bit
 * DIV reads EDX:EAX only, and its results replace RAX and RDX whole. */
static struct result divl_upper_halves_set(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("movabsq $0xAAAAAAAA00000000, %%rax\n\torq %%rsi, %%rax\n\t"
                     "movabsq $0xBBBBBBBB00000000, %%rdx\n\tdivl %%ecx"
                     : "=&a"(r.quotient), "=&d"(r.remainder)
                     : "S"((uint64_t)dividend), "c"(divisor));
    return r;
}

/* idivl %ecx the same way, EDX holding the dividend's sign. */
static struct result idivl_upper_halves_set(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("movabsq $0xAAAAAAAA00000000, %%rax\n\torq %%rsi, %%rax\n\t"
                     "movabsq $0xBBBBBBBB00000000, %%rdx\n\torq %%rdi, %%rdx\n\tidivl %%ecx"
                     : "=&a"(r.quotient), "=&d"(r.remainder)
                     : "S"((uint64_t)(uint32_t)dividend), "D"((uint64_t)(dividend < 0 ? UINT32_MAX : 0)), "c"(divisor));
    return r;
}

/* divl %r9d (41 F7 F1): REX.B. */
static struct result divl_r9d(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("movl %k2, %%r9d\n\txorl %%edx, %%edx\n\tdivl %%r9d"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(divisor), "a"((uint64_t)dividend)
                     : "r9");
    return r;
}

/* idivq %r15 (49 F7 FF): REX.W and REX.B. */
static struct result idivq_r15(int64_t dividend, int64_t divisor)
{
    struct result r;
    __asm__ volatile("movq %2, %%r15\n\tcqto\n\tidivq %%r15"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(divisor), "a"(dividend)
                     : "r15");
    return r;
}

/* 44 F7 F1: divl %ecx with REX.R, which F7h ignores (its reg field is an
 * opcode extension). */
static struct result divl_rex_r(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x44, 0xF7, 0xF1"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"(divisor));
    return r;
}

/* 40 F7 F1: divl %ecx with a REX prefix that sets no bit. */
static struct result divl_rex_empty(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x40, 0xF7, 0xF1"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"(divisor));
    return r;
}

/* 3E 48 F7 F1: ds; divq %rcx, a segment prefix and then REX.W. */
static struct result divq_prefixed(uint64_t dividend, uint64_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x3E, 0x48, 0xF7, 0xF1"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor));
    return r;
}

/* Divisors for the memory-operand tests: every byte of every slot is
 * non-zero (slot i is the byte 0x40 + i eight times) except the bytes aim()
 * zeroes: the 4 a 32-bit DIV reads, or the 8 of a 64-bit one. A decoder that
 * finds the divisor at the wrong address, even one byte off either way,
 * reads a non-zero value, completes nothing and the divide error is passed
 * on (the process dies), instead of reading a zero by luck. 64 slots, so
 * that a negative disp8 misread as positive (up to 255 bytes on) still
 * lands on one. */
#define SLOT_COUNT 64u
#define SLOT_SENTINEL_BASE 0x40u            /* + SLOT_COUNT - 1 stays a byte */
#define SLOT_EVERY_BYTE 0x0101010101010101u /* the sentinel byte in each byte */
static uint64_t slots[SLOT_COUNT];

static uint64_t sentinel(unsigned slot)
{
    return (uint64_t)(SLOT_SENTINEL_BASE + slot) * SLOT_EVERY_BYTE;
}

static void aim(unsigned zero_slot, size_t operand_size)
{
    for (unsigned i = 0; i < SLOT_COUNT; i++)
        slots[i] = sentinel(i);
    memset(&slots[zero_slot], 0, operand_size);
}

/* divl (%rcx) (F7 31): mod 0. */
static struct result divl_base(uint32_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%rcx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* divq 8(%rcx) (48 F7 71 08): mod 1, an 8-bit displacement. */
static struct result divq_disp8(uint64_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivq 8(%%rcx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor - 1)
                     : "memory");
    return r;
}

/* divl -8(%rcx) (F7 71 F8): a negative disp8, sign-extended to 64 bits. */
static struct result divl_negative_disp8(uint32_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl -8(%%rcx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"(divisor + 1)
                     : "memory");
    return r;
}

/* divq 0x100(%r10) (49 F7 B2 00 01 00 00): REX.W and REX.B, mod 2, a 32-bit
 * displacement. */
static struct result divq_r10_disp32(uint64_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("movq %2, %%r10\n\txorl %%edx, %%edx\n\tdivq 0x100(%%r10)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"((uintptr_t)divisor - 0x100u), "a"(dividend)
                     : "r10", "memory");
    return r;
}

/* divl -0x100(%rcx) (F7 B1 00 FF FF FF): a negative disp32, sign-extended. */
static struct result divl_negative_disp32(uint32_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl -0x100(%%rcx)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"((uintptr_t)divisor + 0x100u)
                     : "memory");
    return r;
}

/* divl (%r13) (41 F7 75 00): rm 101 with REX.B is R13, which needs mod 1
 * and a zero disp8 (rm 101 with mod 0 is RIP-relative). */
static struct result divl_r13(uint32_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("movq %2, %%r13\n\txorl %%edx, %%edx\n\tdivl (%%r13)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(divisor), "a"((uint64_t)dividend)
                     : "r13", "memory");
    return r;
}

/* divq (%r12) (49 F7 34 24): rm 100 with REX.B is still a SIB byte, whose
 * base 100 with REX.B is R12. */
static struct result divq_r12(uint64_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("movq %2, %%r12\n\txorl %%edx, %%edx\n\tdivq (%%r12)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(divisor), "a"(dividend)
                     : "r12", "memory");
    return r;
}

/* divl 8(%rcx,%rbx,4) (F7 74 99 08): SIB with base, index, scale 4, disp8. */
static struct result divl_sib(uint32_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 8(%%rcx,%%rbx,4)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)dividend), "c"((uintptr_t)slots - 8u), "b"(slot * 2u)
                     : "memory");
    return r;
}

/* divq (%r8,%r9,8) (4B F7 34 C8): REX.W, REX.X and REX.B. */
static struct result divq_sib_r8_r9(uint64_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("movq %2, %%r8\n\tmovq %3, %%r9\n\txorl %%edx, %%edx\n\tdivq (%%r8,%%r9,8)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(slots), "r"(slot), "a"(dividend)
                     : "r8", "r9", "memory");
    return r;
}

/* divl (%rcx,%r12,1) (42 F7 34 21): SIB index 100 with REX.X is R12, not
 * "no index". */
static struct result divl_index_r12(uint32_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("movq %2, %%r12\n\txorl %%edx, %%edx\n\tdivl (%%rcx,%%r12,1)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(slot * sizeof slots[0]), "a"((uint64_t)dividend), "c"(slots)
                     : "r12", "memory");
    return r;
}

/* divl (%r13,%rbx,2) (41 F7 74 5D 00): SIB base 101 with REX.B and mod 1 is
 * R13 (only mod 0 means "no base"). */
static struct result divl_sib_base_r13(uint32_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("movq %2, %%r13\n\txorl %%edx, %%edx\n\tdivl (%%r13,%%rbx,2)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "r"(slots), "a"((uint64_t)dividend), "b"(slot * 4u)
                     : "r13", "memory");
    return r;
}

/* divl slots(,%rbx,4) (F7 34 9D disp32): SIB with no base, an index and an
 * absolute disp32 (the -fno-pie code's table form; platform/Makefile). */
static struct result divl_sib_no_base(uint32_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %c2(,%%rbx,4)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(slots), "a"((uint64_t)dividend), "b"(slot * 2u)
                     : "memory");
    return r;
}

/* divq slots(,%r9,8) (4A F7 34 CD disp32): the same with REX.W and REX.X. */
static struct result divq_sib_no_base_r9(uint64_t dividend, uint64_t slot)
{
    struct result r;
    __asm__ volatile("movq %3, %%r9\n\txorl %%edx, %%edx\n\tdivq %c2(,%%r9,8)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(slots), "r"(slot), "a"(dividend)
                     : "r9", "memory");
    return r;
}

/* The fixed slots of the forms whose address is a constant. */
#define SLOT_ABSOLUTE 4u
#define SLOT_NO_BASE_REX_B 5u
#define SLOT_RIP_32 6u
#define SLOT_RIP_64 7u
#define SLOT_RIP_PREFIXED 8u
#define SLOT_RIP_REX_B 9u

/* divl slots+32 (F7 34 25 disp32): an absolute address, which 64-bit mode
 * encodes as SIB with no base and no index. */
static struct result divl_absolute(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %c2"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_ABSOLUTE]), "a"((uint64_t)dividend)
                     : "memory");
    return r;
}

/* 41 F7 34 25 disp32: the same with REX.B, which does not make SIB base 101
 * with mod 0 R13: still no base. R13 points at a non-zero slot. */
static struct result divl_no_base_rex_b(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("movq %3, %%r13\n\txorl %%edx, %%edx\n\t.byte 0x41, 0xF7, 0x34, 0x25\n\t.long %c2"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_NO_BASE_REX_B]), "r"(slots), "a"((uint64_t)dividend)
                     : "r13", "memory");
    return r;
}

/* divl slots+48(%rip) (F7 35 disp32): RIP-relative. */
static struct result divl_rip(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %c2(%%rip)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_RIP_32]), "a"((uint64_t)dividend)
                     : "memory");
    return r;
}

/* divq slots+56(%rip) (48 F7 35 disp32): one byte longer, so the
 * instruction ends one byte later. */
static struct result divq_rip(uint64_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivq %c2(%%rip)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_RIP_64]), "a"(dividend)
                     : "memory");
    return r;
}

/* ds; cs; idivq slots+64(%rip) (3E 2E 48 F7 3D disp32): two prefixes and
 * REX.W before a RIP-relative operand. */
static struct result idivq_rip_prefixed(int64_t dividend)
{
    struct result r;
    __asm__ volatile("cqto\n\t.byte 0x3E, 0x2E\n\tidivq %c2(%%rip)"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_RIP_PREFIXED]), "a"(dividend)
                     : "memory");
    return r;
}

/* 41 F7 35 disp32: rm 101 with mod 0 and REX.B is RIP-relative, not R13.
 * R13 points at a non-zero slot. */
static struct result divl_rip_rex_b(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("movq %3, %%r13\n\txorl %%edx, %%edx\n\t.byte 0x41, 0xF7, 0x35\n\t.long %c2 - . - 4"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "i"(&slots[SLOT_RIP_REX_B]), "r"(slots), "a"((uint64_t)dividend)
                     : "r13", "memory");
    return r;
}

/* divq 8(%rsp) (48 F7 74 24 08): SIB with RSP as the base, below the red
 * zone. */
static struct result divq_rsp_disp8(uint64_t dividend, uint64_t divisor)
{
    struct result r;
    __asm__ volatile("subq $256, %%rsp\n\tmovq %%rcx, 8(%%rsp)\n\txorl %%edx, %%edx\n\tdivq 8(%%rsp)\n\t"
                     "addq $256, %%rsp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"(dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* idivl 0x100(%rsp) (F7 BC 24 00 01 00 00): RSP with mod 2. */
static struct result idivl_rsp_disp32(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("subq $0x180, %%rsp\n\tmovl %%ecx, 0x100(%%rsp)\n\tcltd\n\tidivl 0x100(%%rsp)\n\t"
                     "addq $0x180, %%rsp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)(uint32_t)dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* idivl -4(%rbp) (F7 7D FC): RBP as the base needs mod 1 (rm 101 with mod 0
 * is RIP-relative); the divisor is 4 bytes below it. RBP (the frame
 * pointer) is saved around it, below the red zone. */
static struct result idivl_rbp_negative_disp8(int32_t dividend, const uint64_t *divisor)
{
    struct result r;
    __asm__ volatile("subq $128, %%rsp\n\tpushq %%rbp\n\tleaq 4(%%rcx), %%rbp\n\tcltd\n\tidivl -4(%%rbp)\n\t"
                     "popq %%rbp\n\taddq $128, %%rsp"
                     : "=a"(r.quotient), "=&d"(r.remainder)
                     : "a"((uint64_t)(uint32_t)dividend), "c"(divisor)
                     : "memory");
    return r;
}

/* What libgcc gives the GBA for a 64-bit division by zero, from the record
 * (platform/arm_division_libgcc/libgcc_division.txt, the apcs-gnu column). */
struct by_zero {
    uint64_t dividend;
    uint64_t quotient;
    uint64_t remainder;
};

static const struct by_zero unsigned_by_zero[] = {
    {0, 1, 0},
    {1, 3, 1},
    {5, 0xF, 5},
    {0x80000000u, 0x1FFFFFFFFu, 0x80000000u},
    {0xDEADBEEFu, 0x1FFFFFFFFu, 0xDEADBEEFu},
    {0x100000000u, 0x3FFFFFFFFu, 0},
    {0x1FFFFFFFFu, 0x3FFFFFFFFu, 0x7FFFFFFFu},
    {0xFFFFFFFFFu, 0x1FFFFFFFFFu, 0xFFFFFFFu},
    {0x123456789ABCDEF0u, 0x3FFFFFFFFFFFFFFFu, 0},
    {0x3FFFFFFFFFFFFFFFu, 0x7FFFFFFFFFFFFFFFu, 3},
    {0x4000000000000000u, UINT64_MAX, 0},
    {0x7FFFFFFFFFFFFFFFu, UINT64_MAX, 1},
    {0x8000000000000000u, UINT64_MAX, 0},
    {UINT64_MAX, UINT64_MAX, 0},
};

/* Signed values as their two's complement. */
static const struct by_zero signed_by_zero[] = {
    {0, 1, 0},
    {1, 3, 1},
    {0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFDu, 0xFFFFFFFFFFFFFFFFu},
    {5, 0xF, 5},
    {0xFFFFFFFFFFFFFFFBu, 0xFFFFFFFFFFFFFFF1u, 0xFFFFFFFFFFFFFFFBu},
    {0xFFFFFFFF80000000u, 0xFFFFFFFE00000001u, 0xFFFFFFFF80000000u},
    {0xFFFFFFFF00000000u, 0xFFFFFFFC00000001u, 0},
    {0xFFFFFFFE00000001u, 0xFFFFFFFC00000001u, 0xFFFFFFFF80000001u},
    {0x4000000000000000u, 0xFFFFFFFFFFFFFFFFu, 0},
    {0xC000000000000000u, 1, 0},
    {0xC000000000000001u, 0x8000000000000001u, 0xFFFFFFFFFFFFFFFDu},
    {0x7FFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu, 1},
    {0x8000000000000001u, 1, 0xFFFFFFFFFFFFFFFFu},
    {0x8000000000000000u, 1, 0},
};

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

PTEST(unsigned_32_bit_division_by_zero_gives_all_ones_or_zero)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result nonzero = divl_register(16711680u, 0);
    struct result one = divl_register(1, 0);
    struct result zero = divl_register(0, 0);
    arm_division_shutdown();
    expect(nonzero, UINT32_MAX, UINT32_MAX);
    expect(one, UINT32_MAX, UINT32_MAX);
    expect(zero, 0, 0);
}

PTEST(signed_32_bit_division_by_zero_saturates_by_the_sign)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result positive = idivl_register(255, 0);
    struct result negative = idivl_register(-3, 0);
    struct result zero = idivl_register(0, 0);
    arm_division_shutdown();
    expect(positive, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX);
    expect(negative, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN); /* zero-extended into RAX and RDX */
    expect(zero, 0, 0);
}

PTEST(int32_min_by_minus_one_is_int32_min_remainder_zero)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result r = idivl_register(INT32_MIN, -1);
    arm_division_shutdown();
    expect(r, (uint32_t)INT32_MIN, 0);
}

PTEST(a_32_bit_result_replaces_rax_and_rdx_whole)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result u = divl_upper_halves_set(5, 0);
    struct result s = idivl_upper_halves_set(-3, 0);
    arm_division_shutdown();
    expect(u, UINT32_MAX, UINT32_MAX);
    expect(s, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN);
}

PTEST(unsigned_64_bit_division_by_zero_gives_udivdi3_results)
{
    struct result r[COUNT(unsigned_by_zero)];
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    for (size_t i = 0; i < COUNT(unsigned_by_zero); i++)
        r[i] = divq_register(unsigned_by_zero[i].dividend, 0);
    arm_division_shutdown();
    for (size_t i = 0; i < COUNT(unsigned_by_zero); i++)
        expect(r[i], unsigned_by_zero[i].quotient, unsigned_by_zero[i].remainder);
}

PTEST(signed_64_bit_division_by_zero_gives_divdi3_results)
{
    struct result r[COUNT(signed_by_zero)];
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    for (size_t i = 0; i < COUNT(signed_by_zero); i++)
        r[i] = idivq_register((int64_t)signed_by_zero[i].dividend, 0);
    arm_division_shutdown();
    for (size_t i = 0; i < COUNT(signed_by_zero); i++)
        expect(r[i], signed_by_zero[i].quotient, signed_by_zero[i].remainder);
}

PTEST(int64_min_by_minus_one_is_int64_min_remainder_zero)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result r = idivq_register(INT64_MIN, -1);
    arm_division_shutdown();
    expect(r, (uint64_t)INT64_MIN, 0);
}

PTEST(ordinary_division_is_untouched)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result u32 = divl_register(7, 2);
    struct result s32 = idivl_register(-7, 2);
    struct result u64 = divq_register(0x700000000u, 2);
    struct result s64 = idivq_register(-7, 2);
    arm_division_shutdown();
    expect(u32, 3, 1);
    expect(s32, (uint32_t)-3, (uint32_t)-1);
    expect(u64, 0x380000000u, 0);
    expect(s64, (uint64_t)-3, (uint64_t)-1);
}

PTEST(register_operands_with_rex_are_decoded)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result r9d = divl_r9d(5, 0);
    struct result r15 = idivq_r15(-5, 0);
    struct result rex_r = divl_rex_r(5, 0);
    struct result rex_empty = divl_rex_empty(5, 0);
    struct result prefixed = divq_prefixed(5, 0);
    arm_division_shutdown();
    expect(r9d, UINT32_MAX, UINT32_MAX);
    expect(r15, (uint64_t)-15, (uint64_t)-5);
    expect(rex_r, UINT32_MAX, UINT32_MAX);
    expect(rex_empty, UINT32_MAX, UINT32_MAX);
    expect(prefixed, 0xF, 5);
}

PTEST(every_memory_operand_form_is_decoded)
{
    /* Each form is aimed at its own slot; the other fifteen divisors are
     * non-zero, so only a decoder that finds the right address completes it.
     * A 32-bit form by zero gives (0xFFFFFFFF, 0xFFFFFFFF), a 64-bit one of
     * 5 gives (15, 5). */
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    aim(1, sizeof(uint32_t));
    struct result base = divl_base(5, &slots[1]);
    aim(2, sizeof(uint64_t));
    struct result disp8 = divq_disp8(5, &slots[2]);
    aim(3, sizeof(uint32_t));
    struct result negative_disp8 = divl_negative_disp8(5, &slots[3]);
    aim(10, sizeof(uint64_t));
    struct result r10_disp32 = divq_r10_disp32(5, &slots[10]);
    aim(11, sizeof(uint32_t));
    struct result negative_disp32 = divl_negative_disp32(5, &slots[11]);
    aim(12, sizeof(uint32_t));
    struct result r13 = divl_r13(5, &slots[12]);
    aim(13, sizeof(uint64_t));
    struct result r12 = divq_r12(5, &slots[13]);
    aim(14, sizeof(uint32_t));
    struct result sib = divl_sib(5, 14);
    aim(15, sizeof(uint64_t));
    struct result sib_r8_r9 = divq_sib_r8_r9(5, 15);
    aim(6, sizeof(uint32_t));
    struct result index_r12 = divl_index_r12(5, 6);
    aim(7, sizeof(uint32_t));
    struct result sib_base_r13 = divl_sib_base_r13(5, 7);
    aim(8, sizeof(uint32_t));
    struct result sib_no_base = divl_sib_no_base(5, 8);
    aim(9, sizeof(uint64_t));
    struct result sib_no_base_r9 = divq_sib_no_base_r9(5, 9);
    aim(SLOT_ABSOLUTE, sizeof(uint32_t));
    struct result absolute = divl_absolute(5);
    aim(SLOT_NO_BASE_REX_B, sizeof(uint32_t));
    struct result no_base_rex_b = divl_no_base_rex_b(5);
    arm_division_shutdown();
    expect(base, UINT32_MAX, UINT32_MAX);
    expect(disp8, 0xF, 5);
    expect(negative_disp8, UINT32_MAX, UINT32_MAX);
    expect(r10_disp32, 0xF, 5);
    expect(negative_disp32, UINT32_MAX, UINT32_MAX);
    expect(r13, UINT32_MAX, UINT32_MAX);
    expect(r12, 0xF, 5);
    expect(sib, UINT32_MAX, UINT32_MAX);
    expect(sib_r8_r9, 0xF, 5);
    expect(index_r12, UINT32_MAX, UINT32_MAX);
    expect(sib_base_r13, UINT32_MAX, UINT32_MAX);
    expect(sib_no_base, UINT32_MAX, UINT32_MAX);
    expect(sib_no_base_r9, 0xF, 5);
    expect(absolute, UINT32_MAX, UINT32_MAX);
    expect(no_base_rex_b, UINT32_MAX, UINT32_MAX);
}

PTEST(rip_relative_operands_count_from_the_end_of_the_instruction)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    aim(SLOT_RIP_32, sizeof(uint32_t));
    struct result rip_32 = divl_rip(5);
    aim(SLOT_RIP_64, sizeof(uint64_t));
    struct result rip_64 = divq_rip(5);
    aim(SLOT_RIP_PREFIXED, sizeof(uint64_t));
    struct result prefixed = idivq_rip_prefixed(-5);
    aim(SLOT_RIP_REX_B, sizeof(uint32_t));
    struct result rex_b = divl_rip_rex_b(5);
    arm_division_shutdown();
    expect(rip_32, UINT32_MAX, UINT32_MAX);
    expect(rip_64, 0xF, 5);
    expect(prefixed, (uint64_t)-15, (uint64_t)-5);
    expect(rex_b, UINT32_MAX, UINT32_MAX);
}

PTEST(rsp_and_rbp_based_operands_are_decoded)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result rsp_disp8 = divq_rsp_disp8(5, 0);
    struct result rsp_disp32 = idivl_rsp_disp32(-5, 0);
    aim(20, sizeof(uint32_t));
    struct result rbp = idivl_rbp_negative_disp8(7, &slots[20]);
    arm_division_shutdown();
    expect(rsp_disp8, 0xF, 5);
    expect(rsp_disp32, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN);
    expect(rbp, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX);
}

PTEST(memory_operands_are_read_not_assumed_zero)
{
    /* Non-zero divisors at the same addressing forms divide normally, so each
     * form addresses the divisor the decoder looks for: twice the divisor
     * plus one divides into 2 rem 1, minus the divisor minus one into -1 rem
     * -1. */
    aim(0, sizeof(uint64_t));
    const uint32_t d3 = (uint32_t)sentinel(3), d5 = (uint32_t)sentinel(5), d6 = (uint32_t)sentinel(6);
    const uint32_t d9 = (uint32_t)sentinel(9), d20 = (uint32_t)sentinel(20);
    const uint64_t d8 = sentinel(8), d10 = sentinel(10);
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result base = divl_base(2u * d3 + 1u, &slots[3]);
    struct result r10_disp32 = divq_r10_disp32(2u * d10 + 1u, &slots[10]);
    struct result index_r12 = divl_index_r12(2u * d6 + 1u, 6);
    struct result no_base_rex_b = divl_no_base_rex_b(2u * d5 + 1u);    /* SLOT_NO_BASE_REX_B */
    struct result rip_rex_b = divl_rip_rex_b(2u * d9 + 1u);            /* SLOT_RIP_REX_B */
    struct result rip_prefixed = idivq_rip_prefixed(-(int64_t)d8 - 1); /* SLOT_RIP_PREFIXED */
    struct result rsp_disp8 = divq_rsp_disp8(10, 4);
    struct result rbp = idivl_rbp_negative_disp8(-(int32_t)d20 - 1, &slots[20]);
    arm_division_shutdown();
    expect(base, 2, 1);
    expect(r10_disp32, 2, 1);
    expect(index_r12, 2, 1);
    expect(no_base_rex_b, 2, 1);
    expect(rip_rex_b, 2, 1);
    expect(rip_prefixed, (uint64_t)-1, (uint64_t)-1);
    expect(rsp_disp8, 2, 2);
    expect(rbp, (uint32_t)-1, (uint32_t)-1);
}

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

#else
#error "test_arm_division covers i386 and x86-64"
#endif

PTEST_MAIN()
