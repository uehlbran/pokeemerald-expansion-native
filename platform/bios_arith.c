/*
 * bios_arith.c: the BIOS arithmetic functions, SWI 06h-0Ah.
 *
 * Stands in for the BIOS's integer routines (GBATEK "BIOS Arithmetic
 * Functions", https://problemkaputt.de/gbatek.htm#biosarithmeticfunctions).
 * The GBA has no divide instruction and no floating point, so the BIOS
 * divides, takes square roots and arc tangents in 32-bit integer code; this
 * file does the same, with the 32-bit wraparound done on unsigned values
 * (bios_internal.h) so nothing here is undefined behaviour.
 *
 * Div: GBATEK gives the three results and the example -1234/10 = -123
 * remainder -4; that is truncation toward zero with the remainder taking
 * the numerator's sign, computed here on magnitudes. Division by zero hangs
 * the BIOS; here it returns zeros and says so once.
 *
 * ArcTan: GBATEK gives the fixed-point formats and warns of inaccuracy
 * beyond pi/4 but not the method. The BIOS evaluates an odd polynomial in
 * Horner form on a = -(t*t >> 14), seven 14-bit fixed-point steps, then
 * (t * b) >> 16, all in 32-bit registers. The coefficients are the BIOS
 * ROM's own constants; they are in neither GBATEK nor tonc, and come from
 * the implementer's knowledge of the BIOS (they are widely reproduced,
 * among other places in emulators' high-level BIOS code, none of which was
 * opened for this file). Two checks tie them to arctangent: at t = 4000h
 * (tan 1.0) every step is exactly b' = c - b and the result is exactly 2000h
 * (45 degrees), and for |t| <= 4000h the result is within 1.36 units of
 * atan (test_bios_arith.c). A primary-source check (a BIOS disassembly) is
 * still worth doing.
 *
 * ArcTan2: picks the octant from the signs of x and y and from |x| against
 * |y|, so ArcTan only ever sees a ratio of magnitude <= 1, and adds or
 * subtracts the angle from the octant's axis. The ratio is (num * 4000h) /
 * den, truncated toward zero as the BIOS's own division truncates.
 */
#include <stdbool.h>
#include <stdio.h>

#include "bios.h"
#include "bios_internal.h"

/* Angles: 10000h units per turn (GBATEK ArcTan2: "0000h-FFFFh for
 * 0<=THETA<2PI"). */
#define ANGLE_QUARTER_TURN INT32_C(0x4000)
#define ANGLE_HALF_TURN INT32_C(0x8000)
#define ANGLE_THREE_QUARTERS INT32_C(0xC000)
#define ANGLE_MASK UINT32_C(0xFFFF)

/* GBATEK ArcTan: "Tan, 16bit (1bit sign, 1bit integral part, 14bit decimal
 * part)": 1.0 is 4000h. */
#define ARCTAN_FRACTION_BITS 14u
#define ARCTAN_ONE INT32_C(0x4000)
/* The final product t * b carries 16 more fractional bits than the angle. */
#define ARCTAN_RESULT_SHIFT 16u

/* The BIOS polynomial: starting from b = A9h, b = (b * a) >> 14 + c for each
 * c below in turn (the first step is (A9h * a) >> 14 + 390h). */
#define ARCTAN_LEADING_COEFFICIENT INT32_C(0xA9)
static const int32_t arctan_coefficients[] = {
    0x0390, 0x091C, 0x0FB6, 0x16AA, 0x2081, 0x3651, 0xA2F9,
};

/* Sqrt works one base-4 digit (two bits) at a time, from the highest power
 * of 4 a u32 can hold, 4^15. */
#define SQRT_HIGHEST_POWER_OF_4 UINT32_C(0x40000000)
#define SQRT_DIGIT_SHIFT 2u

static bool warned_div_by_zero;

static uint32_t magnitude(int32_t value)
{
    return value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
}

