// deps: gba_memmap.c
/*
 * Tests for transfer_bounds: which byte ranges the game may read or write.
 * Expected values come from GBATEK "GBA Memory Map" (reads from any mapped
 * region, writes to RAM) and from the module's header comment (the native
 * image and stack). The native heap is deliberately not accessible.
 */
#include "ptest.h"
#include "gba_memmap.h"
#include "transfer_bounds.h"

#include <stdlib.h>
#include <string.h>

#define NULL_PAGE_SIZE 4096u
#define BYTE_COUNT 16u

static const uint8_t rodata_bytes[BYTE_COUNT] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static uint8_t bss_bytes[BYTE_COUNT];
static uint8_t data_bytes[BYTE_COUNT] = {1};

static int code_marker(void)
{
    return 7;
}

static uintptr_t code_address(void)
{
    int (*volatile fn)(void) = code_marker;
    uintptr_t address = 0;
    memcpy(&address, (const void *)&fn, sizeof address);
    return address;
}

static uintptr_t address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

static void start(void)
{
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

PTEST(before_init_everything_is_refused)
{
    transfer_bounds_shutdown();
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(address_of(rodata_bytes), 1, TRANSFER_READ));
}

PTEST(shutdown_twice_and_without_init_is_safe)
{
    transfer_bounds_shutdown();
    transfer_bounds_shutdown();
    start();
    transfer_bounds_shutdown();
    transfer_bounds_shutdown();
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, 1, TRANSFER_READ));
}

PTEST(init_twice_is_fine)
{
    start();
    start();
    PT_ASSERT(transfer_bounds_ok(GBA_EWRAM_BASE, 1, TRANSFER_READ));
    transfer_bounds_shutdown();
}

PTEST(every_gba_region_first_and_last_byte_and_whole)
{
    start();
    for (int r = GBA_REGION_EWRAM; r < GBA_REGION_COUNT; r++) {
        const struct gba_region_info *info = gba_memmap_info((enum gba_region)r);
        PT_ASSERT(info != NULL);
        for (int a = TRANSFER_READ; a <= TRANSFER_WRITE; a++) {
            enum transfer_access access = (enum transfer_access)a;
            PT_ASSERT(transfer_bounds_ok(info->base, 1, access));
            PT_ASSERT(transfer_bounds_ok(info->base + info->size - 1, 1, access));
            PT_ASSERT(transfer_bounds_ok(info->base, info->size, access));
            PT_ASSERT(!transfer_bounds_ok(info->base, info->size + 1, access));
            /* the byte after the region is refused unless another region starts there */
            PT_ASSERT(transfer_bounds_ok(info->base + info->size, 1, access)
                      == (gba_memmap_region_of(info->base + info->size, 1) != GBA_REGION_NONE));
            PT_ASSERT(transfer_bounds_ok(info->base - 1, 1, access)
                      == (gba_memmap_region_of(info->base - 1, 1) != GBA_REGION_NONE));
        }
    }
    transfer_bounds_shutdown();
}

PTEST(range_straddling_a_region_end_is_refused)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE + GBA_EWRAM_SIZE - 1, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - 2, 4, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(GBA_VRAM_BASE + GBA_VRAM_SIZE - 1, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(GBA_SRAM_BASE + GBA_SRAM_SIZE - 1, 2, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

PTEST(length_zero_is_refused)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, 0, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, 0, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(address_of(bss_bytes), 0, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

PTEST(address_plus_length_overflow_is_refused)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(UINTPTR_MAX, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(UINTPTR_MAX - 3, 8, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(GBA_EWRAM_BASE, SIZE_MAX, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(address_of(bss_bytes), SIZE_MAX, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

PTEST(address_zero_and_first_page_are_refused)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(0, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(0, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(1, 4, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(NULL_PAGE_SIZE - 1, 1, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(0, GBA_EWRAM_BASE + 1u, TRANSFER_READ));
    transfer_bounds_shutdown();
}

PTEST(rodata_is_readable_not_writable)
{
    start();
    PT_ASSERT(transfer_bounds_ok(address_of(rodata_bytes), sizeof rodata_bytes, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(address_of(rodata_bytes), sizeof rodata_bytes, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

PTEST(code_is_readable_not_writable)
{
    start();
    PT_ASSERT(transfer_bounds_ok(code_address(), 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(code_address(), 1, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

PTEST(mutable_globals_are_readable_and_writable)
{
    start();
    PT_ASSERT(transfer_bounds_ok(address_of(bss_bytes), sizeof bss_bytes, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(address_of(bss_bytes), sizeof bss_bytes, TRANSFER_WRITE));
    PT_ASSERT(transfer_bounds_ok(address_of(data_bytes), sizeof data_bytes, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(address_of(data_bytes), sizeof data_bytes, TRANSFER_WRITE));
    transfer_bounds_shutdown();
}

/* no_sanitize_address: with AddressSanitizer's use-after-return (on by default
 * in GCC 13's runtime) a local lives on a heap-allocated "fake stack", not on
 * the thread stack this module bounds. This function's local must be real. */
__attribute__((no_sanitize_address))
PTEST(stack_locals_are_readable_and_writable)
{
    uint8_t local[BYTE_COUNT] = {0};
    start();
    PT_ASSERT(transfer_bounds_ok(address_of(local), sizeof local, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(address_of(local), sizeof local, TRANSFER_WRITE));
    PT_ASSERT(local[0] == 0);
    transfer_bounds_shutdown();
}

PTEST(native_heap_is_neither_readable_nor_writable)
{
    uint8_t *heap = malloc(BYTE_COUNT);
    PT_ASSERT(heap != NULL);
    start();
    bool read_ok = transfer_bounds_ok(address_of(heap), BYTE_COUNT, TRANSFER_READ);
    bool write_ok = transfer_bounds_ok(address_of(heap), BYTE_COUNT, TRANSFER_WRITE);
    transfer_bounds_shutdown();
    free(heap);
    PT_ASSERT(!read_ok);
    PT_ASSERT(!write_ok);
}

PTEST(range_running_off_the_image_end_is_refused)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(address_of(bss_bytes), (size_t)1 << 24, TRANSFER_READ));
    transfer_bounds_shutdown();
}

PTEST_MAIN()
