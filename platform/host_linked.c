/*
 * host_linked.c: where the link put the GBA regions and the battle runner's
 * stack (host_memory.h), for every target linked with GNU ld's scripts:
 * gba_regions.ld on Linux and gba_regions.pe.ld on Windows define the same
 * symbols around the same sections, at the regions' GBA addresses (GBATEK
 * "GBA Memory Map"); only the call stack's place differs, 0x01800000 on
 * Linux and 0x01801000 on Windows, where the image's headers take
 * 0x01800000. What each OS does its own way is in host_linux.c and
 * host_windows.c.
 */
#include <stdint.h>

#include "gba_memmap.h"
#include "host_memory.h"

/* Section bounds the linker scripts define. Only their addresses are used. */
extern const unsigned char gba_linked_ewram_start[], gba_linked_ewram_end[];
extern const unsigned char gba_linked_iwram_start[], gba_linked_iwram_end[];
extern const unsigned char gba_linked_io_start[], gba_linked_io_end[];
extern const unsigned char gba_linked_mgba_debug_start[], gba_linked_mgba_debug_end[];
extern const unsigned char gba_linked_palette_ram_start[], gba_linked_palette_ram_end[];
extern const unsigned char gba_linked_vram_start[], gba_linked_vram_end[];
extern const unsigned char gba_linked_oam_start[], gba_linked_oam_end[];
extern const unsigned char gba_linked_sram_start[], gba_linked_sram_end[];
extern const unsigned char platform_call_stack_start[], platform_call_stack_end[];

struct linked_section {
    const unsigned char *start;
    const unsigned char *end;
};

static const struct linked_section linked_regions[GBA_REGION_COUNT] = {
    [GBA_REGION_EWRAM] = {gba_linked_ewram_start, gba_linked_ewram_end},
    [GBA_REGION_IWRAM] = {gba_linked_iwram_start, gba_linked_iwram_end},
    [GBA_REGION_IO] = {gba_linked_io_start, gba_linked_io_end},
    [GBA_REGION_MGBA_DEBUG] = {gba_linked_mgba_debug_start, gba_linked_mgba_debug_end},
    [GBA_REGION_PALETTE_RAM] = {gba_linked_palette_ram_start, gba_linked_palette_ram_end},
    [GBA_REGION_VRAM] = {gba_linked_vram_start, gba_linked_vram_end},
    [GBA_REGION_OAM] = {gba_linked_oam_start, gba_linked_oam_end},
    [GBA_REGION_SRAM] = {gba_linked_sram_start, gba_linked_sram_end},
};

/* Pointers to integers: the linked addresses are compared with GBA addresses. */
static struct host_span span_of(const unsigned char *start, const unsigned char *end)
{
    return (struct host_span){.start = (uintptr_t)start, .end = (uintptr_t)end};
}

struct host_span host_memory_region(enum gba_region region)
{
    if (region <= GBA_REGION_NONE || region >= GBA_REGION_COUNT)
        return (struct host_span){.start = 0, .end = 0};
    return span_of(linked_regions[region].start, linked_regions[region].end);
}

struct host_span host_memory_call_stack(void)
{
    return span_of(platform_call_stack_start, platform_call_stack_end);
}
