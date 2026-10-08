// deps: gba_memmap.c psg_channel.c psg_square.c psg_wave.c psg_noise.c
/*
 * Tests of the PSG. Every expected value is worked out here by hand from
 * GBATEK "GBA Sound Controller" and its channel sections, with the Game Boy
 * frame sequencer from Pan Docs "Audio Details" (length 256 Hz, sweep 128 Hz,
 * envelope 64 Hz, from one 512 Hz counter), never read back from the module.
 * The register offsets are written out from GBATEK and checked against
 * expansion's own at compile time.
 *
 * Conventions the derivations rely on (documented in psg.c):
 *  - Time starts at the psg_generate() call in which the PSG first sees
 *    SOUNDCNT_X's master enable; its frame sequencer's first step (step 0)
 *    comes 1/512 s later. Length ticks on steps 0, 2, 4, 6: at 1, 3, 5, 7 ...
 *    /512 s. Sweep ticks on steps 2 and 6: at 3, 7, 11 ... /512 s. Envelope
 *    ticks on step 7: at 8, 16, 24 ... /512 s.
 *  - Output sample k is the state at the end of its interval: at 65536 Hz
 *    that is (k + 1) * 256 system clock cycles (2^24 Hz / 65536 Hz = 256).
 *    An event at exactly that cycle is included, so an event at cycle E
 *    shows from the (E / 256)th sample generated.
 *  - Output scale: a channel at level v with every volume at maximum is
 *    v * LEVEL_STEP on each side it is enabled on (derivation below).
 *
 * Every test starts from cleared sound registers and a freshly initialised
 * module, so tests do not depend on each other's order.
 */
#include <gba/io_reg.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gba_memmap.h"
#include "psg.h"
#include "psg_internal.h"
#include "ptest.h"

/* Register offsets in the I/O region, GBATEK "GBA I/O Map" and the sound
 * sections: SOUND1CNT_L = NR10 at 060h, SOUND1CNT_H = NR11/NR12 at 062h,
 * SOUND1CNT_X = NR13/NR14 at 064h, SOUND2CNT_L = NR21/NR22 at 068h,
 * SOUND2CNT_H = NR23/NR24 at 06Ch, SOUND3CNT_L = NR30 at 070h, SOUND3CNT_H =
 * NR31/NR32 at 072h, SOUND3CNT_X = NR33/NR34 at 074h, SOUND4CNT_L = NR41/NR42
 * at 078h, SOUND4CNT_H = NR43/NR44 at 07Ch, SOUNDCNT_L = NR50/NR51 at 080h,
 * SOUNDCNT_H at 082h, SOUNDCNT_X = NR52 at 084h, WAVE_RAM0-3 at 090h-09Fh. */
#define NR10 0x060u
#define NR11 0x062u
#define NR12 0x063u
#define NR13 0x064u
#define NR14 0x065u
#define NR21 0x068u
#define NR22 0x069u
#define NR23 0x06Cu
#define NR24 0x06Du
#define NR30 0x070u
#define NR31 0x072u
#define NR32 0x073u
#define NR33 0x074u
#define NR34 0x075u
#define NR41 0x078u
#define NR42 0x079u
#define NR43 0x07Cu
#define NR44 0x07Du
#define NR50 0x080u
#define NR51 0x081u
#define SOUNDCNT_H 0x082u
#define NR52 0x084u
#define WAVE_RAM 0x090u
#define WAVE_RAM_BYTES 16u
/* GBATEK SOUNDCNT_X: "all PSG registers at 4000060h..4000081h are reset to
 * zero" while the master enable is clear. */
#define PSG_REGISTERS_FIRST 0x060u
#define PSG_REGISTERS_END 0x082u /* one past 081h */
#define SOUND_AREA_END 0x0A0u    /* one past wave RAM */

_Static_assert(NR10 == REG_OFFSET_NR10, "NR10");
_Static_assert(NR11 == REG_OFFSET_NR11 && NR12 == REG_OFFSET_NR12, "NR11/NR12");
_Static_assert(NR13 == REG_OFFSET_NR13 && NR14 == REG_OFFSET_NR14, "NR13/NR14");
_Static_assert(NR21 == REG_OFFSET_NR21 && NR22 == REG_OFFSET_NR22, "NR21/NR22");
_Static_assert(NR23 == REG_OFFSET_NR23 && NR24 == REG_OFFSET_NR24, "NR23/NR24");
_Static_assert(NR30 == REG_OFFSET_NR30 && NR31 == REG_OFFSET_NR31, "NR30/NR31");
_Static_assert(NR32 == REG_OFFSET_NR32 && NR33 == REG_OFFSET_NR33, "NR32/NR33");
_Static_assert(NR34 == REG_OFFSET_NR34, "NR34");
_Static_assert(NR41 == REG_OFFSET_NR41 && NR42 == REG_OFFSET_NR42, "NR41/NR42");
_Static_assert(NR43 == REG_OFFSET_NR43 && NR44 == REG_OFFSET_NR44, "NR43/NR44");
_Static_assert(NR50 == REG_OFFSET_NR50 && NR51 == REG_OFFSET_NR51, "NR50/NR51");
_Static_assert(SOUNDCNT_H == REG_OFFSET_SOUNDCNT_H && NR52 == REG_OFFSET_NR52, "SOUNDCNT");
_Static_assert(WAVE_RAM == REG_OFFSET_WAVE_RAM0, "WAVE_RAM0");

