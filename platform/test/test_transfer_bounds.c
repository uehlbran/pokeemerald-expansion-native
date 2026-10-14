// deps: gba_memmap.c
/*
 * Tests for transfer_bounds: which byte ranges the game may read or write.
 * Expected values come from GBATEK "GBA Memory Map" (reads from any mapped
 * region, writes to RAM) and from the module's header comment (the native
 * image and stack). The native heap is deliberately not accessible. The two
 * tests that read the image's and the stack's edges from Linux itself
 * (glibc's linker symbols, pthread_getattr_np) are in
 * linux/test_transfer_bounds_linux.c.
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
    /* cppcheck-suppress constVariablePointer ; a function pointer has no const pointee to add, and volatile keeps the load */
    int (*volatile fn)(void) = code_marker;
    uintptr_t address = 0;
    memcpy(&address, (const void *)&fn, sizeof address);
    return address;
}

#define TRANSFER_FIRST_PAGE_BOUNDARY ((uintptr_t)0x1000) /* the first byte after the refused page */

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

/* Literal GBATEK "GBA Memory Map" addresses, deliberately not gba_memmap's
 * constants, so a wrong constant there is caught here. The mGBA debug page is
 * not GBATEK (see gba_memmap.h). */
static const struct {
    uintptr_t first;
    uintptr_t last;
} literal_regions[] = {
    {0x02000000u, 0x0203FFFFu}, /* EWRAM */
    {0x03000000u, 0x03007FFFu}, /* IWRAM */
    {0x04000000u, 0x040003FFu}, /* I/O */
    {0x04FFF000u, 0x04FFFFFFu}, /* mGBA debug page */
    {0x05000000u, 0x050003FFu}, /* palette RAM */
    {0x06000000u, 0x06017FFFu}, /* VRAM */
    {0x07000000u, 0x070003FFu}, /* OAM */
    {0x0E000000u, 0x0E01FFFFu}, /* SRAM / flash, 128 KiB chip */
};

PTEST(every_gba_region_edges_by_literal_address)
{
    start();
    for (size_t r = 0; r < sizeof literal_regions / sizeof literal_regions[0]; r++) {
        uintptr_t first = literal_regions[r].first;
        uintptr_t last = literal_regions[r].last;
        size_t size = (size_t)(last - first) + 1;
        for (int a = TRANSFER_READ; a <= TRANSFER_WRITE; a++) {
            enum transfer_access access = (enum transfer_access)a;
            PT_ASSERT(transfer_bounds_ok(first, 1, access));
            PT_ASSERT(transfer_bounds_ok(last, 1, access));
            PT_ASSERT(transfer_bounds_ok(first, size, access));
            PT_ASSERT(!transfer_bounds_ok(first, size + 1, access));
            PT_ASSERT(!transfer_bounds_ok(last, 2, access));
            PT_ASSERT(!transfer_bounds_ok(first - 1, 2, access));
            PT_ASSERT(!transfer_bounds_ok(last + 1, 1, access) || last + 1 == 0x05000000u);
            PT_ASSERT(!transfer_bounds_ok(first - 1, 1, access) || first == 0x05000000u);
        }
    }
    transfer_bounds_shutdown();
}

PTEST(adjacent_regions_are_not_one_range)
{
    start();
    /* mGBA debug page ends at 04FFFFFF, palette RAM starts at 05000000 */
    PT_ASSERT(!transfer_bounds_ok(0x04FFFFFFu, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(0x04FFFFFFu, 2, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(0x04FFF000u, 0x1001u, TRANSFER_READ));
    /* I/O ends at 040003FF; nothing is mapped until the debug page */
    PT_ASSERT(!transfer_bounds_ok(0x040003FFu, 2, TRANSFER_WRITE));
    /* palette RAM ends at 050003FF; VRAM is far above */
    PT_ASSERT(!transfer_bounds_ok(0x050003FFu, 2, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(0x050003FFu, 2, TRANSFER_WRITE));
    PT_ASSERT(!transfer_bounds_ok(0x05000000u, 0x401u, TRANSFER_READ));
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

PTEST(the_first_page_boundary_is_refused_on_both_sides)
{
    start();
    PT_ASSERT(!transfer_bounds_ok(TRANSFER_FIRST_PAGE_BOUNDARY - 1, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(TRANSFER_FIRST_PAGE_BOUNDARY, 1, TRANSFER_READ));
    PT_ASSERT(!transfer_bounds_ok(TRANSFER_FIRST_PAGE_BOUNDARY, 1, TRANSFER_WRITE));
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

/* Under ASan's use-after-return detection this local lives in the fake stack,
 * so this proves that path. The real thread stack is tested separately,
 * independent of ASan, by real_stack_mapping_edges. */
PTEST(stack_locals_are_readable_and_writable)
{
    uint8_t local[BYTE_COUNT] = {0};
    start();
    PT_ASSERT(transfer_bounds_ok(address_of(local), sizeof local, TRANSFER_READ));
    PT_ASSERT(transfer_bounds_ok(address_of(local), sizeof local, TRANSFER_WRITE));
    PT_ASSERT(local[0] == 0);
    transfer_bounds_shutdown();
}

/* gba_regions.ld's .platform_call_stack, where game_hooks runs the battle
 * test runner's functions, whose local buffers the game copies with DMA and
 * CpuSet like any other stack's. */
extern char platform_call_stack_start[], platform_call_stack_end[];

PTEST(the_battle_runners_call_stack_is_readable_and_writable_to_its_edges)
{
    uintptr_t low = address_of(platform_call_stack_start);
    size_t size = (size_t)(address_of(platform_call_stack_end) - low);
    start();
    bool whole = transfer_bounds_ok(low, size, TRANSFER_WRITE) && transfer_bounds_ok(low, size, TRANSFER_READ);
    bool past_end = transfer_bounds_ok(low + size - 1, 2, TRANSFER_WRITE);
    bool before = transfer_bounds_ok(low - 1, 2, TRANSFER_READ);
    transfer_bounds_shutdown();
    PT_ASSERT(size > 0);
    PT_ASSERT(whole);
    PT_ASSERT(!past_end);
    PT_ASSERT(!before);
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
