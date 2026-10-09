/*
 * Tests of gba_memmap. The expected addresses and sizes are written out here
 * from GBATEK "GBA Memory Map" (and mGBA src/gba/io.c for the debug page)
 * instead of being read from the header, so the header's constants are under
 * test too, and they are checked against expansion's own at compile time.
 *
 * Writes into the regions restore the old contents before asserting, so the
 * zero-fill checks hold whatever order the tests run in.
 */
#include <gba/defines.h>
#include <gba/io_reg.h>

#include "gba_memmap.h"
#include "ptest.h"

/* The header agrees with expansion (include/gba/defines.h, include/gba/io_reg.h). */
_Static_assert(GBA_EWRAM_BASE == EWRAM_START, "EWRAM base is not expansion's EWRAM_START");
_Static_assert(GBA_EWRAM_BASE + GBA_EWRAM_SIZE == EWRAM_END, "EWRAM size disagrees with EWRAM_END");
_Static_assert(GBA_IWRAM_BASE == IWRAM_START, "IWRAM base is not expansion's IWRAM_START");
_Static_assert(GBA_IWRAM_BASE + GBA_IWRAM_SIZE == IWRAM_END, "IWRAM size disagrees with IWRAM_END");
_Static_assert(GBA_IO_BASE == REG_BASE, "I/O base is not expansion's REG_BASE");
_Static_assert(GBA_PALETTE_RAM_BASE == PLTT, "palette RAM base is not expansion's PLTT");
_Static_assert(GBA_PALETTE_RAM_SIZE == PLTT_SIZE, "palette RAM size is not expansion's PLTT_SIZE");
_Static_assert(GBA_VRAM_BASE == VRAM, "VRAM base is not expansion's VRAM");
_Static_assert(GBA_VRAM_SIZE == VRAM_SIZE, "VRAM size is not expansion's VRAM_SIZE");
_Static_assert(GBA_OAM_BASE == OAM, "OAM base is not expansion's OAM");
_Static_assert(GBA_OAM_SIZE == OAM_SIZE, "OAM size is not expansion's OAM_SIZE");

#define KIB 1024u

struct expected_region {
    enum gba_region id;
    uintptr_t base;
    size_t size;
    const char *name;
};

static const struct expected_region expected_regions[] = {
    {GBA_REGION_EWRAM, 0x02000000u, 256 * KIB, "EWRAM"},
    {GBA_REGION_IWRAM, 0x03000000u, 32 * KIB, "IWRAM"},
    {GBA_REGION_IO, 0x04000000u, 1 * KIB, "I/O"},
    {GBA_REGION_MGBA_DEBUG, 0x04FFF000u, 4 * KIB, "mGBA debug"},
    {GBA_REGION_PALETTE_RAM, 0x05000000u, 1 * KIB, "palette RAM"},
    {GBA_REGION_VRAM, 0x06000000u, 96 * KIB, "VRAM"},
    {GBA_REGION_OAM, 0x07000000u, 1 * KIB, "OAM"},
    {GBA_REGION_SRAM, 0x0E000000u, 128 * KIB, "SRAM"},
};
#define EXPECTED_REGION_COUNT (sizeof expected_regions / sizeof expected_regions[0])
_Static_assert(EXPECTED_REGION_COUNT == GBA_REGION_COUNT - 1, "a region has no expected entry");

/* Fixed IWRAM addresses expansion uses (include/gba/defines.h; the test
 * runner's persistent block from ld_script_test.ld). */
#define SOUND_INFO_PTR_ADDRESS 0x03007FF0u
#define INTR_CHECK_ADDRESS 0x03007FF8u
#define INTR_VECTOR_ADDRESS 0x03007FFCu
#define TEST_RUNNER_PERSISTENT_ADDRESS 0x03007F00u
#define TEST_RUNNER_PERSISTENT_SIZE 0x100u

