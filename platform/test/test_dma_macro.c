// deps: dma.c gba_memmap.c transfer_bounds.c
/*
 * Tests of the PLATFORM_NATIVE branch of expansion's include/gba/macro.h:
 * DmaSet, DmaCopy*, DmaFill*, DmaClear* and DmaStop, compiled as the native
 * build compiles them (MODERN, PLATFORM_NATIVE), reach platform/dma.c and
 * leave memory and the DMA registers as on the GBA. Argument forms are the
 * game's: pointers, volatile pointers, register addresses and plain integer
 * addresses (src/starter_choose.c passes VRAM, an int).
 */
#define MODERN 1
#define PLATFORM_NATIVE 1

#include <gba/types.h>
#include <gba/defines.h>
#include <gba/io_reg.h>
#include <gba/macro.h>

#include <stdbool.h>
#include <string.h>

#include "dma.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

/* include/scanline_effect.h, SCANLINE_EFFECT_DMACNT_16BIT, in u32. */
#define SCANLINE_16BIT_CNT_H (DMA_ENABLE | DMA_START_HBLANK | DMA_REPEAT | DMA_SRC_INC \
                              | DMA_DEST_INC | DMA_16BIT | DMA_DEST_RELOAD)
#define SCANLINE_16BIT (((u32)SCANLINE_16BIT_CNT_H << 16) | 1)
/* What expansion's DmaCopy16/DmaCopy32 put in CNT_H. */
#define COPY16_CNT_H (DMA_ENABLE | DMA_START_NOW | DMA_16BIT | DMA_SRC_INC | DMA_DEST_INC)
#define COPY32_CNT_H (DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_INC)
#define GUARD 0xA5u

static unsigned raised;

static void fake_raise(enum gba_irq source)
{
    (void)source;
    raised++;
}

static uint8_t *ewram(size_t offset)
{
    return (uint8_t *)(uintptr_t)(GBA_EWRAM_BASE + offset);
}

static uint8_t *vram(size_t offset)
{
    return (uint8_t *)(uintptr_t)(GBA_VRAM_BASE + offset);
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

static void reset(void)
{
    memset(ewram(0), 0, GBA_EWRAM_SIZE);
    memset(vram(0), 0, GBA_VRAM_SIZE);
    memset((void *)(uintptr_t)GBA_IO_BASE, 0, GBA_IO_SIZE);
    raised = 0;
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

PTEST(dma_copy16_copies_and_leaves_the_registers_as_on_the_gba)
{
    setup();
    fill_pattern(ewram(0x100), 0x40, 1);
    memset(vram(0x200 - 4), GUARD, 0x40 + 8);
    DmaCopy16(3, ewram(0x100), vram(0x200), 0x40);
    PT_ASSERT_MEM_EQ(vram(0x200), ewram(0x100), 0x40);
    PT_ASSERT(all_equal(vram(0x200 - 4), 4, GUARD));
    PT_ASSERT(all_equal(vram(0x240), 4, GUARD));
    PT_ASSERT_EQ_U32(REG_DMA3SAD, GBA_EWRAM_BASE + 0x100);
    PT_ASSERT_EQ_U32(REG_DMA3DAD, GBA_VRAM_BASE + 0x200);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_L, 0x40 / 2);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H, COPY16_CNT_H & ~DMA_ENABLE);
    teardown();
}

PTEST(dma_copy32_copies)
{
    u32 *source = (u32 *)ewram(0x400);
    u32 *destination = (u32 *)vram(0x800);
    setup();
    fill_pattern(ewram(0x400), 0x80, 3);
    DmaCopy32(3, source, destination, 0x80);
    PT_ASSERT_MEM_EQ(vram(0x800), ewram(0x400), 0x80);
    PT_ASSERT(all_equal(vram(0x880), 4, 0));
    PT_ASSERT_EQ_INT(REG_DMA3CNT_L, 0x80 / 4);
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H, COPY32_CNT_H & ~DMA_ENABLE);
    teardown();
}

