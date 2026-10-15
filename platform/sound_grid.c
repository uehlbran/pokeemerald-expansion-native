/*
 * sound_grid.c: the output grid and the FIFOs read on it (sound_grid.h).
 *
 * Written from GBATEK "GBA Sound Control Registers" (SOUNDBIAS,
 * "Amplitude Resolution/Sampling Cycle") and "GBA Sound Channel A and B -
 * DMA Sound" (a FIFO's sample plays from one timer overflow to the next),
 * with the frame's length from "LCD Dimensions and Timings". Integer arithmetic only.
 */
#include "sound_grid.h"

struct sound_grid_frame sound_grid_next_frame(struct sound_grid *grid)
{
    const uint32_t cycles = grid->carried_cycles + GBA_CYCLES_PER_FRAME;
    const struct sound_grid_frame frame = {
        .carried_cycles = grid->carried_cycles,
        .samples = cycles / SOUND_GRID_CYCLES_PER_SAMPLE,
    };
    grid->carried_cycles = cycles % SOUND_GRID_CYCLES_PER_SAMPLE;
    return frame;
}

void sound_grid_hold_fifo(const struct sound_grid_frame *frame, const int8_t *fifo, size_t fifo_samples, int8_t *out)
{
    for (size_t k = 0; k < frame->samples; k++) {
        /* Below GBA_CYCLES_PER_FRAME: every span ends inside the frame. The
         * product below is at most 280895 * 1584 (the whole ring, the largest
         * frame m4a_output passes), about 4.4e8, inside 32 bits; 64 bits are
         * headroom for a count the type does not bound, not a need of any
         * count in the engine's table. */
        const uint64_t last_cycle = (uint64_t)(k + 1u) * SOUND_GRID_CYCLES_PER_SAMPLE - frame->carried_cycles - 1u;
        out[k] = fifo[last_cycle * fifo_samples / GBA_CYCLES_PER_FRAME];
    }
}
