/*
 * test_host_memory.c: memory at fixed addresses (host_memory.h). The places
 * are written out here, independently of gba_memmap.h's table: each GBA
 * region at its GBATEK base, its size rounded up to whole 4 KiB pages
 * (GBATEK "GBA Memory Map"), and the battle runner's stack, 1 MiB in whole
 * pages below the lowest region (0x01800000 on Linux, 0x01801000 on Windows,
 * where the image's headers take 0x01800000). A page mapped at run time is
 * zero-filled and writable, and mapping over anything already there, a region
 * or another mapped page, is refused and leaves it as it was.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba_memmap.h"
#include "host_memory.h"
#include "ptest.h"

#define PAGE 0x1000u
#define KIB 1024u
#define MIB (1024u * 1024u)

static const struct {
    enum gba_region id;
    uintptr_t base;
    size_t linked_size;
} expected_regions[] = {
    {GBA_REGION_EWRAM, 0x02000000u, 256 * KIB},  {GBA_REGION_IWRAM, 0x03000000u, 32 * KIB},
    {GBA_REGION_IO, 0x04000000u, PAGE},          {GBA_REGION_MGBA_DEBUG, 0x04FFF000u, PAGE},
    {GBA_REGION_PALETTE_RAM, 0x05000000u, PAGE}, {GBA_REGION_VRAM, 0x06000000u, 96 * KIB},
    {GBA_REGION_OAM, 0x07000000u, PAGE},         {GBA_REGION_SRAM, 0x0E000000u, 128 * KIB},
};
#define EXPECTED_REGION_COUNT (sizeof expected_regions / sizeof expected_regions[0])
_Static_assert(EXPECTED_REGION_COUNT == GBA_REGION_COUNT - 1, "a region has no expected place");

/* An address no region, no stack and nothing else in a test binary uses,
 * below the linked blocks (the page the m4a tests map), and a multiple of
 * 64 KiB, which Windows needs (host_windows.c): the last 64 KiB below the
 * Windows image's base, 0x01800000. Windows fills the addresses below the
 * image from the bottom up with the process's own allocations, which end
 * around 0x00FF1000 in a test (measured on the CI runner, run 38613404450),
 * so 0x01000000 was sometimes taken there; on Linux nothing is mapped below
 * the call stack. */
#define FREE_PAGE_ADDRESS 0x017F0000u

/* The lowest region's base (EWRAM), which the call stack ends at or below. */
#define LOWEST_REGION_BASE 0x02000000u

PTEST(every_region_is_linked_at_its_base_in_whole_pages)
{
    for (size_t i = 0; i < EXPECTED_REGION_COUNT; i++) {
        const struct host_span span = host_memory_region(expected_regions[i].id);
        PT_ASSERT(span.start == expected_regions[i].base);
        PT_ASSERT(span.end - span.start == expected_regions[i].linked_size);
    }
}

PTEST(a_value_that_names_no_region_has_no_memory)
{
    const struct host_span none = host_memory_region(GBA_REGION_NONE);
    const struct host_span past = host_memory_region(GBA_REGION_COUNT);
    PT_ASSERT(none.start == 0 && none.end == 0);
    PT_ASSERT(past.start == 0 && past.end == 0);
}

PTEST(the_call_stack_is_linked_below_the_regions)
{
    const struct host_span stack = host_memory_call_stack();
    PT_ASSERT(stack.start >= FREE_PAGE_ADDRESS + PAGE);
    PT_ASSERT(stack.start % PAGE == 0);
    PT_ASSERT(stack.end - stack.start == MIB);
    PT_ASSERT(stack.end <= LOWEST_REGION_BASE);
}

static volatile uint8_t *byte_at(uintptr_t address)
{
    return (volatile uint8_t *)address;
}

PTEST(a_fixed_page_is_mapped_zero_filled_and_writable)
{
    PT_ASSERT_EQ_INT(host_memory_map_fixed(FREE_PAGE_ADDRESS, PAGE), 0);
    const bool zero = *byte_at(FREE_PAGE_ADDRESS) == 0 && *byte_at(FREE_PAGE_ADDRESS + PAGE - 1u) == 0;
    *byte_at(FREE_PAGE_ADDRESS + PAGE - 1u) = 0xA5u;
    const bool written = *byte_at(FREE_PAGE_ADDRESS + PAGE - 1u) == 0xA5u;
    const int unmapped = host_memory_unmap_fixed(FREE_PAGE_ADDRESS, PAGE);
    PT_ASSERT(zero);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(unmapped, 0);
}

PTEST(mapping_over_a_mapped_page_is_refused_and_leaves_it)
{
    PT_ASSERT_EQ_INT(host_memory_map_fixed(FREE_PAGE_ADDRESS, PAGE), 0);
    *byte_at(FREE_PAGE_ADDRESS) = 0x5Au;
    const int again = host_memory_map_fixed(FREE_PAGE_ADDRESS, PAGE);
    const bool kept = *byte_at(FREE_PAGE_ADDRESS) == 0x5Au;
    const int unmapped = host_memory_unmap_fixed(FREE_PAGE_ADDRESS, PAGE);
    const int after_unmap = host_memory_map_fixed(FREE_PAGE_ADDRESS, PAGE);
    const bool fresh = after_unmap == 0 && *byte_at(FREE_PAGE_ADDRESS) == 0;
    const int unmapped_again = after_unmap == 0 ? host_memory_unmap_fixed(FREE_PAGE_ADDRESS, PAGE) : 0;
    PT_ASSERT_EQ_INT(again, -1);
    PT_ASSERT(kept);
    PT_ASSERT_EQ_INT(unmapped, 0);
    PT_ASSERT(fresh);
    PT_ASSERT_EQ_INT(unmapped_again, 0);
}

PTEST(mapping_over_a_linked_region_is_refused_and_leaves_it)
{
    volatile uint8_t *iwram = byte_at(GBA_IWRAM_BASE);
    const uint8_t before = *iwram;
    *iwram = 0x3Cu;
    const int mapped = host_memory_map_fixed(GBA_IWRAM_BASE, PAGE);
    const bool kept = *iwram == 0x3Cu;
    *iwram = before;
    PT_ASSERT_EQ_INT(mapped, -1);
    PT_ASSERT(kept);
}

PTEST_MAIN()
