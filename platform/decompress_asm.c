/*
 * decompress_asm.c: FastUnsafeCopy32 and LZ77UnCompWRAMOptimized, the two
 * routines of expansion's src/decompress_asm.s, in C.
 *
 * Stands in for ARM code, not hardware: the game's fast LZ77 decoder (a
 * hand-written copy of the BIOS's LZ77UnCompWram that runs from a stack
 * buffer) and the 32-byte block copy it is copied with. Followed: the
 * assembly itself; the format is GBATEK "BIOS Decompression Functions",
 * LZ77UnCompReadNormalWrite8bit (https://problemkaputt.de/gbatek.htm#biosdecompressionfunctions).
 *
 * One departure, for memory safety: the assembly subtracts a back-reference's
 * whole length from the remaining size and then copies all of it, so a
 * stream whose last block is longer than the bytes left writes past the size
 * in its header. gbagfx never produces such a stream; this version writes
 * exactly the header's size. A back-reference reaching before the output's
 * first byte (garbage the assembly would copy) stops it, with a report.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "decompress_asm.h"

/* LZ77 header: bits 8-31 the decompressed size (GBATEK "Data header"). */
#define LZ77_SIZE_SHIFT 8u
/* A flag byte covers eight blocks, most significant bit first. */
#define LZ77_BLOCKS_PER_FLAG 8u
#define LZ77_FIRST_BLOCK_FLAG 0x80u
/* A back-reference: count = (b0 >> 4) + 3, distance = ((b0 & 0xF) << 8 | b1) + 1. */
#define LZ77_COUNT_SHIFT 4u
#define LZ77_MIN_COUNT 3u
#define LZ77_DISTANCE_HIGH_MASK 0x0Fu
#define LZ77_DISTANCE_HIGH_SHIFT 8u
#define LZ77_MIN_DISTANCE 1u

void FastUnsafeCopy32(void *dest, const void *src, uint32_t size)
{
    /* The assembly copies a block, subtracts 32 and loops while the result is
     * greater than zero as a signed number. */
    uint32_t blocks = 1;
    if (size <= (uint32_t)INT32_MAX && size > FAST_UNSAFE_COPY32_BLOCK)
        blocks = (size + FAST_UNSAFE_COPY32_BLOCK - 1u) / FAST_UNSAFE_COPY32_BLOCK;
    memcpy(dest, src, (size_t)blocks * FAST_UNSAFE_COPY32_BLOCK);
}

void LZ77UnCompWRAMOptimized(const uint32_t *src, void *dest)
{
    uint32_t header;
    memcpy(&header, src, sizeof header);
    const uint8_t *in = (const uint8_t *)src + sizeof header;
    uint8_t *out = dest;
    size_t size = header >> LZ77_SIZE_SHIFT;
    size_t written = 0;

    while (written < size) {
        uint8_t flags = *in++;
        for (unsigned block = 0; block < LZ77_BLOCKS_PER_FLAG && written < size; block++) {
            bool compressed = (flags & (LZ77_FIRST_BLOCK_FLAG >> block)) != 0;
            if (!compressed) {
                out[written++] = *in++;
                continue;
            }
            size_t count = (size_t)(in[0] >> LZ77_COUNT_SHIFT) + LZ77_MIN_COUNT;
            size_t distance = ((size_t)(in[0] & LZ77_DISTANCE_HIGH_MASK) << LZ77_DISTANCE_HIGH_SHIFT | in[1])
                              + LZ77_MIN_DISTANCE;
            in += 2;
            if (distance > written) {
                fprintf(stderr, "decompress_asm: LZ77UnCompWRAMOptimized: a back-reference %zu bytes back "
                        "at output byte %zu reaches before the output; stopping\n", distance, written);
                return;
            }
            if (count > size - written)
                count = size - written;
            /* Byte by byte: a reference may overlap the bytes it produces. */
            for (size_t i = 0; i < count; i++, written++)
                out[written] = out[written - distance];
        }
    }
}
