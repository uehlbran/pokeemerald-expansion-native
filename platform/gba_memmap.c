/*
 * gba_memmap.c: the GBA memory map in the native process.
 *
 * Stands in for the GBA's address decoder and the memories behind it, GBATEK
 * "GBA Memory Map" (https://problemkaputt.de/gbatek.htm#gbamemorymap), plus
 * the page holding mGBA's debug registers (mGBA src/gba/io.c,
 * docs/research/test-runner-protocol.md). On the GBA each load or store goes
 * to the block its address selects; here each block is ordinary memory at the
 * same address, so game code's fixed addresses and REG_* macros work as
 * written. Written from GBATEK and expansion's headers; no reference port does
 * this (they keep GBA memory in C arrays and rewrite the register macros).
 *
 * HOW THE REGIONS EXIST. platform/gba_regions.ld, a GNU ld fragment inserted
 * after .bss, declares each region a NOLOAD output section at its GBA address.
 * Those are SHT_NOBITS sections, so ld emits a file-less PT_LOAD segment for
 * each, and the kernel maps them zero-filled and read-write while loading the
 * executable, before any code runs. The executable is linked non-PIE at
 * 0x40000000 (-Wl,-Ttext-segment) to stay clear of every region on both bit
 * widths. So init has nothing to map: it checks that the linker put each
 * section where this module's table says, at the region's base and exactly
 * the region's size in whole 4 KiB pages long (the script cannot include
 * this module's header, so the numbers exist twice), and fails loudly if
 * not; and shutdown has nothing to release. Every test binary and the game link with
 * the fragment (platform/Makefile; Makefile.native must do the same).
 *
 * WHY NOT RUNTIME MMAP. Both ways were tried on 2026-10-08 with 32-bit and
 * 64-bit binaries under ASan and UBSan: this linker-script way, and mmap with
 * MAP_FIXED_NOREPLACE at init (docs/research/memory-map-experiment.md). Both
 * map every region. The linker script was chosen because:
 *  1. EWRAM must also hold the game's EWRAM_DATA, which the linker places.
 *     With mmap, the loader has already mapped those pages and mapping all of
 *     EWRAM fails with EEXIST (observed), so EWRAM would be part linker, part
 *     mmap. Here one output section holds both.
 *  2. The regions exist before the first instruction runs: there is no window
 *     in which the sanitizer runtime, the dynamic loader or malloc could take
 *     one of the addresses, and no runtime failure path to handle.
 *  3. LeakSanitizer scans the executable's writable segments for pointers, so
 *     a heap pointer kept only in GBA memory is not reported as leaked. An
 *     mmap'd region is not scanned, and such a pointer was reported (observed).
 * Costs: it needs GNU ld and a non-PIE link. ld sorts segments by address, so
 * the ELF and program headers land in the page below EWRAM (0x01FFF000, unused
 * on the GBA). The file-less segments rely on the kernel mapping a segment's
 * zero-fill part before it maps the next segment, which holds because ld emits
 * them in address order (verified on Linux 5.15). Windows and macOS linkers
 * cannot do this; there init will reserve the regions at run time, which the
 * init/shutdown contract already allows.
 *
 * EWRAM_DATA. Expansion declares EWRAM variables with
 * __attribute__((section(".sbss"))) (include/gba/defines.h). gba_regions.ld
 * collects *(.sbss .sbss.*) at the bottom of the EWRAM section, as
 * ld_script_modern.ld does on the GBA, and fails the link if they exceed
 * 256 KiB; the game's EWRAM variables land in EWRAM by linking with the same
 * fragment. GCC's ASan does not instrument globals in named sections, so they
 * keep their GBA layout without redzones. EWRAM_INIT (section .ewram,
 * initialised data) is the loaded section gba_regions.ld places ahead of
 * .sbss; crt0.c keeps the copy that a soft reset restores.
 */
#include <inttypes.h>
#include <stdio.h>

#include "gba_memmap.h"

/* Section bounds defined by gba_regions.ld. Only their addresses are used. */
extern const unsigned char gba_linked_ewram_start[], gba_linked_ewram_end[];
extern const unsigned char gba_linked_iwram_start[], gba_linked_iwram_end[];
extern const unsigned char gba_linked_io_start[], gba_linked_io_end[];
extern const unsigned char gba_linked_mgba_debug_start[], gba_linked_mgba_debug_end[];
extern const unsigned char gba_linked_palette_ram_start[], gba_linked_palette_ram_end[];
extern const unsigned char gba_linked_vram_start[], gba_linked_vram_end[];
extern const unsigned char gba_linked_oam_start[], gba_linked_oam_end[];
extern const unsigned char gba_linked_sram_start[], gba_linked_sram_end[];

