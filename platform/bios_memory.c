/*
 * bios_memory.c: CpuSet (SWI 0Bh) and CpuFastSet (SWI 0Ch).
 *
 * Stands in for the BIOS's memory copy and fill loops (GBATEK "BIOS Memory
 * Copy", https://problemkaputt.de/gbatek.htm#biosmemorycopy). CpuSet moves
 * 16- or 32-bit units one at a time (LDRH/STRH or LDMIA/STMIA of one
 * register); CpuFastSet moves 32-byte blocks (LDMIA/STMIA of eight
 * registers) and rounds its word count up to a multiple of 8 ("otherwise
 * the GBA is forcefully rounding-up the length"). A fill reads the source
 * unit once and stores it repeatedly.
 *
 * Each unit or block goes through a local buffer, as it goes through
 * registers on the GBA: so an overlapping copy behaves as the hardware's
 * does (a CpuSet onto itself one unit higher repeats the first unit; a
 * CpuFastSet moves whole blocks), and memcpy never sees overlapping ranges.
 *
 * Differences from the hardware, deliberate: GBATEK's alignment
 * requirements are not enforced (bios.h, "Alignment": the native ABI does
 * not pad structs to 4 bytes as expansion's -mabi=apcs-gnu GBA build does,
 * so the game passes 2- and 1-byte aligned addresses, and masking the low
 * bits as the hardware would writes the neighbouring bytes); every unit is
 * copied at the address given. The hardware's only check, "silently
 * reject[s] ... if the source ... [reaches] into the BIOS area", becomes
 * transfer_bounds_ok() on both whole ranges, which prints and copies
 * nothing.
 */
#include <stdbool.h>

#include "bios.h"
#include "bios_internal.h"

/* GBATEK: "units of 4 bytes or 2 bytes"; CpuFastSet "in units of 32 bytes". */
#define HALFWORD_SIZE 2u
#define WORD_SIZE 4u
#define FAST_BLOCK_WORDS 8u
#define FAST_BLOCK_SIZE (FAST_BLOCK_WORDS * WORD_SIZE)

/* Validates the ranges a copy or fill of count units touches. */
static bool ranges_ok(const char *function, const void *src, void *dest, size_t count, size_t unit, bool fill)
{
    size_t bytes = count * unit; /* count <= 200000h, unit <= 4: fits */
    return bios_range_ok(function, "source", src, fill ? unit : bytes, TRANSFER_READ)
           && bios_range_ok(function, "destination", dest, bytes, TRANSFER_WRITE);
}

/* Stores the unit at value into count consecutive units at out. */
static void fill_units(uint8_t *out, const uint8_t *value, size_t count, size_t unit)
{
    for (size_t i = 0; i < count; i++) {
        memcpy(out + i * unit, value, unit);
    }
}

void CpuSet(const void *src, void *dest, uint32_t control)
{
    size_t count = control & BIOS_CPU_SET_COUNT_MASK;
    size_t unit = (control & BIOS_CPU_SET_32BIT) != 0 ? WORD_SIZE : HALFWORD_SIZE;
    bool fill = (control & BIOS_CPU_SET_SRC_FIXED) != 0;
    if (count == 0 || !ranges_ok("CpuSet", src, dest, count, unit, fill)) {
        return;
    }
    const uint8_t *in = src;
    uint8_t *out = dest;
    uint8_t value[WORD_SIZE];
    if (fill) {
        memcpy(value, in, unit);
        fill_units(out, value, count, unit);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        memcpy(value, in + i * unit, unit);
        memcpy(out + i * unit, value, unit);
    }
}

void CpuFastSet(const void *src, void *dest, uint32_t control)
{
    size_t words = control & BIOS_CPU_SET_COUNT_MASK;
    size_t blocks = (words + FAST_BLOCK_WORDS - 1) / FAST_BLOCK_WORDS;
    bool fill = (control & BIOS_CPU_SET_SRC_FIXED) != 0;
    if (blocks == 0 || !ranges_ok("CpuFastSet", src, dest, blocks * FAST_BLOCK_WORDS, WORD_SIZE, fill)) {
        return;
    }
    const uint8_t *in = src;
    uint8_t *out = dest;
    if (fill) {
        uint8_t value[WORD_SIZE];
        memcpy(value, in, WORD_SIZE);
        fill_units(out, value, blocks * FAST_BLOCK_WORDS, WORD_SIZE);
        return;
    }
    uint8_t block[FAST_BLOCK_SIZE];
    for (size_t i = 0; i < blocks; i++) {
        memcpy(block, in + i * FAST_BLOCK_SIZE, FAST_BLOCK_SIZE);
        memcpy(out + i * FAST_BLOCK_SIZE, block, FAST_BLOCK_SIZE);
    }
}
