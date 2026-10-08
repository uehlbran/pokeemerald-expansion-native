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
#define GBA_CYCLES_PER_FRAME UINT32_C(280896)

#endif /* GBA_TIMING_H */