/* Register values, GBATEK. */
#define MASTER_ENABLE 0x80u   /* SOUNDCNT_X bit 7 */
#define INITIAL 0x80u         /* NRx4 bit 7, "Initial (1=Restart Sound)" */
#define LENGTH_ENABLE 0x40u   /* NRx4 bit 6, "Length Flag" */
#define NR50_FULL 0x77u       /* master volume 7 right (bits 0-2) and left (bits 4-6) */
#define NR51_ALL 0xFFu        /* channels 1-4 right (bits 0-3) and left (bits 4-7) */
#define PSG_MIX_100 0x02u     /* SOUNDCNT_H bits 0-1: 0=25%, 1=50%, 2=100% */
#define NRX2_FULL_STEADY 0xF0u /* initial volume 15, decrease, step time 0 (no envelope) */
#define WAVE_DAC_ON 0x80u     /* NR30 bit 7, "Sound Channel 3 Off (0=Stop, 1=Playback)" */
#define WAVE_BANK_1 0x40u     /* NR30 bit 6, bank selected for playback */
#define WAVE_TWO_BANKS 0x20u  /* NR30 bit 5, "Two banks/64 digits" */
#define WAVE_VOLUME_100 0x20u /* NR32 bits 5-6 = 1 */
#define NOISE_7_BIT 0x08u     /* NR43 bit 3 */

/* Output scale. GBATEK "Max Output Levels": each FIFO spans the full output
 * range, +/-200h units of the 10-bit output, and each PSG channel "one
 * QUARTER of the output range (+/-80h)", 100h units over its 16 levels: 10h
 * units per level at full volume. The module writes 16 int16_t steps per
 * output unit (psg.c, "OUTPUT SCALE"), so one level is 256. */
#define GBA_UNITS_PER_LEVEL 16
#define INT16_PER_GBA_UNIT 16
#define LEVEL_STEP (GBA_UNITS_PER_LEVEL * INT16_PER_GBA_UNIT)

/* 2^24 Hz / 65536 Hz = 256 cycles per output sample, so the sequencer's 512 Hz
 * is exactly 128 samples. */
#define RATE_EXACT 65536u
#define SAMPLES_PER_SEQUENCER_STEP 128u
#define MAX_FRAMES 65536u

static int16_t buffer[2 * MAX_FRAMES];

static uint8_t *io(uint32_t offset)
{
    /* The I/O region is mapped at its GBA address (gba_memmap.h). */
    return (uint8_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

static void poke(uint32_t offset, uint8_t value)
{
    *io(offset) = value;
}

static uint8_t peek(uint32_t offset)
{
    return *io(offset);
}

/* Cleared sound registers, a fresh module at rate, master enable on, every
 * volume at maximum and every channel routed to both sides. */
static bool start(uint32_t rate)
{
    memset(io(PSG_REGISTERS_FIRST), 0, SOUND_AREA_END - PSG_REGISTERS_FIRST);
    memset(buffer, 0, sizeof buffer);
    psg_shutdown();
    if (psg_init(rate) != PSG_OK)
        return false;
    poke(NR52, MASTER_ENABLE);
    poke(NR50, NR50_FULL);
    poke(NR51, NR51_ALL);
    poke(SOUNDCNT_H, PSG_MIX_100);
    return true;
}

static bool run(size_t frames)
{
    return frames <= MAX_FRAMES && psg_generate(buffer, frames) == frames;
}

static int16_t left(size_t frame)
{
    return buffer[2 * frame];
}

static int16_t right(size_t frame)
{
    return buffer[2 * frame + 1];
}

static int16_t max_left(size_t from, size_t count)
{
    int16_t max = INT16_MIN;
    for (size_t i = from; i < from + count; i++)
        if (left(i) > max)
            max = left(i);
    return max;
}

static size_t rising_edges_left(size_t frames)
{
    size_t edges = 0;
    for (size_t i = 1; i < frames; i++)
        if (left(i - 1) == 0 && left(i) > 0)
            edges++;
    return edges;
}

static size_t nonzero_left(size_t frames)
{
    size_t count = 0;
    for (size_t i = 0; i < frames; i++)
        if (left(i) != 0)
            count++;
    return count;
}

static bool all_zero(size_t frames)
{
    for (size_t i = 0; i < 2 * frames; i++)
        if (buffer[i] != 0)
            return false;
    return true;
}

/* Channel 1 or 2: NRx1, NRx2, then the frequency and NRx4's flags. The two
 * channels' registers are not laid out alike (06Ah is unused). */
static void square(unsigned channel, uint8_t duty_length, uint8_t envelope, uint16_t frequency,
                   uint8_t control)
{
    static const uint32_t at[2][4] = { { NR11, NR12, NR13, NR14 }, { NR21, NR22, NR23, NR24 } };
    const uint32_t *const nr = at[channel - 1];
    poke(nr[0], duty_length);
    poke(nr[1], envelope);
    poke(nr[2], (uint8_t)(frequency & 0xFFu));
    poke(nr[3], (uint8_t)(control | (frequency >> 8)));
}

static uint16_t channel1_frequency(void)
{
    return (uint16_t)(peek(NR13) | (peek(NR14) & 0x07u) << 8);
}

/* Duty pattern selector, NRx1 bits 6-7. */
static uint8_t duty(uint8_t pattern)
{
    return (uint8_t)(pattern << 6);
}

/* Fills the wave RAM window with bytes. */
static void write_wave_ram(const uint8_t bytes[WAVE_RAM_BYTES])
{
    memcpy(io(WAVE_RAM), bytes, WAVE_RAM_BYTES);
}

/* A 32-sample pattern 0..15, 15..0 packed as GBATEK orders wave RAM: "MSBs
 * of 1st byte, followed by LSBs of 1st byte, followed by MSBs of 2nd byte". */
static uint8_t triangle_nibble(unsigned index)
{
    return (uint8_t)(index < 16 ? index : 31 - index);
}

static void pack_nibbles(uint8_t bytes[WAVE_RAM_BYTES], uint8_t (*nibble)(unsigned))
{
    for (unsigned i = 0; i < WAVE_RAM_BYTES; i++)
        bytes[i] = (uint8_t)(nibble(2 * i) << 4 | nibble(2 * i + 1));
}

static uint8_t inverted_triangle_nibble(unsigned index)
{
    return (uint8_t)(15u - triangle_nibble(index));
}

/* Wave frequency giving one sample per output sample at RATE_EXACT: GBATEK
 * "Sample Rate; 2097152/(2048-n) Hz"; 2^24 / 2097152 = 8 cycles per unit, so
 * n = 2048 - 32 steps every 32 * 8 = 256 cycles. */
#define WAVE_ONE_PER_SAMPLE 2016u

static void wave_trigger(uint8_t nr30, uint8_t nr32, uint16_t frequency)
{
    poke(NR30, nr30);
    poke(NR32, nr32);
    poke(NR33, (uint8_t)(frequency & 0xFFu));
    poke(NR34, (uint8_t)(INITIAL | (frequency >> 8)));
}

/* Square frequency stepping the duty pattern once per output sample at
 * RATE_EXACT: GBATEK "Frequency; 131072/(2048-n)Hz", eight duty steps per
 * period, so steps come at 1048576/(2048-n) Hz, 16 cycles per unit;
 * n = 2048 - 16 steps every 256 cycles and repeats every 8 samples. */
#define SQUARE_ONE_STEP_PER_SAMPLE 2032u
/* n = 2048 - 8: a step every 128 cycles, a period of 4 samples, high for the
 * first 2 at 50% duty. */
#define SQUARE_FOUR_SAMPLE_PERIOD 2040u

/* --- Lifecycle ----------------------------------------------------------- */

PTEST(init_rejects_rates_of_zero_and_above_the_system_clock)
{
    psg_shutdown();
    PT_ASSERT_EQ_INT(psg_init(0), PSG_ERR_OUTPUT_RATE);
    PT_ASSERT_EQ_INT(psg_init(PSG_MAX_OUTPUT_RATE_HZ + 1u), PSG_ERR_OUTPUT_RATE);
    buffer[0] = 0x1234;
    PT_ASSERT(psg_generate(buffer, 1) == 0);
    PT_ASSERT_EQ_INT(buffer[0], 0x1234);
    PT_ASSERT_EQ_INT(psg_init(1), PSG_OK);
    PT_ASSERT_EQ_INT(psg_init(PSG_MAX_OUTPUT_RATE_HZ), PSG_OK);
    psg_shutdown();
}

PTEST(shutdown_without_init_is_safe_and_generate_then_writes_nothing)
{
    psg_shutdown();
    psg_shutdown();
    buffer[0] = 0x1234;
    PT_ASSERT(psg_generate(buffer, 1) == 0);
    PT_ASSERT_EQ_INT(buffer[0], 0x1234);
}

PTEST(zero_frames_writes_nothing_but_applies_the_registers)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), NRX2_FULL_STEADY, 1536, INITIAL);
    buffer[0] = 0x1234;
    PT_ASSERT(psg_generate(buffer, 0) == 0);
    PT_ASSERT_EQ_INT(buffer[0], 0x1234);
    PT_ASSERT(psg_generate(NULL, 0) == 0);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT_EQ_U32(peek(NR14) & INITIAL, 0);
}

