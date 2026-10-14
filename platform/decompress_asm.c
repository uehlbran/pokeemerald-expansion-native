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
 *
 * The other, for the same reason: the header and the destination
 * [dest, dest + size) are checked with transfer_bounds before any byte is
 * touched, as the BIOS twin LZ77UnCompWram does (bios.h), because the size
 * comes from the data and can be up to 16 MiB. A refused range writes nothing
 * and is reported on stderr.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "decompress_asm.h"
#include "transfer_bounds.h"

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
/* A back-reference block is two bytes. */
#define LZ77_BLOCK_SIZE 2u

/* transfer_bounds_ok() for [start, start + length) that, on refusal, prints
 * "decompress_asm: LZ77UnCompWRAMOptimized: <what> <address> + <length> refused"
 * to stderr, in the shape the BIOS twin's refusals have. */
static bool range_ok(const char *what, const void *start, size_t length, enum transfer_access access)
{
    uintptr_t address = 0;
    memcpy(&address, &start, sizeof address);
    if (transfer_bounds_ok(address, length, access))
        return true;
    (void)fprintf(stderr,
                  "decompress_asm: LZ77UnCompWRAMOptimized: %s %#" PRIxPTR " + %#zx refused (transfer_bounds)\n", what,
                  address, length);
    return false;
}

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
    if (!range_ok("header", src, sizeof header, TRANSFER_READ))
        return;
    memcpy(&header, src, sizeof header);
    const uint8_t *in = (const uint8_t *)src + sizeof header;
    uint8_t *out = dest;
    size_t size = header >> LZ77_SIZE_SHIFT;
    size_t written = 0;

    if (size != 0 && !range_ok("destination", dest, size, TRANSFER_WRITE))
        return;
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
            in += LZ77_BLOCK_SIZE;
            if (distance > written) {
                (void)fprintf(stderr,
                              "decompress_asm: LZ77UnCompWRAMOptimized: a back-reference %zu bytes back "
                              "at output byte %zu reaches before the output; stopping\n",
                              distance, written);
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
