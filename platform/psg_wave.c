/*
 * psg_wave.c: sound channel 3, wave output.
 *
 * Stands in for GBATEK "GBA Sound Channel 3 - Wave Output"
 * (https://problemkaputt.de/gbatek.htm#gbasoundchannel3waveoutput). On the
 * GBA the channel plays 4-bit samples from wave RAM, one every
 * (2048-n) * 8 system clock cycles ("Sample Rate; 2097152/(2048-n) Hz"),
 * "MSBs of 1st byte, followed by LSBs of 1st byte, followed by MSBs of 2nd
 * byte". Wave RAM is two banks of 32 samples; NR30 bit 6 selects the bank
 * that plays, and in two-bank mode (bit 5) "output will start by replaying
 * the currently selected bank" and continues into the other. NR32 scales the
 * sample: mute, 100%, 50%, 25%, or 75% when bit 7 forces it; the shifts
 * truncate (Pan Docs NR32: "shift samples read from Wave RAM right once"),
 * and 75% is (3 * sample) / 4, truncated the same way (as mGBA's
 * src/gba/audio.c and src/gb/audio.c compute it). NR30 bit 7 is the
 * channel's DAC: clear, it stops the channel and a trigger does nothing.
 *
 * WAVE RAM BANKS. Hardware routes the window to the unselected bank at each
 * write; this module sees the window once per psg_generate() call, not each
 * write. m4a (src/m4a.c CgbSound, channel 3) loads a wave as "NR30 = 40h,
 * write the four WAVE_RAM words, NR30 = 00h, later 80h" within one frame, so
 * GBATEK's rule applied at the call would play the old bank and silence every
 * m4a wave instrument. Rule here: a window that changed since the last call
 * goes into the bank selected for playback at this call. That matches
 * GBATEK for m4a's "select the other bank, write, select back" and for
 * double buffering's "write the idle bank, then select it". Wrong only if a
 * call lands between a write and its bank switch, or the game rewrites
 * identical bytes meant for the other bank (undetectable in plain memory).
 * The window's memory is never written by this module.
 *
 * Not modelled (Pan Docs "Audio Details"): the first sample after a trigger
 * repeating the previous one, and GBATEK's shift-register rotation of wave
 * RAM visible to reads while playing. A trigger starts at sample 0. Written
 * from those documents; no reference port's code.
 */
#include <string.h>

#include "psg_internal.h"

/* NR30 (SOUND3CNT_L): bit 5 "Wave RAM Dimension (0=One bank/32 digits,
 * 1=Two banks/64 digits)", bit 6 "Wave RAM Bank Number", bit 7 "Sound
 * Channel 3 Off (0=Stop, 1=Playback)". */
#define NR30_TWO_BANKS 0x20u
#define NR30_BANK_SHIFT 6u
#define NR30_BANK_MASK 0x01u
#define NR30_PLAYBACK 0x80u

/* NR32 (high byte of SOUND3CNT_H): bits 5-6 "Sound Volume (0=Mute/Zero,
 * 1=100%, 2=50%, 3=25%)", bit 7 "Force Volume (1=Force 75%)". */
#define NR32_VOLUME_SHIFT 5u
#define NR32_VOLUME_MASK 0x03u
#define NR32_FORCE_75 0x80u
#define VOLUME_MUTE 0u
static const uint8_t volume_shift[] = {0, 0, 1, 2}; /* by NR32 volume; 0 is muted instead */
#define FORCED_VOLUME_NUMERATOR 3u
#define FORCED_VOLUME_DENOMINATOR 4u

/* NR31 (low byte of SOUND3CNT_H): "Sound length; units of (256-n)/256s". */
#define WAVE_LENGTH_UNITS 256u

/* NR33/NR34: "Sample Rate; 2097152/(2048-n) Hz", (2048-n) * 8 cycles. */
#define WAVE_SAMPLE_BASE_HZ UINT32_C(2097152)
#define WAVE_CYCLES_PER_UNIT (PSG_SYSTEM_CLOCK_HZ / WAVE_SAMPLE_BASE_HZ)

/* Two 4-bit samples per byte, the high nibble first. */
#define SAMPLES_PER_BYTE 2u
#define SAMPLES_PER_BANK (PSG_WAVE_BANK_BYTES * SAMPLES_PER_BYTE)
#define NIBBLE_SHIFT 4u
#define NIBBLE_MASK 0x0Fu