PTEST(null_buffer_with_frames_is_rejected)
{
    PT_ASSERT(start(RATE_EXACT));
    PT_ASSERT(psg_generate(NULL, 4) == 0);
}

/* --- Square channels ------------------------------------------------------ */

/* GBATEK: tone frequency 131072/(2048-n) Hz. n = 1536 is 256 Hz, n = 1792
 * 512 Hz, n = 0 64 Hz; one second of output holds that many periods, one
 * rising edge each (one fewer if the phase starts high). */
static void check_square_periods(uint32_t rate, uint16_t frequency, size_t periods_per_second)
{
    PT_ASSERT(start(rate));
    square(1, duty(2), NRX2_FULL_STEADY, frequency, INITIAL);
    PT_ASSERT(run(rate));
    const size_t edges = rising_edges_left(rate);
    PT_ASSERT(edges + 1 >= periods_per_second && edges <= periods_per_second + 1);
}

PTEST(square_period_follows_gbatek_frequency_formula)
{
    check_square_periods(48000, 1536, 256);
    check_square_periods(44100, 1792, 512);
    check_square_periods(48000, 0, 64);
    check_square_periods(RATE_EXACT, 1536, 256);
}

/* GBATEK duty patterns: 12.5% "-_______", 25% "--______", 50% "----____",
 * 75% "------__". With one duty step per output sample, every 8 samples hold
 * exactly 1, 2, 4 or 6 high samples at level 15. */
