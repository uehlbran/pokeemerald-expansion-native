/*
 * script_tag.h: how expansion tags function pointers in event script tables,
 * natively.
 *
 * Not hardware, but a hardware assumption of expansion's: the ROM is visible
 * at 08000000h and, as a mirror, at 0A000000h (GBATEK "GBA Memory Map",
 * https://problemkaputt.de/gbatek.htm#gbamemorymap). A script command, native
 * or special that can be run for effect analysis (include/script.h, "Script
 * effects analysis") is listed in its table as the function's address plus
 * ROM_SIZE (0x02000000, constants/gba_constants.inc, used by
 * data/script_cmd_table.inc, data/specials.inc and asm/macros/event.inc), and
 * src/script.c asks whether bits 25-27 of the pointer are 0A000000h. On the
 * GBA the tagged pointer is also a valid pointer: the mirror has the same
 * code.
 *
 * Natively there is no mirror: the code is at its link address (0x40000000
 * and up, gba_memmap.h), below 0x42000000, so bit 25 of a code address is
 * clear and a tagged pointer (address + 0x02000000) has it set, in memory
 * nothing maps. The tag is therefore kept as bit 25, tested with
 * script_tag_is_set and removed with script_tag_strip before the pointer is
 * called or compared with an untagged one. Each place expansion's code does
 * that is a guarded change (docs/UPSTREAM_MODIFICATIONS.md). Linking the
 * executable above 0x42000000 would break this, and so would code that
 * reaches it (gba_regions.ld asserts etext < 0x42000000). Measured
 * 2026-10-08, the code ends at 0x4031C2B8 (3 MiB, plain), 0x4078E498
 * (7.6 MiB, TEST=1), 0x40EF3518 (14.9 MiB, SANITIZE=1) and 0x41BB1358
 * (27.7 MiB, TEST=1 SANITIZE=1, which only fits because Makefile.native
 * makes AddressSanitizer's checks calls; inline they are about 40 MB).
 *
 * (Mapping the code a second time at +0x02000000 was tried: it works for
 * code compiled without position-dependent data references, which is only
 * the 32-bit build; x86-64 code addresses its globals relative to the
 * program counter, so code run through the mirror would read the wrong
 * data.)
 */
#ifndef PLATFORM_SCRIPT_TAG_H
#define PLATFORM_SCRIPT_TAG_H

#include <stdbool.h>
#include <stdint.h>

/* ROM_SIZE: the offset between the ROM's first and second window. */
#define SCRIPT_TAG UINT32_C(0x02000000)

/* True if the tag is on the pointer. */
static inline bool script_tag_is_set(uintptr_t pointer)
{
    return (pointer & SCRIPT_TAG) != 0;
}

/* The pointer without the tag (unchanged if it has none). */
static inline uintptr_t script_tag_strip(uintptr_t pointer)
{
    return pointer & ~(uintptr_t)SCRIPT_TAG;
}

/* A function pointer without the tag, as the same type. */
#define SCRIPT_UNTAG(function) ((__typeof__(function))script_tag_strip((uintptr_t)(function)))

#endif /* PLATFORM_SCRIPT_TAG_H */
