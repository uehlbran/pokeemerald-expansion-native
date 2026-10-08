/*
 * psg_internal.h: the PSG module's internal interface, shared by psg.c (the
 * register interface, frame sequencer and mixer) and the channel files
 * psg_channel.c (length counter, volume envelope, frequency timer),
 * psg_square.c (channels 1 and 2, and channel 1's sweep), psg_wave.c
 * (channel 3) and psg_noise.c (channel 4). Nothing outside the PSG includes
 * it except its unit test; the public interface is psg.h.
 *
 * The channel files decode their own registers' bit fields (GBATEK "GBA Sound
 * Channel 1 - Tone & Sweep" to "Channel 4 - Noise"); psg.c knows where the
 * registers are and when they are read. Each channel's state is updated from
 * its registers at every psg_generate() call, then run in system clock cycles.
 */
#ifndef PSG_INTERNAL_H
#define PSG_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

/* --- Timing --------------------------------------------------------------- */

/* GBATEK "LCD Dimensions and Timings", System Clock: "16.78MHz
 * (16*1024*1024 Hz)". Every PSG timer here counts these cycles. */
#define PSG_SYSTEM_CLOCK_HZ UINT32_C(16777216)

/* The frame sequencer. GBATEK gives the units of the three things it clocks:
 * length in "units of (64-n)/256s", sweep time in "units of 7.8ms" (1/128 s),
 * envelope step time in "units of n/64s". The Game Boy derives all three from
 * one 512 Hz counter, DIV-APU (Pan Docs "Audio Details"): length on the even
 * steps of eight (256 Hz), sweep on steps 2 and 6 (128 Hz), envelope on
 * step 7 (64 Hz). */
#define PSG_SEQUENCER_HZ 512u
#define PSG_CYCLES_PER_SEQUENCER_STEP (PSG_SYSTEM_CLOCK_HZ / PSG_SEQUENCER_HZ)
#define PSG_SEQUENCER_STEPS 8u
#define PSG_LENGTH_HZ 256u /* on every second step, from step 0 */
#define PSG_SEQUENCER_SWEEP_STEP_A 2u
#define PSG_SEQUENCER_SWEEP_STEP_B 6u
#define PSG_SEQUENCER_ENVELOPE_STEP 7u

/* Channels 1-3 take an 11-bit frequency n from NRx3 and NRx4 bits 0-2, and
 * GBATEK gives every rate as a constant divided by (2048 - n). */
#define PSG_FREQUENCY_RANGE 2048u
#define PSG_FREQUENCY_MAX (PSG_FREQUENCY_RANGE - 1u)

/* Digital channel output: 4 bits, 0-15 (GBATEK "Initial Volume of envelope
 * (1-15, 0=No Sound)"; wave RAM "32 x 4bit"). */
#define PSG_LEVEL_MAX 15u

/* --- Register bits shared by several channels ---------------------------- */

/* NRx4 (high byte of SOUNDxCNT_X / SOUNDxCNT_H): bit 7 "Initial (1=Restart
 * Sound)" (write-only), bit 6 "Length Flag (1=Stop output when length in NRx1
 * expires)", bits 0-2 the frequency's top three bits (channels 1-3). */
#define PSG_NRX4_INITIAL 0x80u
#define PSG_NRX4_LENGTH_ENABLE 0x40u
#define PSG_NRX4_FREQUENCY_HIGH_MASK 0x07u
#define PSG_NRX4_FREQUENCY_HIGH_SHIFT 8u

/* NRx2 of channels 1, 2 and 4 (high byte of SOUND1CNT_H, SOUND2CNT_L,
 * SOUND4CNT_L): bits 0-2 "Envelope Step-Time; units of n/64s (1-7, 0=No
 * Envelope)", bit 3 "Envelope Direction (0=Decrease, 1=Increase)", bits 4-7
 * "Initial Volume of envelope". Bits 3-7 all zero switch the channel's DAC
 * off, which also stops the channel (Pan Docs "Audio Details", DACs; on the
 * GBA a disabled DAC outputs 0). */
