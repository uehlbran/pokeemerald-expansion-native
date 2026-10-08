// deps: bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of BitUnPack (SWI 10h) and the difference unfilters (SWI 16h-18h),
 * from GBATEK "BIOS Decompression Functions": BitUnPack's unpack information
 * (source length in bytes, source width 1/2/4/8, destination width
 * 1/2/4/8/16/32, data offset in bits 0-30 and the zero-data flag in bit 31,
 * "Data is written in 32bit units"), and the Diff header (unit size in bits
 * 0-3, type 8 in bits 4-7, size in bits 8-31) with GBATEK's example "10 +1
 * +1 ..." unfiltering to "10 11 12 ...". Expected words are worked by hand:
 * units are taken from each source byte's low bits first and packed into
 * each word from its low bits up.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define STALE 0xEE
#define STALE_32 UINT32_C(0xEEEEEEEE)
#define ZERO_DATA_FLAG UINT32_C(0x80000000)

/* GBATEK's unpack information, 8 bytes; expansion's src/assertf.c declares
 * the same layout with bitfields (test_bios.c checks it). */
struct BitUnPackArgs {
    uint16_t length;
    uint8_t source_width;
    uint8_t destination_width;
    uint32_t offset_and_flag;
};

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

static uint32_t unpack_one(uint8_t byte, uint8_t from, uint8_t to, uint32_t offset_and_flag)
{
    const struct BitUnPackArgs args = { 1, from, to, offset_and_flag };
    uint32_t out[2] = { STALE_32, STALE_32 };
    BitUnPack(&byte, out, &args);
    return out[0];
}

/* B1h = bits 1,0,0,0,1,1,0,1 from bit 0: nibbles 1,0,0,0,1,1,0,1. */
PTEST(bitunpack_1_to_4)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xB1, 1, 4, 0), UINT32_C(0x10110001));
    teardown();
}

/* The offset is added to the nonzero units only... */
PTEST(bitunpack_1_to_4_offset_without_zero_flag)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xB1, 1, 4, 2), UINT32_C(0x30330003));
    teardown();
}

/* ...and to the zero units too when the flag is set. */
PTEST(bitunpack_1_to_4_offset_with_zero_flag)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xB1, 1, 4, ZERO_DATA_FLAG | 2), UINT32_C(0x32332223));
    PT_ASSERT_EQ_U32(unpack_one(0x00, 1, 4, ZERO_DATA_FLAG), UINT32_C(0x00000000));
    teardown();
}

/* E4h = 11 10 01 00: units 0, 1, 2, 3 from the low bits. */
PTEST(bitunpack_2_to_8)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xE4, 2, 8, 0), UINT32_C(0x03020100));
    teardown();
}

/* 21h = units 1, 2: halfwords 1, 2, each plus the offset. */
PTEST(bitunpack_4_to_16)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0x21, 4, 16, 0x7000), UINT32_C(0x70027001));
    teardown();
}

PTEST(bitunpack_8_to_32)
{
    setup();
    const uint8_t src[2] = { 0, 5 };
    const struct BitUnPackArgs plain = { 2, 8, 32, 0x100 };
    const struct BitUnPackArgs zeros = { 2, 8, 32, ZERO_DATA_FLAG | 0x100 };
    uint32_t out[3] = { STALE_32, STALE_32, STALE_32 };
    BitUnPack(src, out, &plain);
    PT_ASSERT_EQ_U32(out[0], 0);
    PT_ASSERT_EQ_U32(out[1], 0x105);
    PT_ASSERT_EQ_U32(out[2], STALE_32);
    BitUnPack(src, out, &zeros);
    PT_ASSERT_EQ_U32(out[0], 0x100);
    PT_ASSERT_EQ_U32(out[1], 0x105);
    teardown();
}

