/*
 * test_sound_mix.c: the GBA's sound mixer (sound_mix.h).
 *
 * Expected values from GBATEK "GBA Sound Control Registers": SOUNDCNT_H
 * routes Direct Sound A and B to the right and left outputs at 50% or 100%,
 * SOUNDCNT_X bit 7 is the master enable; the scale (a FIFO sample s is 4s
 * units at 100%, 16 int16_t steps per unit) is psg.c's "OUTPUT SCALE". The
 * DC filter is a first-order high-pass: a step passes at once and decays,
 * a constant input decays to zero.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ptest.h"
#include "sound_mix.h"

#define ENABLED UINT16_C(0x0080)
#define FRAMES 4u

static void mix_once(const struct sound_mix_registers *registers, const int8_t *a, const int8_t *b,
                     const int16_t *psg, int16_t *out, size_t frames)
{
    struct sound_mix_state state;
    memset(&state, 0, sizeof state);
    sound_mix(&state, registers, a, b, psg, out, frames);
}

PTEST(fifo_a_right_and_b_left_at_full_volume)
{
    /* What src/m4a.c SoundInit writes: A right at 100%, B left at 100%. */
    struct sound_mix_registers r = {
        .soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_RIGHT | SOUND_MIX_SOUNDCNT_H_B_LEFT | SOUND_MIX_SOUNDCNT_H_A_FULL
                      | SOUND_MIX_SOUNDCNT_H_B_FULL,
        .soundcnt_x = ENABLED,
    };
    const int8_t a[1] = { 10 }, b[1] = { -3 };
    int16_t out[2];
    mix_once(&r, a, b, NULL, out, 1);
    /* The first sample of a step passes the DC filter unchanged. */
    PT_ASSERT_EQ_INT(out[0], -3 * SOUND_MIX_FIFO_FULL_SCALE);
    PT_ASSERT_EQ_INT(out[1], 10 * SOUND_MIX_FIFO_FULL_SCALE);
}

PTEST(half_volume_halves)
{
    struct sound_mix_registers r = {
        .soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_LEFT | SOUND_MIX_SOUNDCNT_H_B_RIGHT,
        .soundcnt_x = ENABLED,
    };
    const int8_t a[1] = { 100 }, b[1] = { -128 };
    int16_t out[2];
    mix_once(&r, a, b, NULL, out, 1);
    PT_ASSERT_EQ_INT(out[0], 100 * SOUND_MIX_FIFO_HALF_SCALE);
    PT_ASSERT_EQ_INT(out[1], -128 * SOUND_MIX_FIFO_HALF_SCALE);
}

PTEST(both_fifos_on_both_sides_add)
{
    struct sound_mix_registers r = {
        .soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_LEFT | SOUND_MIX_SOUNDCNT_H_A_RIGHT | SOUND_MIX_SOUNDCNT_H_B_LEFT
                      | SOUND_MIX_SOUNDCNT_H_B_RIGHT | SOUND_MIX_SOUNDCNT_H_A_FULL,
        .soundcnt_x = ENABLED,
    };
    const int8_t a[1] = { 2 }, b[1] = { 4 };
    int16_t out[2];
    mix_once(&r, a, b, NULL, out, 1);
    PT_ASSERT_EQ_INT(out[0], 2 * SOUND_MIX_FIFO_FULL_SCALE + 4 * SOUND_MIX_FIFO_HALF_SCALE);
    PT_ASSERT_EQ_INT(out[1], out[0]);
}

PTEST(unrouted_fifos_and_missing_input_are_silent)
{
    struct sound_mix_registers r = { .soundcnt_h = 0, .soundcnt_x = ENABLED };
    const int8_t a[FRAMES] = { 50, 50, 50, 50 };
    int16_t out[2 * FRAMES];
    mix_once(&r, a, a, NULL, out, FRAMES);
    for (size_t i = 0; i < 2 * FRAMES; i++)
        PT_ASSERT_EQ_INT(out[i], 0);
    r.soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_LEFT | SOUND_MIX_SOUNDCNT_H_B_RIGHT;
    mix_once(&r, NULL, NULL, NULL, out, FRAMES);
    for (size_t i = 0; i < 2 * FRAMES; i++)
        PT_ASSERT_EQ_INT(out[i], 0);
}

