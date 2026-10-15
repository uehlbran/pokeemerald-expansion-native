/*
 * sound_speedup.c: the sound of n frames in the time of one (sound_speedup.h).
 */
#include "sound_speedup.h"

#include <stdio.h>

int sound_speedup_start(struct sound_speedup *speedup, uint32_t factor)
{
    if (factor == 0u || factor > SOUND_SPEEDUP_MAX_FACTOR) {
        (void)fprintf(stderr, "sound_speedup: a factor of %lu is not 1 to %u\n", (unsigned long)factor,
                      SOUND_SPEEDUP_MAX_FACTOR);
        return -1;
    }
    *speedup = (struct sound_speedup){.factor = factor};
    return 0;
}

size_t sound_speedup_run(struct sound_speedup *speedup, int16_t *stereo, size_t frames)
{
    size_t out = 0;
    for (size_t in = 0; in < frames; in++) {
        for (size_t channel = 0; channel < SOUND_SPEEDUP_CHANNELS; channel++)
            speedup->sum[channel] += stereo[in * SOUND_SPEEDUP_CHANNELS + channel];
        if (++speedup->count < speedup->factor)
            continue;
        /* out <= in: the output is written over input already read. */
        for (size_t channel = 0; channel < SOUND_SPEEDUP_CHANNELS; channel++) {
            stereo[out * SOUND_SPEEDUP_CHANNELS + channel] = (int16_t)(speedup->sum[channel]
                                                                       / (int32_t)speedup->factor);
            speedup->sum[channel] = 0;
        }
        speedup->count = 0;
        out++;
    }
    return out;
}
