/*
 * psg_channel.c: the units every PSG channel is built from: the on flag with
 * its length counter and DAC (all four channels), the volume envelope
 * (channels 1, 2 and 4), the NRx2 DAC enable (channels 1, 2 and 4) and the
 * frequency timer (all four).
 *
 * Stands in for those parts of GBATEK "GBA Sound Channel 1 - Tone & Sweep"
 * (https://problemkaputt.de/gbatek.htm#gbasoundchannel1tonesweep), which the
 * other channel sections refer back to: "Sound length; units of
 * (64-n)/256s", "Length Flag (1=Stop output when length in NR11 expires)",
 * "Envelope Step-Time; units of n/64s (1-7, 0=No Envelope)", "Envelope
 * Direction", "Initial Volume of envelope". GBATEK gives the units; the
 * counter and timer behaviour (a step every step_time 64 Hz ticks, stopping
 * at 0 or 15; the DAC switched off by NRx2 bits 3-7 all zero) is the Game Boy
 * APU's, from Pan Docs "Audio Registers" and "Audio Details"
 * (https://gbdev.io/pandocs/Audio_details.html). Written from those
 * documents; no reference port's code.
 */
#include "psg_internal.h"

bool psg_channel_start(struct psg_channel *channel, bool dac_enabled, uint16_t length_ticks)
{
    if (!dac_enabled)
        return false;
    channel->on = true;
    channel->length.remaining = length_ticks;
    return true;
}

void psg_channel_update(struct psg_channel *channel, uint8_t nrx4, bool dac_enabled)
{
    channel->length.enabled = (nrx4 & PSG_NRX4_LENGTH_ENABLE) != 0;
    if (!dac_enabled)
        channel->on = false;
}

void psg_channel_clock_length(struct psg_channel *channel)
{
    struct psg_length *const length = &channel->length;
    if (!length->enabled || length->remaining == 0)
        return;
    length->remaining--;
    if (length->remaining == 0)
        channel->on = false;
}

void psg_envelope_trigger(struct psg_envelope *envelope, uint8_t nrx2)
{
    envelope->volume = (uint8_t)(nrx2 >> PSG_NRX2_VOLUME_SHIFT);
    envelope->step_time = (uint8_t)(nrx2 & PSG_NRX2_STEP_TIME_MASK);
    envelope->timer = envelope->step_time;
    envelope->increase = (nrx2 & PSG_NRX2_INCREASE) != 0;
}

void psg_envelope_clock(struct psg_envelope *envelope)
{
    if (envelope->step_time == 0)
        return; /* "0=No Envelope" */
    envelope->timer--;
    if (envelope->timer != 0)
        return;
    envelope->timer = envelope->step_time;
    if (envelope->increase && envelope->volume < PSG_LEVEL_MAX)
        envelope->volume++;
    else if (!envelope->increase && envelope->volume > 0)
        envelope->volume--;
}

bool psg_dac_enabled(uint8_t nrx2)
{
    return (nrx2 & PSG_NRX2_DAC_MASK) != 0;
}

uint32_t psg_timer_run(uint32_t *countdown, uint32_t period, uint32_t cycles)
{
    if (cycles < *countdown) {
        *countdown -= cycles;
        return 0;
    }
    /* Expires once after *countdown cycles, then once per period. */
    const uint32_t after_first = cycles - *countdown;
    *countdown = period - after_first % period;
    return 1 + after_first / period;
}
