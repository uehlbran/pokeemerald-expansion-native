/*
 * sound_grid.h: the grid the GBA's sound output is sampled on, and the Direct
 * Sound FIFOs read on it.
 *
 * Stands in for the last stage of the GBA Sound Controller (GBATEK "GBA Sound
 * Control Registers": SOUNDBIAS bits 14-15, "Amplitude Resolution/Sampling
 * Cycle", and "Resampling to 32.768kHz"; https://problemkaputt.de/gbatek.htm#gbasoundcontrolregisters).
 * The PSG channels run off the system clock and each FIFO's DAC holds its
 * current sample until its timer's next overflow (GBATEK "GBA Sound Channel A
 * and B - DMA Sound"); the PWM stage samples their sum once per sampling
 * cycle, 32768 times a second at SOUNDBIAS's default, one sample every
 * 2^24 / 32768 = 512 CPU cycles. The host plays that grid: the PSG is
 * generated on it (psg.c, "RESAMPLING"), and the FIFO frame the m4a engine
 * mixed is read on it by zero-order hold on cycle time, which is what the
 * DAC and the PWM stage do. The tone channels play at 131072 / (2048 - n)
 * Hz (square) and 65536 / (2048 - n) Hz (wave), so on this grid their
 * periods are whole numbers of quarter samples and what folds lands on
 * their own harmonics or quarter-harmonics, never between them.
 *
 * Emerald itself selects the next mode, 8 bits at 65536 Hz (src/m4a.c:
 * m4aSoundInit asks for SOUND_MODE_DA_BIT_8, SoundInit writes SOUNDBIAS_H
 * bits 6-7 = 1); this grid stays at GBATEK's default, as docs/DECISIONS.md
 * records. SOUND_GRID_RATE_HZ is the grid's one constant (sound_mix.c's DC
 * pole is set for its 32768 Hz).
 *
 * Pure: no state of its own, no register access; the frame loop keeps the
 * grid's position and passes the FIFO frame in.
 */
#ifndef PLATFORM_SOUND_GRID_H
#define PLATFORM_SOUND_GRID_H

#include <stddef.h>
#include <stdint.h>

#include "gba_timing.h"

/* GBATEK SOUNDBIAS, Amplitude Resolution/Sampling Cycle 0: "9bit /
 * 32.768kHz", the default. */
#define SOUND_GRID_RATE_HZ 32768u
#define SOUND_GRID_CYCLES_PER_SAMPLE (GBA_CPU_HZ / SOUND_GRID_RATE_HZ)
_Static_assert(GBA_CPU_HZ % SOUND_GRID_RATE_HZ == 0, "a sampling cycle is a whole number of CPU cycles");

/* A frame of 280896 cycles holds 548 or 549 sampling cycles (548.625 on
 * average): at most this many. */
#define SOUND_GRID_MAX_SAMPLES_PER_FRAME \
    ((GBA_CYCLES_PER_FRAME + SOUND_GRID_CYCLES_PER_SAMPLE - 1u) / SOUND_GRID_CYCLES_PER_SAMPLE)

/* Where the grid stands at a frame boundary: the cycles since the last
 * output sample's span ended, 0 to SOUND_GRID_CYCLES_PER_SAMPLE - 1. Zero
 * (power-on) puts a span's end on the first frame's start. */
struct sound_grid {
    uint32_t carried_cycles;
};

/* One frame on the grid. Output sample k (0 <= k < samples) stands for the
 * 512-cycle span that ends at cycle 512 * (k + 1) - carried_cycles of the
 * frame (the first span began carried_cycles before the frame did); every
 * span ends inside the frame, and the ones that end in the next frame are
 * the next frame's. */
struct sound_grid_frame {
    uint32_t carried_cycles;
    size_t samples;
};

/* The frame starting at the grid's position, and the grid moved to the
 * next: samples = (carried + 280896) / 512, carried = (carried + 280896) %
 * 512. Integers only, so no drift: eight frames are 4389 samples exactly. */
struct sound_grid_frame sound_grid_next_frame(struct sound_grid *grid);

/* The FIFO frame on the grid: out[k] is the sample FIFO's DAC holds during
 * the last cycle of output sample k's span, frame->samples of them. The
 * frame's fifo_samples samples share its cycles evenly: sample i is held for
 * cycles [i * 280896 / n, (i + 1) * 280896 / n), the timer's interval
 * (280896 / n cycles, a whole number for every count SampleFreqSet's table
 * holds: 1254 for 224). The last cycle of a span is the cycle the PSG's
 * sample is taken at too (the channels' state after the span), and the
 * last span ends inside the frame, so the lookup never reaches past the
 * FIFO frame. frame is one sound_grid_next_frame returned (carried below
 * 512, its sample count from it), fifo_samples at least 1, out room for
 * frame->samples. */
void sound_grid_hold_fifo(const struct sound_grid_frame *frame, const int8_t *fifo, size_t fifo_samples, int8_t *out);

#endif /* PLATFORM_SOUND_GRID_H */