PTEST(square_duty_cycles_have_gbatek_high_fractions)
{
    static const size_t high_per_eight[4] = { 1, 2, 4, 6 };
    for (uint8_t pattern = 0; pattern < 4; pattern++) {
        PT_ASSERT(start(RATE_EXACT));
        square(1, duty(pattern), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
        PT_ASSERT(run(800));
        PT_ASSERT(nonzero_left(800) == 100 * high_per_eight[pattern]);
        PT_ASSERT_EQ_INT(max_left(0, 800), 15 * LEVEL_STEP);
    }
}

PTEST(channel_2_is_a_square_channel_at_its_own_registers)
{
    PT_ASSERT(start(RATE_EXACT));
    square(2, duty(1), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
    PT_ASSERT(run(800));
    PT_ASSERT(nonzero_left(800) == 200);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x02u);
}

/* The game changes pitch by rewriting NRx3/NRx4 without bit 7 (m4a's
 * CgbSound does for vibrato); GBATEK lists the frequency as write-only and
 * takes it at once. 256 Hz for half a second, then 512 Hz. */
PTEST(frequency_change_without_trigger_takes_effect)
{
    PT_ASSERT(start(48000));
    square(1, duty(2), NRX2_FULL_STEADY, 1536, INITIAL);
    PT_ASSERT(run(24000));
    size_t edges = rising_edges_left(24000);
    PT_ASSERT(edges + 1 >= 128 && edges <= 129);
    square(1, duty(2), NRX2_FULL_STEADY, 1792, 0);
    PT_ASSERT(run(24000));
    edges = rising_edges_left(24000);
    PT_ASSERT(edges + 1 >= 256 && edges <= 257);
}

/* --- Triggers -------------------------------------------------------------- */

/* Envelope from 15 down one level per 64 Hz tick (step time 1); ticks at
 * 1024-sample intervals at RATE_EXACT (8 sequencer steps of 128). */
static void start_decaying_square(void)
{
    square(1, duty(2), 0xF1, SQUARE_FOUR_SAMPLE_PERIOD, INITIAL);
}

PTEST(trigger_acts_once_and_reads_back_clear)
{
    PT_ASSERT(start(RATE_EXACT));
    start_decaying_square();
    PT_ASSERT(run(1));
    /* Bit 7 is write-only on hardware and reads 0; the frequency's top bits
     * and NR13 are left as written. */
    PT_ASSERT_EQ_U32(peek(NR14), SQUARE_FOUR_SAMPLE_PERIOD >> 8);
    PT_ASSERT_EQ_U32(peek(NR13), SQUARE_FOUR_SAMPLE_PERIOD & 0xFFu);
    PT_ASSERT(run(2055)); /* samples 2048-2055 follow the second envelope tick */
    PT_ASSERT_EQ_INT(max_left(2047, 8), 13 * LEVEL_STEP);
    PT_ASSERT(run(100)); /* no new trigger: the envelope is not restarted */
    PT_ASSERT_EQ_INT(max_left(92, 8), 13 * LEVEL_STEP);
}

/* m4a's CgbSound writes NRx4 with bit 7 set on every volume change, often
 * with the same value as last time; each write is a trigger on hardware. */
PTEST(rewriting_the_same_trigger_value_restarts_the_channel)
{
    PT_ASSERT(start(RATE_EXACT));
    start_decaying_square();
    PT_ASSERT(run(2056));
    PT_ASSERT_EQ_INT(max_left(2048, 8), 13 * LEVEL_STEP);
    poke(NR14, (uint8_t)(INITIAL | SQUARE_FOUR_SAMPLE_PERIOD >> 8));
    PT_ASSERT(run(100));
    PT_ASSERT_EQ_INT(max_left(92, 8), 15 * LEVEL_STEP);
}

/* Pan Docs "Audio Details", DACs: NRx2 bits 3-7 all zero switch the DAC off;
 * a trigger then does not start the channel, and switching the DAC off stops
 * it. Volume 0 with direction increase (08h) keeps the DAC on. */
PTEST(dac_off_blocks_trigger_and_stops_the_channel)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), 0x00, 1536, INITIAL);
    PT_ASSERT(run(64));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    PT_ASSERT(all_zero(64));
    square(1, duty(2), 0x08, 1536, INITIAL);
    PT_ASSERT(run(64));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(all_zero(64));
    poke(NR12, 0x00);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* --- Length counter ------------------------------------------------------- */

/* GBATEK NR11 "Sound length; units of (64-n)/256s": n = 60 is 4 ticks, the
 * 4th at 7/512 s = 896 samples. */
PTEST(length_counter_stops_the_channel_after_exactly_its_ticks)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, (uint8_t)(duty(2) | 60), NRX2_FULL_STEADY, SQUARE_FOUR_SAMPLE_PERIOD,
           INITIAL | LENGTH_ENABLE);
    PT_ASSERT(run(895));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    PT_ASSERT(run(64));
    PT_ASSERT(all_zero(64));
}

/* n = 0 is the longest, 64 ticks: the 64th at 127/512 s = 16256 samples. */
PTEST(length_zero_means_sixty_four_ticks)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), NRX2_FULL_STEADY, SQUARE_FOUR_SAMPLE_PERIOD, INITIAL | LENGTH_ENABLE);
    PT_ASSERT(run(16255));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* GBATEK: "The Length value is used only if Bit 6 in NR14 is set." */
PTEST(length_counter_is_ignored_while_length_enable_is_clear)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, (uint8_t)(duty(2) | 60), NRX2_FULL_STEADY, SQUARE_FOUR_SAMPLE_PERIOD, INITIAL);
    PT_ASSERT(run(MAX_FRAMES));
    PT_ASSERT(run(MAX_FRAMES));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT_EQ_INT(max_left(MAX_FRAMES - 8, 8), 15 * LEVEL_STEP);
}

/* GBATEK NR31 "units of (256-n)/256s": n = 254 is 2 ticks, at 3/512 s =
 * 384 samples. */
PTEST(wave_length_counts_256_minus_n)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR31, 254);
    poke(NR30, WAVE_DAC_ON);
    poke(NR32, WAVE_VOLUME_100);
    poke(NR33, WAVE_ONE_PER_SAMPLE & 0xFFu);
    poke(NR34, (uint8_t)(INITIAL | LENGTH_ENABLE | WAVE_ONE_PER_SAMPLE >> 8));
    PT_ASSERT(run(383));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x04u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* GBATEK NR41 "units of (64-n)/256s": n = 63 is one tick, at 1/512 s =
 * 128 samples. */
PTEST(noise_length_counts_64_minus_n)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR41, 63);
    poke(NR42, NRX2_FULL_STEADY);
    poke(NR44, INITIAL | LENGTH_ENABLE);
    PT_ASSERT(run(127));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x08u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* --- Envelope ------------------------------------------------------------- */

/* GBATEK NR12 "Envelope Step-Time; units of n/64s": step time 1 lowers the
 * level by one every 1024 samples, from 15; it stops at 0, and the channel
 * stays on ("The bits are NOT reset when an volume envelope ends"). */
