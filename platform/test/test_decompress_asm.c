/*
 * Tests of decompress_asm.h: FastUnsafeCopy32 and LZ77UnCompWRAMOptimized.
 *
 * The behaviour comes from expansion's src/decompress_asm.s (the block
 * rounding and the signed loop test of FastUnsafeCopy32) and the LZ77 format
 * from GBATEK "BIOS Decompression Functions" (header word, flag byte most
 * significant bit first, back-references of N+3 bytes from distance+1 back).
 */
// deps: bios_internal.c transfer_bounds.c gba_memmap.c
#include <stdint.h>
#include <string.h>

#include "decompress_asm.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define COPY_GUARD 0xEE
#define COPY_AREA 160u

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

static void check_fast_copy(uint32_t size, size_t expected_bytes, size_t dest_offset)
{
    uint8_t source[COPY_AREA];
    uint8_t dest[COPY_AREA + 8];
    for (size_t i = 0; i < sizeof source; i++)
        source[i] = (uint8_t)(i + 1);
    memset(dest, COPY_GUARD, sizeof dest);
    FastUnsafeCopy32(dest + dest_offset, source, size);
    PT_ASSERT_MEM_EQ(dest + dest_offset, source, expected_bytes);
    for (size_t i = 0; i < dest_offset; i++)
        PT_ASSERT_EQ_INT(dest[i], COPY_GUARD);
    for (size_t i = dest_offset + expected_bytes; i < sizeof dest; i++)
        PT_ASSERT_EQ_INT(dest[i], COPY_GUARD);
}

PTEST(fast_unsafe_copy32_copies_whole_blocks)
{
    check_fast_copy(0, 32, 0); /* a block before the count is tested */
    check_fast_copy(1, 32, 0);
    check_fast_copy(32, 32, 0);
    check_fast_copy(33, 64, 0); /* rounded up */
    check_fast_copy(96, 96, 0);
    check_fast_copy(97, 128, 0);
}

PTEST(fast_unsafe_copy32_negative_size_copies_one_block)
{
    /* bgt after subs: a size with bit 31 set is negative, one block. */
    check_fast_copy(UINT32_C(0x80000000), 32, 0);
    check_fast_copy(UINT32_MAX, 32, 0);
}

PTEST(fast_unsafe_copy32_any_alignment)
{
    check_fast_copy(40, 64, 1);
    check_fast_copy(8, 32, 3);
}

#define LZ77_TYPE 0x10u /* GBATEK: bits 4-7 compressed type 1 */

static uint32_t lz_header(uint32_t size)
{
    return (size << 8) | LZ77_TYPE;
}

/* Builds a stream: header then the payload bytes, word-aligned. */
static void lz_stream(uint32_t *stream, size_t words, uint32_t size, const uint8_t *payload, size_t payload_size)
{
    memset(stream, 0, words * sizeof *stream);
    stream[0] = lz_header(size);
    memcpy(&stream[1], payload, payload_size);
}

