/*
 * psg_square.c: sound channels 1 and 2, the tone channels, and channel 1's
 * frequency sweep.
 *
 * Stands in for GBATEK "GBA Sound Channel 1 - Tone & Sweep" and "GBA Sound
 * Channel 2 - Tone" (https://problemkaputt.de/gbatek.htm#gbasoundchannel1tonesweep).
 * On the GBA each channel outputs a pulse wave: a frequency timer steps
 * through one of four eight-step duty patterns at 1048576/(2048-n) Hz (eight
 * steps per period of the documented 131072/(2048-n) Hz tone), the pattern's
 * high steps output the envelope's level and the low steps 0; the length
 * counter and envelope are the shared units of psg_channel.c. Channel 1's
 * sweep recomputes the frequency every sweep time * 7.8 ms as
 * "X(t) = X(t-1) +/- X(t-1)/2^n" and writes it back to NR13/NR14.
 *
 * GBATEK gives the formula but not the sweep unit's internals; those follow
 * Pan Docs "Audio Details", Pulse channel with sweep
 * (https://gbdev.io/pandocs/Audio_details.html): a shadow copy of the
 * frequency taken on trigger; on trigger, the overflow check runs at once if
 * the shift is non-zero; on each sweep step the new frequency is written back
 * only if the shift is non-zero and it does not overflow, then the check runs
 * again on the value after it; any result above 2047 stops the channel; a
 * sweep time of 0 reloads the sweep timer with 8 and does no steps. Also from
 * Pan Docs: a trigger restarts the frequency timer but not the duty step,
 * which only power-off resets. Written from those documents; no reference
 * port's code.
 *
 * Not modelled: the Game Boy's negate quirk, where clearing NR10's direction
 * bit after a decreasing calculation since the last trigger stops channel 1
 * at once (Pan Docs "Audio Details", Obscure Behavior); GBATEK does not
 * describe it for the GBA.
 */
#include "psg_internal.h"

/* NRx1 (low byte of SOUND1CNT_H, SOUND2CNT_L): bits 0-5 "Sound length;
 * units of (64-n)/256s", bits 6-7 "Wave Pattern Duty". */
#define NRX1_LENGTH_MASK 0x3Fu
#define NRX1_DUTY_SHIFT 6u
#define SQUARE_LENGTH_UNITS 64u

/* GBATEK "Wave Duty": 0: 12.5% "-_______", 1: 25% "--______", 2: 50%
 * "----____", 3: 75% "------__". One bit per step, step 0 in bit 0. */
#define DUTY_STEPS 8u
static const uint8_t duty_patterns[] = { 0x01, 0x03, 0x0F, 0x3F };

/* GBATEK NR13/NR14 "Frequency; 131072/(2048-n)Hz", eight duty steps per
 * period: steps at 1048576/(2048-n) Hz, (2048-n) * 16 system clock cycles
 * apart. (The Game Boy's (2048-n) * 4 cycles at its 4.19 MHz clock, the same
 * rate.) */
#define SQUARE_TONE_BASE_HZ UINT32_C(131072)
#define SQUARE_CYCLES_PER_UNIT (PSG_SYSTEM_CLOCK_HZ / (SQUARE_TONE_BASE_HZ * DUTY_STEPS))

/* NR10 (SOUND1CNT_L): bits 0-2 "Number of sweep shift (n=0-7)", bit 3
 * "Sweep Frequency Direction (0=Increase, 1=Decrease)", bits 4-6 "Sweep
 * Time; units of 7.8ms (0-7)". */
#define NR10_SHIFT_MASK 0x07u
#define NR10_DECREASE 0x08u
#define NR10_TIME_SHIFT 4u
#define NR10_TIME_MASK 0x07u
/* Pan Docs "Audio Details", Obscure Behavior: "The volume envelope and sweep
 * timers treat a period of 0 as 8." */
#define SWEEP_TIMER_FOR_TIME_ZERO 8u

static uint32_t step_period(uint16_t frequency)
{
    return (PSG_FREQUENCY_RANGE - frequency) * SQUARE_CYCLES_PER_UNIT;
}

