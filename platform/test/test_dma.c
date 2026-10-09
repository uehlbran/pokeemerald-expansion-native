// deps: gba_memmap.c transfer_bounds.c
/*
 * Tests of dma, written from GBATEK "GBA DMA Transfers": unit counts and the
 * "0 means maximum" rule, 16/32-bit units, destination increment, decrement,
 * fixed and increment/reload, source increment, decrement, fixed and the
 * prohibited value, repeat (count reloaded, destination reloaded on
 * request, source carrying on), start timing, the IRQ bit, the enable bit,
 * the registers the hardware leaves alone; and from the native policy in
 * dma.h (any alignment, transfer_bounds, special timing, refusals).
 * Registers and control bits are expansion's own REG_DMAx and DMA_* names
 * (include/gba/io_reg.h), so the module is checked against the game's view.
 * The raise function is a counting fake, so the interrupt controller is not
 * linked. Every test starts from zeroed EWRAM, VRAM and I/O.
 */
#include <gba/types.h>
#include <gba/defines.h>
#include <gba/io_reg.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dma.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

_Static_assert(DMA_TIMING_VBLANK == DMA_START_VBLANK, "dma.h's VBlank timing is not expansion's DMA_START_VBLANK");
_Static_assert(DMA_TIMING_HBLANK == DMA_START_HBLANK, "dma.h's HBlank timing is not expansion's DMA_START_HBLANK");

/* GBATEK "GBA DMA Transfers": CNT_L 0 is 4000h units on DMA0-2, 10000h on DMA3. */
#define DMA0_MAX_UNITS 0x4000u
#define DMA3_MAX_UNITS 0x10000u
#define UNIT16 2u
#define UNIT32 4u
/* GBATEK: source address control 3 is "Prohibited". */
#define DMA_SRC_PROHIBITED (DMA_SRC_DEC | DMA_SRC_FIXED)
/* GBATEK: the DMA registers run from 40000B0h to 40000DFh. */
#define DMA_REGISTERS_FIRST 0xB0u
#define DMA_REGISTERS_END 0xE0u
#define GUARD 0xA5u
#define DISPLAY_LINES 160u

#define CONTROL(cnt_h, cnt_l) (((uint32_t)(cnt_h) << 16) | (uint32_t)(cnt_l))
/* CNT_H values the tests build on. */
#define NOW16 (DMA_ENABLE | DMA_START_NOW | DMA_16BIT)
#define NOW32 (DMA_ENABLE | DMA_START_NOW | DMA_32BIT)
#define HBLANK16 (DMA_ENABLE | DMA_START_HBLANK | DMA_16BIT)
#define HBLANK_REPEAT16 (HBLANK16 | DMA_REPEAT)
#define VBLANK32 (DMA_ENABLE | DMA_START_VBLANK | DMA_32BIT)
#define COPY16 (NOW16 | DMA_SRC_INC | DMA_DEST_INC)
#define COPY32 (NOW32 | DMA_SRC_INC | DMA_DEST_INC)

static unsigned raised[GBA_IRQ_GAMEPAK + 1];

static void fake_raise(enum gba_irq source)
{
    raised[source]++;
}

static unsigned raised_total(void)
{
    unsigned total = 0;
    for (size_t i = 0; i < sizeof raised / sizeof raised[0]; i++)
        total += raised[i];
    return total;
}

static uint8_t *region_at(uint32_t base, size_t offset)
{
    return (uint8_t *)(uintptr_t)(base + offset);
}

static uint8_t *ewram(size_t offset)
{
    return region_at(GBA_EWRAM_BASE, offset);
}

static uint8_t *vram(size_t offset)
{
    return region_at(GBA_VRAM_BASE, offset);
}

static uint8_t *io(size_t offset)
{
    return region_at(GBA_IO_BASE, offset);
}

/* An I/O register's address as the pointer dma_set() takes. */
static void *reg(uint32_t address)
{
    return (void *)(uintptr_t)address;
}

static uint32_t address_of(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer; /* the low 32 bits, as SAD and DAD hold */
}

static uint16_t read16(const uint8_t *p)
{
    uint16_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

static uint32_t read32(const uint8_t *p)
{
    uint32_t value;
    memcpy(&value, p, sizeof value);
    return value;
}

static void write16(uint8_t *p, uint16_t value)
{
    memcpy(p, &value, sizeof value);
}

/* Bytes that differ from their neighbours at every stride a wrong unit or
 * offset would produce. */
static void fill_pattern(uint8_t *p, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++)
        p[i] = (uint8_t)(((uint32_t)i * UINT32_C(2654435761) + seed) >> 24);
}

static bool all_equal(const uint8_t *p, size_t n, uint8_t value)
{
    for (size_t i = 0; i < n; i++) {
        if (p[i] != value)
            return false;
    }
    return true;
}

/* True if every VRAM byte outside [offset, offset + length) still holds GUARD. */
static bool vram_guarded_outside(size_t offset, size_t length)
{
    return all_equal(vram(0), offset, GUARD)
           && all_equal(vram(offset + length), GBA_VRAM_SIZE - offset - length, GUARD);
}

static void reset(void)
{
    memset(ewram(0), 0, GBA_EWRAM_SIZE);
    memset(vram(0), 0, GBA_VRAM_SIZE);
    memset(io(0), 0, GBA_IO_SIZE);
    memset(raised, 0, sizeof raised);
}

