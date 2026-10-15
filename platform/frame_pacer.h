/*
 * frame_pacer.h: holds the main loop to the GBA frame rate.
 *
 * The GBA draws a frame every 280896 cycles of its 16777216 Hz clock, which
 * is 59.7275 Hz (GBATEK "LCD Dimensions and Timings"), not 60. The pacer
 * sleeps to exact multiples of that period, carrying the fractional tick so
 * the long-run rate has no drift. After a stall longer than
 * FRAME_PACER_MAX_STALL_MS it restarts the schedule instead of running the
 * missed frames back to back (the reference ports' uncapped accumulator).
 *
 * Fast-forward (fastforward.h) is a speed here and nowhere else: at n times
 * the speed the period is the GBA's divided by n, so the same frames, with
 * the same game logic, come n times as often; unlimited does not wait.
 * Single-threaded; uses only SDL's timer functions, no SDL_Init needed.
 */
#ifndef FRAME_PACER_H
#define FRAME_PACER_H

#include <stdbool.h>
#include <stdint.h>

#include "gba_timing.h"

#define FRAME_PACER_MAX_STALL_MS 250
/* Highest counter rate accepted: the period arithmetic multiplies the rate by
 * 280896 in 64 bits, which overflows near 6.5e13 Hz. Real counters are 1e3 to
 * 1e9 Hz. */
#define FRAME_PACER_MAX_COUNTER_HZ UINT64_C(60000000000000)

/* The speeds, as multipliers of the GBA's frame rate: the normal speed, up
 * to FRAME_PACER_MAX_MULTIPLIER (fast-forward's fastest finite speed), or
 * FRAME_PACER_UNLIMITED, no wait at all. */
#define FRAME_PACER_NORMAL_SPEED 1u
#define FRAME_PACER_MAX_MULTIPLIER 4u
#define FRAME_PACER_UNLIMITED 0u

/* FRAME_PACER_UNTHROTTLED: frame_pacer_wait returns at once (headless gates). */
enum frame_pacer_mode { FRAME_PACER_REALTIME, FRAME_PACER_UNTHROTTLED };

/* The mode for the --unthrottled option. */
enum frame_pacer_mode frame_pacer_mode_for(bool unthrottled);

/* Fails if the performance counter rate is 0 or above FRAME_PACER_MAX_COUNTER_HZ.
 * Starts at FRAME_PACER_NORMAL_SPEED. */
int frame_pacer_init(enum frame_pacer_mode mode);

/* The speed of the waits from now on (above). A change restarts the
 * schedule from now, as a long stall does, so that frames owed at the old
 * speed (behind at 4x, or the stretch run unlimited) are not run at the new
 * one; the same speed again changes nothing. Unthrottled waits stay
 * unthrottled at any speed. Returns 0, or -1 (reported) for a multiplier
 * above FRAME_PACER_MAX_MULTIPLIER, which leaves the speed as it was. */
int frame_pacer_set_multiplier(uint32_t multiplier);

/* The speed in force. */
uint32_t frame_pacer_multiplier(void);

/* Returns when the next frame boundary is reached; returns at once if the
 * pacer is unthrottled, at FRAME_PACER_UNLIMITED or not initialised. */
void frame_pacer_wait(void);

void frame_pacer_shutdown(void);

/* Pure helpers, public for tests. All times are performance-counter ticks.
 *
 * Next deadline after prev_deadline: prev_deadline + period, unless now is
 * more than max_stall past that, when the schedule restarts at now + period. */
uint64_t frame_pacer_next_deadline(uint64_t prev_deadline, uint64_t period, uint64_t now, uint64_t max_stall);

/* One frame period at multiplier times the speed in ticks of a counter
 * running at counter_hz. multiplier is 1 to FRAME_PACER_MAX_MULTIPLIER, never
 * FRAME_PACER_UNLIMITED (0), which has no period: frame_pacer_wait returns
 * before asking for one then.
 * The period is not a whole number of ticks; *remainder (start at 0, and
 * again for another multiplier) carries the fraction, in units of
 * 1/(GBA_CPU_HZ * multiplier) tick, so n calls sum to exactly
 * floor(n * 280896 * counter_hz / (16777216 * multiplier)). */
uint64_t frame_pacer_period_ticks(uint64_t counter_hz, uint32_t multiplier, uint32_t *remainder);

#endif /* FRAME_PACER_H */