static uint16_t frequency_of(const struct psg_square_registers *registers)
{
    return (uint16_t)(registers->nrx3 | (registers->nrx4 & PSG_NRX4_FREQUENCY_HIGH_MASK)
                                            << PSG_NRX4_FREQUENCY_HIGH_SHIFT);
}

void psg_square_update(struct psg_square *square, const struct psg_square_registers *registers)
{
    square->duty = (uint8_t)(registers->nrx1 >> NRX1_DUTY_SHIFT);
    square->frequency = frequency_of(registers);
    psg_channel_update(&square->channel, registers->nrx4, psg_dac_enabled(registers->nrx2));
}

void psg_square_trigger(struct psg_square *square, const struct psg_square_registers *registers)
{
    const uint16_t length = (uint16_t)(SQUARE_LENGTH_UNITS - (registers->nrx1 & NRX1_LENGTH_MASK));
    if (!psg_channel_start(&square->channel, psg_dac_enabled(registers->nrx2), length))
        return;
    square->countdown = step_period(square->frequency);
    psg_envelope_trigger(&square->envelope, registers->nrx2);
}

void psg_square_run(struct psg_square *square, uint32_t cycles)
{
    if (!square->channel.on)
        return;
    const uint32_t steps = psg_timer_run(&square->countdown, step_period(square->frequency), cycles);
    square->duty_step = (uint8_t)((square->duty_step + steps % DUTY_STEPS) % DUTY_STEPS);
}

uint8_t psg_square_level(const struct psg_square *square)
{
    if (!square->channel.on || !(duty_patterns[square->duty] >> square->duty_step & 1u))
        return 0;
    return square->envelope.volume;
}

static uint8_t sweep_time(const struct psg_sweep *sweep)
{
    return (uint8_t)(sweep->nr10 >> NR10_TIME_SHIFT & NR10_TIME_MASK);
}

static uint8_t sweep_shift(const struct psg_sweep *sweep)
{
    return (uint8_t)(sweep->nr10 & NR10_SHIFT_MASK);
}

static void reload_sweep_timer(struct psg_sweep *sweep)
{
    const uint8_t time = sweep_time(sweep);
    sweep->timer = time != 0 ? time : SWEEP_TIMER_FOR_TIME_ZERO;
}

/* GBATEK: X(t) = X(t-1) +/- X(t-1)/2^n. May exceed PSG_FREQUENCY_MAX when
 * increasing; decreasing cannot go below 0. */
static uint32_t next_frequency(const struct psg_sweep *sweep)
{
    const uint32_t delta = (uint32_t)sweep->shadow >> sweep_shift(sweep);
    return (sweep->nr10 & NR10_DECREASE) ? sweep->shadow - delta : sweep->shadow + delta;
}

/* Stops the channel if the next frequency would overflow 11 bits. */
static void check_overflow(const struct psg_sweep *sweep, struct psg_square *square)
{
    if (next_frequency(sweep) > PSG_FREQUENCY_MAX)
        square->channel.on = false;
}

void psg_sweep_update(struct psg_sweep *sweep, uint8_t nr10)
{
    sweep->nr10 = nr10;
}

void psg_sweep_trigger(struct psg_sweep *sweep, struct psg_square *square)
{
    if (!square->channel.on)
        return;
    sweep->shadow = square->frequency;
    reload_sweep_timer(sweep);
    sweep->enabled = sweep_time(sweep) != 0 || sweep_shift(sweep) != 0;
    if (sweep_shift(sweep) != 0)
        check_overflow(sweep, square);
}

void psg_sweep_clock(struct psg_sweep *sweep, struct psg_square *square)
{
    if (!square->channel.on)
        return;
    if (sweep->timer > 1) {
        sweep->timer--;
        return;
    }
    reload_sweep_timer(sweep);
    if (!sweep->enabled || sweep_time(sweep) == 0)
        return;
    const uint32_t next = next_frequency(sweep);
    if (next > PSG_FREQUENCY_MAX) {
        square->channel.on = false;
        return;
    }
    if (sweep_shift(sweep) == 0)
        return;
    sweep->shadow = (uint16_t)next;
    square->frequency = (uint16_t)next;
    check_overflow(sweep, square);
}