static void setup(void)
{
    reset();
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    PT_ASSERT_EQ_INT(dma_init(fake_raise), DMA_OK);
}

static void teardown(void)
{
    dma_shutdown();
    transfer_bounds_shutdown();
    reset();
}

/* ---- init and shutdown ---- */

PTEST(init_rejects_null_raise_and_stays_uninitialised)
{
    reset();
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    fill_pattern(ewram(0), 16, 1);
    PT_ASSERT_EQ_INT(dma_init(NULL), DMA_ERR_NO_RAISE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16, 8)), DMA_ERR_NOT_INITIALISED);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    teardown();
}

PTEST(shutdown_without_init_and_twice_is_safe)
{
    reset();
    dma_shutdown();
    dma_shutdown();
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
    dma_stop(0);
    setup();
    teardown();
    dma_shutdown();
}

PTEST(set_before_init_is_refused_and_touches_no_memory)
{
    dma_shutdown();
    reset();
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    fill_pattern(ewram(0), 16, 1);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16, 8)), DMA_ERR_NOT_INITIALISED);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK_REPEAT16, 1)), DMA_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    teardown();
}

PTEST(shutdown_forgets_armed_channels)
{
    setup();
    fill_pattern(ewram(0), 8, 1);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK_REPEAT16, 1)), DMA_OK);
    dma_shutdown();
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16, 4)), DMA_ERR_NOT_INITIALISED);
    PT_ASSERT_EQ_INT(dma_init(fake_raise), DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    teardown();
}

/* ---- immediate transfers ---- */

PTEST(immediate_16bit_copy_into_vram_is_exact)
{
    const size_t units = 37;
    setup();
    fill_pattern(ewram(0x100), units * UNIT16, 3);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0x100), vram(0x1000), CONTROL(COPY16, units)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x1000), ewram(0x100), units * UNIT16);
    PT_ASSERT(vram_guarded_outside(0x1000, units * UNIT16));
    PT_ASSERT(all_equal(ewram(0), 0x100, 0));
    PT_ASSERT(all_equal(ewram(0x100 + units * UNIT16), 0x100, 0));
    teardown();
}

PTEST(immediate_32bit_copy_into_vram_is_exact)
{
    const size_t units = 29;
    setup();
    fill_pattern(ewram(0x200), units * UNIT32, 5);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0x200), vram(0x2000), CONTROL(COPY32, units)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x2000), ewram(0x200), units * UNIT32);
    PT_ASSERT(vram_guarded_outside(0x2000, units * UNIT32));
    teardown();
}

PTEST(fill_with_fixed_source_on_the_stack_16bit)
{
    const uint16_t value = 0xBEEF;
    const size_t units = 50;
    setup();
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, &value, vram(0x400), CONTROL(NOW16 | DMA_SRC_FIXED | DMA_DEST_INC, units)), DMA_OK);
    for (size_t i = 0; i < units; i++)
        PT_ASSERT_EQ_INT(read16(vram(0x400 + i * UNIT16)), value);
    PT_ASSERT(vram_guarded_outside(0x400, units * UNIT16));
    teardown();
}

PTEST(fill_with_fixed_source_on_the_stack_32bit)
{
    const uint32_t value = 0x12345678u;
    const size_t units = 33;
    setup();
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, &value, vram(0x800), CONTROL(NOW32 | DMA_SRC_FIXED | DMA_DEST_INC, units)), DMA_OK);
    for (size_t i = 0; i < units; i++)
        PT_ASSERT_EQ_U32(read32(vram(0x800 + i * UNIT32)), value);
    PT_ASSERT(vram_guarded_outside(0x800, units * UNIT32));
    teardown();
}

PTEST(source_on_the_stack_is_accepted)
{
    uint8_t local[24];
    setup();
    fill_pattern(local, sizeof local, 9);
    PT_ASSERT_EQ_INT(dma_set(3, local, vram(0x40), CONTROL(COPY16, sizeof local / UNIT16)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x40), local, sizeof local);
    teardown();
}

static const uint16_t rom_table[4] = {0x1111, 0x2222, 0x3333, 0x4444};

PTEST(source_in_the_executables_rodata_is_accepted)
{
    setup();
    PT_ASSERT_EQ_INT(dma_set(3, rom_table, vram(0x60), CONTROL(COPY16, 4)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x60), rom_table, sizeof rom_table);
    teardown();
}

PTEST(count_zero_copies_0x4000_units_on_dma0)
{
    const size_t bytes = DMA0_MAX_UNITS * UNIT16;
    setup();
    fill_pattern(ewram(0), bytes, 11);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0x100), CONTROL(COPY16, 0)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x100), ewram(0), bytes);
    PT_ASSERT(vram_guarded_outside(0x100, bytes));
    teardown();
}

