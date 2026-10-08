/*
 * gba_memmap.h: the GBA memory map in the native process.
 *
 * Stands in for the GBA's address decoder. On the GBA every load and store
 * goes to the memory or register block its address selects (GBATEK "GBA
 * Memory Map"). Here the same blocks exist at the same addresses in the
 * native process, so expansion's REG_* macros, EWRAM_START, VRAM, PLTT, OAM
 * and the test runner's fixed IWRAM addresses work unmodified. How they come
 * to exist, and why that way: gba_memmap.c. Design: docs/RESEARCH.md,
 * "Memory model"; docs/research/memory-map-experiment.md.
 *
 * Other modules use this header for region addresses and for bounds checks:
 * DMA, save and video code ask gba_memmap_contains() or
 * gba_memmap_region_of() before touching a range a game-supplied address
 * names. Addresses are uintptr_t because each GBA address is also the native
 * address of the same byte. Flagged for the 64-bit milestone: GBA addresses
 * fit in 32 bits, so the identity holds there too (checked on both widths).
 *
 * The constants must equal expansion's EWRAM_START, IWRAM_START, REG_BASE,
 * PLTT, VRAM and OAM (include/gba/defines.h, include/gba/io_reg.h);
 * platform/test/test_gba_memmap.c asserts that at compile time.
 */
#ifndef GBA_MEMMAP_H
#define GBA_MEMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GBATEK "GBA Memory Map", General Internal Memory:
 * 02000000-0203FFFF "WRAM - On-board Work RAM (256 KBytes)", known as EWRAM. */
#define GBA_EWRAM_BASE UINT32_C(0x02000000)
#define GBA_EWRAM_SIZE UINT32_C(0x00040000)

/* GBATEK "GBA Memory Map": 03000000-03007FFF "WRAM - On-chip Work RAM
 * (32 KBytes)", known as IWRAM. */
#define GBA_IWRAM_BASE UINT32_C(0x03000000)
#define GBA_IWRAM_SIZE UINT32_C(0x00008000)

/* GBATEK "GBA Memory Map": 04000000-040003FE "I/O Registers"; the block is
 * 1 KiB (the last register is the halfword at 040003FE). Mapped natively as
 * one 4 KiB page. */
#define GBA_IO_BASE UINT32_C(0x04000000)
#define GBA_IO_SIZE UINT32_C(0x00000400)

/* Not GBATEK: mGBA's debug registers, STRING at 04FFF600 (256 bytes), FLAGS
 * at 04FFF700, ENABLE at 04FFF780 (mGBA src/gba/io.c and
 * include/mgba/internal/gba/io.h; docs/research/test-runner-protocol.md).
 * Expansion's test runner prints through them. The region is the whole 4 KiB
 * page that holds them, inside GBATEK's unused 04000400-04FFFFFF. */
#define GBA_MGBA_DEBUG_BASE UINT32_C(0x04FFF000)
#define GBA_MGBA_DEBUG_SIZE UINT32_C(0x00001000)

/* GBATEK "GBA Memory Map", Internal Display Memory:
 * 05000000-050003FF "BG/OBJ Palette RAM (1 Kbyte)". Mapped as one 4 KiB page. */
#define GBA_PALETTE_RAM_BASE UINT32_C(0x05000000)
#define GBA_PALETTE_RAM_SIZE UINT32_C(0x00000400)

/* GBATEK "GBA Memory Map": 06000000-06017FFF "VRAM - Video RAM (96 KBytes)". */
#define GBA_VRAM_BASE UINT32_C(0x06000000)
#define GBA_VRAM_SIZE UINT32_C(0x00018000)

/* GBATEK "GBA Memory Map": 07000000-070003FF "OAM - OBJ Attributes (1 Kbyte)".
 * Mapped as one 4 KiB page. */
#define GBA_OAM_BASE UINT32_C(0x07000000)
#define GBA_OAM_SIZE UINT32_C(0x00000400)

/* GBATEK "GBA Memory Map", External Memory: 0E000000-0E00FFFF "Game Pak SRAM
 * (max 64 KBytes)", the window the cartridge's backup chip answers in.
 * Emerald's chip is a 1 Mbit flash, GBATEK "GBA Cart Backup Flash ROM":
 * "128 KBytes - 1Mbit Flash ROM", seen through that window one 64K bank at a
 * time. The native region backs the whole chip, both banks contiguous, so the
 * save container can persist it as one image. */
#define GBA_SRAM_BASE UINT32_C(0x0E000000)
#define GBA_SRAM_SIZE UINT32_C(0x00020000)

/* The regions, in address order. GBA_REGION_NONE means "no single region". */
enum gba_region {
    GBA_REGION_NONE = 0,
    GBA_REGION_EWRAM,
    GBA_REGION_IWRAM,
    GBA_REGION_IO,
    GBA_REGION_MGBA_DEBUG,
    GBA_REGION_PALETTE_RAM,
    GBA_REGION_VRAM,
    GBA_REGION_OAM,
    GBA_REGION_SRAM,
    GBA_REGION_COUNT /* one past the last region; not a region */
};

struct gba_region_info {
    uintptr_t base;   /* GBA address of the first byte, also its native address */
    size_t size;      /* bytes the hardware decodes, as GBATEK gives them */
    const char *name; /* for messages */
};

enum gba_memmap_error {
    GBA_MEMMAP_OK = 0,
    GBA_MEMMAP_ERR_LAYOUT = -1 /* a region is not linked where this header says */
};

/* Checks that the executable was linked with platform/gba_regions.ld as this
 * header expects: each region's section starts at the region's base and is
 * exactly the region's size, rounded up to whole 4 KiB pages, long. It does
 * not touch the memory: the ELF loader is what makes the sections exist,
 * zero-filled and read-write, before main. Returns GBA_MEMMAP_OK, or
 * GBA_MEMMAP_ERR_LAYOUT after printing the first region that differs to
 * stderr. Idempotent: calling it again, with or without gba_memmap_shutdown()
 * in between, checks again and returns the same result. Region contents are
 * never changed by init or shutdown. */
int gba_memmap_init(void);

/* Releases what gba_memmap_init() acquired. Safe to call at any time:
 * before init, after a failed init, and more than once. */
void gba_memmap_shutdown(void);

/* True only if [address, address + length) lies entirely inside region.
 * False for length 0 (an empty range names no byte), for a range whose end
 * would pass UINTPTR_MAX, and for GBA_REGION_NONE or an unknown region. */
bool gba_memmap_contains(uintptr_t address, size_t length, enum gba_region region);

/* The one region that contains all of [address, address + length), or
 * GBA_REGION_NONE (no region, a range spanning two, or length 0). */
enum gba_region gba_memmap_region_of(uintptr_t address, size_t length);

/* Base, size and name of region; NULL for GBA_REGION_NONE or an unknown region. */
const struct gba_region_info *gba_memmap_info(enum gba_region region);

#endif /* GBA_MEMMAP_H */
