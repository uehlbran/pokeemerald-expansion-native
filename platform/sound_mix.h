/*
 * sound_mix.h: the GBA's sound mixer, from the two Direct Sound FIFOs and
 * the PSG to one stereo output.
 *
 * Stands in for the mixing half of the GBA Sound Controller (GBATEK "GBA
 * Sound Controller", "GBA Sound Control Registers"): SOUNDCNT_H routes FIFO
 * A and FIFO B to the right and/or left output at 50% or 100%, adds them to
 * the PSG's output, and SOUNDCNT_X bit 7 turns everything off. The output
 * capacitor after the GBA's DAC removes the mix's DC offset; here a one-pole
 * high-pass filter does (sound_mix.c). The PSG's samples come from psg.h,
 * already scaled by its own volume bits and at the scale its header derives;
 * the FIFO samples are added at that scale. Pure: no register access, so
 * the frame loop passes the register values in.
 *
 * GBATEK: https://problemkaputt.de/gbatek.htm#gbasoundcontroller and
 * https://problemkaputt.de/gbatek.htm#gbasoundcontrolregisters (SOUNDCNT_H,
 * SOUNDCNT_X, Max Output Levels).
 */
#ifndef SOUND_MIX_H
#define SOUND_MIX_H

#include <stddef.h>
#include <stdint.h>

/* GBATEK "GBA Sound Control Registers", 4000082h SOUNDCNT_H: bit 2 DMA
 * Sound A volume and bit 3 B (0 = 50%, 1 = 100%), bits 8/9 A to right/left,
 * bits 12/13 B to right/left. */
#define SOUND_MIX_SOUNDCNT_H_A_FULL UINT16_C(0x0004)
#define SOUND_MIX_SOUNDCNT_H_B_FULL UINT16_C(0x0008)
#define SOUND_MIX_SOUNDCNT_H_A_RIGHT UINT16_C(0x0100)
#define SOUND_MIX_SOUNDCNT_H_A_LEFT UINT16_C(0x0200)
#define SOUND_MIX_SOUNDCNT_H_B_RIGHT UINT16_C(0x1000)
#define SOUND_MIX_SOUNDCNT_H_B_LEFT UINT16_C(0x2000)
/* 4000084h SOUNDCNT_X bit 7: "PSG/FIFO Master Enable (0=Disable, 1=Enable)". */
#define SOUND_MIX_SOUNDCNT_X_MASTER_ENABLE UINT16_C(0x0080)

/* A FIFO sample s at 100% is 4s units of the 10-bit output (GBATEK: each
 * FIFO spans +/-200h), and psg.h writes 16 int16_t steps per unit, so s
 * contributes 64s at 100% and 32s at 50%. */
#define SOUND_MIX_FIFO_FULL_SCALE 64
#define SOUND_MIX_FIFO_HALF_SCALE 32

/* The DC filter's memory, per output channel; zero is the power-on state. */
struct sound_mix_state {
    int32_t previous_input[2];
    int32_t previous_output[2];
};

/* The register values the mix depends on. */
struct sound_mix_registers {
    uint16_t soundcnt_h;
    uint16_t soundcnt_x;
};

/* Mixes `frames` stereo frames into out (interleaved left, right):
 * fifo_a[i] and fifo_b[i] (NULL for a FIFO playing nothing) routed by
 * SOUNDCNT_H, plus psg[2i] (left) and psg[2i + 1] (right) (NULL for none),
 * then the DC filter, saturated to int16_t. With the master enable off the
 * input is silence, which the filter still runs on. */
void sound_mix(struct sound_mix_state *state, const struct sound_mix_registers *registers,
               const int8_t *fifo_a, const int8_t *fifo_b, const int16_t *psg, int16_t *out, size_t frames);

#endif /* SOUND_MIX_H */
