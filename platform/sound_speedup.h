/*
 * sound_speedup.h: the sound of n frames played in the time of one, for
 * fast-forward (fastforward.h).
 *
 * Not hardware: a GBA played its sound at one speed. At n times the speed
 * the frame loop mixes n frames of sound in each frame period, while the
 * device plays one; queued as they are, the queue's bound (audio.h) would
 * drop most of them and what played would be fragments. Here each n
 * consecutive stereo samples become one, their mean, rounded toward zero:
 * the sound plays continuously, n times as fast and as high, as a tape run
 * fast does, and one frame period of it is queued a period, so the queue
 * stays where it was at the normal speed. The mean is a box filter, a crude
 * low-pass against the aliasing of keeping one sample in n.
 *
 * Pure, and in place: the samples are replaced by the sped-up ones.
 */
#ifndef SOUND_SPEEDUP_H
#define SOUND_SPEEDUP_H

#include <stddef.h>
#include <stdint.h>

/* The fastest: fast-forward's 4x (fastforward.h). The sum of that many
 * samples fits an int32_t many times over. */
#define SOUND_SPEEDUP_MAX_FACTOR 4u

/* Stereo, interleaved left and right, as the mixer writes it (frame.h). */
#define SOUND_SPEEDUP_CHANNELS 2u

struct sound_speedup {
    uint32_t factor;                     /* 1 (as it is) to SOUND_SPEEDUP_MAX_FACTOR */
    uint32_t count;                      /* samples summed so far, below factor */
    int32_t sum[SOUND_SPEEDUP_CHANNELS]; /* their sum, left and right */
};

/* Starts at factor times the speed, forgetting a partial group (less than a
 * sample of output). Returns 0, or -1 (reported) for a factor of 0 or above
 * SOUND_SPEEDUP_MAX_FACTOR, which leaves it as it was. */
int sound_speedup_start(struct sound_speedup *speedup, uint32_t factor);

/* Replaces the frames stereo samples of stereo with the sped-up ones and
 * returns how many: the groups of factor completed, counting the samples
 * left over from the last call; the rest are kept for the next. Never more
 * than frames. */
size_t sound_speedup_run(struct sound_speedup *speedup, int16_t *stereo, size_t frames);

#endif /* SOUND_SPEEDUP_H */
