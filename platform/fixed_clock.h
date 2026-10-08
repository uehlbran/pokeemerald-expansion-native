/*
 * fixed_clock.h: a deterministic wall clock for the replay (--rtc-fixed).
 *
 * Stands in for the host's clock, which would make two replays of the same
 * script see different times (the game reads the RTC for the time of day,
 * berries, the random seed after a save). The time is a base epoch plus the
 * whole seconds the frame count says have passed. The GBA runs 59.7275
 * frames a second; the clock counts FIXED_CLOCK_FRAMES_PER_SECOND (60), the
 * integer the replay can predict exactly, so a game second is not exact to
 * the real GBA. Nothing in the game depends on that.
 */
#ifndef PLATFORM_FIXED_CLOCK_H
#define PLATFORM_FIXED_CLOCK_H

#include <stdint.h>

#define FIXED_CLOCK_FRAMES_PER_SECOND 60u

/* Seconds since the Unix epoch, UTC: base_epoch + frames / 60. Saturates at
 * INT64_MAX instead of overflowing. */
int64_t fixed_clock_at(int64_t base_epoch, uint32_t frames);

#endif /* PLATFORM_FIXED_CLOCK_H */
