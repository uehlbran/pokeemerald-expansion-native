/*
 * bios_internal.h: helpers shared by the bios_*.c files. Not part of the
 * BIOS's public interface (that is bios.h); nothing outside platform/bios_*.c
 * includes it.
 *
 * Range checks with a message, fixed-point helpers that keep the 32-bit
 * hardware's wraparound without signed overflow, and unaligned loads and
 * stores. Host byte order: little-endian, as the GBA is, so a 16- or 32-bit
 * unit is the same bytes on both (GBATEK "GBA Memory Map": the GBA is
 * little-endian); asserted below.
 */
#ifndef BIOS_INTERNAL_H
#define BIOS_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "transfer_bounds.h"

_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "the BIOS functions read and write GBA data in host byte order");

/* The GBA address of a native pointer: the same number (gba_memmap.h).
 * uintptr_t, flagged for the 64-bit milestone as gba_memmap.h is. */
uintptr_t bios_address_of(const void *pointer);

/* transfer_bounds_ok() for [start, start + length) that, on refusal, prints
 * "bios: <function>: <what> <address> + <length> refused" to stderr. */
bool bios_range_ok(const char *function, const char *what, const void *start, size_t length,
                   enum transfer_access access);

/* The signed 32-bit value with these bits (two's complement), as an ARM
 * register reads them. */
int32_t bios_s32_from_bits(uint32_t bits);

/* An ARM "ASR #shift" (shift 0..31): rounds toward minus infinity. Computed
 * on unsigned bits, so it does not rely on C's implementation-defined
 * right shift of a negative value. */
int32_t bios_asr(int32_t value, unsigned shift);

/* The low 32 bits of a * b, signed, as an ARM MUL computes them: wraps
 * instead of overflowing. */
int32_t bios_mul(int32_t a, int32_t b);

/* Unaligned little-endian loads and stores (memcpy, so no alignment or
 * aliasing assumption). */
static inline uint16_t bios_load16(const uint8_t *bytes)
{
    uint16_t value;
    memcpy(&value, bytes, sizeof value);
    return value;
}

static inline uint32_t bios_load32(const uint8_t *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, sizeof value);
    return value;
}

static inline void bios_store16(uint8_t *bytes, uint16_t value)
{
    memcpy(bytes, &value, sizeof value);
}

static inline void bios_store32(uint8_t *bytes, uint32_t value)
{
    memcpy(bytes, &value, sizeof value);
}

#endif /* BIOS_INTERNAL_H */