PTEST(dma0_to_dma2_count_field_is_14_bits)
{
    setup();
    fill_pattern(ewram(0), 0x100, 13);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    /* 4001h in a 14-bit field is 1. */
    PT_ASSERT_EQ_INT(dma_set(1, ewram(0), vram(0x10), CONTROL(COPY16, DMA0_MAX_UNITS + 1)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x10), ewram(0), UNIT16);
    PT_ASSERT(vram_guarded_outside(0x10, UNIT16));
    /* 4000h in a 14-bit field is 0, which is 4000h units. */
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    fill_pattern(ewram(0), DMA0_MAX_UNITS * UNIT16, 17);
    PT_ASSERT_EQ_INT(dma_set(2, ewram(0), vram(0), CONTROL(COPY16, DMA0_MAX_UNITS)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0), ewram(0), DMA0_MAX_UNITS * UNIT16);
    PT_ASSERT(vram_guarded_outside(0, DMA0_MAX_UNITS * UNIT16));
    teardown();
}

PTEST(dma3_count_field_is_16_bits)
{
    const size_t units = DMA0_MAX_UNITS + 1;
    setup();
    fill_pattern(ewram(0), units * UNIT16, 19);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16, units)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0), ewram(0), units * UNIT16);
    PT_ASSERT(vram_guarded_outside(0, units * UNIT16));
    teardown();
}

PTEST(count_zero_copies_0x10000_units_on_dma3)
{
    const size_t bytes = DMA3_MAX_UNITS * UNIT16; /* 128 KiB: half of EWRAM */
    setup();
    fill_pattern(ewram(0), bytes, 23);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), ewram(bytes), CONTROL(COPY16, 0)), DMA_OK);
    PT_ASSERT_MEM_EQ(ewram(bytes), ewram(0), bytes);
    teardown();
}

PTEST(count_zero_32bit_fill_on_dma3_covers_all_of_ewram)
{
    const uint32_t value = 0xC0FFEE11u;
    setup();
    PT_ASSERT_EQ_INT(dma_set(3, &value, ewram(0), CONTROL(NOW32 | DMA_SRC_FIXED | DMA_DEST_INC, 0)), DMA_OK);
    for (size_t offset = 0; offset < GBA_EWRAM_SIZE; offset += UNIT32)
        PT_ASSERT_EQ_U32(read32(ewram(offset)), value);
    teardown();
}

PTEST(maximum_count_one_unit_past_the_region_end_is_rejected)
{
    const size_t bytes = DMA3_MAX_UNITS * UNIT16;
    setup();
    fill_pattern(ewram(0), bytes, 29);
    /* Destination [20002h, 40002h): one unit beyond EWRAM. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), ewram(bytes + UNIT16), CONTROL(COPY16, 0)), DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(ewram(bytes), bytes, 0));
    /* Source running one unit past the end of VRAM, on DMA0 with 4000h units. */
    PT_ASSERT_EQ_INT(
        dma_set(0, vram(GBA_VRAM_SIZE - DMA0_MAX_UNITS * UNIT16 + UNIT16), ewram(bytes), CONTROL(COPY16, 0)),
        DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(ewram(bytes), bytes, 0));
    /* The same block one unit lower fits exactly. */
    PT_ASSERT_EQ_INT(dma_set(0, vram(GBA_VRAM_SIZE - DMA0_MAX_UNITS * UNIT16), ewram(bytes), CONTROL(COPY16, 0)),
                     DMA_OK);
    teardown();
}

PTEST(maximum_32bit_fill_one_unit_past_ewram_is_rejected)
{
    const uint32_t value = 0xFFFFFFFFu;
    setup();
    PT_ASSERT_EQ_INT(dma_set(3, &value, ewram(UNIT32), CONTROL(NOW32 | DMA_SRC_FIXED | DMA_DEST_INC, 0)),
                     DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(ewram(0), GBA_EWRAM_SIZE, 0));
    teardown();
}

PTEST(misaligned_source_and_destination_copy_correctly)
{
    setup();
    fill_pattern(ewram(0), 64, 31);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    /* 16-bit units from an odd source to an odd destination. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(1), vram(0x103), CONTROL(COPY16, 5)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x103), ewram(1), 5 * UNIT16);
    PT_ASSERT(vram_guarded_outside(0x103, 5 * UNIT16));
    /* 32-bit units from a 2-aligned source to a 2-aligned destination, as
     * x86 struct layout produces where apcs-gnu would have rounded to 4. */
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(2), vram(0x206), CONTROL(COPY32, 3)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x206), ewram(2), 3 * UNIT32);
    PT_ASSERT(vram_guarded_outside(0x206, 3 * UNIT32));
    /* 32-bit units at odd addresses. */
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(5), vram(0x301), CONTROL(COPY32, 4)), DMA_OK);
    PT_ASSERT_MEM_EQ(vram(0x301), ewram(5), 4 * UNIT32);
    PT_ASSERT(vram_guarded_outside(0x301, 4 * UNIT32));
    teardown();
}

PTEST(misaligned_fixed_source_fill_copies_correctly)
{
    uint8_t local[8];
    setup();
    fill_pattern(local, sizeof local, 37);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, local + 1, vram(0x11), CONTROL(NOW32 | DMA_SRC_FIXED | DMA_DEST_INC, 3)), DMA_OK);
    for (size_t i = 0; i < 3; i++)
        PT_ASSERT_MEM_EQ(vram(0x11 + i * UNIT32), local + 1, UNIT32);
    PT_ASSERT(vram_guarded_outside(0x11, 3 * UNIT32));
    teardown();
}

