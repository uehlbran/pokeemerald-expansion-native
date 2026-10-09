/*
 * bios_affine.c: BgAffineSet (SWI 0Eh) and ObjAffineSet (SWI 0Fh).
 *
 * Stands in for the BIOS routines that turn a scale and an angle into the
 * rotation/scaling matrix the LCD's BG2/BG3 and OBJ affine parameters take
 * (GBATEK "BIOS Rotation/Scaling Functions",
 * https://problemkaputt.de/gbatek.htm#biosrotationscalingfunctions, for the
 * struct layouts, the 8-bit angle and the Offset stride; tonc "BIOS calls",
 * https://www.coranac.com/tonc/text/swi.htm, eq. 17.1, for the matrix
 *   pa = sx*cos(a)   pb = -sx*sin(a)
 *   pc = sy*sin(a)   pd = sy*cos(a)
 * and for ObjAffineSet's word-aligned, 8-byte source entries).
 *
 * The BIOS has no floating point; it looks sin and cos up in a 256-entry
 * table indexed by the angle's upper byte (GBATEK: "the GBA BIOS recurses
 * only the upper 8bit"). GBATEK gives neither the table nor its precision.
 * This file embeds the first quarter, 65 entries of
 * round(sin(2*pi*k/256) * 4000h) in 1.14 fixed point (computed offline; no
 * floating point at run time), and derives the other three quarters by
 * symmetry. 1.14 matches the 14 fractional bits of the BIOS's ArcTan format;
 * an entry of the real ROM table may differ in its last bit, which would
 * move a matrix element by at most one 1/256 at angles other than multiples
 * of 90 degrees (those are exact here and on hardware: 0 and 4000h).
 * Each element is the 32-bit product shifted right 14 with ASR (rounding
 * toward minus infinity), the natural ARM sequence; a ROM that negated after
 * shifting would give pb one unit higher at some angles.
 *
 * Structs are read and written at GBATEK's byte offsets with memcpy, so any
 * alignment works (bios.h, alignment) and expansion's struct definitions are
 * not needed here; test_bios.c asserts they have these offsets.
 */
#include <inttypes.h>
#include <stdio.h>

#include "bios.h"
#include "bios_internal.h"

/* GBATEK BgAffineSet source entry: s32 center X, s32 center Y (8 fractional
 * bits), s16 display X, s16 display Y, s16 scale X, s16 scale Y (8
 * fractional bits), u16 angle; entries are word-aligned, 20 bytes apart. */
#define BG_SRC_TEX_X 0u
#define BG_SRC_TEX_Y 4u
#define BG_SRC_SCR_X 8u
#define BG_SRC_SCR_Y 10u
#define BG_SRC_SX 12u
#define BG_SRC_SY 14u
#define BG_SRC_ALPHA 16u
#define BG_SRC_FIELDS_SIZE 18u
#define BG_SRC_STRIDE 20u

/* GBATEK BgAffineSet destination entry: s16 pa, pb, pc, pd, s32 start X,
 * start Y; 16 bytes. The same layout as BG2PA..BG2Y (GBATEK "GBA I/O Map",
 * 4000020h-400002Fh). */
#define BG_DST_PA 0u
#define BG_DST_PB 2u
#define BG_DST_PC 4u
#define BG_DST_PD 6u
#define BG_DST_X 8u
#define BG_DST_Y 12u
#define BG_DST_STRIDE 16u

/* GBATEK ObjAffineSet source entry: s16 scale X, s16 scale Y, u16 angle;
 * entries 8 bytes apart (tonc: the BIOS "expects its source structs to be
 * word-aligned"). Four parameters are written, Offset bytes apart. */
#define OBJ_SRC_SX 0u
#define OBJ_SRC_SY 2u
#define OBJ_SRC_ALPHA 4u
#define OBJ_SRC_FIELDS_SIZE 6u
#define OBJ_SRC_STRIDE 8u
#define OBJ_PARAMETERS 4u
#define PARAMETER_SIZE 2u

/* The angle: 10000h per turn, of which the BIOS uses the upper 8 bits. */
#define ANGLE_INDEX_SHIFT 8u
#define SINE_INDEX_MASK 0xFFu
#define SINE_QUARTER 64u /* table entries per 90 degrees */
#define SINE_QUADRANT_SHIFT 6u
#define SINE_FRACTION_BITS 14u

/* round(sin(2*pi*k/256) * 4000h), k = 0..64: sin from 0 to 90 degrees. */
static const int16_t sine_quarter[SINE_QUARTER + 1] = {
    0x0000, 0x0192, 0x0324, 0x04B5, 0x0646, 0x07D6, 0x0964, 0x0AF1, 0x0C7C, 0x0E06, 0x0F8D, 0x1112, 0x1294,
    0x1413, 0x1590, 0x1709, 0x187E, 0x19EF, 0x1B5D, 0x1CC6, 0x1E2B, 0x1F8C, 0x20E7, 0x223D, 0x238E, 0x24DA,
    0x2620, 0x2760, 0x289A, 0x29CE, 0x2AFB, 0x2C21, 0x2D41, 0x2E5A, 0x2F6C, 0x3076, 0x3179, 0x3274, 0x3368,
    0x3453, 0x3537, 0x3612, 0x36E5, 0x37B0, 0x3871, 0x392B, 0x39DB, 0x3A82, 0x3B21, 0x3BB6, 0x3C42, 0x3CC5,
    0x3D3F, 0x3DAF, 0x3E15, 0x3E72, 0x3EC5, 0x3F0F, 0x3F4F, 0x3F85, 0x3FB1, 0x3FD4, 0x3FEC, 0x3FFB, 0x4000,
};