/* mGBA's debug registers as expansion's test runner uses them (test/test_runner.c). */
#define MGBA_REG_DEBUG_STRING 0x04FFF600u
#define MGBA_REG_DEBUG_STRING_SIZE 0x100u
#define MGBA_REG_DEBUG_FLAGS 0x04FFF700u
#define MGBA_REG_DEBUG_ENABLE 0x04FFF780u
#define MGBA_DEBUG_ENABLE_REQUEST 0xC0DEu /* written to ENABLE to turn the channel on */

/* GBATEK "GBA Memory Map": the last I/O register is the halfword at 040003FE. */
#define LAST_IO_REGISTER_ADDRESS 0x040003FEu
/* GBATEK "GBA Memory Map": Game Pak ROM starts at 08000000; not a mapped region. */
#define GAME_PAK_ROM_ADDRESS 0x08000000u

static const struct expected_region *expected_for(enum gba_region id)
{
    for (size_t i = 0; i < EXPECTED_REGION_COUNT; i++)
        if (expected_regions[i].id == id)
            return &expected_regions[i];
    return NULL;
}

/* The native object at a GBA address: the regions are mapped at their GBA
 * addresses, so the integer-to-pointer conversion is the identity. */
static volatile uint8_t *gba_byte(uintptr_t address)
{
    return (volatile uint8_t *)address;
}

static volatile uint16_t *gba_halfword(uintptr_t address)
{
    return (volatile uint16_t *)address;
}

static volatile uint32_t *gba_word(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

/* ---- per-region checks ---- */

static void check_writable_at_both_ends(enum gba_region id)
{
    const struct expected_region *const region = expected_for(id);
    PT_ASSERT(region != NULL);
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);

    volatile uint8_t *const first = gba_byte(region->base);
    volatile uint8_t *const last = gba_byte(region->base + region->size - 1);
    volatile uint32_t *const first_word = gba_word(region->base);
    const uint8_t saved_first = *first, saved_last = *last;
    *first = 0xA5;
    *last = 0x5A;
    const uint8_t read_first = *first, read_last = *last;
    *first = saved_first;
    *last = saved_last;
    const uint32_t saved_word = *first_word;
    *first_word = UINT32_C(0xC0FFEE42);
    const uint32_t read_word = *first_word;
    *first_word = saved_word;

    PT_ASSERT_EQ_U32(read_first, 0xA5);
    PT_ASSERT_EQ_U32(read_last, 0x5A);
    PT_ASSERT_EQ_U32(read_word, UINT32_C(0xC0FFEE42));
}

static void check_zero_filled(enum gba_region id)
{
    const struct expected_region *const region = expected_for(id);
    PT_ASSERT(region != NULL);
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    size_t first_nonzero = 0;
    while (first_nonzero < region->size && *gba_byte(region->base + first_nonzero) == 0)
        first_nonzero++;
    PT_ASSERT(first_nonzero == region->size);
}

static void check_contains_bounds(enum gba_region id)
{
    const struct expected_region *const region = expected_for(id);
    PT_ASSERT(region != NULL);
    const uintptr_t base = region->base;
    const uintptr_t end = region->base + region->size; /* one past the last byte */

    PT_ASSERT(gba_memmap_contains(base, 1, id));
    PT_ASSERT(gba_memmap_contains(end - 1, 1, id));
    PT_ASSERT(gba_memmap_contains(base, region->size, id));
    PT_ASSERT(!gba_memmap_contains(base, region->size + 1, id));
    PT_ASSERT(!gba_memmap_contains(end, 1, id));
    PT_ASSERT(!gba_memmap_contains(end - 1, 2, id));
    PT_ASSERT(!gba_memmap_contains(base - 1, 1, id));
    PT_ASSERT(!gba_memmap_contains(base - 1, 2, id));
    /* Zero length names no byte. */
    PT_ASSERT(!gba_memmap_contains(base, 0, id));
    PT_ASSERT(!gba_memmap_contains(end - 1, 0, id));
    /* address + length past UINTPTR_MAX: a wrapping sum would land inside. */
    PT_ASSERT(!gba_memmap_contains(base, SIZE_MAX, id));
    PT_ASSERT(!gba_memmap_contains(end - 1, SIZE_MAX, id));
    PT_ASSERT(!gba_memmap_contains(UINTPTR_MAX, 1, id));
    PT_ASSERT(!gba_memmap_contains(UINTPTR_MAX, 2, id));
    /* From address 0, to the end of the region. */
    PT_ASSERT(!gba_memmap_contains(0, 1, id));
    PT_ASSERT(!gba_memmap_contains(0, end, id));
}

