// deps: bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of CpuSet (SWI 0Bh) and CpuFastSet (SWI 0Ch), from GBATEK "BIOS
 * Memory Copy": the control word (bits 0-20 count, bit 24 fixed source,
 * bit 26 32-bit units for CpuSet), CpuFastSet's 8-word blocks and its
 * rounding of the count ("the GBA is forcefully rounding-up the length"),
 * and the transfer bounds every game-supplied range goes through. Alignment
 * is deliberately not enforced (bios.h: expansion's GBA ABI pads structs to 4
 * bytes, the native one does not), so misaligned ranges must copy exactly.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define FILL_BYTE 0xEE
#define FILL_16 UINT16_C(0xEEEE)
#define FILL_32 UINT32_C(0xEEEEEEEE)
#define EWRAM_WORDS (GBA_EWRAM_SIZE / 4)

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

static uint8_t *ewram(void)
{
    return (uint8_t *)(uintptr_t)GBA_EWRAM_BASE;
}

static uint32_t load32(const uint8_t *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, sizeof value);
    return value;
}

PTEST(cpuset_copy_16bit)
{
    setup();
    const uint16_t src[5] = {1, 2, 3, 4, 0xFFFF};
    uint16_t dst[8];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(src, dst, 5);
    PT_ASSERT_MEM_EQ(dst, src, sizeof src);
    PT_ASSERT_EQ_U32(dst[5], FILL_16);
    PT_ASSERT_EQ_U32(dst[7], FILL_16);
    teardown();
}

PTEST(cpuset_copy_32bit)
{
    setup();
    const uint32_t src[3] = {0x11111111, 0x22222222, 0xDEADBEEF};
    uint32_t dst[4];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(src, dst, BIOS_CPU_SET_32BIT | 3);
    PT_ASSERT_MEM_EQ(dst, src, sizeof src);
    PT_ASSERT_EQ_U32(dst[3], FILL_32);
    teardown();
}

/* A fill reads exactly one unit (ASan would report a read past `value`). */
PTEST(cpuset_fill_16bit_and_32bit)
{
    setup();
    const uint16_t value16 = 0xABCD;
    uint16_t dst16[8];
    memset(dst16, FILL_BYTE, sizeof dst16);
    CpuSet(&value16, dst16, BIOS_CPU_SET_SRC_FIXED | 6);
    for (size_t i = 0; i < 6; i++)
        PT_ASSERT_EQ_U32(dst16[i], 0xABCD);
    PT_ASSERT_EQ_U32(dst16[6], FILL_16);

    const uint32_t value32 = 0x01234567;
    uint32_t dst32[4];
    memset(dst32, FILL_BYTE, sizeof dst32);
    CpuSet(&value32, dst32, BIOS_CPU_SET_SRC_FIXED | BIOS_CPU_SET_32BIT | 3);
    for (size_t i = 0; i < 3; i++)
        PT_ASSERT_EQ_U32(dst32[i], 0x01234567);
    PT_ASSERT_EQ_U32(dst32[3], FILL_32);
    teardown();
}

PTEST(cpuset_count_zero_touches_nothing)
{
    setup();
    uint16_t dst[2] = {FILL_16, FILL_16};
    const uint16_t src[2] = {1, 2};
    CpuSet(src, dst, 0);
    CpuSet(src, dst, BIOS_CPU_SET_32BIT);
    CpuSet(src, dst, BIOS_CPU_SET_SRC_FIXED);
    CpuSet(NULL, NULL, 0); /* nothing is read or written, so nothing is checked */
    PT_ASSERT_EQ_U32(dst[0], FILL_16);
    PT_ASSERT_EQ_U32(dst[1], FILL_16);
    teardown();
}

/* Bits 21-23, 25 and 27-31 of the control word mean nothing. */
PTEST(cpuset_ignores_unused_control_bits)
{
    setup();
    const uint16_t src[2] = {0x1111, 0x2222};
    uint16_t dst[4];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(src, dst, UINT32_C(0xFAE00002)); /* count 2, 16-bit copy */
    PT_ASSERT_MEM_EQ(dst, src, sizeof src);
    PT_ASSERT_EQ_U32(dst[2], FILL_16);
    teardown();
}

/* The largest count that fits a region works; one unit more, or the 21-bit
 * maximum, is refused and writes nothing. */
