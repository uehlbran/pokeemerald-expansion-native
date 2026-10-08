/*
 * m4a_output.h: what the Direct Sound FIFOs play, read from the m4a engine.
 *
 * Stands in for DMA1 and DMA2 in sound FIFO mode (GBATEK "GBA DMA
 * Transfers", Special start timing; "GBA Sound Channel A and B - DMA
 * Sound"), which on the GBA feed the m4a engine's PCM ring to FIFOs A and B
 * one sample per timer 0 overflow. The platform does not run those DMAs
 * (dma.h: special timing is not emulated); once a frame the frame loop takes
 * the samples the DMAs would play from the ring itself. The ring's layout
 * and how the engine walks it: platform/m4a/README.md, "The PCM ring".
 */
#ifndef M4A_OUTPUT_H
#define M4A_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One frame of the ring: `samples` signed 8-bit samples per FIFO. FIFO A is
 * the ring's first half, FIFO B its second (SoundInit routes A right and B
 * left through SOUNDCNT_H). The pointers are into the game's SoundInfo and
 * stay valid until the engine next mixes. */
struct m4a_output_frame {
    const int8_t *fifo_a;
    const int8_t *fifo_b;
    size_t samples;
};

/* The frame the FIFO DMAs play from the engine's last VSync to its next:
 * frame pcmDmaPeriod - pcmDmaCounter of the ring (frame 0 while the counter
 * is 0, between m4aSoundVSyncOn and the first VSync). False, with *out
 * untouched, when there is nothing to play: no SoundInfo at SOUND_INFO_PTR,
 * the engine not initialised or its VSync off (ident not ID_NUMBER or
 * ID_NUMBER + 1, as m4aSoundVSync checks), or a ring geometry that does not
 * fit the ring. */
bool m4a_output_current_frame(struct m4a_output_frame *out);

/* The engine's sample rate, SoundInfo.pcmFreq; 0 when there is no engine. */
uint32_t m4a_output_sample_rate(void);

#endif /* M4A_OUTPUT_H */
