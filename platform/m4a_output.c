/*
 * m4a_output.c: the frame of the m4a engine's PCM ring the FIFO DMAs play.
 *
 * Stands in for the sound FIFO DMAs (m4a_output.h; GBATEK
 * https://problemkaputt.de/gbatek.htm#gbasoundchannelaandbdmasound). Followed: the m4a
 * engine's own bookkeeping, src/m4a_1.s m4aSoundVSync and SoundMain as
 * translated in platform/m4a/ (README.md "The PCM ring"): m4aSoundVSync
 * counts pcmDmaCounter down once a frame from pcmDmaPeriod to 1 and restarts
 * both DMAs at the ring's start when it runs out, so the DMAs play frame
 * pcmDmaPeriod - pcmDmaCounter while SoundMain mixes the one after.
 *
 * The one platform file that includes an expansion header other than
 * through gba_memmap.h: the engine's SoundInfo (include/gba/m4a_internal.h),
 * whose layout platform/m4a/m4a_layout.h asserts. The engine is game code
 * translated, so this is reading the game's own data structure, the way
 * the DMA read it on the GBA.
 */
#include "m4a_output.h"

#include <gba/m4a_internal.h>

_Static_assert(sizeof(((struct SoundInfo *)0)->pcmBuffer) == 2 * PCM_DMA_BUF_SIZE,
               "the ring is two FIFO halves of PCM_DMA_BUF_SIZE bytes");

/* m4aSoundVSync runs while ident is ID_NUMBER or ID_NUMBER + 1 (the latter
 * while the engine holds its lock); m4aSoundVSyncOff adds 10. */
#define M4A_IDENT_VSYNC_ON_SPAN 1u

static const struct SoundInfo *sound_info(void)
{
    const struct SoundInfo *info = SOUND_INFO_PTR;
    if (info == NULL || info->ident - (u32)ID_NUMBER > M4A_IDENT_VSYNC_ON_SPAN)
        return NULL;
    return info;
}

bool m4a_output_current_frame(struct m4a_output_frame *out)
{
    const struct SoundInfo *info = sound_info();
    if (info == NULL || out == NULL)
        return false;
    s32 samples = info->pcmSamplesPerVBlank;
    u8 period = info->pcmDmaPeriod;
    u8 counter = info->pcmDmaCounter;
    if (samples <= 0 || period == 0 || counter > period || (size_t)samples * period > PCM_DMA_BUF_SIZE)
        return false;
    size_t frame = counter == 0 ? 0u : (size_t)(period - counter);
    size_t offset = frame * (size_t)samples;
    *out = (struct m4a_output_frame){
        .fifo_a = &info->pcmBuffer[offset],
        .fifo_b = &info->pcmBuffer[PCM_DMA_BUF_SIZE + offset],
        .samples = (size_t)samples,
    };
    return true;
}

uint32_t m4a_output_sample_rate(void)
{
    const struct SoundInfo *info = sound_info();
    if (info == NULL || info->pcmFreq <= 0)
        return 0;
    return (uint32_t)info->pcmFreq;
}