PTEST(destination_in_rodata_is_rejected)
{
    /* A string literal is a char array in C, stored in the executable's
     * read-only data, which transfer_bounds does not let a transfer write. */
    char *rodata = "read-only bytes";
    const size_t length = strlen(rodata);
    setup();
    PT_ASSERT(!transfer_bounds_ok((uintptr_t)rodata, length, TRANSFER_WRITE));
    fill_pattern(ewram(0), 16, 41);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), rodata, CONTROL(COPY16, 4)), DMA_ERR_BOUNDS);
    PT_ASSERT_EQ_STR(rodata, "read-only bytes");
    teardown();
}

PTEST(native_heap_is_rejected_as_source_and_destination)
{
    uint8_t *heap = malloc(16);
    PT_ASSERT(heap != NULL);
    memset(heap, 0x5A, 16);
    setup();
    int as_source = dma_set(3, heap, vram(0), CONTROL(COPY16, 8));
    bool vram_untouched = all_equal(vram(0), 16, 0);
    int as_destination = dma_set(3, ewram(0), heap, CONTROL(COPY16, 8));
    bool heap_untouched = all_equal(heap, 16, 0x5A);
    teardown();
    free(heap);
    PT_ASSERT_EQ_INT(as_source, DMA_ERR_BOUNDS);
    PT_ASSERT(vram_untouched);
    PT_ASSERT_EQ_INT(as_destination, DMA_ERR_BOUNDS);
    PT_ASSERT(heap_untouched);
}

PTEST(destination_decrement_writes_backwards_from_the_start_address)
{
    const uint16_t units[4] = {0xA001, 0xB002, 0xC003, 0xD004};
    setup();
    memcpy(ewram(0), units, sizeof units);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0x106), CONTROL(NOW16 | DMA_SRC_INC | DMA_DEST_DEC, 4)), DMA_OK);
    PT_ASSERT_EQ_INT(read16(vram(0x106)), 0xA001);
    PT_ASSERT_EQ_INT(read16(vram(0x104)), 0xB002);
    PT_ASSERT_EQ_INT(read16(vram(0x102)), 0xC003);
    PT_ASSERT_EQ_INT(read16(vram(0x100)), 0xD004);
    PT_ASSERT(vram_guarded_outside(0x100, 4 * UNIT16));
    teardown();
}

PTEST(source_decrement_reads_backwards_from_the_start_address)
{
    const uint32_t units[3] = {0x11111111u, 0x22222222u, 0x33333333u};
    setup();
    memcpy(ewram(0x40), units, sizeof units);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0x48), vram(0x200), CONTROL(NOW32 | DMA_SRC_DEC | DMA_DEST_INC, 3)), DMA_OK);
    PT_ASSERT_EQ_U32(read32(vram(0x200)), 0x33333333u);
    PT_ASSERT_EQ_U32(read32(vram(0x204)), 0x22222222u);
    PT_ASSERT_EQ_U32(read32(vram(0x208)), 0x11111111u);
    PT_ASSERT(vram_guarded_outside(0x200, 3 * UNIT32));
    teardown();
}

PTEST(decrement_below_the_region_start_is_rejected)
{
    setup();
    fill_pattern(ewram(0), 16, 43);
    /* [VRAM - 4, VRAM + 4): the first two units would land below VRAM. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(UNIT16), CONTROL(NOW16 | DMA_SRC_INC | DMA_DEST_DEC, 4)),
                     DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    PT_ASSERT_EQ_INT(dma_set(3, ewram(UNIT16), vram(0), CONTROL(NOW16 | DMA_SRC_DEC | DMA_DEST_INC, 4)),
                     DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    teardown();
}

PTEST(walks_that_wrap_the_address_space_are_rejected)
{
    setup();
    fill_pattern(ewram(0), 16, 139);
    /* Decrementing from address 2: the block would pass below address 0. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), reg(UNIT16), CONTROL(NOW16 | DMA_DEST_DEC, 4)), DMA_ERR_BOUNDS);
    /* Incrementing from the top of the address space: the block would wrap. */
    PT_ASSERT_EQ_INT(dma_set(3, (const void *)(UINTPTR_MAX - 1), vram(0), CONTROL(COPY16, 4)), DMA_ERR_BOUNDS);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), (void *)(UINTPTR_MAX - 1), CONTROL(COPY16, 4)), DMA_ERR_BOUNDS);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    teardown();
}

PTEST(destination_fixed_writes_every_unit_to_one_address)
{
    const uint16_t units[4] = {0x0102, 0x0304, 0x0506, 0x0708};
    setup();
    memcpy(ewram(0), units, sizeof units);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0x500), CONTROL(NOW16 | DMA_SRC_INC | DMA_DEST_FIXED, 4)), DMA_OK);
    PT_ASSERT_EQ_INT(read16(vram(0x500)), 0x0708);
    PT_ASSERT(vram_guarded_outside(0x500, UNIT16));
    teardown();
}