static void check_region_of(enum gba_region id)
{
    const struct expected_region *const region = expected_for(id);
    PT_ASSERT(region != NULL);
    const uintptr_t base = region->base;
    const uintptr_t end = region->base + region->size;

    PT_ASSERT_EQ_INT(gba_memmap_region_of(base, 1), id);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(end - 1, 1), id);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(base, region->size), id);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(end - 1, 2), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(base - 1, 2), GBA_REGION_NONE);
    PT_ASSERT(gba_memmap_region_of(base - 1, 1) != id);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(base, 0), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(base, SIZE_MAX), GBA_REGION_NONE);
}

static void check_info(enum gba_region id)
{
    const struct expected_region *const region = expected_for(id);
    const struct gba_region_info *const info = gba_memmap_info(id);
    PT_ASSERT(region != NULL);
    PT_ASSERT(info != NULL);
    PT_ASSERT(info->base == region->base);
    PT_ASSERT(info->size == region->size);
    PT_ASSERT_EQ_STR(info->name, region->name);
}

PTEST(ewram_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_EWRAM);
}
PTEST(iwram_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_IWRAM);
}
PTEST(io_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_IO);
}
PTEST(mgba_debug_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_MGBA_DEBUG);
}
PTEST(palette_ram_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_PALETTE_RAM);
}
PTEST(vram_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_VRAM);
}
PTEST(oam_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_OAM);
}
PTEST(sram_is_writable_at_both_ends)
{
    check_writable_at_both_ends(GBA_REGION_SRAM);
}

PTEST(ewram_is_zero_filled)
{
    check_zero_filled(GBA_REGION_EWRAM);
}
PTEST(iwram_is_zero_filled)
{
    check_zero_filled(GBA_REGION_IWRAM);
}
PTEST(io_is_zero_filled)
{
    check_zero_filled(GBA_REGION_IO);
}
PTEST(mgba_debug_is_zero_filled)
{
    check_zero_filled(GBA_REGION_MGBA_DEBUG);
}
PTEST(palette_ram_is_zero_filled)
{
    check_zero_filled(GBA_REGION_PALETTE_RAM);
}
PTEST(vram_is_zero_filled)
{
    check_zero_filled(GBA_REGION_VRAM);
}
PTEST(oam_is_zero_filled)
{
    check_zero_filled(GBA_REGION_OAM);
}
PTEST(sram_is_zero_filled)
{
    check_zero_filled(GBA_REGION_SRAM);
}

PTEST(ewram_contains_bounds)
{
    check_contains_bounds(GBA_REGION_EWRAM);
}
PTEST(iwram_contains_bounds)
{
    check_contains_bounds(GBA_REGION_IWRAM);
}
PTEST(io_contains_bounds)
{
    check_contains_bounds(GBA_REGION_IO);
}
PTEST(mgba_debug_contains_bounds)
{
    check_contains_bounds(GBA_REGION_MGBA_DEBUG);
}
PTEST(palette_ram_contains_bounds)
{
    check_contains_bounds(GBA_REGION_PALETTE_RAM);
}
PTEST(vram_contains_bounds)
{
    check_contains_bounds(GBA_REGION_VRAM);
}
PTEST(oam_contains_bounds)
{
    check_contains_bounds(GBA_REGION_OAM);
}
PTEST(sram_contains_bounds)
{
    check_contains_bounds(GBA_REGION_SRAM);
}

