/*
 * sound_mix.c: the GBA's sound mixer and output capacitor (sound_mix.h).
 *
 * Written from GBATEK "GBA Sound Control Registers"
 * (https://problemkaputt.de/gbatek.htm#gbasoundcontrolregisters: SOUNDCNT_H,
 * SOUNDCNT_X, "Max Output Levels"). The scale follows psg.c's "OUTPUT SCALE" so the two
 * FIFOs and the PSG fit int16_t together.
 *
 * DC removal: y[n] = x[n] - x[n-1] + R * y[n-1], a first-order high-pass
 * whose corner is fc = fs * (1 - R) / 2pi. The corner is held at 67 / 2pi,
 * about 10.7 Hz, below anything the game plays, as the GBA's output
 * capacitor is; with R = (fs - 67) / fs that is fs * (67 / fs) / 2pi for any
 * output rate fs (frame.h), so the pole follows the grid:
 *   at 65536 Hz, R = 65469 / 65536 (about 0.99898, 1 - R = 67 / 65536);
 *   at 32768 Hz it was 32701 / 32768, at 13379 Hz 32604 / 32768 (before).
 * Integer arithmetic, rounding toward zero.
 */
#include "sound_mix.h"

#include <stdbool.h>

#include "sound_grid.h"

/* 2pi * 10.66 Hz, in radians a second: the corner, as 1 - R = this / fs. */
#define DC_FILTER_CORNER_RADIANS_PER_SECOND 67
#define DC_FILTER_POLE_DENOMINATOR ((int64_t)SOUND_GRID_RATE_HZ)
#define DC_FILTER_POLE_NUMERATOR (DC_FILTER_POLE_DENOMINATOR - DC_FILTER_CORNER_RADIANS_PER_SECOND)

/* The output channels, indexes into struct sound_mix_state and a frame. */
enum sound_mix_side { SOUND_MIX_LEFT = 0, SOUND_MIX_RIGHT = 1 };

static int32_t fifo_scale(uint16_t soundcnt_h, uint16_t full_bit)
{
    return (soundcnt_h & full_bit) ? SOUND_MIX_FIFO_FULL_SCALE : SOUND_MIX_FIFO_HALF_SCALE;
}

static int16_t saturate(int32_t value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)value;
}

static int32_t remove_dc(struct sound_mix_state *state, enum sound_mix_side side, int32_t input)
{
    /* 64-bit product: the output can swing to about twice the input's range. */
    int64_t decayed = (int64_t)state->previous_output[side] * DC_FILTER_POLE_NUMERATOR / DC_FILTER_POLE_DENOMINATOR;
    int32_t output = input - state->previous_input[side] + (int32_t)decayed;
    state->previous_input[side] = input;
    state->previous_output[side] = output;
    return output;
}

void sound_mix(struct sound_mix_state *state, const struct sound_mix_registers *registers, const int8_t *fifo_a,
               const int8_t *fifo_b, const int16_t *psg, int16_t *out, size_t frames)
{
    uint16_t h = registers->soundcnt_h;
    bool enabled = (registers->soundcnt_x & SOUND_MIX_SOUNDCNT_X_MASTER_ENABLE) != 0;
    int32_t a_scale = fifo_scale(h, SOUND_MIX_SOUNDCNT_H_A_FULL);
    int32_t b_scale = fifo_scale(h, SOUND_MIX_SOUNDCNT_H_B_FULL);

    for (size_t i = 0; i < frames; i++) {
        int32_t mix[2] = {0, 0};
        if (enabled) {
            int32_t a = fifo_a != NULL ? fifo_a[i] * a_scale : 0;
            int32_t b = fifo_b != NULL ? fifo_b[i] * b_scale : 0;
            if (h & SOUND_MIX_SOUNDCNT_H_A_LEFT)
                mix[SOUND_MIX_LEFT] += a;
            if (h & SOUND_MIX_SOUNDCNT_H_A_RIGHT)
                mix[SOUND_MIX_RIGHT] += a;
            if (h & SOUND_MIX_SOUNDCNT_H_B_LEFT)
                mix[SOUND_MIX_LEFT] += b;
            if (h & SOUND_MIX_SOUNDCNT_H_B_RIGHT)
                mix[SOUND_MIX_RIGHT] += b;
            if (psg != NULL) {
                mix[SOUND_MIX_LEFT] += psg[2 * i];
                mix[SOUND_MIX_RIGHT] += psg[2 * i + 1];
            }
        }
        out[2 * i] = saturate(remove_dc(state, SOUND_MIX_LEFT, mix[SOUND_MIX_LEFT]));
        out[2 * i + 1] = saturate(remove_dc(state, SOUND_MIX_RIGHT, mix[SOUND_MIX_RIGHT]));
    }
}