struct affine_matrix {
    int32_t pa, pb, pc, pd;
};

/* sin(2*pi*index/256) in 1.14, for any index (taken modulo 256). */
static int32_t sine(unsigned index)
{
    unsigned within = index & (SINE_QUARTER - 1);
    switch ((index & SINE_INDEX_MASK) >> SINE_QUADRANT_SHIFT) {
    case 0:
        return sine_quarter[within];
    case 1:
        return sine_quarter[SINE_QUARTER - within];
    case 2:
        return -sine_quarter[within];
    default:
        return -sine_quarter[SINE_QUARTER - within];
    }
}

/* One table product: 8.8 scale times 1.14 sine, back to 8.8. |scale| <=
 * 8000h and |sine| <= 4000h, so the product fits 30 bits. */
static int32_t scaled(int32_t scale, int32_t sine_value)
{
    return bios_asr(scale * sine_value, SINE_FRACTION_BITS);
}

/* The matrix as stored: each element cut to the 16 bits it is written as. */
static struct affine_matrix rotation_scaling(int16_t sx, int16_t sy, uint16_t alpha)
{
    unsigned index = (unsigned)alpha >> ANGLE_INDEX_SHIFT;
    int32_t sin_a = sine(index);
    int32_t cos_a = sine(index + SINE_QUARTER);
    struct affine_matrix m = {
        bios_sign_extend16(scaled(sx, cos_a)),
        bios_sign_extend16(scaled(-(int32_t)sx, sin_a)),
        bios_sign_extend16(scaled(sy, sin_a)),
        bios_sign_extend16(scaled(sy, cos_a)),
    };
    return m;
}

/* texture - (p * scrX + q * scrY), in wrapping 32-bit arithmetic. */
static uint32_t start_point(int32_t texture, int32_t p, int32_t q, int32_t scr_x, int32_t scr_y)
{
    uint32_t offset = (uint32_t)bios_mul(p, scr_x) + (uint32_t)bios_mul(q, scr_y);
    return (uint32_t)texture - offset;
}

void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, int32_t count)
{
    size_t src_bytes = 0;
    size_t dest_bytes = 0;
    if (count <= 0) {
        return;
    }
    if (!bios_span((size_t)count, BG_SRC_STRIDE, BG_SRC_FIELDS_SIZE, &src_bytes)
        || !bios_span((size_t)count, BG_DST_STRIDE, BG_DST_STRIDE, &dest_bytes)
        || !bios_range_ok("BgAffineSet", "source", src, src_bytes, TRANSFER_READ)
        || !bios_range_ok("BgAffineSet", "destination", dest, dest_bytes, TRANSFER_WRITE)) {
        return;
    }
    for (size_t i = 0; i < (size_t)count; i++) {
        const uint8_t *in = (const uint8_t *)src + i * BG_SRC_STRIDE;
        uint8_t *out = (uint8_t *)dest + i * BG_DST_STRIDE;
        int32_t scr_x = bios_load_s16(in + BG_SRC_SCR_X);
        int32_t scr_y = bios_load_s16(in + BG_SRC_SCR_Y);
        struct affine_matrix m = rotation_scaling(bios_load_s16(in + BG_SRC_SX), bios_load_s16(in + BG_SRC_SY),
                                                  bios_load16(in + BG_SRC_ALPHA));
        bios_store16(out + BG_DST_PA, bios_low16(m.pa));
        bios_store16(out + BG_DST_PB, bios_low16(m.pb));
        bios_store16(out + BG_DST_PC, bios_low16(m.pc));
        bios_store16(out + BG_DST_PD, bios_low16(m.pd));
        bios_store32(out + BG_DST_X, start_point(bios_load_s32(in + BG_SRC_TEX_X), m.pa, m.pb, scr_x, scr_y));
        bios_store32(out + BG_DST_Y, start_point(bios_load_s32(in + BG_SRC_TEX_Y), m.pc, m.pd, scr_x, scr_y));
    }
}

void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, int32_t count, int32_t offset)
{
    size_t src_bytes = 0;
    size_t dest_bytes = 0;
    if (count <= 0) {
        return;
    }
    if (offset < 0) {
        (void)fprintf(stderr, "bios: ObjAffineSet: negative offset %" PRId32 " refused\n", offset);
        return;
    }
    size_t stride = (size_t)offset;
    if (!bios_span((size_t)count, OBJ_SRC_STRIDE, OBJ_SRC_FIELDS_SIZE, &src_bytes)
        || (size_t)count > SIZE_MAX / OBJ_PARAMETERS
        || !bios_span((size_t)count * OBJ_PARAMETERS, stride, PARAMETER_SIZE, &dest_bytes)
        || !bios_range_ok("ObjAffineSet", "source", src, src_bytes, TRANSFER_READ)
        || !bios_range_ok("ObjAffineSet", "destination", dest, dest_bytes, TRANSFER_WRITE)) {
        return;
    }
    for (size_t i = 0; i < (size_t)count; i++) {
        const uint8_t *in = (const uint8_t *)src + i * OBJ_SRC_STRIDE;
        struct affine_matrix m = rotation_scaling(bios_load_s16(in + OBJ_SRC_SX), bios_load_s16(in + OBJ_SRC_SY),
                                                  bios_load16(in + OBJ_SRC_ALPHA));
        const int32_t parameters[OBJ_PARAMETERS] = {m.pa, m.pb, m.pc, m.pd};
        for (size_t p = 0; p < OBJ_PARAMETERS; p++) {
            bios_store16((uint8_t *)dest + (i * OBJ_PARAMETERS + p) * stride, bios_low16(parameters[p]));
        }
    }
}