PTEST(envelope_decreases_one_level_per_period_and_stops_at_zero)
{
    PT_ASSERT(start(RATE_EXACT));
    start_decaying_square();
    PT_ASSERT(run(1000));
    PT_ASSERT_EQ_INT(max_left(992, 8), 15 * LEVEL_STEP);
    for (int ticks = 1; ticks <= 20; ticks++) {
        PT_ASSERT(run(1024)); /* now 1000 + 1024 * ticks samples in */
        const int level = ticks < 15 ? 15 - ticks : 0;
        PT_ASSERT_EQ_INT(max_left(1016, 8), level * LEVEL_STEP);
    }
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
}

/* Step time 2, increasing from 0: one level per two 64 Hz ticks (2048
 * samples), stopping at 15. */
PTEST(envelope_increases_every_step_time_ticks_and_stops_at_fifteen)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), 0x0A, SQUARE_FOUR_SAMPLE_PERIOD, INITIAL);
    PT_ASSERT(run(2000));
    PT_ASSERT_EQ_INT(max_left(1992, 8), 0);
    for (int steps = 1; steps <= 18; steps++) {
        PT_ASSERT(run(2048)); /* now 2000 + 2048 * steps samples in */
        const int level = steps < 15 ? steps : 15;
        PT_ASSERT_EQ_INT(max_left(2040, 8), level * LEVEL_STEP);
    }
}

PTEST(noise_channel_has_an_envelope)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR42, 0xF1);
    poke(NR43, NOISE_7_BIT | 0x21u); /* one LFSR clock per sample, see below */
    poke(NR44, INITIAL);
    PT_ASSERT(run(1000));
    PT_ASSERT_EQ_INT(max_left(744, 256), 15 * LEVEL_STEP);
    PT_ASSERT(run(1024));
    PT_ASSERT_EQ_INT(max_left(768, 256), 14 * LEVEL_STEP);
}

/* --- Sweep ----------------------------------------------------------------- */

/* GBATEK NR10: "X(t) = X(t-1) +/- X(t-1)/2^n", every sweep time * 7.8 ms;
 * the result is written back to NR13/NR14. With sweep time 1 the steps come
 * on each 128 Hz tick, at 3/512 s (384 samples) and then every 512. */
PTEST(sweep_increases_frequency_by_the_shifted_amount)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR10, 0x12); /* time 1, increase, shift 2 */
    square(1, duty(2), NRX2_FULL_STEADY, 256, INITIAL);
    PT_ASSERT(run(383));
    PT_ASSERT_EQ_U32(channel1_frequency(), 256);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(channel1_frequency(), 256 + 256 / 4);
    PT_ASSERT(run(512));
    PT_ASSERT_EQ_U32(channel1_frequency(), 320 + 320 / 4);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
}

PTEST(sweep_decreases_frequency_when_direction_is_set)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR10, 0x19); /* time 1, decrease, shift 1 */
    square(1, duty(2), NRX2_FULL_STEADY, 1024, INITIAL);
    PT_ASSERT(run(384));
    PT_ASSERT_EQ_U32(channel1_frequency(), 512);
    PT_ASSERT(run(512));
    PT_ASSERT_EQ_U32(channel1_frequency(), 256);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
}

/* Sweep time 2: a step every second 128 Hz tick, the first at 7/512 s. */
PTEST(sweep_time_counts_128_hz_ticks)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR10, 0x22); /* time 2, increase, shift 2 */
    square(1, duty(2), NRX2_FULL_STEADY, 256, INITIAL);
    PT_ASSERT(run(895));
    PT_ASSERT_EQ_U32(channel1_frequency(), 256);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(channel1_frequency(), 320);
}

/* Overflow past 2047 stops the channel (Pan Docs "Audio Details": the check
 * runs after each written step on the next value too). From 1024 with shift
 * 2: 1280, then 1600, then 2000, whose next value 2500 overflows, so the
 * channel stops at the third step, 384 + 2 * 512 = 1408 samples. */
PTEST(sweep_overflow_past_2047_disables_the_channel)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR10, 0x12);
    square(1, duty(2), NRX2_FULL_STEADY, 1024, INITIAL);
    PT_ASSERT(run(1407));
    PT_ASSERT_EQ_U32(channel1_frequency(), 1600);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(channel1_frequency(), 2000);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    PT_ASSERT(run(64));
    PT_ASSERT(all_zero(64));
}

/* The trigger runs the overflow check at once: 1366 + 1366/2 = 2049 stops
 * the channel before it plays; 1365 + 682 = 2047 does not. */
PTEST(sweep_overflow_check_runs_at_trigger)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR10, 0x11); /* time 1, increase, shift 1 */
    square(1, duty(2), NRX2_FULL_STEADY, 1366, INITIAL);
    PT_ASSERT(psg_generate(buffer, 0) == 0);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    square(1, duty(2), NRX2_FULL_STEADY, 1365, INITIAL);
    PT_ASSERT(psg_generate(buffer, 0) == 0);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
}

/* --- Wave channel ----------------------------------------------------------- */

/* One wave sample per output sample. Output k is taken after the channel's
 * (k+1)th step from sample 0, so it plays sample k + 1. */
PTEST(wave_plays_a_32_sample_pattern_at_the_gbatek_rate)
{
    PT_ASSERT(start(RATE_EXACT));
    uint8_t bytes[WAVE_RAM_BYTES];
    pack_nibbles(bytes, triangle_nibble);
    write_wave_ram(bytes);
    wave_trigger(WAVE_DAC_ON, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(96));
    for (unsigned k = 0; k < 96; k++)
        PT_ASSERT_EQ_INT(left(k), triangle_nibble((k + 1) % 32) * LEVEL_STEP);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x04u);
}

/* Expansion's m4a CgbSound loads a wave by selecting bank 1 (NR30 = 40h),
 * writing WAVE_RAM0-3, which on hardware fills bank 0, then selecting bank 0
 * and starting playback, all between two psg_generate() calls. The pattern
 * must play. (psg_wave.c: wave RAM written since the last call goes to the
 * bank selected for playback when the call sees it.) */