PTEST(fifo_pattern_fixed_destination_with_repeat_is_accepted_and_runs_once)
{
    const uint32_t samples[4] = {0x01010101u, 0x02020202u, 0x03030303u, 0x04040404u};
    setup();
    memcpy(ewram(0), samples, sizeof samples);
    PT_ASSERT_EQ_INT(
        dma_set(1, ewram(0), reg(REG_ADDR_FIFO_A), CONTROL(NOW32 | DMA_SRC_INC | DMA_DEST_FIXED | DMA_REPEAT, 4)),
        DMA_OK);
    PT_ASSERT_EQ_U32(REG_FIFO_A, 0x04040404u);
    PT_ASSERT_EQ_U32(REG_FIFO_B, 0);
    /* "Immediately" runs once even with the repeat bit: enable is cleared. */
    PT_ASSERT_EQ_INT(REG_DMA1CNT_H & DMA_ENABLE, 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    teardown();
}

PTEST(source_control_prohibited_is_rejected)
{
    setup();
    fill_pattern(ewram(0), 16, 47);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(NOW16 | DMA_SRC_PROHIBITED | DMA_DEST_INC, 4)),
                     DMA_ERR_SOURCE_CONTROL);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK16 | DMA_SRC_PROHIBITED, 4)), DMA_ERR_SOURCE_CONTROL);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    teardown();
}

PTEST(overlapping_blocks_copy_unit_by_unit_in_order)
{
    const uint16_t units[5] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0FED};
    setup();
    memcpy(ewram(0), units, sizeof units);
    /* Forward copy one unit up: each unit read is the one just written. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), ewram(UNIT16), CONTROL(COPY16, 4)), DMA_OK);
    for (size_t i = 0; i < 5; i++)
        PT_ASSERT_EQ_INT(read16(ewram(i * UNIT16)), 0x1234);
    /* Source equal to destination leaves memory as it was. */
    fill_pattern(ewram(0x100), 32, 53);
    memcpy(ewram(0x200), ewram(0x100), 32);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0x100), ewram(0x100), CONTROL(COPY32, 8)), DMA_OK);
    PT_ASSERT_MEM_EQ(ewram(0x100), ewram(0x200), 32);
    teardown();
}

/* ---- HBlank and VBlank timing, repeat, reload ---- */

PTEST(hblank_channel_waits_and_runs_once_without_repeat)
{
    setup();
    fill_pattern(ewram(0), 16, 59);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0x20), CONTROL(HBLANK16 | DMA_SRC_INC | DMA_DEST_INC, 2)), DMA_OK);
    PT_ASSERT(all_equal(vram(0x20), 8, 0));
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
    PT_ASSERT(all_equal(vram(0x20), 8, 0));
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_MEM_EQ(vram(0x20), ewram(0), 2 * UNIT16);
    PT_ASSERT(all_equal(vram(0x24), 4, 0));
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H & DMA_ENABLE, 0);
    fill_pattern(ewram(0), 16, 61);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0x24), 4, 0));
    teardown();
}

PTEST(hblank_repeat_with_reload_resets_destination_and_reloads_count)
{
    uint8_t expected[6 * UNIT16];
    setup();
    fill_pattern(ewram(0), sizeof expected, 67);
    memcpy(expected, ewram(0), sizeof expected);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0x300), CONTROL(HBLANK_REPEAT16 | DMA_SRC_INC | DMA_DEST_RELOAD, 2)),
                     DMA_OK);
    for (size_t run = 0; run < 3; run++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
        /* Two units each time, always at DAD, from where the source stopped. */
        PT_ASSERT_MEM_EQ(vram(0x300), expected + run * 2 * UNIT16, 2 * UNIT16);
        PT_ASSERT(vram_guarded_outside(0x300, 2 * UNIT16));
        PT_ASSERT(REG_DMA0CNT_H & DMA_ENABLE);
    }
    teardown();
}

PTEST(hblank_repeat_without_reload_continues_destination)
{
    setup();
    fill_pattern(ewram(0), 16, 71);
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0x300), CONTROL(HBLANK_REPEAT16 | DMA_SRC_INC | DMA_DEST_INC, 2)),
                     DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_MEM_EQ(vram(0x300), ewram(0), 4 * UNIT16);
    PT_ASSERT(vram_guarded_outside(0x300, 4 * UNIT16));
    teardown();
}

/* src/scanline_effect.c: SCANLINE_EFFECT_DMACNT_16BIT, one register value
 * per line from a table, into a BG offset register. */
PTEST(scanline_effect_frame_writes_one_table_entry_per_hblank)
{
    uint16_t table[DISPLAY_LINES];
    setup();
    for (size_t line = 0; line < DISPLAY_LINES; line++)
        table[line] = (uint16_t)(line * 3 + 7);
    memcpy(ewram(0x1000), table, sizeof table);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0x1000 + UNIT16), reg(REG_ADDR_BG0HOFS),
                             /* SCANLINE_EFFECT_DMACNT_16BIT */
                             CONTROL(HBLANK_REPEAT16 | DMA_SRC_INC | DMA_DEST_INC | DMA_DEST_RELOAD, 1)),
                     DMA_OK);
    for (size_t line = 1; line < DISPLAY_LINES; line++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
        PT_ASSERT_EQ_INT(REG_BG0HOFS, table[line]);
        PT_ASSERT_EQ_INT(REG_BG0VOFS, 0);
    }
    teardown();
}

/* src/battle_transition.c: B_TRANS_DMA_FLAGS, fixed destination. */
PTEST(battle_transition_pattern_writes_next_source_unit_to_one_register)
{
    const uint16_t values[3] = {0x00F0, 0x10E0, 0x20D0};
    setup();
    memcpy(ewram(0x80), values, sizeof values);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0x80), reg(REG_ADDR_WIN0H),
                             /* B_TRANS_DMA_FLAGS */
                             CONTROL(HBLANK_REPEAT16 | DMA_SRC_INC | DMA_DEST_FIXED, 1)),
                     DMA_OK);
    for (size_t i = 0; i < 3; i++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
        PT_ASSERT_EQ_INT(REG_WIN0H, values[i]);
        PT_ASSERT_EQ_INT(REG_WIN1H, 0);
    }
    teardown();
}

