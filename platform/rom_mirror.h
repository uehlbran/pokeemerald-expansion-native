/*
 * rom_mirror.h: the Game Pak ROM's second address window, for code pointers.
 *
 * Stands in for the cartridge bus's wait-state mirrors. The ROM is visible
 * at 08000000h (wait state 0), 0A000000h (wait state 1) and 0C000000h (wait
 * state 2): the same bytes at three addresses (GBATEK "GBA Memory Map",
 * https://problemkaputt.de/gbatek.htm#gbamemorymap). Expansion uses the
 * second window as a tag: an event script command, native or special whose
 * function may be run for analysis is listed in its tables as the function's
 * address plus ROM_SIZE (0x02000000, constants/gba_constants.inc), and the
 * analysis tests bits 25-27 of the pointer (src/script.c,
 * Script_IsEffectInstrumentedCommand); calling such a pointer works on the
 * GBA because the window is a mirror.
 *
 * Natively the executable's code is at its link address (0x40000000 and up,
 * gba_memmap.h) and nothing is at that address plus 0x02000000, so the first
 * script command would call unmapped memory. rom_mirror_init() maps the same
 * executable code a second time at its address plus ROM_MIRROR_OFFSET, as a
 * private read-only executable mapping of the program file. The copy is
 * byte for byte the original: branches inside it stay inside it, and
 * references to data and to other functions by absolute address reach the
 * originals, so code run through the mirror does what the original does.
 * Function pointers (and the tag) are the only thing that ever point into it.
 */
#ifndef PLATFORM_ROM_MIRROR_H
#define PLATFORM_ROM_MIRROR_H

#include <stdint.h>

/* GBATEK: wait state 1 is wait state 0 plus 02000000h; expansion's ROM_SIZE. */
#define ROM_MIRROR_OFFSET UINT32_C(0x02000000)

enum rom_mirror_error {
    ROM_MIRROR_OK = 0,
    ROM_MIRROR_ERR_NO_TEXT = -1,   /* no executable segment in the program headers */
    ROM_MIRROR_ERR_OPEN = -2,      /* cannot open the program file */
    ROM_MIRROR_ERR_MAP = -3,       /* the address is taken, or mmap failed */
    ROM_MIRROR_ERR_ALREADY = -4    /* init twice without shutdown */
};

/* Maps the mirror. Returns ROM_MIRROR_OK, or an error after printing the
 * reason to stderr; after an error nothing is mapped. */
int rom_mirror_init(void);

/* Unmaps it. Safe before init, after a failed init, and more than once. */
void rom_mirror_shutdown(void);

#endif /* PLATFORM_ROM_MIRROR_H */