PTEST(wave_ram_loaded_the_way_m4a_loads_it_plays)
{
    PT_ASSERT(start(RATE_EXACT));
    uint8_t bytes[WAVE_RAM_BYTES];
    pack_nibbles(bytes, triangle_nibble);
    poke(NR30, WAVE_BANK_1);
    write_wave_ram(bytes);
    poke(NR30, 0);
    poke(NR31, 0);
    wave_trigger(WAVE_DAC_ON, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(64));
    for (unsigned k = 0; k < 64; k++)
        PT_ASSERT_EQ_INT(left(k), triangle_nibble((k + 1) % 32) * LEVEL_STEP);
}

/* GBATEK NR30 bit 5: two banks, 64 samples, "output will start by replaying
 * the currently selected bank". Bank 0 holds the triangle, bank 1 its
 * inverse; with bank 0 selected the output walks 0..31 of bank 0, then bank 1. */
PTEST(wave_two_bank_mode_walks_both_banks_from_the_selected_one)
{
    PT_ASSERT(start(RATE_EXACT));
    uint8_t bytes[WAVE_RAM_BYTES];
    pack_nibbles(bytes, triangle_nibble);
    poke(NR30, WAVE_TWO_BANKS);
    write_wave_ram(bytes);
    PT_ASSERT(psg_generate(buffer, 0) == 0);
    pack_nibbles(bytes, inverted_triangle_nibble);
    poke(NR30, WAVE_TWO_BANKS | WAVE_BANK_1);
    write_wave_ram(bytes);
    PT_ASSERT(psg_generate(buffer, 0) == 0);
    wave_trigger(WAVE_DAC_ON | WAVE_TWO_BANKS, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(192));
    for (unsigned k = 0; k < 192; k++) {
        const unsigned position = (k + 1) % 64;
        const uint8_t expected = position < 32 ? triangle_nibble(position)
                                               : inverted_triangle_nibble(position - 32);
        PT_ASSERT_EQ_INT(left(k), expected * LEVEL_STEP);
    }
    /* Starting from bank 1 instead plays the inverse first. */
    wave_trigger(WAVE_DAC_ON | WAVE_TWO_BANKS | WAVE_BANK_1, WAVE_VOLUME_100,
                 WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(64));
    for (unsigned k = 0; k < 31; k++)
        PT_ASSERT_EQ_INT(left(k), inverted_triangle_nibble(k + 1) * LEVEL_STEP);
}

/* GBATEK NR32 bits 5-6 "Sound Volume (0=Mute/Zero, 1=100%, 2=50%, 3=25%)"
 * and bit 7 "Force Volume (1=Force 75% regardless of above)". Sample 12
 * gives 12, 6, 3 and 9 exactly; sample 15 shows the rounding, down, as the
 * hardware's shifts do (Pan Docs NR32: "shift samples ... right once"):
 * 7, 3 and 45/4 = 11. */
static void check_wave_volume(uint8_t sample_byte, uint8_t nr32, int expected_level)
{
    PT_ASSERT(start(RATE_EXACT));
    uint8_t bytes[WAVE_RAM_BYTES];
    memset(bytes, sample_byte, sizeof bytes);
    write_wave_ram(bytes);
    wave_trigger(WAVE_DAC_ON, nr32, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(16));
    PT_ASSERT_EQ_INT(left(15), expected_level * LEVEL_STEP);
}

PTEST(wave_volume_is_mute_100_50_25_or_forced_75_percent)
{
    check_wave_volume(0xCC, 0x00, 0);
    check_wave_volume(0xCC, 0x20, 12);
    check_wave_volume(0xCC, 0x40, 6);
    check_wave_volume(0xCC, 0x60, 3);
    check_wave_volume(0xCC, 0x80, 9);
    check_wave_volume(0xCC, 0xE0, 9);
    check_wave_volume(0xFF, 0x40, 7);
    check_wave_volume(0xFF, 0x60, 3);
    check_wave_volume(0xFF, 0x80, 11);
}

/* NR30 bit 7 clear: the channel does not start, and clearing it stops it. */
PTEST(wave_dac_off_blocks_trigger_and_stops_the_channel)
{
    PT_ASSERT(start(RATE_EXACT));
    wave_trigger(0, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(4));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    wave_trigger(WAVE_DAC_ON, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
    PT_ASSERT(run(4));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x04u);
    poke(NR30, 0);
    PT_ASSERT(run(4));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* --- Noise channel ----------------------------------------------------------- */

/* GBATEK: "The data stream repeats after 7Fh (7bit) or 7FFFh (15bit) steps",
 * starting from X=40h or X=4000h. Checked on the generator's state. */
static void check_lfsr_period(enum psg_lfsr_width width, uint16_t seed, uint32_t period)
{
    PT_ASSERT_EQ_U32(psg_lfsr_seed(width), seed);
    uint16_t lfsr = seed;
    for (uint32_t step = 1; step < period; step++) {
        lfsr = psg_lfsr_clock(lfsr, width);
        PT_ASSERT(lfsr != seed);
    }
    PT_ASSERT_EQ_U32(psg_lfsr_clock(lfsr, width), seed);
}

PTEST(lfsr_7_bit_repeats_after_127_clocks)
{
    check_lfsr_period(PSG_LFSR_7_BIT, 0x40, 127);
}

PTEST(lfsr_15_bit_repeats_after_32767_clocks)
{
    check_lfsr_period(PSG_LFSR_15_BIT, 0x4000, 32767);
}

/* GBATEK's generator written out: "X=X SHR 1, IF carry THEN Out=HIGH, X=X
 * XOR 60h ELSE Out=LOW" (6000h for 15 bits). */
static bool reference_lfsr_clock(uint16_t *x, uint16_t taps)
{
    const bool carry = (*x & 1u) != 0;
    *x = (uint16_t)(*x >> 1);
    if (carry)
        *x = (uint16_t)(*x ^ taps);
    return carry;
}

/* GBATEK NR43: "Frequency = 524288 Hz / r / 2^(s+1) ;For r=0 assume r=0.5".
 * r=1 s=2, r=0 s=3 and r=2 s=1 are all 65536 Hz, one clock per output sample
 * at RATE_EXACT; output k follows clock k+1, at level 15 when HIGH. */
static void check_noise_sequence(uint8_t nr43, uint16_t seed, uint16_t taps,
                                 unsigned samples_per_clock)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR42, NRX2_FULL_STEADY);
    poke(NR43, nr43);
    poke(NR44, INITIAL);
    PT_ASSERT(run(4096));
    uint16_t x = seed;
    bool high = false;
    unsigned clocks = 0;
    for (unsigned k = 0; k < 4096; k++) {
        while (clocks < (k + 1) / samples_per_clock) {
            high = reference_lfsr_clock(&x, taps);
            clocks++;
        }
        PT_ASSERT_EQ_INT(left(k), high ? 15 * LEVEL_STEP : 0);
    }
}

