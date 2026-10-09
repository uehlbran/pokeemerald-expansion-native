/*
 * fixed_clock.c: see fixed_clock.h.
 */
#include "fixed_clock.h"

int64_t fixed_clock_at(int64_t base_epoch, uint32_t frames)
{
    const int64_t seconds = (int64_t)(frames / FIXED_CLOCK_FRAMES_PER_SECOND);
    if (base_epoch > INT64_MAX - seconds)
        return INT64_MAX;
    return base_epoch + seconds;
}
