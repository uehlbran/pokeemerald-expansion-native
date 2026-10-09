/*
 * test_arm_division.c: x86 division by zero completed as the GBA's libgcc
 * completes it (arm_division.h).
 *
 * Expected results from the libgcc routines linked into expansion's GBA test
 * ELF (gcc-arm-none-eabi 13.2.1; arm_division.h quotes them): unsigned n/0 is
 * 0xFFFFFFFF unless n is 0; signed n/0 is INT32_MAX, INT32_MIN or 0 by the
 * sign of n; the same for %; INT32_MIN / -1 is INT32_MIN rem 0. The divisions
 * are inline assembly, so that each operand encoding of DIV and IDIV (Intel
 * SDM vol. 2: register, [reg], [reg+disp8], [reg+disp32], [disp32], SIB
 * with and without base, a segment prefix) is the one tested, and so that
 * UndefinedBehaviorSanitizer does not stop the C division first.
 */
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

#include "arm_division.h"
#include "ptest.h"

#if defined(__i386__)

struct result {
    uint32_t quotient;
    uint32_t remainder;
};

static struct result div_register(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %%ecx" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor));
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
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl (%%ecx)" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor) : "memory");
    return r;
}

/* divl 4(%ecx): mod 1, an 8-bit displacement. */
static struct result div_disp8(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 4(%%ecx)" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor - 1) : "memory");
    return r;
}

/* divl 0x100(%ecx): mod 2, a 32-bit displacement. */
static struct result div_disp32(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 0x100(%%ecx)" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"((uintptr_t)divisor - 0x100) : "memory");
    return r;
}

/* divl global_divisor: mod 0, rm 5, an absolute address. */
static struct result div_absolute(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %2" : "=a"(r.quotient), "=&d"(r.remainder) : "m"(global_divisor), "a"(dividend) : "memory");
    return r;
}

/* divl 8(%ecx,%ebx,4): a SIB byte and a displacement. */
static struct result div_sib(uint32_t dividend, const uint32_t *array, uint32_t index)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl 8(%%ecx,%%ebx,4)" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(array - 2), "b"(index) : "memory");
    return r;
}

/* divl global_divisor(,%ebx,4): SIB with no base, a 32-bit displacement. */
static struct result div_sib_no_base(uint32_t dividend)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\tdivl %c2(,%%ebx,4)" : "=a"(r.quotient), "=&d"(r.remainder) : "i"(&global_divisor), "a"(dividend), "b"(0) : "memory");
    return r;
}

/* idivl (%esp): SIB with ESP as base, after a push. */
static struct result idiv_stack(int32_t dividend, int32_t divisor)
{
    struct result r;
    __asm__ volatile("pushl %%ecx\n\tcltd\n\tidivl (%%esp)\n\taddl $4, %%esp" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor) : "memory");
    return r;
}

/* ds; divl (%ecx): a segment override prefix. */
static struct result div_prefixed(uint32_t dividend, const uint32_t *divisor)
{
    struct result r;
    __asm__ volatile("xorl %%edx, %%edx\n\t.byte 0x3e\n\tdivl (%%ecx)" : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor) : "memory");
    return r;
}

/* idivl -4(%ebp): mod 1 with EBP as the base and a negative disp8; the
 * divisor is 4 bytes below the base. EBP is saved and restored around it. */
static struct result idiv_ebp_negative_disp8(int32_t dividend, const int32_t *divisor)
{
    struct result r;
    __asm__ volatile("pushl %%ebp\n\tleal 4(%%ecx), %%ebp\n\tcltd\n\tidivl -4(%%ebp)\n\tpopl %%ebp"
                     : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor) : "memory");
    return r;
}