PTEST(noise_follows_the_gbatek_generator_at_the_divided_rate)
{
    check_noise_sequence(NOISE_7_BIT | 0x21u, 0x40, 0x60, 1);
    check_noise_sequence(NOISE_7_BIT | 0x30u, 0x40, 0x60, 1);
    check_noise_sequence(NOISE_7_BIT | 0x12u, 0x40, 0x60, 1);
    check_noise_sequence(0x21u, 0x4000, 0x6000, 1);
    check_noise_sequence(NOISE_7_BIT | 0x31u, 0x40, 0x60, 2); /* 32768 Hz */
}

/* --- Mixing ----------------------------------------------------------------- */

/* Channel 3 playing a constant 15: a steady level to measure the mixer with. */
static void start_constant_wave(void)
{
    uint8_t bytes[WAVE_RAM_BYTES];
    memset(bytes, 0xFF, sizeof bytes);
    write_wave_ram(bytes);
    wave_trigger(WAVE_DAC_ON, WAVE_VOLUME_100, WAVE_ONE_PER_SAMPLE);
}

/* GBATEK SOUNDCNT_L bits 8-11 enable channels 1-4 on the right, bits 12-15
 * (NR51 bits 4-7) on the left. */
PTEST(left_and_right_enables_route_each_channel)
{
    for (unsigned channel = 0; channel < 4; channel++) {
        for (unsigned side = 0; side < 2; side++) { /* 0 right, 1 left */
            PT_ASSERT(start(RATE_EXACT));
            square(1, duty(2), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
            square(2, duty(2), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
            start_constant_wave();
            poke(NR42, NRX2_FULL_STEADY);
            poke(NR43, NOISE_7_BIT | 0x21u);
            poke(NR44, INITIAL);
            poke(NR51, (uint8_t)(1u << (channel + 4 * side)));
            PT_ASSERT(run(256));
            int16_t max_on = 0, max_off = 0, min_both = 0;
            for (size_t k = 0; k < 256; k++) {
                const int16_t on = side ? left(k) : right(k);
                const int16_t off = side ? right(k) : left(k);
                if (on > max_on)
                    max_on = on;
                if (off > max_off)
                    max_off = off;
                if (on < min_both)
                    min_both = on;
                if (off < min_both)
                    min_both = off;
            }
            PT_ASSERT_EQ_INT(max_on, 15 * LEVEL_STEP);
            PT_ASSERT_EQ_INT(max_off, 0);
            PT_ASSERT_EQ_INT(min_both, 0);
        }
    }
}

/* NR50: right volume bits 0-2, left bits 4-6; Pan Docs NR50: "A value of 0
 * is treated as a volume of 1 (very quiet), and a value of 7 is treated as a
 * volume of 8". Left 7 is 8/8, right 3 is 4/8, 0 is 1/8. */
PTEST(master_volume_scales_each_side)
{
    PT_ASSERT(start(RATE_EXACT));
    start_constant_wave();
    poke(NR50, 0x73);
    PT_ASSERT(run(8));
    PT_ASSERT_EQ_INT(left(7), 15 * LEVEL_STEP);
    PT_ASSERT_EQ_INT(right(7), 15 * LEVEL_STEP * 4 / 8);
    poke(NR50, 0x00);
    PT_ASSERT(run(8));
    PT_ASSERT_EQ_INT(left(7), 15 * LEVEL_STEP / 8);
    PT_ASSERT_EQ_INT(right(7), 15 * LEVEL_STEP / 8);
}

/* SOUNDCNT_H bits 0-1 "Sound # 1-4 Volume (0=25%, 1=50%, 2=100%,
 * 3=Prohibited)"; this module plays 3 as 100% (psg.c). */
PTEST(soundcnt_h_scales_the_psg_to_25_50_or_100_percent)
{
    static const int quarters[4] = { 1, 2, 4, 4 };
    for (uint8_t setting = 0; setting < 4; setting++) {
        PT_ASSERT(start(RATE_EXACT));
        start_constant_wave();
        poke(SOUNDCNT_H, (uint8_t)(0x0Cu | setting)); /* DMA A/B volume bits do not matter */
        PT_ASSERT(run(8));
        PT_ASSERT_EQ_INT(left(7), 15 * LEVEL_STEP * quarters[setting] / 4);
    }
}

/* All four channels at 15 with every volume at maximum is the largest output,
 * 4 * 15 levels; the squares are high 6 samples in 8 and the noise about half
 * the time, so within 1024 samples they coincide. */
PTEST(four_channels_at_full_volume_reach_the_documented_maximum)
{
    PT_ASSERT_EQ_INT(PSG_MAX_OUTPUT_LEVEL, 4 * 15 * LEVEL_STEP);
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(3), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
    square(2, duty(3), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
    start_constant_wave();
    poke(NR42, NRX2_FULL_STEADY);
    poke(NR43, NOISE_7_BIT | 0x21u);
    poke(NR44, INITIAL);
    PT_ASSERT(run(1024));
    PT_ASSERT_EQ_INT(max_left(0, 1024), PSG_MAX_OUTPUT_LEVEL);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x0Fu);
}

/* --- Master enable and status ---------------------------------------------- */

/* GBATEK SOUNDCNT_X bit 7: while clear, "both PSG and FIFO sounds are
 * disabled, and all PSG registers at 4000060h..4000081h are reset to zero";
 * 4000082h (SOUNDCNT_H) is kept. */
PTEST(master_disable_silences_resets_registers_and_clears_status)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
    start_constant_wave();
    PT_ASSERT(run(16));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x05u);
    PT_ASSERT(nonzero_left(16) > 0);
    poke(NR52, 0);
    PT_ASSERT(run(64));
    PT_ASSERT(all_zero(64));
    PT_ASSERT_EQ_U32(peek(NR52), 0);
    for (uint32_t offset = PSG_REGISTERS_FIRST; offset < PSG_REGISTERS_END; offset++)
        PT_ASSERT_EQ_U32(peek(offset), 0);
    PT_ASSERT_EQ_U32(peek(SOUNDCNT_H), PSG_MIX_100);
    /* Registers stay zero while the PSG is off: a write is discarded. */
    poke(NR12, NRX2_FULL_STEADY);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR12), 0);
}

