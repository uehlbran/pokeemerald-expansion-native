/*
 * libgcc_division.c: what the GBA's integer division returns where x86's
 * DIV and IDIV raise a divide error (a zero divisor, and the most negative
 * value divided by -1), measured by running the ARM7TDMI's own libgcc under
 * qemu-arm. The output is the record platform/arm_division.c follows
 * (libgcc_division.txt; `make -C platform/arm_division_libgcc run` rebuilds
 * it and compares).
 *
 * The ARM7TDMI has no divide instruction (GBATEK "ARM Instruction Summary"),
 * so every / and % is a libgcc call; which routine depends on the ABI the
 * caller was compiled for (libgcc_division.h). Each line gives, for one
 * operand type and dividend, the quotient and remainder under both ABIs:
 *
 *   <type> <n> / <d>  apcs-gnu <n / d> <n % d>  aapcs <n / d> <n % d>
 *
 * Values are in hexadecimal, signed ones as two's complement. Dividends:
 * the edges of each type, and for the 64-bit types every bit length from 1
 * to 64 (the smallest and largest value of each), because the 64-bit
 * routines' result by zero depends on it. The last line checks the rule
 * platform/arm_division.c applies for a 64-bit division by zero against
 * the apcs-gnu routines for pseudo-random dividends of every bit length.
 */
#include <stdint.h>
#include <stdio.h>

#include "libgcc_division.h"

#define U32_HEX_DIGITS 8
#define U64_BITS 64u
#define HALF_SHIFT 32u
#define LOW_HALF_MASK 0xFFFFFFFFu

static void print_u32(uint32_t value)
{
    (void)printf(" 0x%0*lx", U32_HEX_DIGITS, (unsigned long)value);
}

/* Two halves: no 64-bit argument through printf's varargs. */
static void print_u64(uint64_t value)
{
    (void)printf(" 0x%0*lx%0*lx", U32_HEX_DIGITS, (unsigned long)(value >> HALF_SHIFT), U32_HEX_DIGITS,
                 (unsigned long)(value & LOW_HALF_MASK));
}

/* The record's names for the two ABIs (libgcc_division.h). */
#define ABI_GAME "apcs-gnu"
#define ABI_EABI "aapcs"

static void print_results_u32(const char *abi, uint32_t quotient, uint32_t remainder)
{
    (void)printf("  %s", abi);
    print_u32(quotient);
    print_u32(remainder);
}

static void print_results_u64(const char *abi, uint64_t quotient, uint64_t remainder)
{
    (void)printf("  %s", abi);
    print_u64(quotient);
    print_u64(remainder);
}

static void line_u32(uint32_t n, uint32_t d)
{
    (void)printf("u32");
    print_u32(n);
    (void)printf(" /");
    print_u32(d);
    print_results_u32(ABI_GAME, game_division_ops.udiv32(n, d), game_division_ops.umod32(n, d));
    print_results_u32(ABI_EABI, eabi_division_ops.udiv32(n, d), eabi_division_ops.umod32(n, d));
    (void)printf("\n");
}

static void line_s32(int32_t n, int32_t d)
{
    (void)printf("s32");
    print_u32((uint32_t)n);
    (void)printf(" /");
    print_u32((uint32_t)d);
    print_results_u32(ABI_GAME, (uint32_t)game_division_ops.sdiv32(n, d), (uint32_t)game_division_ops.smod32(n, d));
    print_results_u32(ABI_EABI, (uint32_t)eabi_division_ops.sdiv32(n, d), (uint32_t)eabi_division_ops.smod32(n, d));
    (void)printf("\n");
}

static void line_u64(uint64_t n, uint64_t d)
{
    (void)printf("u64");
    print_u64(n);
    (void)printf(" /");
    print_u64(d);
    print_results_u64(ABI_GAME, game_division_ops.udiv64(n, d), game_division_ops.umod64(n, d));
    print_results_u64(ABI_EABI, eabi_division_ops.udiv64(n, d), eabi_division_ops.umod64(n, d));
    (void)printf("\n");
}

static void line_s64(int64_t n, int64_t d)
{
    (void)printf("s64");
    print_u64((uint64_t)n);
    (void)printf(" /");
    print_u64((uint64_t)d);
    print_results_u64(ABI_GAME, (uint64_t)game_division_ops.sdiv64(n, d), (uint64_t)game_division_ops.smod64(n, d));
    print_results_u64(ABI_EABI, (uint64_t)eabi_division_ops.sdiv64(n, d), (uint64_t)eabi_division_ops.smod64(n, d));
    (void)printf("\n");
}

