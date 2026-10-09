/*
 * rom_mirror.h: the Game Pak ROM's first mirror.
 *
 * Stands in for the cartridge bus's mirroring of the ROM. GBATEK "GBA Memory
 * Map": 08000000-09FFFFFF is the Game Pak ROM (max 32 MB) at wait state 0,
 * and 0A000000-0BFFFFFF the same ROM again at wait state 1 (0C000000 at wait
 * state 2). Expansion relies on the first mirror: its script engine marks a
 * script command, special or callnative function that "requests effects" by
 * storing its address plus ROM_SIZE (2000000h, constants/gba_constants.inc;
 * data/script_cmd_table.inc, data/specials.inc, asm/macros/event.inc), calls
 * it there, where the same code runs, and tests a pointer for the mark with
 * (pointer & E000000h) == A000000h (src/script.c, include/script.h).
 *
 * Natively the executable is the cartridge: Makefile.native and
 * platform/Makefile link it at 08000000h (-Wl,-Ttext-segment), so its code
 * and read-only data sit in the ROM window, and rom_mirror_init maps the
 * executable's read-only segments a second time, from the file, 2000000h
 * higher. Code runs the same from the mirror on 32-bit x86, whose
 * position-dependent code (-fno-pie) addresses data absolutely and calls
 * relative to itself, so a call made in the mirror stays in the mirror and
 * every data access reaches the one copy of the data. Flagged for 64-bit:
 * x86-64 code addresses data relative to the instruction pointer, so from
 * the mirror it would reach 2000000h past every global; the 64-bit
 * milestone needs another answer (untagging the pointers in the script
 * engine, or mirroring the writable data as shared memory).
 *
 * Linux only (getauxval, /proc/self/exe, MAP_FIXED_NOREPLACE).
 * Single-threaded.
 */
#ifndef ROM_MIRROR_H
#define ROM_MIRROR_H

#include <stdint.h>

/* GBATEK "GBA Memory Map": Game Pak ROM, wait state 0, and its size. */
#define ROM_MIRROR_ROM_BASE UINT32_C(0x08000000)
#define ROM_MIRROR_ROM_SIZE UINT32_C(0x02000000)

/* GBATEK "GBA Memory Map": the wait state 1 mirror starts one ROM size up
 * (0A000000h); expansion's ROM_SIZE. */
#define ROM_MIRROR_OFFSET ROM_MIRROR_ROM_SIZE

/* The most read-only segments mirrored: GNU ld makes two or three (headers
 * and read-only data, code, more read-only data). */
#define ROM_MIRROR_MAX_SEGMENTS 8u

/* Where the executable's file is, for mapping it again. */
#define ROM_MIRROR_EXECUTABLE "/proc/self/exe"

enum rom_mirror_error {
    ROM_MIRROR_OK = 0,
    ROM_MIRROR_ERR_LAYOUT = -1, /* no read-only segment in the ROM window, or one that leaves it */
    ROM_MIRROR_ERR_MAP = -2,    /* opening the executable or mapping the mirror failed */
    ROM_MIRROR_ERR_64_BIT = -3  /* a 64-bit build: its code cannot run from a mirror (above) */
};

/* Maps every read-only loadable segment of the executable that starts in the
 * ROM window again at ROM_MIRROR_OFFSET above it, with the same protection.
 * Returns ROM_MIRROR_OK, or an error after a report on stderr with nothing
 * left mapped; always ROM_MIRROR_ERR_64_BIT in a 64-bit build. Calling it
 * again maps again (after unmapping). */
int rom_mirror_init(void);

/* Unmaps the mirror. Safe at any time, and twice. */
void rom_mirror_shutdown(void);

#endif /* ROM_MIRROR_H */
