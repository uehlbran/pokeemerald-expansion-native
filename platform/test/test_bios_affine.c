// deps: bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of BgAffineSet (SWI 0Eh) and ObjAffineSet (SWI 0Fh), from GBATEK
 * "BIOS Rotation/Scaling Functions" (layouts, "the GBA BIOS recurses only the
 * upper 8bit" of the angle, the Offset stride) and tonc "BIOS calls" eq.
 * 17.1 (pa = sx cos a, pb = -sx sin a, pc = sy sin a, pd = sy cos a). Values
 * at 0, 90, 180 and 270 degrees are exact whatever the table's rounding.
 * Elsewhere: the table is round(sin(2 pi k / 256) * 4000h) and each element is
 * the product shifted right 14, rounding toward minus infinity (bios.h).
 */
#include <gba/types.h>

#include <stdint.h>
#include <string.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define ANGLE_90 0x4000
#define ANGLE_180 0x8000
#define ANGLE_270 0xC000
#define ONE_8_8 0x100
/* A scale of 4000h (64.0 in 8.8) makes pa..pd the table's own 1.14 values:
 * (4000h * v) >> 14 = v. */
#define TABLE_SCALE 0x4000
#define TABLE_ENTRIES 256

static void setup(void)
{
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

static struct BgAffineDstData bg(s32 texX, s32 texY, s16 scrX, s16 scrY, s16 sx, s16 sy, u16 alpha)
{
    struct BgAffineSrcData src = {texX, texY, scrX, scrY, sx, sy, alpha};
    struct BgAffineDstData dst;
    memset(&dst, 0x5A, sizeof dst);
    BgAffineSet(&src, &dst, 1);
    return dst;
}

/* sin and cos of table index k (alpha = k << 8), read back through
 * ObjAffineSet. */
static void table_entry(unsigned k, int32_t *sine, int32_t *cosine)
{
    struct ObjAffineSrcData src = {TABLE_SCALE, TABLE_SCALE, (u16)(k << 8)};
    s16 m[4] = {0};
    ObjAffineSet(&src, m, 1, 2);
    *cosine = m[0];
    *sine = m[2];
}

PTEST(bg_identity)
{
    setup();
    struct BgAffineDstData d = bg(0x8000, 0x8000, 120, 80, ONE_8_8, ONE_8_8, 0);
    PT_ASSERT_EQ_INT(d.pa, 0x100);
    PT_ASSERT_EQ_INT(d.pb, 0);
    PT_ASSERT_EQ_INT(d.pc, 0);
    PT_ASSERT_EQ_INT(d.pd, 0x100);
    PT_ASSERT_EQ_INT(d.dx, 0x8000 - 0x100 * 120); /* 800h */
    PT_ASSERT_EQ_INT(d.dy, 0x8000 - 0x100 * 80);  /* 3000h */
    teardown();
}

PTEST(bg_scale_two)
{
    setup();
    struct BgAffineDstData d = bg(0x8000, 0x8000, 120, 80, 0x200, 0x200, 0);
    PT_ASSERT_EQ_INT(d.pa, 0x200);
    PT_ASSERT_EQ_INT(d.pb, 0);
    PT_ASSERT_EQ_INT(d.pc, 0);
    PT_ASSERT_EQ_INT(d.pd, 0x200);
    PT_ASSERT_EQ_INT(d.dx, -28672); /* 8000h - 200h * 120 */
    PT_ASSERT_EQ_INT(d.dy, -8192);  /* 8000h - 200h * 80 */
    teardown();
}

PTEST(bg_rotation_90)
{
    setup();
    /* sin 90 = 1, cos 90 = 0: pa 0, pb -sx, pc sy, pd 0. */
    struct BgAffineDstData d = bg(0x8000, 0x8000, 120, 80, ONE_8_8, ONE_8_8, ANGLE_90);
    PT_ASSERT_EQ_INT(d.pa, 0);
    PT_ASSERT_EQ_INT(d.pb, -0x100);
    PT_ASSERT_EQ_INT(d.pc, 0x100);
    PT_ASSERT_EQ_INT(d.pd, 0);
    PT_ASSERT_EQ_INT(d.dx, 0x8000 + 0x100 * 80);  /* texX - (pb * scrY) = 53248 */
    PT_ASSERT_EQ_INT(d.dy, 0x8000 - 0x100 * 120); /* texY - (pc * scrX) = 2048 */
    teardown();
}

PTEST(bg_rotation_180_and_270)
{
    setup();
    struct BgAffineDstData d = bg(0x8000, 0x8000, 120, 80, ONE_8_8, ONE_8_8, ANGLE_180);
    PT_ASSERT_EQ_INT(d.pa, -0x100);
    PT_ASSERT_EQ_INT(d.pb, 0);
    PT_ASSERT_EQ_INT(d.pc, 0);
    PT_ASSERT_EQ_INT(d.pd, -0x100);
    PT_ASSERT_EQ_INT(d.dx, 0x8000 + 0x100 * 120);
    PT_ASSERT_EQ_INT(d.dy, 0x8000 + 0x100 * 80);
    d = bg(0, 0, 0, 0, ONE_8_8, 0x200, ANGLE_270);
    PT_ASSERT_EQ_INT(d.pa, 0);
    PT_ASSERT_EQ_INT(d.pb, 0x100);  /* -sx * sin 270 = -1 * -1 */
    PT_ASSERT_EQ_INT(d.pc, -0x200); /* sy * sin 270 */
    PT_ASSERT_EQ_INT(d.pd, 0);
    teardown();
}

/* GBATEK: only the upper 8 bits of the angle are used. */
PTEST(bg_angle_low_byte_ignored)
{
    setup();
    struct BgAffineDstData a = bg(0x1234, 0x5678, 7, 9, 0x180, 0xC0, 0x2A00);
    struct BgAffineDstData b = bg(0x1234, 0x5678, 7, 9, 0x180, 0xC0, 0x2AFF);
    PT_ASSERT_MEM_EQ(&a, &b, sizeof a);
    teardown();
}

/* 45 degrees: sin = cos = 2D41h (11585.24 rounded). (100h * 2D41h) >> 14 =
 * 181.02 -> 181; the negated product -181.02 rounds down to -182. */
PTEST(bg_rotation_45_rounding)
{
    setup();
    struct BgAffineDstData d = bg(0, 0, 0, 0, ONE_8_8, ONE_8_8, 0x2000);
    PT_ASSERT_EQ_INT(d.pa, 181);
    PT_ASSERT_EQ_INT(d.pb, -182);
    PT_ASSERT_EQ_INT(d.pc, 181);
    PT_ASSERT_EQ_INT(d.pd, 181);
    teardown();
}

/* The start point is computed from the stored elements, so the screen centre
 * lands exactly on the texture centre at any angle and scale. */
PTEST(bg_screen_centre_maps_to_texture_centre)
{
    setup();
    struct BgAffineDstData d = bg(0x12345, -0x6789, -37, 91, 0x180, -0xC0, 0x1234);
    PT_ASSERT_EQ_INT(d.dx + d.pa * -37 + d.pb * 91, 0x12345);
    PT_ASSERT_EQ_INT(d.dy + d.pc * -37 + d.pd * 91, -0x6789);
    teardown();
}

/* The 32-bit start point wraps as the hardware's registers do. */
PTEST(bg_start_point_wraps)
{
    setup();
    struct BgAffineDstData d = bg(INT32_MAX, INT32_MIN, -1, 1, ONE_8_8, ONE_8_8, 0);
    /* INT32_MAX - (100h * -1) and INT32_MIN - (100h * 1), modulo 2^32. */
    PT_ASSERT_EQ_U32((uint32_t)d.dx, UINT32_C(0x800000FF));
    PT_ASSERT_EQ_U32((uint32_t)d.dy, UINT32_C(0x7FFFFF00));
    teardown();
}

PTEST(bg_count_two_and_zero)
{
    setup();
    struct BgAffineSrcData src[2] = {
        {0x8000, 0x8000, 120, 80, ONE_8_8, ONE_8_8, 0},
        {0, 0, 0, 0, 0x200, 0x300, ANGLE_90},
    };
    struct BgAffineDstData dst[3];
    memset(dst, 0x5A, sizeof dst);
    BgAffineSet(src, dst, 2);
    PT_ASSERT_EQ_INT(dst[0].pa, 0x100);
    PT_ASSERT_EQ_INT(dst[0].dx, 0x800);
    PT_ASSERT_EQ_INT(dst[1].pb, -0x200);
    PT_ASSERT_EQ_INT(dst[1].pc, 0x300);
    PT_ASSERT_EQ_INT(dst[2].pa, 0x5A5A); /* untouched */

    memset(dst, 0x5A, sizeof dst);
    BgAffineSet(src, dst, 0);
    BgAffineSet(src, dst, -1);
    PT_ASSERT_EQ_INT(dst[0].pa, 0x5A5A);
    teardown();
}

PTEST(bg_destination_must_be_writable)
{
    setup();
    static const struct BgAffineDstData rodata = {1, 2, 3, 4, 5, 6};
    struct BgAffineSrcData src = {0, 0, 0, 0, ONE_8_8, ONE_8_8, 0};
    /* The game can name any address; here, read-only data. */
    BgAffineSet(&src, (struct BgAffineDstData *)(uintptr_t)&rodata, 1);
    PT_ASSERT_EQ_INT(rodata.pa, 1);
    teardown();
}

PTEST(obj_contiguous_offset_2)
{
    setup();
    struct ObjAffineSrcData src = {0x100, 0x200, ANGLE_90};
    s16 m[6];
    memset(m, 0x5A, sizeof m);
    ObjAffineSet(&src, m, 1, 2);
    PT_ASSERT_EQ_INT(m[0], 0);
    PT_ASSERT_EQ_INT(m[1], -0x100);
    PT_ASSERT_EQ_INT(m[2], 0x200);
    PT_ASSERT_EQ_INT(m[3], 0);
    PT_ASSERT_EQ_INT(m[4], 0x5A5A);
    teardown();
}

/* Offset 8 matches OAM: pa, pb, pc, pd are the affineParam halfwords of four
 * consecutive OAM entries; the next matrix starts 4 * 8 bytes on. */
PTEST(obj_oam_stride_offset_8_count_2)
{
    setup();
    /* Two source entries 8 bytes apart (the BIOS's word-aligned stride). */
    u16 src[8] = {0x100, 0x100, 0, 0, 0x200, 0x300, ANGLE_180, 0};
    s16 oam[36];
    memset(oam, 0x5A, sizeof oam);
    ObjAffineSet((struct ObjAffineSrcData *)(void *)src, oam, 2, 8);
    PT_ASSERT_EQ_INT(oam[0], 0x100);   /* pa */
    PT_ASSERT_EQ_INT(oam[4], 0);       /* pb */
    PT_ASSERT_EQ_INT(oam[8], 0);       /* pc */
    PT_ASSERT_EQ_INT(oam[12], 0x100);  /* pd */
    PT_ASSERT_EQ_INT(oam[16], -0x200); /* second matrix: pa = 200h * cos 180 */
    PT_ASSERT_EQ_INT(oam[28], -0x300); /* pd */
    PT_ASSERT_EQ_INT(oam[1], 0x5A5A);  /* the other halfwords are untouched */
    PT_ASSERT_EQ_INT(oam[3], 0x5A5A);
    PT_ASSERT_EQ_INT(oam[29], 0x5A5A);
    PT_ASSERT_EQ_INT(oam[32], 0x5A5A);
    teardown();
}

PTEST(obj_offset_zero_overwrites_one_halfword)
{
    setup();
    struct ObjAffineSrcData src = {0x100, 0x200, 0};
    s16 m[2] = {0x5A5A, 0x5A5A};
    ObjAffineSet(&src, m, 1, 0);
    PT_ASSERT_EQ_INT(m[0], 0x200); /* pd, written last */
    PT_ASSERT_EQ_INT(m[1], 0x5A5A);
    teardown();
}

PTEST(obj_negative_offset_and_count_do_nothing)
{
    setup();
    struct ObjAffineSrcData src = {0x100, 0x100, 0};
    s16 m[8];
    memset(m, 0x5A, sizeof m);
    ObjAffineSet(&src, &m[4], 1, -2);
    ObjAffineSet(&src, m, 0, 2);
    ObjAffineSet(&src, m, -3, 2);
    for (size_t i = 0; i < 8; i++)
        PT_ASSERT_EQ_INT(m[i], 0x5A5A);
    teardown();
}

/* The odd destination address a native struct may have. */
PTEST(obj_unaligned_destination)
{
    setup();
    struct ObjAffineSrcData src = {0x100, 0x100, 0};
    u8 bytes[10];
    memset(bytes, 0x5A, sizeof bytes);
    ObjAffineSet(&src, bytes + 1, 1, 2);
    s16 pa;
    memcpy(&pa, bytes + 1, sizeof pa);
    PT_ASSERT_EQ_INT(pa, 0x100);
    PT_ASSERT_EQ_INT(bytes[0], 0x5A);
    PT_ASSERT_EQ_INT(bytes[9], 0x5A);
    teardown();
}

/* The sine table: hand-computed entries (sin(2 pi k / 256) * 16384). */
PTEST(sine_table_known_entries)
{
    setup();
    static const struct {
        unsigned k;
        int32_t sine;
    } known[] = {
        {0, 0},       {1, 0x0192}, /* 402.08 */
        {8, 0x0C7C},               /* sin(pi/16)  = 0.195090: 3196.36 */
        {16, 0x187E},              /* sin(pi/8)   = 0.382683: 6269.89 */
        {32, 0x2D41},              /* sin(pi/4)   = 0.707107: 11585.24 */
        {48, 0x3B21},              /* sin(3pi/8)  = 0.923880: 15136.84 */
        {64, 0x4000}, {128, 0},    {192, -0x4000},
    };
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++) {
        int32_t s, c;
        table_entry(known[i].k, &s, &c);
        PT_ASSERT_EQ_INT(s, known[i].sine);
    }
    teardown();
}

/* Symmetry, monotonicity and sin^2 + cos^2 = 1 within the rounding: catches a
 * wrong entry anywhere in the table. */
PTEST(sine_table_identities)
{
    setup();
    int32_t sine[TABLE_ENTRIES], cosine[TABLE_ENTRIES];
    for (unsigned k = 0; k < TABLE_ENTRIES; k++)
        table_entry(k, &sine[k], &cosine[k]);
    for (unsigned k = 0; k < TABLE_ENTRIES; k++) {
        PT_ASSERT_EQ_INT(cosine[k], sine[(k + 64) % TABLE_ENTRIES]);
        PT_ASSERT_EQ_INT(sine[(128 - k) % TABLE_ENTRIES], sine[k]);
        PT_ASSERT_EQ_INT(sine[(256 - k) % TABLE_ENTRIES], -sine[k]);
        int32_t norm = sine[k] * sine[k] + cosine[k] * cosine[k];
        /* each entry is within 0.5 of exact: |error| < 2 * 4000h * (0.5 + 0.5) */
        PT_ASSERT(norm - (1 << 28) < (1 << 15) && (1 << 28) - norm < (1 << 15));
    }
    for (unsigned k = 0; k < 64; k++)
        PT_ASSERT(sine[k + 1] > sine[k]);
    teardown();
}

PTEST_MAIN()