PTEST(vblank_channel_runs_only_at_vblank_and_once_without_repeat)
{
    setup();
    fill_pattern(ewram(0), 16, 73);
    PT_ASSERT_EQ_INT(dma_set(1, ewram(0), vram(0x40), CONTROL(VBLANK32 | DMA_SRC_INC | DMA_DEST_INC, 2)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0x40), 8, 0));
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 1);
    PT_ASSERT_MEM_EQ(vram(0x40), ewram(0), 2 * UNIT32);
    PT_ASSERT_EQ_INT(REG_DMA1CNT_H & DMA_ENABLE, 0);
    fill_pattern(ewram(0), 16, 79);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
    PT_ASSERT(all_equal(vram(0x48), 8, 0));
    teardown();
}

PTEST(vblank_repeat_runs_at_every_vblank)
{
    setup();
    fill_pattern(ewram(0), 32, 83);
    PT_ASSERT_EQ_INT(
        dma_set(2, ewram(0), vram(0x80), CONTROL(VBLANK32 | DMA_REPEAT | DMA_SRC_INC | DMA_DEST_RELOAD, 1)), DMA_OK);
    for (size_t frame = 0; frame < 4; frame++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 1);
        PT_ASSERT_MEM_EQ(vram(0x80), ewram(frame * UNIT32), UNIT32);
    }
    PT_ASSERT(all_equal(vram(0x84), 8, 0));
    teardown();
}

PTEST(other_timing_values_run_nothing)
{
    setup();
    fill_pattern(ewram(0), 8, 89);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK_REPEAT16, 1)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_START_NOW), 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_START_SPECIAL), 0);
    PT_ASSERT_EQ_INT(dma_run_timing(0xFFFF), 0);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    teardown();
}

PTEST(channels_run_in_priority_order_dma0_first)
{
    const uint16_t first = 0x1111;
    const uint16_t second = 0x2222;
    setup();
    write16(ewram(0), first);
    write16(ewram(2), second);
    PT_ASSERT_EQ_INT(dma_set(1, ewram(2), vram(0), CONTROL(HBLANK16, 1)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK16, 1)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 2);
    PT_ASSERT_EQ_INT(read16(vram(0)), second);
    teardown();
}

PTEST(hblank_source_running_off_its_region_is_refused_at_that_run)
{
    setup();
    write16(ewram(GBA_EWRAM_SIZE - UNIT16), 0x7777);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(GBA_EWRAM_SIZE - UNIT16), reg(REG_ADDR_BG0HOFS),
                             CONTROL(HBLANK_REPEAT16 | DMA_INTR_ENABLE | DMA_SRC_INC | DMA_DEST_RELOAD, 1)),
                     DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0x7777);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA0], 1);
    /* The next unit is past the end of EWRAM: refused, channel stopped. */
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0x7777);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA0], 1);
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H & DMA_ENABLE, 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    teardown();
}

PTEST(deferred_channel_with_bad_destination_is_refused_when_armed)
{
    char *rodata = "read-only bytes";
    setup();
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), rodata, CONTROL(HBLANK_REPEAT16, 1)), DMA_ERR_BOUNDS);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_STR(rodata, "read-only bytes");
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H & DMA_ENABLE, 0);
    teardown();
}

/* ---- stopping ---- */

PTEST(dma_stop_prevents_a_pending_hblank_transfer)
{
    const uint16_t cnt_h = HBLANK_REPEAT16 | DMA_DEST_RELOAD | DMA_INTR_ENABLE;
    setup();
    fill_pattern(ewram(0), 8, 97);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(cnt_h, 1)), DMA_OK);
    dma_stop(0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    PT_ASSERT_EQ_INT(raised_total(), 0);
    /* What expansion's DmaStop leaves in CNT_H; SAD, DAD, CNT_L untouched. */
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H, cnt_h & ~(DMA_START_MASK | DMA_DREQ_ON | DMA_REPEAT | DMA_ENABLE));
    PT_ASSERT_EQ_INT(REG_DMA0CNT_L, 1);
    PT_ASSERT_EQ_U32(REG_DMA0SAD, address_of(ewram(0)));
    PT_ASSERT_EQ_U32(REG_DMA0DAD, address_of(vram(0)));
    teardown();
}

PTEST(dma_set_without_enable_disarms_the_channel)
{
    setup();
    fill_pattern(ewram(0), 8, 101);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(HBLANK_REPEAT16, 1)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0), CONTROL(DMA_START_HBLANK | DMA_REPEAT, 1)), DMA_OK);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    /* Nothing enabled: an immediate control without the enable bit copies nothing. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16 & ~DMA_ENABLE, 4)), DMA_OK);
    PT_ASSERT(all_equal(vram(0), 8, 0));
    teardown();
}

PTEST(dma_stop_on_an_idle_channel_and_out_of_range_is_harmless)
{
    uint8_t before[DMA_REGISTERS_END - DMA_REGISTERS_FIRST];
    setup();
    dma_stop(3);
    memcpy(before, io(DMA_REGISTERS_FIRST), sizeof before);
    dma_stop(4);
    dma_stop(UINT32_MAX);
    PT_ASSERT_MEM_EQ(io(DMA_REGISTERS_FIRST), before, sizeof before);
    PT_ASSERT(all_equal(io(DMA_REGISTERS_END), 0x20, 0));
    teardown();
}

/* ---- interrupts ---- */