/* src/starter_choose.c: DmaFill16(3, 0, VRAM, VRAM_SIZE), VRAM an int. */
PTEST(dma_fill_with_integer_addresses_fills_the_whole_region)
{
    setup();
    memset(vram(0), GUARD, GBA_VRAM_SIZE);
    DmaFill16(3, 0x1234, VRAM, VRAM_SIZE);
    for (size_t offset = 0; offset < GBA_VRAM_SIZE; offset += 2)
        PT_ASSERT_EQ_INT(read16(vram(offset)), 0x1234);
    DmaFill32(3, 0xDEADBEEF, OAM, OAM_SIZE);
    for (size_t offset = 0; offset < OAM_SIZE; offset += 4)
        PT_ASSERT_EQ_U32(read32((const uint8_t *)(uintptr_t)(OAM + offset)), 0xDEADBEEFu);
    PT_ASSERT_EQ_U32(REG_DMA3DAD, (uint32_t)OAM);
    teardown();
}

/* DMA_CLEAR passes a u32 size into DMA_FILL, whose control word ORs it with
 * ((DMA_ENABLE | ...) << 16), a negative int: -Wsign-conversion reports that
 * inside expansion's macro text, which is not ours to change. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
PTEST(dma_clear_zeroes_exactly_the_range)
{
    u16 *destination = (u16 *)ewram(0x1000);
    setup();
    memset(ewram(0x1000 - 2), GUARD, 0x100 + 4);
    DmaClear16(3, destination, 0x100);
    PT_ASSERT(all_equal(ewram(0x1000), 0x100, 0));
    PT_ASSERT(all_equal(ewram(0x1000 - 2), 2, GUARD));
    PT_ASSERT(all_equal(ewram(0x1100), 2, GUARD));
    memset(ewram(0x2000), GUARD, 0x40);
    DmaClear32(3, (u32 *)ewram(0x2000), 0x20);
    PT_ASSERT(all_equal(ewram(0x2000), 0x20, 0));
    PT_ASSERT(all_equal(ewram(0x2020), 0x20, GUARD));
    teardown();
}
#pragma GCC diagnostic pop

/* src/scanline_effect.c: DmaStop(0) then DmaSet(0, ...) into a register,
 * run between scanlines by the frame loop. */
PTEST(dma_set_hblank_then_dma_stop)
{
    volatile void *destination = &REG_BG0HOFS;
    u16 *table = (u16 *)ewram(0x3000);
    setup();
    for (u16 line = 0; line < 8; line++)
        table[line] = (u16)(0x100 + line);
    DmaStop(0);
    DmaSet(0, table + 1, destination, SCANLINE_16BIT);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0x101);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 1);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0x102);
    PT_ASSERT_EQ_U32(REG_DMA0CNT, SCANLINE_16BIT);
    DmaStop(0);
    PT_ASSERT_EQ_INT(dma_run_timing(DMA_TIMING_HBLANK), 0);
    PT_ASSERT_EQ_INT(REG_BG0HOFS, 0x102);
    /* What the GBA's DmaStop leaves in CNT_H. */
    PT_ASSERT_EQ_INT(REG_DMA0CNT_H, SCANLINE_16BIT_CNT_H & ~(DMA_START_MASK | DMA_DREQ_ON | DMA_REPEAT)
                                        & ~DMA_ENABLE);
    teardown();
}

/* A transfer the platform refuses is a statement like any other: the
 * macro discards the result (dma_set has printed why) and nothing is
 * written. */
PTEST(refused_transfer_through_the_macro_writes_nothing)
{
    setup();
    fill_pattern(ewram(0), 8, 5);
    DmaCopy16(3, ewram(0), vram(GBA_VRAM_SIZE - 2), 8);
    PT_ASSERT(all_equal(vram(GBA_VRAM_SIZE - 2), 2, 0));
    PT_ASSERT_EQ_INT(REG_DMA3CNT_H & DMA_ENABLE, 0);
    PT_ASSERT_EQ_INT(raised, 0);
    teardown();
}

PTEST_MAIN()
