/*
 * gba_memmap.c: empty module; the implementation follows its tests.
 */
#include "gba_memmap.h"

int gba_memmap_init(void)
{
    return GBA_MEMMAP_ERR_LAYOUT;
}

void gba_memmap_shutdown(void)
{
}

bool gba_memmap_contains(uintptr_t address, size_t length, enum gba_region region)
{
    (void)address;
    (void)length;
    (void)region;
    return false;
}

enum gba_region gba_memmap_region_of(uintptr_t address, size_t length)
{
    (void)address;
    (void)length;
    return GBA_REGION_NONE;
}

const struct gba_region_info *gba_memmap_info(enum gba_region region)
{
    (void)region;
    return NULL;
}