/* divl 8(%esp): a SIB byte with ESP as the base and no index, mod 1. */
static struct result div_esp_disp8(uint32_t dividend, uint32_t divisor)
{
    struct result r;
    __asm__ volatile("subl $12, %%esp\n\tmovl %%ecx, 8(%%esp)\n\txorl %%edx, %%edx\n\tdivl 8(%%esp)\n\taddl $12, %%esp"
                     : "=a"(r.quotient), "=&d"(r.remainder) : "a"(dividend), "c"(divisor) : "memory");
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
    memset(divisor_in_memory, 0, sizeof divisor_in_memory);
    global_divisor = 0;
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    struct result base = div_base(5, &divisor_in_memory[0]);
    struct result disp8 = div_disp8(5, &divisor_in_memory[1]);
    struct result disp32 = div_disp32(5, &divisor_in_memory[2]);
    struct result absolute = div_absolute(5);
    struct result sib = div_sib(5, divisor_in_memory, 3);
    struct result sib_no_base = div_sib_no_base(5);
    struct result stack = idiv_stack(-5, 0);
    struct result prefixed = div_prefixed(5, &divisor_in_memory[0]);
    const int32_t below_ebp = 0;
    struct result ebp_negative = idiv_ebp_negative_disp8(7, &below_ebp);
    struct result esp_disp8 = div_esp_disp8(5, 0);
    arm_division_shutdown();
    expect(base, UINT32_MAX, UINT32_MAX);
    expect(disp8, UINT32_MAX, UINT32_MAX);
    expect(disp32, UINT32_MAX, UINT32_MAX);
    expect(absolute, UINT32_MAX, UINT32_MAX);
    expect(sib, UINT32_MAX, UINT32_MAX);
    expect(sib_no_base, UINT32_MAX, UINT32_MAX);
    expect(stack, (uint32_t)INT32_MIN, (uint32_t)INT32_MIN);
    expect(prefixed, UINT32_MAX, UINT32_MAX);
    expect(ebp_negative, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX);
    expect(esp_disp8, UINT32_MAX, UINT32_MAX);
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

static void byte_division_by_zero(void)
{
    __asm__ volatile("movw $5, %%ax\n\tmovb $0, %%cl\n\tdivb %%cl" : : : "eax", "ecx");
}

static void wide_quotient(void)
{
    /* EDX:EAX = 2^33 divided by 1: the quotient does not fit 32 bits. */
    __asm__ volatile("movl $2, %%edx\n\tmovl $0, %%eax\n\tmovl $1, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx");
}

/* divw %cx (66h F7h F1h): the operand-size prefix makes it a 16-bit
 * division, not one the decoder completes. */
static void word_division_by_zero(void)
{
    __asm__ volatile("movl $5, %%eax\n\txorl %%edx, %%edx\n\txorl %%ecx, %%ecx\n\t.byte 0x66, 0xF7, 0xF1"
                     : : : "eax", "ecx", "edx");
}

/* 67h F7h F1h: an address-size prefix (meaningless with a register operand,
 * the CPU still divides EDX:EAX by ECX), which the decoder refuses. */
static void address_size_prefixed_division_by_zero(void)
{
    __asm__ volatile("movl $5, %%eax\n\txorl %%edx, %%edx\n\txorl %%ecx, %%ecx\n\t.byte 0x67, 0xF7, 0xF1"
                     : : : "eax", "ecx", "edx");
}

/* An unsigned division by zero of a 64-bit dividend (EDX non-zero): not
 * one of libgcc's 32-bit routines' cases. */
static void wide_dividend_by_zero(void)
{
    __asm__ volatile("movl $1, %%edx\n\tmovl $5, %%eax\n\txorl %%ecx, %%ecx\n\tdivl %%ecx" : : : "eax", "ecx", "edx");
}

static int calls_after(void (*division)(void))
{
    struct sigaction mine, saved;
    memset(&mine, 0, sizeof mine);
    mine.sa_sigaction = previous_handler;
    mine.sa_flags = SA_SIGINFO;
    sigemptyset(&mine.sa_mask);
    if (sigaction(SIGFPE, &mine, &saved) != 0)
        return -1;
    previous_handler_calls = 0;
    if (arm_division_init() != ARM_DIVISION_OK)
        return -1;
    if (sigsetjmp(previous_handler_target, 1) == 0)
        division();
    arm_division_shutdown();
    struct sigaction now;
    sigaction(SIGFPE, &saved, &now);
    return now.sa_sigaction == previous_handler ? previous_handler_calls : -2;
}

PTEST(other_divide_errors_go_to_the_previous_handler)
{
    PT_ASSERT_EQ_INT(calls_after(byte_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_quotient), 1);
    PT_ASSERT_EQ_INT(calls_after(word_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(address_size_prefixed_division_by_zero), 1);
    PT_ASSERT_EQ_INT(calls_after(wide_dividend_by_zero), 1);
}

PTEST(shutdown_twice_is_safe)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK);
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_OK); /* again: restores, then installs */
    arm_division_shutdown();
    arm_division_shutdown();
    struct sigaction now;
    PT_ASSERT_EQ_INT(sigaction(SIGFPE, NULL, &now), 0);
    PT_ASSERT((now.sa_flags & SA_SIGINFO) == 0 || now.sa_sigaction != NULL);
}

#else

PTEST(a_64_bit_build_does_not_install_the_handler)
{
    PT_ASSERT_EQ_INT(arm_division_init(), ARM_DIVISION_ERR_64_BIT);
    arm_division_shutdown();
}

#endif

PTEST_MAIN()
