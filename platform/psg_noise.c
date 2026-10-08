/*
 * psg_noise.c: sound channel 4, noise.
 *
 * Stands in for GBATEK "GBA Sound Channel 4 - Noise"
 * (https://problemkaputt.de/gbatek.htm#gbasoundchannel4noise). On the GBA a
 * shift register X is clocked at "524288 Hz / r / 2^(s+1)" (r = 0 counting
 * as 0.5) and each clock does "X=X SHR 1, IF carry THEN Out=HIGH, X=X XOR
 * 60h ELSE Out=LOW" (6000h in 15-bit mode), from "X=40h (7bit) or X=4000h
 * (15bit)" on trigger; HIGH outputs the envelope's level, LOW outputs 0. The
 * length counter and envelope are the shared units of psg_channel.c.
 *
 * Follows GBATEK's generator exactly. Pan Docs "Audio Registers" describes
 * the Game Boy's as an XNOR register starting from 0 and says shifts 14 and
 * 15 stop the clock; GBATEK gives neither for the GBA, so neither is
 * modelled. Written from GBATEK; no reference port's code.
 */
#include "psg_internal.h"

/* NR43 (low byte of SOUND4CNT_H): bits 0-2 "Dividing Ratio of Frequencies
 * (r)", bit 3 "Counter Step/Width (0=15 bits, 1=7 bits)", bits 4-7 "Shift
 * Clock Frequency (s)". */
#define NR43_DIVIDER_MASK 0x07u
#define NR43_7_BIT 0x08u
#define NR43_SHIFT_SHIFT 4u

/* "Frequency = 524288 Hz / r / 2^(s+1) ;For r=0 assume r=0.5 instead":
 * r * 2^(s+1) * 32 system clock cycles per clock, 16 * 2^(s+1) for r = 0. */
#define NOISE_BASE_HZ UINT32_C(524288)
#define NOISE_CYCLES_PER_UNIT (PSG_SYSTEM_CLOCK_HZ / NOISE_BASE_HZ)
#define NOISE_HALF_UNIT_CYCLES (NOISE_CYCLES_PER_UNIT / 2u)

/* NR41 (low byte of SOUND4CNT_L): "Sound length; units of (64-n)/256s". */
#define NR41_LENGTH_MASK 0x3Fu
#define NOISE_LENGTH_UNITS 64u

/* GBATEK's seeds and XOR values. */
#define LFSR_7_BIT_SEED 0x0040u
#define LFSR_7_BIT_TAPS 0x0060u
#define LFSR_15_BIT_SEED 0x4000u
#define LFSR_15_BIT_TAPS 0x6000u

uint16_t psg_lfsr_seed(enum psg_lfsr_width width)
{
    return width == PSG_LFSR_7_BIT ? LFSR_7_BIT_SEED : LFSR_15_BIT_SEED;
}

uint16_t psg_lfsr_clock(uint16_t lfsr, enum psg_lfsr_width width)
{
    const bool carry = (lfsr & 1u) != 0;
    const uint16_t shifted = (uint16_t)(lfsr >> 1);
    if (!carry)
        return shifted;
    return (uint16_t)(shifted ^ (width == PSG_LFSR_7_BIT ? LFSR_7_BIT_TAPS : LFSR_15_BIT_TAPS));
}

static enum psg_lfsr_width width_of(uint8_t nr43)
{
    return (nr43 & NR43_7_BIT) ? PSG_LFSR_7_BIT : PSG_LFSR_15_BIT;
}

static uint32_t clock_period(uint8_t nr43)
{
    const uint32_t divider = nr43 & NR43_DIVIDER_MASK;
    const uint32_t shift = (uint32_t)(nr43 >> NR43_SHIFT_SHIFT);
    const uint32_t cycles = divider == 0 ? NOISE_HALF_UNIT_CYCLES : divider * NOISE_CYCLES_PER_UNIT;
    return cycles << (shift + 1u); /* at most 7 * 32 << 16, well inside 32 bits */
}

void psg_noise_update(struct psg_noise *noise, const struct psg_noise_registers *registers)
{
    noise->nr43 = registers->nr43;
    psg_channel_update(&noise->channel, registers->nr44, psg_dac_enabled(registers->nr42));
}

void psg_noise_trigger(struct psg_noise *noise, const struct psg_noise_registers *registers)
{
    const uint16_t length = (uint16_t)(NOISE_LENGTH_UNITS - (registers->nr41 & NR41_LENGTH_MASK));
    if (!psg_channel_start(&noise->channel, psg_dac_enabled(registers->nr42), length))
        return;
    noise->lfsr = psg_lfsr_seed(width_of(noise->nr43));
    noise->high = false;
    noise->countdown = clock_period(noise->nr43);
    psg_envelope_trigger(&noise->envelope, registers->nr42);
}

void psg_noise_run(struct psg_noise *noise, uint32_t cycles)
{
    if (!noise->channel.on)
        return;
    const uint32_t clocks = psg_timer_run(&noise->countdown, clock_period(noise->nr43), cycles);
    const enum psg_lfsr_width width = width_of(noise->nr43);
    for (uint32_t i = 0; i < clocks; i++) {
        noise->high = (noise->lfsr & 1u) != 0;
        noise->lfsr = psg_lfsr_clock(noise->lfsr, width);
    }
}

uint8_t psg_noise_level(const struct psg_noise *noise)
{
    return noise->channel.on && noise->high ? noise->envelope.volume : 0;
}