PTEST(irq_raised_once_per_completed_transfer_only_with_the_irq_bit)
{
    setup();
    fill_pattern(ewram(0), 64, 103);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16, 4)), DMA_OK);
    PT_ASSERT_EQ_INT(raised_total(), 0);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(0), CONTROL(COPY16 | DMA_INTR_ENABLE, 4)), DMA_OK);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA3], 1);
    PT_ASSERT_EQ_INT(raised_total(), 1);
    PT_ASSERT_EQ_INT(dma_set(2, ewram(0), vram(0), CONTROL(COPY32 | DMA_INTR_ENABLE, 4)), DMA_OK);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA2], 1);
    PT_ASSERT_EQ_INT(raised_total(), 2);
    /* A refused transfer completes nothing. */
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(GBA_VRAM_SIZE - UNIT16), CONTROL(COPY16 | DMA_INTR_ENABLE, 2)),
                     DMA_ERR_BOUNDS);
    PT_ASSERT_EQ_INT(raised_total(), 2);
    /* An armed channel raises at each completed block, not when armed. */
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0), vram(0x100), CONTROL(HBLANK_REPEAT16 | DMA_INTR_ENABLE | DMA_DEST_RELOAD, 1)),
                     DMA_OK);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA0], 0);
    for (unsigned run = 1; run <= 3; run++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
        PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA0], run);
    }
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_DMA1], 0);
    PT_ASSERT_EQ_INT(raised_total(), 5);
    teardown();
}

/* ---- special timing, channel number ---- */

PTEST(special_timing_is_recorded_and_never_runs)
{
    const uint16_t cnt_h = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT | DMA_DEST_FIXED | DMA_INTR_ENABLE;
    setup();
    fill_pattern(ewram(0), 64, 107);
    /* DMA1/2: sound FIFO; DMA3: video capture; DMA0: prohibited. */
    PT_ASSERT(dma_set(1, ewram(0), reg(REG_ADDR_FIFO_A), CONTROL(cnt_h, 4)) == DMA_ERR_TIMING);
    PT_ASSERT(dma_set(2, ewram(16), reg(REG_ADDR_FIFO_B), CONTROL(cnt_h, 4)) == DMA_ERR_TIMING);
    PT_ASSERT(dma_set(3, ewram(32), vram(0), CONTROL(cnt_h, 4)) == DMA_ERR_TIMING);
    PT_ASSERT(dma_set(0, ewram(48), vram(0x10), CONTROL(cnt_h, 4)) == DMA_ERR_TIMING);
    PT_ASSERT_EQ_INT(REG_DMA1CNT_H, cnt_h);
    PT_ASSERT_EQ_INT(REG_DMA1CNT_L, 4);
    PT_ASSERT_EQ_U32(REG_DMA1SAD, address_of(ewram(0)));
    PT_ASSERT_EQ_U32(REG_DMA1DAD, (uint32_t)REG_ADDR_FIFO_A);
    for (int i = 0; i < 3; i++) {
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
        PT_ASSERT_EQ_INT(dma_run_timing(DMA_START_SPECIAL), 0);
    }
    PT_ASSERT_EQ_U32(REG_FIFO_A, 0);
    PT_ASSERT_EQ_U32(REG_FIFO_B, 0);
    PT_ASSERT(all_equal(vram(0), 32, 0));
    PT_ASSERT_EQ_INT(raised_total(), 0);
    teardown();
}

PTEST(channel_4_and_above_is_rejected_and_writes_nothing)
{
    setup();
    fill_pattern(ewram(0), 16, 109);
    PT_ASSERT_EQ_INT(dma_set(4, ewram(0), vram(0), CONTROL(COPY16, 4)), DMA_ERR_CHANNEL);
    PT_ASSERT_EQ_INT(dma_set(UINT32_MAX, ewram(0), vram(0), CONTROL(COPY16, 4)), DMA_ERR_CHANNEL);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    PT_ASSERT(all_equal(io(DMA_REGISTERS_FIRST), 0x40, 0));
    PT_ASSERT_EQ_INT(raised_total(), 0);
    teardown();
}

/* src/m4a.c writes DMA1 and DMA2 directly for the sound FIFOs; a plain
 * store to the registers must not start anything. */
PTEST(direct_register_writes_start_nothing)
{
    setup();
    fill_pattern(ewram(0), 64, 113);
    REG_DMA1SAD = address_of(ewram(0));
    REG_DMA1DAD = (uint32_t)REG_ADDR_FIFO_A;
    REG_DMA1CNT_H = DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT;
    REG_DMA2CNT = CONTROL(NOW32 | DMA_SRC_INC | DMA_DEST_FIXED, 4);
    REG_DMA0SAD = address_of(ewram(0));
    REG_DMA0DAD = address_of(vram(0));
    REG_DMA0CNT = CONTROL(HBLANK_REPEAT16, 1);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 0);
    PT_ASSERT_EQ_U32(REG_FIFO_A, 0);
    PT_ASSERT(all_equal(vram(0), 16, 0));
    teardown();
}