#define PSG_NRX2_STEP_TIME_MASK 0x07u
#define PSG_NRX2_INCREASE 0x08u
#define PSG_NRX2_VOLUME_SHIFT 4u
#define PSG_NRX2_DAC_MASK 0xF8u

/* --- Shared units (psg_channel.c) ---------------------------------------- */

/* Length counter: stops the channel after a number of 256 Hz ticks. */
struct psg_length {
    uint16_t remaining; /* ticks left; loaded on trigger */
    bool enabled;       /* NRx4 bit 6, applied at every update */
};

/* Loads the counter with the ticks the game's length field asks for. */
void psg_length_load(struct psg_length *length, uint16_t ticks);

/* One 256 Hz tick. Returns true when this tick expires the counter, meaning
 * the channel stops now; false otherwise, including when disabled. */
bool psg_length_clock(struct psg_length *length);

/* Volume envelope of channels 1, 2 and 4. */
struct psg_envelope {
    uint8_t volume;    /* current level, 0-15 */
    uint8_t step_time; /* 64 Hz ticks per step; 0 = no envelope */
    uint8_t timer;     /* 64 Hz ticks until the next step */
    bool increase;
};

/* Restarts the envelope from NRx2, as a trigger does. */
void psg_envelope_trigger(struct psg_envelope *envelope, uint8_t nrx2);

/* One 64 Hz tick: every step_time ticks, one level up or down, stopping at
 * 15 or 0. */
void psg_envelope_clock(struct psg_envelope *envelope);

/* Whether NRx2 leaves the channel's DAC on. */
bool psg_dac_enabled(uint8_t nrx2);

/* A channel's frequency timer: counts `period` cycles between steps. Runs it
 * for `cycles` cycles and returns how many steps (expiries) that covered.
 * Expiries reload from the period given, so a frequency change takes effect
 * from the next step. *countdown must be at least 1 and period at least 1. */
uint32_t psg_timer_run(uint32_t *countdown, uint32_t period, uint32_t cycles);

/* --- Channels 1 and 2: tone (psg_square.c) ------------------------------- */

/* The registers of one square channel as the game left them (channel 1's
 * NR10 goes to its sweep, struct psg_sweep). */
struct psg_square_registers {
    uint8_t nrx1, nrx2, nrx3, nrx4;
};

struct psg_square {
    bool on;               /* SOUNDCNT_X status bit */
    uint8_t duty;          /* NRx1 bits 6-7 */
    uint8_t duty_step;     /* 0-7, position in the duty pattern */
    uint16_t frequency;    /* 11 bits from NRx3 and NRx4 */
    uint32_t countdown;    /* cycles until the next duty step */
    struct psg_length length;
    struct psg_envelope envelope;
};

/* Takes the fields that act at once: duty, frequency, length enable, DAC
 * (switching the DAC off stops the channel). */
void psg_square_update(struct psg_square *square, const struct psg_square_registers *registers);

/* Restarts the channel (NRx4 bit 7); does nothing while the DAC is off. */
void psg_square_trigger(struct psg_square *square, const struct psg_square_registers *registers);

void psg_square_run(struct psg_square *square, uint32_t cycles);
void psg_square_clock_length(struct psg_square *square);
void psg_square_clock_envelope(struct psg_square *square);

/* Digital output, 0-15: the envelope's level while the duty pattern is high,
 * 0 while it is low or the channel is off. */
uint8_t psg_square_level(const struct psg_square *square);

/* Channel 1's frequency sweep (NR10). */
struct psg_sweep {
    uint8_t nr10;      /* as of the last update */
    uint16_t shadow;   /* the frequency the sweep computes from */
    uint8_t timer;     /* 128 Hz ticks until the next sweep step */
    bool enabled;
};

void psg_sweep_update(struct psg_sweep *sweep, uint8_t nr10);

/* The sweep's part of a trigger; call after psg_square_trigger(). May stop
 * the channel (the overflow check). */
void psg_sweep_trigger(struct psg_sweep *sweep, struct psg_square *square);