static const uint32_t u32_dividends[] = {0, 1, 2, 5, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
static const int32_t s32_dividends[] = {0, 1, 5, INT32_MAX, -1, -5, INT32_MIN};
static const uint64_t u64_other_dividends[] = {5, 0xDEADBEEFu, 0x123456789ABCDEF0u, 0xFEDCBA9876543210u};
/* Bit lengths for the signed sweep: the short ones, around 32 and the top. */
static const unsigned s64_bit_lengths[] = {1, 2, 3, 4, 31, 32, 33, 62, 63};
static const int64_t s64_other_dividends[] = {INT64_MIN, INT64_MAX, 5, -5, 0x123456789ABCDEF0, -0x123456789ABCDEF0};

/* An ordinary division of each type, to show the operators divide. */
#define CONTROL_DIVIDEND 7
#define CONTROL_DIVISOR 2

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static void unsigned_64(void)
{
    line_u64(0, 0);
    for (unsigned bits = 1; bits <= U64_BITS; bits++) {
        const uint64_t lowest = (uint64_t)1 << (bits - 1u);
        line_u64(lowest, 0);
        if (bits > 1)
            line_u64(lowest + (lowest - 1u), 0); /* the largest value of that length */
    }
    for (size_t i = 0; i < COUNT(u64_other_dividends); i++)
        line_u64(u64_other_dividends[i], 0);
    line_u64(CONTROL_DIVIDEND, CONTROL_DIVISOR);
}

static void signed_64(void)
{
    line_s64(0, 0);
    for (size_t i = 0; i < COUNT(s64_bit_lengths); i++) {
        const int64_t lowest = (int64_t)1 << (s64_bit_lengths[i] - 1u);
        const int64_t largest = lowest + (lowest - 1);
        line_s64(lowest, 0);
        line_s64(-lowest, 0);
        if (largest != lowest) {
            line_s64(largest, 0);
            line_s64(-largest, 0);
        }
    }
    for (size_t i = 0; i < COUNT(s64_other_dividends); i++)
        line_s64(s64_other_dividends[i], 0);
    line_s64(INT64_MIN, -1);
    line_s64(-CONTROL_DIVIDEND, CONTROL_DIVISOR);
}

/* platform/arm_division.c's rule for __udivdi3 and __umoddi3 by zero: with
 * k the bit length of m, the quotient is 2^(k+1) - 1 (all ones from k = 63)
 * and the remainder m mod 2^(64-k). __divdi3 and __moddi3 apply it to the
 * magnitude and give both results the dividend's sign. */
static void rule_unsigned(uint64_t m, uint64_t *quotient, uint64_t *remainder)
{
    unsigned length = 0;
    for (uint64_t rest = m; rest != 0; rest >>= 1)
        length++;
    *quotient = length >= U64_BITS - 1u ? UINT64_MAX : ((uint64_t)2 << length) - 1u;
    *remainder = length == 0 || length == U64_BITS ? 0 : m & (((uint64_t)1 << (U64_BITS - length)) - 1u);
}

static void rule_signed(int64_t n, uint64_t *quotient, uint64_t *remainder)
{
    const uint64_t magnitude = n < 0 ? 0u - (uint64_t)n : (uint64_t)n;
    rule_unsigned(magnitude, quotient, remainder);
    if (n < 0) {
        *quotient = 0u - *quotient;
        *remainder = 0u - *remainder;
    }
}

/* xorshift64 (Marsaglia 2003), from a fixed seed: the same dividends on
 * every run. */
#define CHECK_SEED 0x2545F4914F6CDD1Du
#define CHECK_COUNT 100000u
#define XORSHIFT_A 13u
#define XORSHIFT_B 7u
#define XORSHIFT_C 17u

static uint64_t next_random(uint64_t *state)
{
    *state ^= *state << XORSHIFT_A;
    *state ^= *state >> XORSHIFT_B;
    *state ^= *state << XORSHIFT_C;
    return *state;
}

static void check_rule(void)
{
    uint64_t state = CHECK_SEED;
    unsigned long differences = 0;
    for (unsigned i = 0; i < CHECK_COUNT; i++) {
        const uint64_t bits = next_random(&state);
        const uint64_t n = bits >> (next_random(&state) % U64_BITS); /* every bit length */
        uint64_t quotient, remainder;
        rule_unsigned(n, &quotient, &remainder);
        if (game_division_ops.udiv64(n, 0) != quotient || game_division_ops.umod64(n, 0) != remainder)
            differences++;
        const int64_t s = (next_random(&state) & 1u) != 0 ? (int64_t)(0u - n) : (int64_t)n;
        rule_signed(s, &quotient, &remainder);
        if ((uint64_t)game_division_ops.sdiv64(s, 0) != quotient
            || (uint64_t)game_division_ops.smod64(s, 0) != remainder)
            differences++;
    }
    (void)printf("rule: %u unsigned and %u signed dividends by zero, %lu differences from " ABI_GAME "\n", CHECK_COUNT,
                 CHECK_COUNT, differences);
}

int main(void)
{
    (void)printf("# arm-none-eabi-gcc %s, libgcc for -mthumb (expansion's), qemu-arm\n", __VERSION__);
    for (size_t i = 0; i < COUNT(u32_dividends); i++)
        line_u32(u32_dividends[i], 0);
    line_u32(CONTROL_DIVIDEND, CONTROL_DIVISOR);
    for (size_t i = 0; i < COUNT(s32_dividends); i++)
        line_s32(s32_dividends[i], 0);
    line_s32(INT32_MIN, -1);
    line_s32(-CONTROL_DIVIDEND, CONTROL_DIVISOR);
    unsigned_64();
    signed_64();
    check_rule();
    return 0;
}
