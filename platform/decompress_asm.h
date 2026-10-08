/*
 * decompress_asm.h: the two routines of expansion's src/decompress_asm.s, in C.
 *
 * On the GBA, src/decompress.c copies these ARM routines into a stack buffer
 * and runs the copy (FastLZ77UnCompWram), and uses FastUnsafeCopy32 for that
 * copy. Natively code cannot run from a data buffer, so decompress.c calls
 * LZ77UnCompWRAMOptimized directly under PLATFORM_NATIVE
 * (docs/UPSTREAM_MODIFICATIONS.md); FastUnsafeCopy32 keeps its contract for
 * anything that copies data with it. Implementation notes: decompress_asm.c.
 */
#ifndef DECOMPRESS_ASM_H
#define DECOMPRESS_ASM_H

#include <stdint.h>

/* The block FastUnsafeCopy32 moves per iteration: one ldmia/stmia of eight
 * words (src/decompress_asm.s). */
#define FAST_UNSAFE_COPY32_BLOCK 32u

/* Copies `size` bytes from src to dest in whole 32-byte blocks: the number of
 * blocks is size rounded up to a multiple of 32, and at least one, because the
 * assembly copies a block before it tests the count, and tests it as signed.
 * So it writes up to 31 bytes past `size` (32 for size 0): "make sure [the
 * buffer] is at least 32 bytes larger than the desired size". Any alignment
 * natively (the ARM needs words). The ranges must not overlap. */
void FastUnsafeCopy32(void *dest, const void *src, uint32_t size);

/* GBATEK "LZ77UnCompReadNormalWrite8bit" data (the BIOS LZ77 format): a
 * 32-bit header whose bits 8-31 are the decompressed size (bits 0-7 are not
 * checked, as the assembly does not), then flag bytes, each giving the type
 * of the next eight blocks, most significant bit first: 0 = one literal
 * byte, 1 = two bytes copying 3 + (b0 >> 4) bytes from (((b0 & 0xF) << 8) |
 * b1) + 1 bytes back in the output. Writes exactly the header's size of bytes
 * to dest. A back-reference before the start of dest, which the assembly
 * would follow, stops decompression with a report on stderr. */
void LZ77UnCompWRAMOptimized(const uint32_t *src, void *dest);

#endif /* DECOMPRESS_ASM_H */