PTEST(ewram_region_of)
{
    check_region_of(GBA_REGION_EWRAM);
}
PTEST(iwram_region_of)
{
    check_region_of(GBA_REGION_IWRAM);
}
PTEST(io_region_of)
{
    check_region_of(GBA_REGION_IO);
}
PTEST(mgba_debug_region_of)
{
    check_region_of(GBA_REGION_MGBA_DEBUG);
}
PTEST(palette_ram_region_of)
{
    check_region_of(GBA_REGION_PALETTE_RAM);
}
PTEST(vram_region_of)
{
    check_region_of(GBA_REGION_VRAM);
}
PTEST(oam_region_of)
{
    check_region_of(GBA_REGION_OAM);
}
PTEST(sram_region_of)
{
    check_region_of(GBA_REGION_SRAM);
}

PTEST(ewram_info)
{
    check_info(GBA_REGION_EWRAM);
}
PTEST(iwram_info)
{
    check_info(GBA_REGION_IWRAM);
}
PTEST(io_info)
{
    check_info(GBA_REGION_IO);
}
PTEST(mgba_debug_info)
{
    check_info(GBA_REGION_MGBA_DEBUG);
}
PTEST(palette_ram_info)
{
    check_info(GBA_REGION_PALETTE_RAM);
}
PTEST(vram_info)
{
    check_info(GBA_REGION_VRAM);
}
PTEST(oam_info)
{
    check_info(GBA_REGION_OAM);
}
PTEST(sram_info)
{
    check_info(GBA_REGION_SRAM);
}

/* ---- init and shutdown ---- */

PTEST(io_accessors_address_the_register_at_its_gba_offset)
{
    PT_ASSERT_EQ_U32((uint32_t)(uintptr_t)gba_io_u8(0), GBA_IO_BASE);
    PT_ASSERT_EQ_U32((uint32_t)(uintptr_t)gba_io_u16(GBA_SOUNDCNT_H_OFFSET), GBA_IO_BASE + GBA_SOUNDCNT_H_OFFSET);
    PT_ASSERT_EQ_U32((uint32_t)(uintptr_t)gba_io_u32(GBA_IO_SIZE - 4), GBA_IO_BASE + GBA_IO_SIZE - 4);
    PT_ASSERT_EQ_U32(sizeof *gba_io_u8(0), 1);
    PT_ASSERT_EQ_U32(sizeof *gba_io_u16(0), 2);
    PT_ASSERT_EQ_U32(sizeof *gba_io_u32(0), 4);
}

PTEST(io_accessors_of_different_widths_see_the_same_bytes_little_endian)
{
    const uint32_t word = GBA_SOUNDCNT_H_OFFSET - sizeof(uint16_t); /* SOUNDCNT_L, 4-byte aligned */
    *gba_io_u32(word) = UINT32_C(0xA1B2C3D4);
    PT_ASSERT_EQ_U32(*gba_io_u16(word), 0xC3D4);
    PT_ASSERT_EQ_U32(*gba_io_u16(word + 2), 0xA1B2);
    PT_ASSERT_EQ_U32(*gba_io_u8(word), 0xD4);
    PT_ASSERT_EQ_U32(*gba_io_u8(word + 3), 0xA1);
    *gba_io_u32(word) = 0;
}

PTEST(init_succeeds)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
}

PTEST(init_is_idempotent_and_keeps_contents)
{
    volatile uint8_t *const marker = gba_byte(GBA_IWRAM_BASE);
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    const uint8_t saved = *marker;
    *marker = 0x77;
    const int second = gba_memmap_init();
    const uint8_t after_second = *marker;
    *marker = saved;
    PT_ASSERT_EQ_INT(second, GBA_MEMMAP_OK);
    PT_ASSERT_EQ_U32(after_second, 0x77);
}