PTEST(lz77_literals)
{
    setup();
    static const uint8_t payload[] = {0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0x00, 'I'};
    uint32_t stream[8];
    uint8_t out[16];
    lz_stream(stream, 8, 9, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(out, "ABCDEFGHI", 9);
    for (size_t i = 9; i < sizeof out; i++)
        PT_ASSERT_EQ_INT(out[i], COPY_GUARD);
    teardown();
}

PTEST(lz77_overlapping_back_reference_repeats)
{
    setup();
    /* 'x', then copy 18 bytes from 1 back: 19 'x'. Flags 0100 0000. */
    static const uint8_t payload[] = {0x40, 'x', 0xF0, 0x00};
    uint32_t stream[4];
    uint8_t out[24];
    lz_stream(stream, 4, 19, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    for (size_t i = 0; i < 19; i++)
        PT_ASSERT_EQ_INT(out[i], 'x');
    PT_ASSERT_EQ_INT(out[19], COPY_GUARD);
    teardown();
}

PTEST(lz77_back_reference_distance_and_count)
{
    setup();
    /* "abc", then copy 3 from 3 back ("abc"), then 4 from 2 back ("bcbc"). */
    static const uint8_t payload[] = {0x18, 'a', 'b', 'c', 0x00, 0x02, 0x10, 0x01};
    uint32_t stream[4];
    uint8_t out[16];
    lz_stream(stream, 4, 10, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(out, "abcabcbcbc", 10);
    PT_ASSERT_EQ_INT(out[10], COPY_GUARD);
    teardown();
}

PTEST(lz77_largest_distance)
{
    setup();
    /* 4096 literal bytes, then a 3-byte copy from 4096 back (the 12-bit
     * distance field at its maximum, 0xFFF, plus one). */
    enum { LITERALS = 4096, TOTAL = LITERALS + 3 };
    static uint8_t payload[LITERALS / 8 * 9 + 3];
    static uint32_t stream[(sizeof payload + 8) / 4 + 1];
    static uint8_t out[TOTAL + 4];
    size_t p = 0;
    for (size_t i = 0; i < LITERALS; i++) {
        if (i % 8 == 0)
            payload[p++] = 0x00;
        payload[p++] = (uint8_t)(i * 7 + 1);
    }
    payload[p++] = 0x80;
    payload[p++] = 0x0F;
    payload[p++] = 0xFF;
    lz_stream(stream, sizeof stream / 4, TOTAL, payload, p);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(&out[LITERALS], &out[0], 3);
    PT_ASSERT_EQ_INT(out[TOTAL], COPY_GUARD);
    teardown();
}

PTEST(lz77_stops_at_the_headers_size)
{
    setup();
    /* A last block of 18 bytes when only 5 remain: exactly the size is
     * written (the assembly would write all 18). */
    static const uint8_t payload[] = {0x40, 'z', 0xF0, 0x00};
    uint32_t stream[4];
    uint8_t out[24];
    lz_stream(stream, 4, 6, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    for (size_t i = 0; i < 6; i++)
        PT_ASSERT_EQ_INT(out[i], 'z');
    PT_ASSERT_EQ_INT(out[6], COPY_GUARD);
    teardown();
}

PTEST(lz77_zero_size_writes_nothing)
{
    setup();
    static const uint8_t payload[] = {0x00, 'q'};
    uint32_t stream[4];
    uint8_t out[4];
    lz_stream(stream, 4, 0, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], COPY_GUARD);
    teardown();
}

PTEST(lz77_reference_before_the_output_stops)
{
    setup();
    /* 'a', then a copy from 2 back: before the first byte. */
    static const uint8_t payload[] = {0x40, 'a', 0x00, 0x01};
    uint32_t stream[4];
    uint8_t out[8];
    lz_stream(stream, 4, 4, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], 'a');
    PT_ASSERT_EQ_INT(out[1], COPY_GUARD);
    teardown();
}

PTEST(lz77_type_bits_are_not_checked)
{
    setup();
    /* As the assembly: only bits 8-31 of the header are read. */
    static const uint8_t payload[] = {0x00, 'k'};
    uint32_t stream[4];
    uint8_t out[4];
    lz_stream(stream, 4, 1, payload, sizeof payload);
    stream[0] = (1u << 8) | 0x2Fu;
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], 'k');
    teardown();
}

PTEST(lz77_destination_running_off_the_region_is_refused)
{
    setup();
    /* A header size of 16 bytes into the last 8 of VRAM: nothing is written. */
    static const uint8_t payload[] = {0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                      0x00, 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P'};
    uint32_t stream[8];
    lz_stream(stream, 8, 16, payload, sizeof payload);
    uint8_t *tail = (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + GBA_VRAM_SIZE - 8);
    memset(tail, COPY_GUARD, 8);
    LZ77UnCompWRAMOptimized(stream, tail);
    for (size_t i = 0; i < 8; i++)
        PT_ASSERT_EQ_INT(tail[i], COPY_GUARD);
    teardown();
}

PTEST(lz77_header_outside_the_game_memory_is_refused)
{
    setup();
    uint8_t out[4];
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized((const uint32_t *)(uintptr_t)GBA_VRAM_BASE + GBA_VRAM_SIZE / 4, out);
    PT_ASSERT_EQ_INT(out[0], COPY_GUARD);
    teardown();
}

PTEST_MAIN()