/* One 128 Hz tick. May change square->frequency (the game sees it in NR13
 * and NR14, which psg.c writes back) or stop the channel on overflow. */
void psg_sweep_clock(struct psg_sweep *sweep, struct psg_square *square);

/* --- Channel 3: wave output (psg_wave.c) --------------------------------- */

/* GBATEK "GBA Sound Channel 3 - Wave Output": two banks of 16 bytes,
 * "32 x 4bits" each; WAVE_RAM0-3 is a 16-byte window onto one of them. */
#define PSG_WAVE_BANK_BYTES 16u
#define PSG_WAVE_BANK_COUNT 2u

struct psg_wave_registers {
    uint8_t nr30, nr31, nr32, nr33, nr34;
    uint8_t window[PSG_WAVE_BANK_BYTES]; /* WAVE_RAM0-3 */
};

struct psg_wave {
    bool on;
    uint8_t nr30;          /* dimension, bank, DAC; as of the last update */
    uint8_t nr32;          /* volume; as of the last update */
    uint16_t frequency;
    uint32_t countdown;    /* cycles until the next sample */
    uint8_t position;      /* sample index, 0-31, or 0-63 in two-bank mode */
    struct psg_length length;
    uint8_t banks[PSG_WAVE_BANK_COUNT][PSG_WAVE_BANK_BYTES];
    uint8_t window_seen[PSG_WAVE_BANK_BYTES]; /* the window at the last update */
};

/* Takes NR30, NR32, the frequency and length enable, and any wave RAM
 * written since the last update. */
void psg_wave_update(struct psg_wave *wave, const struct psg_wave_registers *registers);
void psg_wave_trigger(struct psg_wave *wave, const struct psg_wave_registers *registers);
void psg_wave_run(struct psg_wave *wave, uint32_t cycles);
void psg_wave_clock_length(struct psg_wave *wave);

/* Digital output, 0-15: the current sample scaled by NR32's volume. */
uint8_t psg_wave_level(const struct psg_wave *wave);

/* Stops the channel and resets what power-off resets; wave RAM survives. */
void psg_wave_power_off(struct psg_wave *wave);

/* --- Channel 4: noise (psg_noise.c) -------------------------------------- */

/* NR43 bit 3, "Counter Step/Width (0=15 bits, 1=7 bits)". */
enum psg_lfsr_width {
    PSG_LFSR_15_BIT,
    PSG_LFSR_7_BIT
};

/* GBATEK "GBA Sound Channel 4 - Noise": "The initial value when
 * (re-)starting the sound is X=40h (7bit) or X=4000h (15bit)." */
uint16_t psg_lfsr_seed(enum psg_lfsr_width width);

/* One clock of GBATEK's random generator: "X=X SHR 1, IF carry THEN
 * Out=HIGH, X=X XOR 60h (7bit) / 6000h (15bit) ELSE Out=LOW". Returns the new
 * X; the output is the bit shifted out, lfsr & 1 before the call. */
uint16_t psg_lfsr_clock(uint16_t lfsr, enum psg_lfsr_width width);

struct psg_noise_registers {
    uint8_t nr41, nr42, nr43, nr44;
};

struct psg_noise {
    bool on;
    uint8_t nr43;          /* divider, width, shift; as of the last update */
    uint16_t lfsr;
    bool high;             /* the last bit shifted out */
    uint32_t countdown;    /* cycles until the next LFSR clock */
    struct psg_length length;
    struct psg_envelope envelope;
};

void psg_noise_update(struct psg_noise *noise, const struct psg_noise_registers *registers);
void psg_noise_trigger(struct psg_noise *noise, const struct psg_noise_registers *registers);
void psg_noise_run(struct psg_noise *noise, uint32_t cycles);
void psg_noise_clock_length(struct psg_noise *noise);
void psg_noise_clock_envelope(struct psg_noise *noise);
uint8_t psg_noise_level(const struct psg_noise *noise);

#endif /* PSG_INTERNAL_H */