/* init fails only on a wrong link layout, which test_gba_memmap_layout.c
 * covers; it acquires nothing either way, so shutdown after a failed init is
 * the same call as shutdown without init, covered here with repeated shutdown. */
PTEST(shutdown_is_safe_without_init_and_when_repeated)
{
    gba_memmap_shutdown();
    gba_memmap_shutdown();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    gba_memmap_shutdown();
    gba_memmap_shutdown();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
}

PTEST(regions_stay_usable_after_shutdown_and_init)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    gba_memmap_shutdown();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    volatile uint8_t *const last_vram = gba_byte(GBA_VRAM_BASE + GBA_VRAM_SIZE - 1);
    const uint8_t saved = *last_vram;
    *last_vram = 0x3C;
    const uint8_t read = *last_vram;
    *last_vram = saved;
    PT_ASSERT_EQ_U32(read, 0x3C);
}

/* ---- the map as a whole ---- */

PTEST(unknown_region_ids_are_rejected)
{
    const enum gba_region past_the_end = (enum gba_region)(GBA_REGION_COUNT + 5);
    PT_ASSERT(!gba_memmap_contains(GBA_EWRAM_BASE, 1, GBA_REGION_NONE));
    PT_ASSERT(!gba_memmap_contains(GBA_EWRAM_BASE, 1, GBA_REGION_COUNT));
    PT_ASSERT(!gba_memmap_contains(GBA_EWRAM_BASE, 1, past_the_end));
    PT_ASSERT(gba_memmap_info(GBA_REGION_NONE) == NULL);
    PT_ASSERT(gba_memmap_info(GBA_REGION_COUNT) == NULL);
    PT_ASSERT(gba_memmap_info(past_the_end) == NULL);
}

PTEST(addresses_outside_every_region_have_no_region)
{
    PT_ASSERT_EQ_INT(gba_memmap_region_of(0, 1), GBA_REGION_NONE); /* BIOS */
    PT_ASSERT_EQ_INT(gba_memmap_region_of(GBA_IO_BASE + GBA_IO_SIZE, 1), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(GAME_PAK_ROM_ADDRESS, 1), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(UINTPTR_MAX, 1), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(0, SIZE_MAX), GBA_REGION_NONE);
}

PTEST(ranges_spanning_two_regions_have_no_region)
{
    /* EWRAM through the gap into IWRAM. */
    const size_t ewram_to_iwram = GBA_IWRAM_BASE + 1 - GBA_EWRAM_BASE;
    PT_ASSERT_EQ_INT(gba_memmap_region_of(GBA_EWRAM_BASE, ewram_to_iwram), GBA_REGION_NONE);
    /* The mGBA debug page ends where palette RAM begins: adjacent regions. */
    const uintptr_t last_debug_byte = GBA_MGBA_DEBUG_BASE + GBA_MGBA_DEBUG_SIZE - 1;
    PT_ASSERT(last_debug_byte + 1 == GBA_PALETTE_RAM_BASE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(last_debug_byte, 2), GBA_REGION_NONE);
    PT_ASSERT(!gba_memmap_contains(last_debug_byte, 2, GBA_REGION_MGBA_DEBUG));
    PT_ASSERT(!gba_memmap_contains(last_debug_byte, 2, GBA_REGION_PALETTE_RAM));
    PT_ASSERT_EQ_INT(gba_memmap_region_of(last_debug_byte, 1), GBA_REGION_MGBA_DEBUG);
}

PTEST(regions_do_not_overlap)
{
    for (int a = GBA_REGION_NONE + 1; a < GBA_REGION_COUNT; a++) {
        for (int b = a + 1; b < GBA_REGION_COUNT; b++) {
            const struct gba_region_info *const ra = gba_memmap_info((enum gba_region)a);
            const struct gba_region_info *const rb = gba_memmap_info((enum gba_region)b);
            PT_ASSERT(ra != NULL && rb != NULL);
            PT_ASSERT(ra->base + ra->size <= rb->base || rb->base + rb->size <= ra->base);
        }
    }
}