/* ---- registers the game reads back ---- */

PTEST(registers_hold_what_dma_set_wrote)
{
    const uint32_t control = CONTROL(HBLANK_REPEAT16 | DMA_DEST_RELOAD, 1);
    setup();
    PT_ASSERT_EQ_INT(dma_set(0, ewram(0x10), reg(REG_ADDR_BG0HOFS), control), DMA_OK);
    PT_ASSERT_EQ_U32(REG_DMA0SAD, address_of(ewram(0x10)));
    PT_ASSERT_EQ_U32(REG_DMA0DAD, (uint32_t)REG_ADDR_BG0HOFS);
    PT_ASSERT_EQ_U32(REG_DMA0CNT, control);
    PT_ASSERT_EQ_INT(REG_DMA0CNT_L, 1);
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H, control >> 16);
    /* Repeating: after runs, every register still holds what was written. */
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_U32(REG_DMA0SAD, address_of(ewram(0x10)));
    PT_ASSERT_EQ_U32(REG_DMA0DAD, (uint32_t)REG_ADDR_BG0HOFS);
    PT_ASSERT_EQ_U32(REG_DMA0CNT, control);
    teardown();
}

PTEST(each_channel_writes_its_own_registers)
{
    setup();
    for (unsigned channel = 0; channel < DMA_CHANNEL_COUNT; channel++) {
        uint32_t control = CONTROL(DMA_ENABLE | DMA_START_VBLANK | DMA_16BIT, channel + 1);
        PT_ASSERT_EQ_INT(dma_set(channel, ewram(channel * 0x100), vram(channel * 0x100), control), DMA_OK);
    }
    PT_ASSERT_EQ_U32(REG_DMA0SAD, address_of(ewram(0x000)));
    PT_ASSERT_EQ_U32(REG_DMA1SAD, address_of(ewram(0x100)));
    PT_ASSERT_EQ_U32(REG_DMA2SAD, address_of(ewram(0x200)));
    PT_ASSERT_EQ_U32(REG_DMA3SAD, address_of(ewram(0x300)));
    PT_ASSERT_EQ_U32(REG_DMA0DAD, address_of(vram(0x000)));
    PT_ASSERT_EQ_U32(REG_DMA1DAD, address_of(vram(0x100)));
    PT_ASSERT_EQ_U32(REG_DMA2DAD, address_of(vram(0x200)));
    PT_ASSERT_EQ_U32(REG_DMA3DAD, address_of(vram(0x300)));
    PT_ASSERT_EQ_INT(REG_DMA0CNT_L, 1);
    PT_ASSERT_EQ_INT(REG_DMA1CNT_L, 2);
    PT_ASSERT_EQ_INT(REG_DMA2CNT_L, 3);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_L, 4);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H, DMA_ENABLE | DMA_START_VBLANK | DMA_16BIT);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_VBLANK), 4);
    PT_ASSERT(all_equal(io(DMA_REGISTERS_END), 0x20, 0));
    teardown();
}

PTEST(immediate_transfer_clears_only_the_enable_bit)
{
    const uint32_t control = CONTROL(COPY32 | DMA_INTR_ENABLE, 6);
    setup();
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0x20), vram(0x40), control), DMA_OK);
    /* GBATEK: the hardware does not change SAD, DAD or CNT_L. */
    PT_ASSERT_EQ_U32(REG_DMA3SAD, address_of(ewram(0x20)));
    PT_ASSERT_EQ_U32(REG_DMA3DAD, address_of(vram(0x40)));
    PT_ASSERT_EQ_INT(REG_DMA3CNT_L, 6);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H, (control >> 16) & ~(uint32_t)DMA_ENABLE);
    teardown();
}

PTEST(refused_transfer_writes_its_registers_and_clears_enable)
{
    const uint32_t control = CONTROL(COPY16, 4);
    setup();
    fill_pattern(ewram(0), 8, 127);
    PT_ASSERT_EQ_INT(dma_set(3, ewram(0), vram(GBA_VRAM_SIZE - UNIT16), control), DMA_ERR_BOUNDS);
    PT_ASSERT_EQ_U32(REG_DMA3SAD, address_of(ewram(0)));
    PT_ASSERT_EQ_U32(REG_DMA3DAD, address_of(vram(GBA_VRAM_SIZE - UNIT16)));
    PT_ASSERT_EQ_INT(REG_DMA3CNT_L, 4);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H, (control >> 16) & ~(uint32_t)DMA_ENABLE);
    PT_ASSERT(all_equal(vram(GBA_VRAM_SIZE - UNIT16), UNIT16, 0));
    teardown();
}

/* 64-bit: a stack address does not fit SAD; the register keeps the low 32
 * bits and the transfer still uses the full address. */
PTEST(stack_source_register_holds_the_low_32_bits)
{
    uint8_t local[8];
    setup();
    fill_pattern(local, sizeof local, 131);
    PT_ASSERT_EQ_INT(dma_set(3, local, vram(0), CONTROL(COPY16, 4)), DMA_OK);
    PT_ASSERT_EQ_U32(REG_DMA3SAD, address_of(local));
    PT_ASSERT_MEM_EQ(vram(0), local, sizeof local);
    teardown();
}

PTEST_MAIN()