/* A unit plus the offset is cut to the destination width: 1 + Fh = 10h -> 0. */
PTEST(bitunpack_sum_cut_to_destination_width)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0x03, 1, 4, 0xF), UINT32_C(0x00000000));
    PT_ASSERT_EQ_U32(unpack_one(0x03, 1, 4, 0xE), UINT32_C(0x000000FF));
    teardown();
}

/* Only whole words are written: 1 byte of 1-bit units to 2 bits is 16 bits,
 * no word; 3 bytes are 48 bits, one word. */
PTEST(bitunpack_partial_word_dropped)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 1, 2, 0), STALE_32);
    const uint8_t src[3] = { 0xFF, 0x00, 0xFF };
    const struct BitUnPackArgs args = { 3, 1, 2, 0 };
    uint32_t out[2] = { STALE_32, STALE_32 };
    BitUnPack(src, out, &args);
    PT_ASSERT_EQ_U32(out[0], UINT32_C(0x00005555));
    PT_ASSERT_EQ_U32(out[1], STALE_32);
    teardown();
}

PTEST(bitunpack_unsupported_widths_and_zero_length_write_nothing)
{
    setup();
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 3, 8, 0), STALE_32);
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 16, 32, 0), STALE_32);
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 0, 8, 0), STALE_32);
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 1, 64, 0), STALE_32);
    PT_ASSERT_EQ_U32(unpack_one(0xFF, 1, 0, 0), STALE_32);
    const struct BitUnPackArgs empty = { 0, 1, 4, 0 };
    uint32_t out = STALE_32;
    BitUnPack(NULL, &out, &empty);
    PT_ASSERT_EQ_U32(out, STALE_32);
    teardown();
}

PTEST(bitunpack_unaligned_destination)
{
    setup();
    const uint8_t src = 0xB1;
    const struct BitUnPackArgs args = { 1, 1, 4, 0 };
    uint8_t out[6];
    memset(out, STALE, sizeof out);
    BitUnPack(&src, out + 1, &args);
    const uint8_t expected[4] = { 0x01, 0x00, 0x11, 0x10 };
    PT_ASSERT_MEM_EQ(out + 1, expected, sizeof expected);
    PT_ASSERT_EQ_U32(out[0], STALE);
    PT_ASSERT_EQ_U32(out[5], STALE);
    teardown();
}

PTEST(bitunpack_ranges_refused)
{
    setup();
    const uint8_t src[2] = { 0xFF, 0xFF };
    const struct BitUnPackArgs args = { 2, 1, 4, 0 }; /* 8 bytes out */
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 4);
    memset(tail, STALE, 4);
    BitUnPack(src, tail, &args);
    PT_ASSERT_EQ_U32(tail[0], STALE);

    uint8_t *heap = malloc(2);
    PT_ASSERT(heap != NULL);
    if (heap == NULL)
        return;
    heap[0] = heap[1] = 0xFF;
    uint32_t out[2] = { STALE_32, STALE_32 };
    BitUnPack(heap, out, &args);
    free(heap);
    PT_ASSERT_EQ_U32(out[0], STALE_32);
    teardown();
}

/* ---- Difference unfilters ------------------------------------------------- */

/* GBATEK's example: 10 +1 +1 ... -> 10 11 ... 19. */
static const _Alignas(4) uint8_t diff8_example[] = {
    0x81, 0x0A, 0x00, 0x00, /* 8-bit units, type 8, size 10 */
    10, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0x00, 0x00,
};

PTEST(diff8_gbatek_example_both_variants)
{
    setup();
    const uint8_t expected[10] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19 };
    uint8_t wram[12], vram[12];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    Diff8bitUnFilterWram(diff8_example, wram);
    Diff8bitUnFilterVram(diff8_example, vram);
    PT_ASSERT_MEM_EQ(wram, expected, sizeof expected);
    PT_ASSERT_MEM_EQ(vram, expected, sizeof expected);
    PT_ASSERT_EQ_U32(wram[10], STALE);
    PT_ASSERT_EQ_U32(vram[10], STALE);
    teardown();
}