PTEST(last_io_register_is_in_io)
{
    PT_ASSERT_EQ_INT(gba_memmap_region_of(LAST_IO_REGISTER_ADDRESS, 2), GBA_REGION_IO);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(LAST_IO_REGISTER_ADDRESS + 1, 2), GBA_REGION_NONE);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(REG_ADDR_DISPCNT, 2), GBA_REGION_IO);
}

PTEST(mgba_debug_registers_are_in_the_debug_page_not_io)
{
    PT_ASSERT_EQ_INT(gba_memmap_region_of(MGBA_REG_DEBUG_STRING, MGBA_REG_DEBUG_STRING_SIZE), GBA_REGION_MGBA_DEBUG);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(MGBA_REG_DEBUG_FLAGS, 2), GBA_REGION_MGBA_DEBUG);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(MGBA_REG_DEBUG_ENABLE, 2), GBA_REGION_MGBA_DEBUG);
    PT_ASSERT(!gba_memmap_contains(MGBA_REG_DEBUG_ENABLE, 2, GBA_REGION_IO));
    PT_ASSERT(!gba_memmap_contains(GBA_IO_BASE, 2, GBA_REGION_MGBA_DEBUG));
}

PTEST(mgba_debug_page_and_io_page_are_separate_memory)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    volatile uint16_t *const enable = gba_halfword(MGBA_REG_DEBUG_ENABLE);
    volatile uint16_t *const dispcnt = gba_halfword(REG_ADDR_DISPCNT);
    const uint16_t saved_enable = *enable, saved_dispcnt = *dispcnt;
    *dispcnt = 0;
    *enable = MGBA_DEBUG_ENABLE_REQUEST;
    const uint16_t dispcnt_after_enable = *dispcnt;
    *dispcnt = 0x1234;
    const uint16_t enable_after_dispcnt = *enable;
    *enable = saved_enable;
    *dispcnt = saved_dispcnt;
    PT_ASSERT_EQ_U32(dispcnt_after_enable, 0);
    PT_ASSERT_EQ_U32(enable_after_dispcnt, MGBA_DEBUG_ENABLE_REQUEST);
}

PTEST(fixed_iwram_addresses_are_in_iwram)
{
    PT_ASSERT_EQ_INT(gba_memmap_region_of(SOUND_INFO_PTR_ADDRESS, 4), GBA_REGION_IWRAM);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(INTR_CHECK_ADDRESS, 2), GBA_REGION_IWRAM);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(INTR_VECTOR_ADDRESS, 4), GBA_REGION_IWRAM);
    PT_ASSERT_EQ_INT(gba_memmap_region_of(TEST_RUNNER_PERSISTENT_ADDRESS, TEST_RUNNER_PERSISTENT_SIZE),
                     GBA_REGION_IWRAM);
}

/* EWRAM_DATA is section ".sbss" (include/gba/defines.h); gba_regions.ld puts
 * it in EWRAM, as the native game link will. */
static uint32_t ewram_data_probe __attribute__((section(".sbss")));

PTEST(ewram_data_lands_in_ewram)
{
    /* Pointer to integer: needed to ask which region holds the object. */
    const uintptr_t address = (uintptr_t)&ewram_data_probe;
    PT_ASSERT_EQ_INT(gba_memmap_region_of(address, sizeof ewram_data_probe), GBA_REGION_EWRAM);
    PT_ASSERT_EQ_U32(ewram_data_probe, 0);
    ewram_data_probe = UINT32_C(0x600DF00D);
    const uint32_t through_address = *gba_word(address);
    ewram_data_probe = 0;
    PT_ASSERT_EQ_U32(through_address, UINT32_C(0x600DF00D));
}

PTEST_MAIN()