PTEST(cpuset_maximum_counts)
{
    setup();
    const uint32_t value = 0x600DF00D;
    memset(ewram(), FILL_BYTE, GBA_EWRAM_SIZE);
    CpuSet(&value, ewram(), BIOS_CPU_SET_SRC_FIXED | BIOS_CPU_SET_32BIT | BIOS_CPU_SET_COUNT_MASK);
    PT_ASSERT_EQ_U32(load32(ewram()), FILL_32);
    CpuSet(&value, ewram(), BIOS_CPU_SET_SRC_FIXED | BIOS_CPU_SET_32BIT | (EWRAM_WORDS + 1));
    PT_ASSERT_EQ_U32(load32(ewram()), FILL_32);
    CpuSet(&value, ewram(), BIOS_CPU_SET_SRC_FIXED | BIOS_CPU_SET_32BIT | EWRAM_WORDS);
    PT_ASSERT_EQ_U32(load32(ewram()), 0x600DF00D);
    PT_ASSERT_EQ_U32(load32(ewram() + GBA_EWRAM_SIZE - 4), 0x600DF00D);
    memset(ewram(), 0, GBA_EWRAM_SIZE);
    teardown();
}

PTEST(cpuset_misaligned_source_and_destination_copy_correctly)
{
    setup();
    const uint8_t src[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    uint8_t dst[12];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(src + 1, dst + 3, 3); /* three halfwords, both addresses odd */
    PT_ASSERT_MEM_EQ(dst + 3, src + 1, 6);
    PT_ASSERT_EQ_U32(dst[2], FILL_BYTE);
    PT_ASSERT_EQ_U32(dst[9], FILL_BYTE);

    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(src + 2, dst + 1, BIOS_CPU_SET_32BIT | 2); /* two words at 2-byte and 1-byte offsets */
    PT_ASSERT_MEM_EQ(dst + 1, src + 2, 8);
    PT_ASSERT_EQ_U32(dst[0], FILL_BYTE);
    PT_ASSERT_EQ_U32(dst[9], FILL_BYTE);

    const uint8_t fill[3] = {0, 0x34, 0x12};
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(fill + 1, dst + 1, BIOS_CPU_SET_SRC_FIXED | 2);
    const uint8_t expected[4] = {0x34, 0x12, 0x34, 0x12};
    PT_ASSERT_MEM_EQ(dst + 1, expected, sizeof expected);
    PT_ASSERT_EQ_U32(dst[5], FILL_BYTE);
    teardown();
}

/* The hardware copies one unit at a time upward, so a copy onto itself one
 * unit higher repeats the first unit. */
PTEST(cpuset_overlapping_copy_goes_forward_unit_by_unit)
{
    setup();
    uint16_t buffer[6] = {1, 2, 3, 4, 5, 6};
    CpuSet(buffer, buffer + 1, 5);
    const uint16_t expected[6] = {1, 1, 1, 1, 1, 1};
    PT_ASSERT_MEM_EQ(buffer, expected, sizeof expected);
    teardown();
}

PTEST(cpuset_destination_in_rodata_refused)
{
    setup();
    static const uint16_t rodata[4] = {9, 9, 9, 9};
    const uint16_t src[4] = {1, 2, 3, 4};
    /* The game can name any address; here, read-only data. */
    CpuSet(src, (void *)(uintptr_t)rodata, 4);
    PT_ASSERT_EQ_U32(rodata[0], 9);
    teardown();
}

PTEST(cpuset_source_outside_the_game_refused)
{
    setup();
    uint16_t *heap = malloc(4 * sizeof *heap); /* the game has no native heap */
    PT_ASSERT(heap != NULL);
    memset(heap, 0x11, 4 * sizeof *heap);
    uint16_t dst[4];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuSet(heap, dst, 4);
    free(heap);
    PT_ASSERT_EQ_U32(dst[0], FILL_16);

    /* A range running off the end of IWRAM. */
    const uint8_t *iwram_end = (const uint8_t *)(uintptr_t)(GBA_IWRAM_BASE + GBA_IWRAM_SIZE);
    CpuSet(iwram_end - 4, dst, 4);
    PT_ASSERT_EQ_U32(dst[0], FILL_16);
    teardown();
}

PTEST(cpufastset_copies_whole_blocks)
{
    setup();
    uint32_t src[16], dst[24];
    for (uint32_t i = 0; i < 16; i++)
        src[i] = 0x01010101u * (i + 1);
    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src, dst, 16);
    PT_ASSERT_MEM_EQ(dst, src, sizeof src);
    PT_ASSERT_EQ_U32(dst[16], FILL_32);
    teardown();
}

