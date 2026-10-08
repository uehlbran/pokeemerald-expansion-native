/*
 * lcd_status.h: the LCD controller's timing registers, VCOUNT and DISPSTAT.
 *
 * Stands in for the half of the LCD controller that keeps time: the line
 * counter VCOUNT (4000006h) and the status bits of DISPSTAT (4000004h), and
 * the three interrupts they request. GBATEK "LCD I/O Interrupts and Status":
 *   DISPSTAT bit 0  V-Blank flag, set in lines 160..226, not 227
 *            bit 1  H-Blank flag, set in the H-Blank of every line 0..227
 *            bit 2  V-Counter flag, set while VCOUNT equals bits 8-15
 *            bits 3, 4, 5  V-Blank, H-Blank, V-Counter IRQ enable
 *            bits 8-15  V-Count setting (LYC), 0..227
 *   VCOUNT   bits 0-7 the current line, 0..227.
 * An interrupt is requested when its event happens with its enable bit set:
 * V-Blank at the start of line 160, H-Blank at the start of each H-Blank,
 * V-Counter at the start of the line that matches. Bits 0-2 are read only on
 * hardware; natively the game's stores land in them, so every call here
 * rewrites them and leaves the game's bits 3-15 alone.
 *
 * The frame loop (frame.h) calls these as the lines go by; the PPU never
 * writes either register (ppu.h). Single-threaded.
 */
#ifndef LCD_STATUS_H
#define LCD_STATUS_H

#include <stdint.h>

#include "gba_timing.h"

/* GBATEK "LCD Dimensions and Timings" (gba_timing.h): lines 0..159 are
 * drawn, 160..227 are V-Blank, 228 lines in all; the V-Blank flag is clear
 * again in the last line. */
#define LCD_VISIBLE_LINES GBA_LCD_VISIBLE_LINES
#define LCD_LINE_VBLANK_START GBA_LCD_VISIBLE_LINES
#define LCD_LINE_COUNT GBA_LCD_TOTAL_LINES
#define LCD_LINE_VBLANK_LAST_FLAGGED (GBA_LCD_TOTAL_LINES - 2u)

/* The line VCOUNT shows while the game runs between frames. On hardware the
 * game's logic starts when VBlankIntrWait returns, just after the V-Blank
 * interrupt of line 160, and normally ends within V-Blank; expansion's
 * SetGpuReg (src/gpu_regs.c) writes a register at once when VCOUNT is
 * 161..225 and otherwise buffers it until the next V-Blank handler. 161 is
 * that window's first line. */
#define LCD_LINE_GAME_LOGIC 161u

/* DISPSTAT bits (GBATEK, above). */
#define LCD_DISPSTAT_VBLANK_FLAG UINT16_C(0x0001)
#define LCD_DISPSTAT_HBLANK_FLAG UINT16_C(0x0002)
#define LCD_DISPSTAT_VCOUNTER_FLAG UINT16_C(0x0004)
#define LCD_DISPSTAT_VBLANK_IRQ UINT16_C(0x0008)
#define LCD_DISPSTAT_HBLANK_IRQ UINT16_C(0x0010)
#define LCD_DISPSTAT_VCOUNTER_IRQ UINT16_C(0x0020)
#define LCD_DISPSTAT_LYC_SHIFT 8u
#define LCD_DISPSTAT_STATUS_MASK (LCD_DISPSTAT_VBLANK_FLAG | LCD_DISPSTAT_HBLANK_FLAG | LCD_DISPSTAT_VCOUNTER_FLAG)

/* I/O offsets of the two registers from GBA_IO_BASE. */
#define LCD_DISPSTAT_OFFSET UINT32_C(0x004)
#define LCD_VCOUNT_OFFSET UINT32_C(0x006)

/* The start of `line` (0..227; others are ignored and return 0): VCOUNT =
 * line, H-Blank flag clear, V-Blank flag as the line has it, V-Counter flag
 * set if line equals LYC. Returns the interrupts requested, as a mask of
 * (1u << enum gba_irq): V-Blank on line 160 with its enable set, V-Counter
 * on a match with its enable set. */
unsigned lcd_status_begin_line(uint16_t line);

/* The start of the current line's H-Blank: sets the H-Blank flag. Returns
 * (1u << GBA_IRQ_HBLANK) if its enable is set, else 0. */
unsigned lcd_status_begin_hblank(void);

/* The current line, as VCOUNT holds it. */
uint16_t lcd_status_line(void);

#endif /* LCD_STATUS_H */
