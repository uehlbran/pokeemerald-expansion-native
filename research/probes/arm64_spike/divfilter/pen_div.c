/*
 * pen_div.c: integer division with the results of the GBA build's libgcc
 * (-mabi=apcs-gnu: __divsi3, __modsi3, __udivsi3, __umodsi3 and the DI
 * forms), as platform/arm_division.h lines 11-19 state them and
 * platform/arm_division_libgcc/libgcc_division.txt records them. The targets
 * of divfilter.py's calls; compiled normally, never through the filter.
 */
#include <stdint.h>

static unsigned bit_length(uint64_t n)
{
    unsigned k = 0;
    while (n != 0) {
        k++;
        n >>= 1;
    }
    return k;
}

/* 64-bit unsigned n / 0: 2^(k+1) - 1, all ones from k = 63. */
static uint64_t u64_quotient_by_zero(uint64_t n)
{
    unsigned k = bit_length(n);
    return k >= 63 ? UINT64_MAX : (UINT64_C(1) << (k + 1)) - 1;
}

/* 64-bit unsigned n % 0: n mod 2^(64-k) (n itself while k <= 32; 0 at k = 64). */
static uint64_t u64_remainder_by_zero(uint64_t n)
{
    unsigned k = bit_length(n);
    return k == 0 ? n : n & ((UINT64_C(1) << (64 - k)) - 1);
}

uint32_t pen_div_u32(uint32_t n, uint32_t d)
{
    return d == 0 ? (n != 0 ? UINT32_MAX : 0) : n / d;
}

uint32_t pen_mod_u32(uint32_t n, uint32_t d)
{
    return d == 0 ? (n != 0 ? UINT32_MAX : 0) : n % d;
}

int32_t pen_div_s32(int32_t n, int32_t d)
{
    if (d == 0)
        return n > 0 ? INT32_MAX : n < 0 ? INT32_MIN : 0;
    if (n == INT32_MIN && d == -1)
        return INT32_MIN;
    return n / d;
}

int32_t pen_mod_s32(int32_t n, int32_t d)
{
    if (d == 0)
        return n > 0 ? INT32_MAX : n < 0 ? INT32_MIN : 0;
    if (d == -1)
        return 0;
    return n % d;
}

uint64_t pen_div_u64(uint64_t n, uint64_t d)
{
    return d == 0 ? u64_quotient_by_zero(n) : n / d;
}

uint64_t pen_mod_u64(uint64_t n, uint64_t d)
{
    return d == 0 ? u64_remainder_by_zero(n) : n % d;
}

/* Signed 64-bit: computed on the magnitudes, the unsigned results negated if
 * n < 0 (and, for a non-zero divisor, the quotient if the signs differ). */
static uint64_t magnitude(int64_t v)
{
    return v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
}

int64_t pen_div_s64(int64_t n, int64_t d)
{
    uint64_t q = d == 0 ? u64_quotient_by_zero(magnitude(n)) : magnitude(n) / magnitude(d);
    int negative = d == 0 ? n < 0 : (n < 0) != (d < 0);
    return (int64_t)(negative ? (uint64_t)0 - q : q);
}

int64_t pen_mod_s64(int64_t n, int64_t d)
{
    uint64_t r = d == 0 ? u64_remainder_by_zero(magnitude(n)) : magnitude(n) % magnitude(d);
    return (int64_t)(n < 0 ? (uint64_t)0 - r : r);
}