struct bios_div_result bios_div(int32_t number, int32_t denom)
{
    struct bios_div_result result = { 0, 0, 0 };
    if (denom == 0) {
        if (!warned_div_by_zero) {
            fprintf(stderr, "bios: Div: division by zero (the BIOS would hang); returning 0\n");
            warned_div_by_zero = true;
        }
        return result;
    }
    uint32_t quotient = magnitude(number) / magnitude(denom);
    uint32_t remainder = magnitude(number) % magnitude(denom);
    bool negative_quotient = (number < 0) != (denom < 0);
    result.quotient = bios_s32_from_bits(negative_quotient ? 0u - quotient : quotient);
    result.remainder = bios_s32_from_bits(number < 0 ? 0u - remainder : remainder);
    result.abs_quotient = quotient;
    return result;
}

int32_t Div(int32_t num, int32_t denom)
{
    return bios_div(num, denom).quotient;
}

int32_t Mod(int32_t num, int32_t denom)
{
    return bios_div(num, denom).remainder;
}

int32_t DivArm(int32_t denom, int32_t num)
{
    return bios_div(num, denom).quotient;
}

int32_t ModArm(int32_t denom, int32_t num)
{
    return bios_div(num, denom).remainder;
}

/* Digit-by-digit (base 4) integer square root: the floor, exact for every
 * u32. */
uint16_t Sqrt(uint32_t num)
{
    uint32_t remainder = num;
    uint32_t root = 0;
    uint32_t bit = SQRT_HIGHEST_POWER_OF_4;
    while (bit > remainder) {
        bit >>= SQRT_DIGIT_SHIFT;
    }
    while (bit != 0) {
        if (remainder >= root + bit) {
            remainder -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= SQRT_DIGIT_SHIFT;
    }
    return (uint16_t)root;
}

int16_t ArcTan(int16_t tan)
{
    int32_t t = tan;
    int32_t a = -bios_asr(bios_mul(t, t), ARCTAN_FRACTION_BITS);
    int32_t b = ARCTAN_LEADING_COEFFICIENT;
    for (size_t i = 0; i < sizeof arctan_coefficients / sizeof arctan_coefficients[0]; i++) {
        b = bios_asr(bios_mul(b, a), ARCTAN_FRACTION_BITS) + arctan_coefficients[i];
    }
    /* An ASR by 16 of a 32-bit value always fits 16 bits. */
    return (int16_t)bios_asr(bios_mul(t, b), ARCTAN_RESULT_SHIFT);
}

/* num/den in ArcTan's 1.1.14 format; |num| <= |den|, den != 0, so the
 * product fits (|num| <= 8000h) and the result is within +-4000h. */
static int16_t tangent(int32_t num, int32_t den)
{
    return (int16_t)((num * ARCTAN_ONE) / den);
}

/* An angle in -8000h..17FFFh as 0..FFFFh. */
static uint16_t whole_turns_removed(int32_t angle)
{
    return (uint16_t)((uint32_t)angle & ANGLE_MASK);
}

uint16_t ArcTan2(int16_t x, int16_t y)
{
    if (y == 0) {
        return whole_turns_removed(x >= 0 ? 0 : ANGLE_HALF_TURN);
    }
    if (x == 0) {
        return whole_turns_removed(y > 0 ? ANGLE_QUARTER_TURN : ANGLE_THREE_QUARTERS);
    }
    if (magnitude(y) <= magnitude(x)) {
        /* Within 45 degrees of the x axis: the axis angle plus atan(y/x). */
        int32_t axis = x > 0 ? 0 : ANGLE_HALF_TURN;
        return whole_turns_removed(axis + ArcTan(tangent(y, x)));
    }
    /* Within 45 degrees of the y axis: the axis angle minus atan(x/y). */
    int32_t axis = y > 0 ? ANGLE_QUARTER_TURN : ANGLE_THREE_QUARTERS;
    return whole_turns_removed(axis - ArcTan(tangent(x, y)));
}
