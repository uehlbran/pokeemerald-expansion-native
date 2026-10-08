/*
 * GBA pages for the differential test. On ARM (qemu-arm, bare-metal
 * newlib) the linker places two sections at the GBA addresses
 * (Makefile: --section-start); natively they are mapped with mmap, as the
 * unit tests do.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "difftest_platform.h"

/* GBATEK "GBA Memory Map": the IWRAM page with SOUND_INFO_PTR (0x03007FF0)
 * and the I/O registers at REG_BASE. */
#define GBA_IWRAM_PAGE 0x03007000u
#define GBA_IO_PAGE    0x04000000u
#define GBA_PAGE_SIZE  0x1000u

#if defined(__arm__)

__attribute__((section(".gba_iwram"), used)) static volatile uint8_t sIwramPage[GBA_PAGE_SIZE];
__attribute__((section(".gba_io"), used)) static volatile uint8_t sIoPage[GBA_PAGE_SIZE];

int DifftestMapGbaPages(void)
{
    if ((uintptr_t)sIwramPage != GBA_IWRAM_PAGE || (uintptr_t)sIoPage != GBA_IO_PAGE)
    {
        fprintf(stderr, "difftest: GBA pages linked at %p and %p\n", (void *)sIwramPage, (void *)sIoPage);
        return -1;
    }
    return 0;
}

void DifftestUnmapGbaPages(void)
{
}

#else

#include <errno.h>
#include <sys/mman.h>

static int MapPage(uint32_t address)
{
    void *wanted = (void *)(uintptr_t)address;
    void *got = mmap(wanted, GBA_PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (got == MAP_FAILED || got != wanted)
    {
        fprintf(stderr, "difftest: mmap at 0x%08x failed: %s\n", (unsigned)address, strerror(errno));
        return -1;
    }
    return 0;
}

static void UnmapPage(uint32_t address)
{
    if (munmap((void *)(uintptr_t)address, GBA_PAGE_SIZE) != 0)
        fprintf(stderr, "difftest: munmap at 0x%08x failed: %s\n", (unsigned)address, strerror(errno));
}

int DifftestMapGbaPages(void)
{
    if (MapPage(GBA_IWRAM_PAGE) != 0)
        return -1;
    if (MapPage(GBA_IO_PAGE) != 0)
    {
        UnmapPage(GBA_IWRAM_PAGE);
        return -1;
    }
    return 0;
}

void DifftestUnmapGbaPages(void)
{
    UnmapPage(GBA_IO_PAGE);
    UnmapPage(GBA_IWRAM_PAGE);
}

#endif