/* After the master enable comes back, channels stay off until triggered. */
PTEST(master_reenable_leaves_channels_off_until_triggered)
{
    PT_ASSERT(start(RATE_EXACT));
    square(1, duty(2), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, INITIAL);
    PT_ASSERT(run(16));
    poke(NR52, 0);
    PT_ASSERT(run(16));
    poke(NR52, MASTER_ENABLE);
    poke(NR50, NR50_FULL);
    poke(NR51, NR51_ALL);
    square(1, duty(2), NRX2_FULL_STEADY, SQUARE_ONE_STEP_PER_SAMPLE, 0);
    PT_ASSERT(run(16));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    PT_ASSERT(all_zero(16));
    poke(NR14, (uint8_t)(INITIAL | SQUARE_ONE_STEP_PER_SAMPLE >> 8));
    PT_ASSERT(run(16));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(nonzero_left(16) > 0);
}

/* GBATEK SOUNDCNT_X bits 0-3 are read-only channel flags the hardware keeps
 * up to date; whatever the game wrote there is replaced, and bits 4-6 read 0. */
PTEST(status_bits_reflect_the_channels_that_are_on)
{
    PT_ASSERT(start(RATE_EXACT));
    poke(NR52, 0xFF);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
    square(1, duty(2), NRX2_FULL_STEADY, 1536, INITIAL);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    square(2, duty(2), NRX2_FULL_STEADY, 1536, INITIAL);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x03u);
    start_constant_wave();
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x07u);
    poke(NR42, NRX2_FULL_STEADY);
    poke(NR44, INITIAL);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x0Fu);
}

/* --- Resampling ----------------------------------------------------------------- */

/* 2^24 / 48000 = 349.53 cycles per sample: exact accumulation puts sample m
 * at floor(m * 2^24 / 48000) cycles. The 64th length tick (n = 0) comes at
 * 127/512 s = 4161536 cycles: sample 11906 ends at 4161448, before it, and
 * sample 11907 at 4161798. (A fixed 349 or 350 cycles per sample would move
 * it to 11925 or 11891.) */
PTEST(fractional_cycles_per_sample_accumulate_exactly)
{
    PT_ASSERT(start(48000));
    square(1, duty(2), NRX2_FULL_STEADY, 1536, INITIAL | LENGTH_ENABLE);
    PT_ASSERT(run(11906));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x01u);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* At 1 Hz one output sample spans 512 sequencer steps: the 64-tick length
 * (127/512 s) runs out inside the first. */
PTEST(a_slow_output_rate_crosses_many_sequencer_steps_per_sample)
{
    PT_ASSERT(start(1));
    square(1, duty(2), NRX2_FULL_STEADY, 1536, INITIAL | LENGTH_ENABLE);
    PT_ASSERT(run(1));
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE);
}

/* The highest frequencies (n = 2047; noise r = 0, s = 0, 524288 Hz) at a low
 * output rate step many times per sample; every sample stays within range. */
PTEST(highest_frequencies_at_a_low_rate_stay_in_range)
{
    PT_ASSERT(start(8000));
    square(1, duty(2), NRX2_FULL_STEADY, 2047, INITIAL);
    square(2, duty(1), NRX2_FULL_STEADY, 2047, INITIAL);
    uint8_t bytes[WAVE_RAM_BYTES];
    pack_nibbles(bytes, triangle_nibble);
    write_wave_ram(bytes);
    wave_trigger(WAVE_DAC_ON, WAVE_VOLUME_100, 2047);
    poke(NR42, NRX2_FULL_STEADY);
    poke(NR43, 0x00);
    poke(NR44, INITIAL);
    PT_ASSERT(run(8000));
    for (size_t i = 0; i < 2 * 8000; i++)
        PT_ASSERT(buffer[i] >= 0 && buffer[i] <= PSG_MAX_OUTPUT_LEVEL);
    PT_ASSERT_EQ_U32(peek(NR52), MASTER_ENABLE | 0x0Fu);
}

/* The other end: one output sample per system clock cycle. */
PTEST(output_rate_equal_to_the_system_clock_works)
{
    PT_ASSERT(start(PSG_MAX_OUTPUT_RATE_HZ));
    square(1, duty(2), NRX2_FULL_STEADY, 2047, INITIAL);
    /* 16-cycle duty steps, 50% duty: high 64 cycles in every 128. */
    PT_ASSERT(run(1024));
    PT_ASSERT(nonzero_left(1024) == 512);
}

PTEST_MAIN()
