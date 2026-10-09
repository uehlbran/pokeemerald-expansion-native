/*
 * frame_pacer.h: holds the main loop to the GBA frame rate.
 *
 * The GBA draws a frame every 280896 cycles of its 16777216 Hz clock, which
 * is 59.7275 Hz (GBATEK "LCD Dimensions and Timings"), not 60. The pacer
 * sleeps to exact multiples of that period, carrying the fractional tick so
 * the long-run rate has no drift. After a stall longer than
 * FRAME_PACER_MAX_STALL_MS it restarts the schedule instead of running the
 * missed frames back to back (the reference ports' uncapped accumulator).
 * Single-threaded; uses only SDL's timer functions, no SDL_Init needed.
 */
#ifndef FRAME_PACER_H
#define FRAME_PACER_H

#include <stdint.h>

#include "gba_timing.h"

#define FRAME_PACER_MAX_STALL_MS 250
/* Highest counter rate accepted: the period arithmetic multiplies the rate by
 * 280896 in 64 bits, which overflows near 6.5e13 Hz. Real counters are 1e3 to
 * 1e9 Hz. */
#define FRAME_PACER_MAX_COUNTER_HZ UINT64_C(60000000000000)

/* FRAME_PACER_UNTHROTTLED: frame_pacer_wait returns at once (headless gates). */
enum frame_pacer_mode { FRAME_PACER_REALTIME, FRAME_PACER_UNTHROTTLED };

/* Fails if the performance counter rate is 0 or above FRAME_PACER_MAX_COUNTER_HZ. */
int frame_pacer_init(enum frame_pacer_mode mode);

/* Returns when the next frame boundary is reached; returns at once if the
 * pacer is unthrottled or not initialised. */
void frame_pacer_wait(void);

void frame_pacer_shutdown(void);

/* Pure helpers, public for tests. All times are performance-counter ticks.
 *
 * Next deadline after prev_deadline: prev_deadline + period, unless now is
 * more than max_stall past that, when the schedule restarts at now + period. */
uint64_t frame_pacer_next_deadline(uint64_t prev_deadline, uint64_t period, uint64_t now, uint64_t max_stall);

/* One frame period in ticks of a counter running at counter_hz. The period
 * is not a whole number of ticks; *remainder (start at 0) carries the
 * fraction, in units of 1/GBA_CPU_HZ tick, so n calls sum to exactly
 * floor(n * 280896 * counter_hz / 16777216). */
uint64_t frame_pacer_period_ticks(uint64_t counter_hz, uint32_t *remainder);

#endif /* FRAME_PACER_H */