static const struct gba_region_info regions[GBA_REGION_COUNT] = {
    [GBA_REGION_EWRAM] = {GBA_EWRAM_BASE, GBA_EWRAM_SIZE, "EWRAM"},
    [GBA_REGION_IWRAM] = {GBA_IWRAM_BASE, GBA_IWRAM_SIZE, "IWRAM"},
    [GBA_REGION_IO] = {GBA_IO_BASE, GBA_IO_SIZE, "I/O"},
    [GBA_REGION_MGBA_DEBUG] = {GBA_MGBA_DEBUG_BASE, GBA_MGBA_DEBUG_SIZE, "mGBA debug"},
    [GBA_REGION_PALETTE_RAM] = {GBA_PALETTE_RAM_BASE, GBA_PALETTE_RAM_SIZE, "palette RAM"},
    [GBA_REGION_VRAM] = {GBA_VRAM_BASE, GBA_VRAM_SIZE, "VRAM"},
    [GBA_REGION_OAM] = {GBA_OAM_BASE, GBA_OAM_SIZE, "OAM"},
    [GBA_REGION_SRAM] = {GBA_SRAM_BASE, GBA_SRAM_SIZE, "SRAM"},
};

struct linked_section {
    const unsigned char *start;
    const unsigned char *end;
};

static const struct linked_section linked_sections[GBA_REGION_COUNT] = {
    [GBA_REGION_EWRAM] = {gba_linked_ewram_start, gba_linked_ewram_end},
    [GBA_REGION_IWRAM] = {gba_linked_iwram_start, gba_linked_iwram_end},
    [GBA_REGION_IO] = {gba_linked_io_start, gba_linked_io_end},
    [GBA_REGION_MGBA_DEBUG] = {gba_linked_mgba_debug_start, gba_linked_mgba_debug_end},
    [GBA_REGION_PALETTE_RAM] = {gba_linked_palette_ram_start, gba_linked_palette_ram_end},
    [GBA_REGION_VRAM] = {gba_linked_vram_start, gba_linked_vram_end},
    [GBA_REGION_OAM] = {gba_linked_oam_start, gba_linked_oam_end},
    [GBA_REGION_SRAM] = {gba_linked_sram_start, gba_linked_sram_end},
};

/* gba_regions.ld gives each region whole pages of this size (the x86 page). */
#define LINKED_PAGE_SIZE 0x1000u

static bool is_region(enum gba_region region)
{
    return region > GBA_REGION_NONE && region < GBA_REGION_COUNT;
}

/* The bytes gba_regions.ld reserves for a region: its size in whole pages. */
static size_t linked_size(const struct gba_region_info *info)
{
    return (info->size + LINKED_PAGE_SIZE - 1) / LINKED_PAGE_SIZE * LINKED_PAGE_SIZE;
}

/* GBA_MEMMAP_OK if the section linked at [start, end) is exactly the one
 * gba_regions.ld must give region: at its base, linked_size() long. Otherwise
 * says which region and how on log, and returns GBA_MEMMAP_ERR_LAYOUT.
 * Tested directly by test/test_gba_memmap_layout.c. */
static int check_linked_section(enum gba_region region, uintptr_t start, uintptr_t end, FILE *log)
{
    const struct gba_region_info *const info = &regions[region];
    const size_t expected_size = linked_size(info);
    if (start == info->base && end - start == expected_size)
        return GBA_MEMMAP_OK;
    (void)fprintf(log,
                  "gba_memmap: %s is linked at 0x%08" PRIXPTR "-0x%08" PRIXPTR " but must be at 0x%08" PRIXPTR
                  "-0x%08" PRIXPTR "; link with "
                  "platform/gba_regions.ld (see platform/Makefile)\n",
                  info->name, start, end, info->base, info->base + expected_size); /* best-effort */
    return GBA_MEMMAP_ERR_LAYOUT;
}

int gba_memmap_init(void)
{
    for (int id = GBA_REGION_NONE + 1; id < GBA_REGION_COUNT; id++) {
        /* Pointers to integers: comparing linked addresses with GBA addresses. */
        const uintptr_t start = (uintptr_t)linked_sections[id].start;
        const uintptr_t end = (uintptr_t)linked_sections[id].end;
        const int status = check_linked_section((enum gba_region)id, start, end, stderr);
        if (status != GBA_MEMMAP_OK)
            return status;
    }
    return GBA_MEMMAP_OK;
}

void gba_memmap_shutdown(void)
{
    /* Nothing to release: the loader created the regions, and they last as
     * long as the process. */
}

bool gba_memmap_contains(uintptr_t address, size_t length, enum gba_region region)
{
    if (!is_region(region) || length == 0 || address < regions[region].base)
        return false;
    /* Compared as offsets into the region, so no sum can wrap. */
    const uintptr_t offset = address - regions[region].base;
    const size_t size = regions[region].size;
    return offset < size && length <= size - offset;
}

enum gba_region gba_memmap_region_of(uintptr_t address, size_t length)
{
    for (int id = GBA_REGION_NONE + 1; id < GBA_REGION_COUNT; id++)
        if (gba_memmap_contains(address, length, (enum gba_region)id))
            return (enum gba_region)id;
    return GBA_REGION_NONE;
}

const struct gba_region_info *gba_memmap_info(enum gba_region region)
{
    return is_region(region) ? &regions[region] : NULL;
}
