// deps: bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of the BIOS arithmetic functions, from GBATEK "BIOS Arithmetic
 * Functions": Div (SWI 06h) returns quotient, remainder and |quotient|
 * ("-1234, 10 should return -123, -4, +123"), DivArm (SWI 07h) takes the
 * parameters exchanged, Sqrt (SWI 08h) is the integer square root ("Sqrt(2)
 * would return 1", "Sqrt(2 shl 30) would return 1.41421 shl 15"), ArcTan
 * (SWI 09h) takes 1.1.14 fixed point and returns -4000h..4000h with "a
 * problem in accuracy" beyond pi/4, ArcTan2 (SWI 0Ah) returns 0..FFFFh for
 * 0 <= theta < 2pi.
 *
 * Reference angles are atan() of the input, in units of 2pi/10000h,
 * computed by hand (to two decimals) and written next to each constant.
 */
#include <limits.h>
#include <stdint.h>

#include "bios.h"
#include "ptest.h"

/* The BIOS polynomial is within 1.36 units of atan for |tan| <= 1; ArcTan2
 * adds up to one unit from truncating the ratio. */
#define ARCTAN_TOLERANCE 2

static int32_t distance(int32_t a, int32_t b)
{
    return a > b ? a - b : b - a;
}

/* Angular distance on the 10000h circle. */
static int32_t angle_distance(uint16_t a, int32_t b)
{
    int32_t d = distance(a, b);
    return d > 0x8000 ? 0x10000 - d : d;
}

PTEST(div_gbatek_example)
{
    struct bios_div_result r = bios_div(-1234, 10);
    PT_ASSERT_EQ_INT(r.quotient, -123);
    PT_ASSERT_EQ_INT(r.remainder, -4);
    PT_ASSERT_EQ_U32(r.abs_quotient, 123);
}

PTEST(div_positive)
{
    struct bios_div_result r = bios_div(1234, 10);
    PT_ASSERT_EQ_INT(r.quotient, 123);
    PT_ASSERT_EQ_INT(r.remainder, 4);
    PT_ASSERT_EQ_U32(r.abs_quotient, 123);
    r = bios_div(0, 5);
    PT_ASSERT_EQ_INT(r.quotient, 0);
    PT_ASSERT_EQ_INT(r.remainder, 0);
    r = bios_div(7, 7);
    PT_ASSERT_EQ_INT(r.quotient, 1);
    PT_ASSERT_EQ_INT(r.remainder, 0);
}

/* Truncation toward zero; the remainder takes the numerator's sign. */
PTEST(div_signs)
{
    struct bios_div_result r = bios_div(1234, -10);
    PT_ASSERT_EQ_INT(r.quotient, -123);
    PT_ASSERT_EQ_INT(r.remainder, 4);
    PT_ASSERT_EQ_U32(r.abs_quotient, 123);
    r = bios_div(-1234, -10);
    PT_ASSERT_EQ_INT(r.quotient, 123);
    PT_ASSERT_EQ_INT(r.remainder, -4);
    PT_ASSERT_EQ_U32(r.abs_quotient, 123);
    r = bios_div(-5, 10);
    PT_ASSERT_EQ_INT(r.quotient, 0);
    PT_ASSERT_EQ_INT(r.remainder, -5);
}

/* The 32-bit result wraps: +2^31 is not representable. */
PTEST(div_int32_min_by_minus_one_wraps)
{
    struct bios_div_result r = bios_div(INT32_MIN, -1);
    PT_ASSERT_EQ_INT(r.quotient, INT32_MIN);
    PT_ASSERT_EQ_INT(r.remainder, 0);
    PT_ASSERT_EQ_U32(r.abs_quotient, UINT32_C(0x80000000));
}

PTEST(div_extremes)
{
    struct bios_div_result r = bios_div(INT32_MIN, 1);
    PT_ASSERT_EQ_INT(r.quotient, INT32_MIN);
    PT_ASSERT_EQ_U32(r.abs_quotient, UINT32_C(0x80000000));
    r = bios_div(INT32_MAX, INT32_MIN);
    PT_ASSERT_EQ_INT(r.quotient, 0);
    PT_ASSERT_EQ_INT(r.remainder, INT32_MAX);
    r = bios_div(INT32_MIN, INT32_MIN);
    PT_ASSERT_EQ_INT(r.quotient, 1);
    PT_ASSERT_EQ_INT(r.remainder, 0);
    r = bios_div(INT32_MIN, 3);
    PT_ASSERT_EQ_INT(r.quotient, -715827882);
    PT_ASSERT_EQ_INT(r.remainder, -2);
}

/* Undefined on hardware (an endless loop); defined here as all zero. Called
 * twice: the message is printed once, the result is the same. */
PTEST(div_by_zero_is_zero)
{
    for (int i = 0; i < 2; i++) {
        struct bios_div_result r = bios_div(1234, 0);
        PT_ASSERT_EQ_INT(r.quotient, 0);
        PT_ASSERT_EQ_INT(r.remainder, 0);
        PT_ASSERT_EQ_U32(r.abs_quotient, 0);
    }
    PT_ASSERT_EQ_INT(Div(-1, 0), 0);
    PT_ASSERT_EQ_INT(Mod(-1, 0), 0);
}

PTEST(div_mod_and_arm_variants)
{
    PT_ASSERT_EQ_INT(Div(-1234, 10), -123);
    PT_ASSERT_EQ_INT(Mod(-1234, 10), -4);
    PT_ASSERT_EQ_INT(DivArm(10, -1234), -123);
    PT_ASSERT_EQ_INT(ModArm(10, -1234), -4);
    PT_ASSERT_EQ_INT(Div(600, 7), 85); /* librfu_rfu.c: Div(600, mcTimer) */
}

PTEST(sqrt_small_values)
{
    PT_ASSERT_EQ_U32(Sqrt(0), 0);
    PT_ASSERT_EQ_U32(Sqrt(1), 1);
    PT_ASSERT_EQ_U32(Sqrt(2), 1);
    PT_ASSERT_EQ_U32(Sqrt(3), 1);
    PT_ASSERT_EQ_U32(Sqrt(4), 2);
    PT_ASSERT_EQ_U32(Sqrt(8), 2);
    PT_ASSERT_EQ_U32(Sqrt(9), 3);
}

PTEST(sqrt_largest_values)
{
    PT_ASSERT_EQ_U32(Sqrt(UINT32_C(0xFFFFFFFF)), 0xFFFF);
    PT_ASSERT_EQ_U32(Sqrt(UINT32_C(0xFFFE0001)), 0xFFFF); /* FFFFh squared */
    PT_ASSERT_EQ_U32(Sqrt(UINT32_C(0xFFFE0000)), 0xFFFE);
    PT_ASSERT_EQ_U32(Sqrt(UINT32_C(2) << 30), 46340); /* 1.41421 shl 15 = 46340.95 */
}

/* Every perfect square and the number below it. */
PTEST(sqrt_every_square_boundary)
{
    for (uint32_t k = 1; k <= 0xFFFF; k++) {
        uint32_t square = k * k;
        if (Sqrt(square) != k || Sqrt(square - 1) != k - 1) {
            PT_ASSERT_EQ_U32(Sqrt(square), k);
            PT_ASSERT_EQ_U32(Sqrt(square - 1), k - 1);
        }
    }
}

PTEST(arctan_exact_points)
{
    PT_ASSERT_EQ_INT(ArcTan(0), 0);
    PT_ASSERT_EQ_INT(ArcTan(0x4000), 0x2000); /* tan 1.0: 45 degrees */
    PT_ASSERT_EQ_INT(ArcTan(-0x4000), -0x2000);
}

/* Within the accuracy band for |theta| <= pi/4, both signs. */
PTEST(arctan_accuracy_band)
{
    static const struct {
        int16_t tan;
        int32_t angle;
    } points[] = {
        {0x0400, 651},  /* atan(0.0625)  = 651.05 */
        {0x1000, 2555}, /* atan(0.25)    = 2555.22 */
        {0x2000, 4836}, /* atan(0.5)     = 4836.02 */
        {0x3000, 6712}, /* atan(0.75)    = 6711.96 */
        {0x3800, 7498}, /* atan(0.875)   = 7497.67 */
        {0x3FFF, 8192}, /* atan(0.99994) = 8191.68 */
    };
    for (size_t i = 0; i < sizeof points / sizeof points[0]; i++) {
        PT_ASSERT(distance(ArcTan(points[i].tan), points[i].angle) <= ARCTAN_TOLERANCE);
        PT_ASSERT(distance(ArcTan((int16_t)-points[i].tan), -points[i].angle) <= ARCTAN_TOLERANCE);
    }
}

/* GBATEK: "there is a problem in accuracy with THETA<-PI/4, PI/4<THETA".
 * The BIOS polynomial diverges there, and past |tan| = 1.79 its 32-bit
 * products wrap; the values are the polynomial evaluated by hand in 32-bit
 * two's complement arithmetic. */
PTEST(arctan_inaccurate_beyond_45_degrees)
{
    PT_ASSERT(distance(ArcTan(0x6000), 10251) > 1000); /* atan(1.5) = 10250.91 */
    PT_ASSERT_EQ_INT(ArcTan(0x6000), 8988);
    PT_ASSERT_EQ_INT(ArcTan(0x5000), 9325); /* atan(1.25) = 9346.20 */
    PT_ASSERT_EQ_INT(ArcTan(0x7FFF), 5848); /* atan(2.0) = 11547.85 */
    PT_ASSERT_EQ_INT(ArcTan(-0x8000), -5795);
}

PTEST(arctan2_axes)
{
    PT_ASSERT_EQ_U32(ArcTan2(0x100, 0), 0x0000);
    PT_ASSERT_EQ_U32(ArcTan2(0, 0x100), 0x4000);
    PT_ASSERT_EQ_U32(ArcTan2(-0x100, 0), 0x8000);
    PT_ASSERT_EQ_U32(ArcTan2(0, -0x100), 0xC000);
    PT_ASSERT_EQ_U32(ArcTan2(0, 0), 0x0000);
    PT_ASSERT_EQ_U32(ArcTan2(INT16_MIN, 0), 0x8000);
    PT_ASSERT_EQ_U32(ArcTan2(0, INT16_MAX), 0x4000);
}

PTEST(arctan2_diagonals)
{
    PT_ASSERT_EQ_U32(ArcTan2(0x100, 0x100), 0x2000);
    PT_ASSERT_EQ_U32(ArcTan2(-0x100, 0x100), 0x6000);
    PT_ASSERT_EQ_U32(ArcTan2(-0x100, -0x100), 0xA000);
    PT_ASSERT_EQ_U32(ArcTan2(0x100, -0x100), 0xE000);
    PT_ASSERT_EQ_U32(ArcTan2(INT16_MAX, INT16_MAX), 0x2000);
    PT_ASSERT_EQ_U32(ArcTan2(INT16_MIN, INT16_MIN), 0xA000);
}

/* One point inside each octant, (2, 1) rotated and reflected. */
PTEST(arctan2_octants)
{
    static const struct {
        int16_t x, y;
        int32_t angle;
    } points[] = {
        {0x2000, 0x1000, 4836},        /* (2, 1):   4836.02 */
        {0x1000, 0x2000, 11548},       /* (1, 2):   11547.98 */
        {-0x1000, 0x2000, 21220},      /* (-1, 2):  21220.02 */
        {-0x2000, 0x1000, 27932},      /* (-2, 1):  27931.98 */
        {-0x2000, -0x1000, 37604},     /* (-2, -1): 37604.02 */
        {-0x1000, -0x2000, 44316},     /* (-1, -2): 44315.98 */
        {0x1000, -0x2000, 53988},      /* (1, -2):  53988.02 */
        {0x2000, -0x1000, 60700},      /* (2, -1):  60699.98 */
        {3, 1, 3356},                  /* (3, 1):   3355.98, small magnitudes */
        {INT16_MAX, INT16_MIN, 57344}, /* 57343.84 */
        {INT16_MIN, 1, 32768},         /* 32767.68 */
    };
    for (size_t i = 0; i < sizeof points / sizeof points[0]; i++) {
        uint16_t angle = ArcTan2(points[i].x, points[i].y);
        PT_ASSERT(angle_distance(angle, points[i].angle) <= ARCTAN_TOLERANCE);
    }
}

/* The octant is picked so ArcTan never sees |ratio| > 1: a steep vector is
 * as accurate as a shallow one. */
PTEST(arctan2_stays_accurate_near_the_y_axis)
{
    /* (1, 8): atan2 = 15086.93; ArcTan(8.0) alone would be far off. */
    PT_ASSERT(angle_distance(ArcTan2(0x400, 0x2000), 15087) <= ARCTAN_TOLERANCE);
    /* (-1, -8): 47854.93 */
    PT_ASSERT(angle_distance(ArcTan2(-0x400, -0x2000), 47855) <= ARCTAN_TOLERANCE);
}

PTEST_MAIN()