/* Sums wrap at 8 bits; the Vram variant holds an odd last byte. */
PTEST(diff8_wraps_and_vram_odd_size)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = { 0x81, 0x03, 0x00, 0x00, 0xFF, 0x02, 0xFF, 0x00 };
    uint8_t wram[4], vram[4];
    memset(wram, STALE, sizeof wram);
    memset(vram, STALE, sizeof vram);
    Diff8bitUnFilterWram(stream, wram);
    Diff8bitUnFilterVram(stream, vram);
    const uint8_t expected[3] = { 0xFF, 0x01, 0x00 };
    PT_ASSERT_MEM_EQ(wram, expected, sizeof expected);
    PT_ASSERT_MEM_EQ(vram, expected, 2);
    PT_ASSERT_EQ_U32(vram[2], STALE);
    teardown();
}

/* 16-bit units, little-endian: 1000, +5, -3 -> 1000, 1005, 1002; FFFFh + 2
 * wraps to 1. */
PTEST(diff16_units_and_wrap)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = {
        0x82, 0x0A, 0x00, 0x00, /* 16-bit units, type 8, size 10 */
        0xE8, 0x03, 0x05, 0x00, 0xFD, 0xFF, 0x15, 0xFC, 0x02, 0x00, 0x00, 0x00,
    };
    uint16_t out[6];
    memset(out, STALE, sizeof out);
    Diff16bitUnFilter(stream, out);
    PT_ASSERT_EQ_U32(out[0], 1000);
    PT_ASSERT_EQ_U32(out[1], 1005);
    PT_ASSERT_EQ_U32(out[2], 1002);
    PT_ASSERT_EQ_U32(out[3], 0xFFFF); /* 1002 + FC15h */
    PT_ASSERT_EQ_U32(out[4], 1);
    PT_ASSERT_EQ_U32(out[5], 0xEEEE);
    teardown();
}

PTEST(diff16_odd_size_leaves_last_byte)
{
    setup();
    static const _Alignas(4) uint8_t stream[] = { 0x82, 0x03, 0x00, 0x00, 0x34, 0x12, 0x56, 0x00 };
    uint8_t out[4];
    memset(out, STALE, sizeof out);
    Diff16bitUnFilter(stream, out);
    PT_ASSERT_EQ_U32(out[0], 0x34);
    PT_ASSERT_EQ_U32(out[1], 0x12);
    PT_ASSERT_EQ_U32(out[2], STALE);
    teardown();
}

PTEST(diff_headers_must_match)
{
    setup();
    uint8_t out[12];
    memset(out, STALE, sizeof out);
    static const _Alignas(4) uint8_t sixteen[] = { 0x82, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00 };
    static const _Alignas(4) uint8_t lz77[] = { 0x10, 0x02, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00 };
    Diff8bitUnFilterWram(sixteen, out);        /* unit size 2 is not 8-bit */
    Diff8bitUnFilterVram(lz77, out);           /* type 1 is not a diff filter */
    Diff16bitUnFilter(diff8_example, out);     /* unit size 1 is not 16-bit */
    for (size_t i = 0; i < sizeof out; i++)
        PT_ASSERT_EQ_U32(out[i], STALE);
    teardown();
}

PTEST(diff_ranges_refused)
{
    setup();
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 8);
    memset(tail, STALE, 8);
    Diff8bitUnFilterWram(diff8_example, tail); /* 10 bytes into 8 */
    PT_ASSERT_EQ_U32(tail[0], STALE);

    /* A header at the end of IWRAM whose units would run past it. */
    uint8_t *iwram_tail = (uint8_t *)(uintptr_t)(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - 8);
    memcpy(iwram_tail, diff8_example, 8);
    uint8_t out[12];
    memset(out, STALE, sizeof out);
    Diff8bitUnFilterWram(iwram_tail, out);
    memset(iwram_tail, 0, 8);
    PT_ASSERT_EQ_U32(out[0], STALE);
    teardown();
}

PTEST_MAIN()