/* GBATEK: "the GBA is forcefully rounding-up the length" to 8 words, and
 * the extra words are copied. */
PTEST(cpufastset_rounds_count_up_to_8_words)
{
    setup();
    uint32_t src[16], dst[24];
    for (uint32_t i = 0; i < 16; i++)
        src[i] = 0x01010101u * (i + 1);

    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src, dst, 1);
    PT_ASSERT_MEM_EQ(dst, src, 8 * sizeof src[0]);
    PT_ASSERT_EQ_U32(dst[8], FILL_32);

    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src, dst, 9);
    PT_ASSERT_MEM_EQ(dst, src, 16 * sizeof src[0]);
    PT_ASSERT_EQ_U32(dst[16], FILL_32);

    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src, dst, 0);
    PT_ASSERT_EQ_U32(dst[0], FILL_32);
    teardown();
}

PTEST(cpufastset_fill_rounds_too)
{
    setup();
    const uint32_t value = 0x12345678;
    uint32_t dst[12];
    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(&value, dst, BIOS_CPU_SET_SRC_FIXED | 3);
    for (size_t i = 0; i < 8; i++)
        PT_ASSERT_EQ_U32(dst[i], 0x12345678);
    PT_ASSERT_EQ_U32(dst[8], FILL_32);
    teardown();
}

/* Rounded up, the range would leave EWRAM: nothing is written at all. */
PTEST(cpufastset_rounding_past_region_end_refused)
{
    setup();
    uint8_t *tail = ewram() + GBA_EWRAM_SIZE - 16;
    memset(tail, FILL_BYTE, 16);
    const uint32_t value = 0;
    CpuFastSet(&value, tail, BIOS_CPU_SET_SRC_FIXED | 4); /* 4 words fit, 8 do not */
    PT_ASSERT_EQ_U32(load32(tail), FILL_32);
    PT_ASSERT_EQ_U32(load32(tail + 12), FILL_32);
    CpuFastSet(&value, ewram(), BIOS_CPU_SET_SRC_FIXED | BIOS_CPU_SET_COUNT_MASK);
    PT_ASSERT_EQ_U32(load32(tail), FILL_32);
    memset(tail, 0, 16);
    teardown();
}

/* Each 8-word block is loaded (LDMIA) before it is stored (STMIA). */
PTEST(cpufastset_overlapping_copy_moves_blocks)
{
    setup();
    uint32_t buffer[17];
    for (uint32_t i = 0; i < 17; i++)
        buffer[i] = i;
    CpuFastSet(buffer, buffer + 1, 8);
    const uint32_t expected[10] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 9};
    PT_ASSERT_MEM_EQ(buffer, expected, sizeof expected);
    teardown();
}

/* Bit 26 (CpuSet's 32-bit flag) means nothing to CpuFastSet. */
PTEST(cpufastset_ignores_32bit_flag)
{
    setup();
    uint32_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t dst[8] = {0};
    CpuFastSet(src, dst, BIOS_CPU_SET_32BIT | 8);
    PT_ASSERT_MEM_EQ(dst, src, sizeof src);
    teardown();
}

PTEST(cpufastset_misaligned_copy_and_fill)
{
    setup();
    uint8_t src[40], dst[40];
    for (uint8_t i = 0; i < 40; i++)
        src[i] = i;
    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src + 2, dst + 1, 8);
    PT_ASSERT_MEM_EQ(dst + 1, src + 2, 32);
    PT_ASSERT_EQ_U32(dst[0], FILL_BYTE);
    PT_ASSERT_EQ_U32(dst[33], FILL_BYTE);

    memset(dst, FILL_BYTE, sizeof dst);
    CpuFastSet(src + 1, dst + 3, BIOS_CPU_SET_SRC_FIXED | 8);
    for (size_t i = 0; i < 8; i++)
        PT_ASSERT_MEM_EQ(dst + 3 + 4 * i, src + 1, 4);
    PT_ASSERT_EQ_U32(dst[35], FILL_BYTE);
    teardown();
}

PTEST(cpufastset_destination_in_rodata_refused)
{
    setup();
    static const uint32_t rodata[8] = {9};
    const uint32_t src[8] = {1};
    CpuFastSet(src, (void *)(uintptr_t)rodata, 8);
    PT_ASSERT_EQ_U32(rodata[0], 9);
    teardown();
}

PTEST_MAIN()
