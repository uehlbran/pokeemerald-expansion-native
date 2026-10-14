/*
 * host_memory.h: memory at fixed addresses, as the GBA regions need it.
 *
 * Stands in for nothing on the GBA, whose memories sit at their addresses by
 * wiring (GBATEK "GBA Memory Map"); this is how the host gives the process
 * memory at an address of its choosing. gba_memmap checks the regions with
 * it, game_hooks and transfer_bounds find the battle runner's stack, and the
 * m4a tests map a page no region covers.
 *
 * Wraps GNU ld (host_linked.c): on Linux platform/gba_regions.ld makes each
 * region and the battle runner's stack a NOLOAD section at its address that
 * the kernel's ELF loader maps zero-filled before main (gba_memmap.c says why
 * a linker script and not mmap at run time); on Windows
 * platform/gba_regions.pe.ld makes them sections of the PE image, which the
 * loader maps at its fixed base. And, for the tests' pages, mmap with
 * MAP_FIXED_NOREPLACE on Linux (host_linux.c), VirtualAlloc on Windows
 * (host_windows.c), where an address must be a multiple of the 64 KiB
 * allocation granularity. Single-threaded.
 */
#ifndef PLATFORM_HOST_MEMORY_H
#define PLATFORM_HOST_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "gba_memmap.h"

/* [start, end): a block of the process's memory, end one past its last
 * byte. Addresses as integers: the GBA's addresses are native addresses
 * (gba_memmap.h). */
struct host_span {
    uintptr_t start;
    uintptr_t end;
};

/* [start, end) as integers, for linker symbols: the linked addresses are
 * compared with GBA addresses and with other addresses. */
static inline struct host_span host_span_of(const unsigned char *start, const unsigned char *end)
{
    return (struct host_span){.start = (uintptr_t)start, .end = (uintptr_t)end};
}

/* Where the executable holds GBA region `region`'s memory, as the link put
 * it (the linker script's gba_linked_<region>_start and _end); gba_memmap_init
 * checks that it is at the region's base, whole pages long. {0, 0} for
 * GBA_REGION_NONE or a value that names no region. */
struct host_span host_memory_region(enum gba_region region);

/* Where the executable holds the stack PlatformCallWithStack runs the battle
 * tests' functions on (game_hooks.h), 1 MiB: gba_regions.ld's
 * .platform_call_stack at 0x01800000 on Linux, gba_regions.pe.ld's .callstk
 * at 0x01801000 on Windows. */
struct host_span host_memory_call_stack(void);

/* Maps size bytes of zero-filled read-write memory at exactly address, both
 * whole pages (on Windows, address a multiple of 64 KiB), refusing to replace
 * anything already mapped there. For the tests that need memory where no
 * region is linked. Returns 0, or -1 after a report on stderr with nothing
 * mapped. */
int host_memory_map_fixed(uintptr_t address, size_t size);

/* Unmaps what host_memory_map_fixed mapped at address, size being what it
 * was given. Returns 0, or -1 after a report on stderr. */
int host_memory_unmap_fixed(uintptr_t address, size_t size);

#endif /* PLATFORM_HOST_MEMORY_H */
