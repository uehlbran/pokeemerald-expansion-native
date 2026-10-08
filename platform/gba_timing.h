/*
 * gba_timing.h: the GBA clock and frame length, in one place.
 *
 * GBATEK "LCD Dimensions and Timings": a frame is 308 dots x 4 cycles x 228
 * lines = 280896 cycles; the CPU clock is 16.78 MHz (GBATEK "GBA Technical
 * Data": 16.78 MHz = 2^24 Hz = 16777216 Hz), so a frame lasts 1/59.7275 s.
 */
#ifndef GBA_TIMING_H
#define GBA_TIMING_H

#include <stdint.h>

#define GBA_CPU_HZ UINT32_C(16777216)

/* GBATEK "LCD Dimensions and Timings": 240 x 160 visible dots; a line is 308
 * dots of 4 cycles; 228 lines, 160 drawn and 68 of V-Blank. */
#define GBA_LCD_VISIBLE_DOTS 240u
#define GBA_LCD_VISIBLE_LINES 160u
#define GBA_LCD_TOTAL_LINES 228u
#define GBA_LCD_DOTS_PER_LINE 308u
#define GBA_LCD_CYCLES_PER_DOT 4u

#define GBA_CYCLES_PER_FRAME UINT32_C(280896)
_Static_assert(GBA_CYCLES_PER_FRAME == GBA_LCD_DOTS_PER_LINE * GBA_LCD_CYCLES_PER_DOT * GBA_LCD_TOTAL_LINES,
               "a frame is 308 dots x 4 cycles x 228 lines");

#endif /* GBA_TIMING_H */
