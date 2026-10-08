/*
 * lcd_status.c: VCOUNT and DISPSTAT's status bits (lcd_status.h).
 *
 * Written from GBATEK "LCD I/O Interrupts and Status" and "LCD Dimensions
 * and Timings". Registers are read and written as halfwords at their GBA
 * addresses in the mapped I/O page (gba_memmap.h).
 */
#include "lcd_status.h"

#include <stdbool.h>

#include "gba_irq.h"
#include "gba_memmap.h"

#define VCOUNT_LINE_MASK UINT16_C(0x00FF)

_Static_assert(LCD_VCOUNT_OFFSET + sizeof(uint16_t) <= GBA_IO_SIZE, "VCOUNT is outside the I/O region");
_Static_assert(LCD_LINE_COUNT - 1u <= VCOUNT_LINE_MASK, "every line fits VCOUNT's eight bits");

static volatile uint16_t *io_halfword(uint32_t offset)
{
    return (volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

static uint16_t lyc(uint16_t dispstat)
{
    return (uint16_t)(dispstat >> LCD_DISPSTAT_LYC_SHIFT);
}

unsigned lcd_status_begin_line(uint16_t line)
{
    if (line >= LCD_LINE_COUNT)
        return 0;
    volatile uint16_t *dispstat = io_halfword(LCD_DISPSTAT_OFFSET);
    uint16_t value = (uint16_t)(*dispstat & ~LCD_DISPSTAT_STATUS_MASK);
    bool vblank = line >= LCD_LINE_VBLANK_START && line <= LCD_LINE_VBLANK_LAST_FLAGGED;
    bool match = line == lyc(value);
    unsigned requested = 0;

    if (vblank)
        value |= LCD_DISPSTAT_VBLANK_FLAG;
    if (match)
        value |= LCD_DISPSTAT_VCOUNTER_FLAG;
    *io_halfword(LCD_VCOUNT_OFFSET) = line;
    *dispstat = value;

    if (line == LCD_LINE_VBLANK_START && (value & LCD_DISPSTAT_VBLANK_IRQ))
        requested |= 1u << GBA_IRQ_VBLANK;
    if (match && (value & LCD_DISPSTAT_VCOUNTER_IRQ))
        requested |= 1u << GBA_IRQ_VCOUNT;
    return requested;
}

unsigned lcd_status_begin_hblank(void)
{
    volatile uint16_t *dispstat = io_halfword(LCD_DISPSTAT_OFFSET);
    uint16_t value = (uint16_t)(*dispstat | LCD_DISPSTAT_HBLANK_FLAG);
    *dispstat = value;
    return (value & LCD_DISPSTAT_HBLANK_IRQ) ? 1u << GBA_IRQ_HBLANK : 0u;
}

uint16_t lcd_status_line(void)
{
    return (uint16_t)(*io_halfword(LCD_VCOUNT_OFFSET) & VCOUNT_LINE_MASK);
}