PTEST(psg_is_added_per_side)
{
    struct sound_mix_registers r = { .soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_RIGHT, .soundcnt_x = ENABLED };
    const int8_t a[1] = { 1 };
    const int16_t psg[2] = { 1000, 2000 };
    int16_t out[2];
    mix_once(&r, a, NULL, psg, out, 1);
    PT_ASSERT_EQ_INT(out[0], 1000);
    PT_ASSERT_EQ_INT(out[1], 2000 + SOUND_MIX_FIFO_HALF_SCALE);
}

PTEST(master_disable_silences_everything)
{
    struct sound_mix_registers r = { .soundcnt_h = 0x330C, .soundcnt_x = 0 };
    const int8_t a[1] = { 127 }, b[1] = { -128 };
    const int16_t psg[2] = { 15360, 15360 };
    int16_t out[2];
    mix_once(&r, a, b, psg, out, 1);
    PT_ASSERT_EQ_INT(out[0], 0);
    PT_ASSERT_EQ_INT(out[1], 0);
}

PTEST(a_constant_offset_decays_to_zero)
{
    /* The PSG's output is never negative (psg.h): its DC offset goes, as the
     * GBA's output capacitor removes it. */
    struct sound_mix_registers r = { .soundcnt_h = 0, .soundcnt_x = ENABLED };
    enum { LONG = 4000 };
    static int16_t psg[2 * LONG], out[2 * LONG];
    for (size_t i = 0; i < 2 * LONG; i++)
        psg[i] = 8000;
    mix_once(&r, NULL, NULL, psg, out, LONG);
    PT_ASSERT_EQ_INT(out[0], 8000);
    PT_ASSERT(out[2] < out[0] && out[2] > 0); /* decaying */
    PT_ASSERT(out[2 * LONG - 2] >= 0 && out[2 * LONG - 2] < 10);
    PT_ASSERT(out[2 * LONG - 1] >= 0 && out[2 * LONG - 1] < 10);
}

PTEST(the_filter_state_carries_across_calls)
{
    struct sound_mix_registers r = { .soundcnt_h = 0, .soundcnt_x = ENABLED };
    const int16_t psg[2] = { 5000, 5000 };
    int16_t out[2];
    struct sound_mix_state state;
    memset(&state, 0, sizeof state);
    sound_mix(&state, &r, NULL, NULL, psg, out, 1);
    PT_ASSERT_EQ_INT(out[0], 5000);
    sound_mix(&state, &r, NULL, NULL, psg, out, 1);
    PT_ASSERT(out[0] < 5000 && out[0] > 4900); /* the same input again: no new step */
}

PTEST(extremes_saturate)
{
    /* The whole mix spans -16384..31616 at this scale (both FIFOs on one side
     * and the PSG at its maximum). After the filter has settled at one end,
     * a full swing to the other is a step of 48000, past int16_t: it is
     * clipped, not wrapped. */
    struct sound_mix_registers r = {
        .soundcnt_h = SOUND_MIX_SOUNDCNT_H_A_LEFT | SOUND_MIX_SOUNDCNT_H_B_LEFT | SOUND_MIX_SOUNDCNT_H_A_FULL
                      | SOUND_MIX_SOUNDCNT_H_B_FULL,
        .soundcnt_x = ENABLED,
    };
    enum { SETTLE = 4000 };
    static int8_t high[SETTLE], low[SETTLE];
    static int16_t psg_high[2 * SETTLE], out[2 * SETTLE];
    memset(high, 127, sizeof high);
    memset(low, -128, sizeof low);
    for (size_t i = 0; i < SETTLE; i++)
        psg_high[2 * i] = 15360;
    struct sound_mix_state state;
    memset(&state, 0, sizeof state);
    sound_mix(&state, &r, low, low, NULL, out, SETTLE);          /* settles near 0 at -16384 */
    sound_mix(&state, &r, high, high, psg_high, out, 1);         /* +48000 */
    PT_ASSERT_EQ_INT(out[0], INT16_MAX);
    sound_mix(&state, &r, high, high, psg_high, out, SETTLE);    /* settles at 31616 */
    sound_mix(&state, &r, low, low, NULL, out, 1);               /* -48000 */
    PT_ASSERT_EQ_INT(out[0], INT16_MIN);
}

PTEST(zero_frames_writes_nothing)
{
    struct sound_mix_registers r = { .soundcnt_h = 0, .soundcnt_x = ENABLED };
    int16_t out[2] = { 77, 77 };
    mix_once(&r, NULL, NULL, NULL, out, 0);
    PT_ASSERT_EQ_INT(out[0], 77);
    PT_ASSERT_EQ_INT(out[1], 77);
}

PTEST_MAIN()
