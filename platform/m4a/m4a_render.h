/*
 * m4a engine: the interface between the mixer's frame loop (m4a_mixer.c)
 * and the rendering of one Direct Sound channel's samples into the PCM
 * ring (m4a_render.c). Internal to the engine.
 */
#ifndef GUARD_PLATFORM_M4A_RENDER_H
#define GUARD_PLATFORM_M4A_RENDER_H

#include "m4a_engine.h"

/* One channel's output: its frame in each half and its envelope volumes. */
struct MixOutput
{
    u8 *right;
    u8 *left;
    u32 samples;
    u32 volumeRight;
    u32 volumeLeft;
};

/* The part of the sample the loop repeats; length 0 means no loop. */
struct LoopRegion
{
    u32 start;
    u32 length;
};

/* SoundMainRAM from _081DD03A: mixes one channel's samples for the frame
 * at its envelope volumes, moving its position on (or ending it). */
void M4aRenderChannel(struct SoundChannel *chan, const struct MixOutput *out, u32 divFreq,
                      const struct LoopRegion *loop);

/* SF_START: the channel will play from offset samples into its wave. */
void M4aRenderSetStart(struct SoundChannel *chan, u32 offset);

#endif // GUARD_PLATFORM_M4A_RENDER_H
