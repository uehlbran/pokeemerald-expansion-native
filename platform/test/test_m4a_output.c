/*
 * test_m4a_output.c: which frame of the m4a engine's PCM ring the FIFO DMAs
 * play (m4a_output.h).
 *
 * Expected values from the engine's bookkeeping (src/m4a_1.s m4aSoundVSync
 * and SoundMain; platform/m4a/README.md "The PCM ring"): the counter runs
 * from pcmDmaPeriod down to 1, the DMAs play frame period - counter, FIFO A
 * reads the ring's first half and FIFO B its second; the engine runs while
 * ident is ID_NUMBER or ID_NUMBER + 1.
 */
// deps: gba_memmap.c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <gba/m4a_internal.h>

#include "gba_memmap.h"
#include "m4a_output.h"
#include "ptest.h"

static struct SoundInfo info;

/* Emerald's configuration: SampleFreqSet with SOUND_MODE_FREQ_13379. */
static void configure(u8 counter)
{
    memset(&info, 0, sizeof info);
    info.ident = ID_NUMBER;
    info.pcmSamplesPerVBlank = 224;
    info.pcmDmaPeriod = PCM_DMA_BUF_SIZE / 224; /* 7 */
    info.pcmDmaCounter = counter;
    info.pcmFreq = 13379;
    SOUND_INFO_PTR = &info;
}

PTEST(the_dmas_play_frame_period_minus_counter)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame;
    for (u8 counter = 1; counter <= 7; counter++) {
        configure(counter);
        PT_ASSERT(m4a_output_current_frame(&frame));
        size_t expected = (size_t)(7 - counter) * 224;
        PT_ASSERT(frame.fifo_a == &info.pcmBuffer[expected]);
        PT_ASSERT(frame.fifo_b == &info.pcmBuffer[PCM_DMA_BUF_SIZE + expected]);
        PT_ASSERT(frame.samples == 224);
    }
    SOUND_INFO_PTR = NULL;
}

PTEST(counter_zero_is_frame_zero)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame;
    configure(0); /* m4aSoundVSyncOn, before the first VSync */
    PT_ASSERT(m4a_output_current_frame(&frame));
    PT_ASSERT(frame.fifo_a == &info.pcmBuffer[0]);
    SOUND_INFO_PTR = NULL;
}

PTEST(the_locked_engine_still_plays)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame;
    configure(3);
    info.ident = ID_NUMBER + 1;
    PT_ASSERT(m4a_output_current_frame(&frame));
    SOUND_INFO_PTR = NULL;
}

PTEST(nothing_to_play)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame = {NULL, NULL, 99};
    SOUND_INFO_PTR = NULL;
    PT_ASSERT(!m4a_output_current_frame(&frame));
    configure(1);
    info.ident = ID_NUMBER + 10; /* m4aSoundVSyncOff */
    PT_ASSERT(!m4a_output_current_frame(&frame));
    info.ident = ID_NUMBER - 1;
    PT_ASSERT(!m4a_output_current_frame(&frame));
    configure(8); /* a counter past the period */
    PT_ASSERT(!m4a_output_current_frame(&frame));
    configure(1);
    info.pcmDmaPeriod = 0;
    PT_ASSERT(!m4a_output_current_frame(&frame));
    configure(1);
    info.pcmSamplesPerVBlank = 0;
    PT_ASSERT(!m4a_output_current_frame(&frame));
    configure(1);
    info.pcmSamplesPerVBlank = 400; /* 7 frames of 400 do not fit the ring */
    PT_ASSERT(!m4a_output_current_frame(&frame));
    PT_ASSERT(!m4a_output_current_frame(NULL));
    PT_ASSERT(frame.samples == 99); /* untouched */
    SOUND_INFO_PTR = NULL;
}

PTEST(the_last_frame_of_a_full_ring)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame;
    configure(1);
    info.pcmSamplesPerVBlank = 792; /* two frames fill the ring exactly */
    info.pcmDmaPeriod = 2;
    PT_ASSERT(m4a_output_current_frame(&frame));
    PT_ASSERT(frame.fifo_a == &info.pcmBuffer[792]);
    PT_ASSERT(frame.fifo_b + frame.samples == &info.pcmBuffer[2 * PCM_DMA_BUF_SIZE]);
    SOUND_INFO_PTR = NULL;
}

PTEST(a_frame_one_sample_past_the_ring_is_refused)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    struct m4a_output_frame frame;
    configure(1);
    info.pcmDmaPeriod = 1;
    info.pcmSamplesPerVBlank = PCM_DMA_BUF_SIZE; /* exactly the ring: allowed */
    PT_ASSERT(m4a_output_current_frame(&frame));
    PT_ASSERT(frame.samples == PCM_DMA_BUF_SIZE);
    info.pcmSamplesPerVBlank = PCM_DMA_BUF_SIZE + 1;
    PT_ASSERT(!m4a_output_current_frame(&frame));
    SOUND_INFO_PTR = NULL;
}

PTEST_MAIN()