static uint32_t sample_period(uint16_t frequency)
{
    return (PSG_FREQUENCY_RANGE - frequency) * WAVE_CYCLES_PER_UNIT;
}

static unsigned selected_bank(const struct psg_wave *wave)
{
    return wave->nr30 >> NR30_BANK_SHIFT & NR30_BANK_MASK;
}

static bool two_banks(const struct psg_wave *wave)
{
    return (wave->nr30 & NR30_TWO_BANKS) != 0;
}

static unsigned samples_in_play(const struct psg_wave *wave)
{
    return two_banks(wave) ? PSG_WAVE_BANK_COUNT * SAMPLES_PER_BANK : SAMPLES_PER_BANK;
}

/* Copies a window the game has written since the last update into the bank
 * selected for playback (the header comment says why). */
static void latch_window(struct psg_wave *wave, const uint8_t window[PSG_WAVE_BANK_BYTES])
{
    if (memcmp(window, wave->window_seen, PSG_WAVE_BANK_BYTES) == 0)
        return;
    memcpy(wave->banks[selected_bank(wave)], window, PSG_WAVE_BANK_BYTES);
    memcpy(wave->window_seen, window, PSG_WAVE_BANK_BYTES);
}

/* NR30 bit 7 is channel 3's DAC. */
static enum psg_dac wave_dac(uint8_t nr30)
{
    return (nr30 & NR30_PLAYBACK) != 0 ? PSG_DAC_ON : PSG_DAC_OFF;
}

void psg_wave_update(struct psg_wave *wave, const struct psg_wave_registers *registers)
{
    wave->nr30 = registers->nr30;
    wave->nr32 = registers->nr32;
    wave->frequency = (uint16_t)(registers->nr33
                                 | (registers->nr34 & PSG_NRX4_FREQUENCY_HIGH_MASK) << PSG_NRX4_FREQUENCY_HIGH_SHIFT);
    latch_window(wave, registers->window);
    psg_channel_update(&wave->channel, registers->nr34, wave_dac(registers->nr30));
}

void psg_wave_trigger(struct psg_wave *wave, const struct psg_wave_registers *registers)
{
    const uint16_t length = (uint16_t)(WAVE_LENGTH_UNITS - registers->nr31);
    if (!psg_channel_start(&wave->channel, wave_dac(registers->nr30), length))
        return;
    wave->position = 0;
    wave->countdown = sample_period(wave->frequency);
}

void psg_wave_run(struct psg_wave *wave, uint32_t cycles)
{
    if (!wave->channel.on)
        return;
    const uint32_t steps = psg_timer_run(&wave->countdown, sample_period(wave->frequency), cycles);
    const unsigned samples = samples_in_play(wave);
    wave->position = (uint8_t)((wave->position + steps % samples) % samples);
}

/* The 4-bit sample at the current position: the selected bank's 32, then, in
 * two-bank mode, the other bank's. */
static uint8_t current_sample(const struct psg_wave *wave)
{
    const unsigned position = wave->position;
    const bool second_bank = two_banks(wave) && position >= SAMPLES_PER_BANK;
    const unsigned bank = second_bank ? selected_bank(wave) ^ 1u : selected_bank(wave);
    const unsigned index = position % SAMPLES_PER_BANK;
    const uint8_t byte = wave->banks[bank][index / SAMPLES_PER_BYTE];
    return (uint8_t)(index % SAMPLES_PER_BYTE == 0 ? byte >> NIBBLE_SHIFT : byte & NIBBLE_MASK);
}

uint8_t psg_wave_level(const struct psg_wave *wave)
{
    if (!wave->channel.on)
        return 0;
    const uint8_t sample = current_sample(wave);
    if (wave->nr32 & NR32_FORCE_75)
        return (uint8_t)(sample * FORCED_VOLUME_NUMERATOR / FORCED_VOLUME_DENOMINATOR);
    const unsigned volume = wave->nr32 >> NR32_VOLUME_SHIFT & NR32_VOLUME_MASK;
    if (volume == VOLUME_MUTE)
        return 0;
    return (uint8_t)(sample >> volume_shift[volume]);
}

void psg_wave_power_off(struct psg_wave *wave)
{
    wave->channel = (struct psg_channel){0};
    wave->nr30 = 0;
    wave->nr32 = 0;
    wave->frequency = 0;
    wave->countdown = 0;
    wave->position = 0;
}
