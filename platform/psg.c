/*
 * psg.c: the PSG's register interface, frame sequencer, resampler and mixer.
 *
 * Stands in for GBATEK "GBA Sound Controller"
 * (https://problemkaputt.de/gbatek.htm#gbasoundcontroller) as far as the four
 * Game Boy channels go: their registers at 4000060h-4000081h, wave RAM at
 * 4000090h-400009Fh, SOUNDCNT_X's master enable and status bits, SOUNDCNT_L's
 * master volume and left/right enables, and SOUNDCNT_H's PSG volume. On the
 * GBA those circuits act on each register write as it happens and run off the
 * 16.78 MHz system clock; their outputs are mixed digitally with the Direct
 * Sound FIFOs, offset by SOUNDBIAS and sent out as PWM. The channels
 * themselves are in psg_square.c, psg_wave.c and psg_noise.c, their shared
 * units in psg_channel.c. Written from GBATEK and Pan Docs
 * (https://gbdev.io/pandocs/Audio_details.html), with mGBA's src/gba/audio.c
 * consulted for behaviour where both are silent (named where used); no
 * reference port's code.
 *
 * WHEN REGISTERS ARE READ. The game's register writes are plain stores into
 * the I/O region (gba_memmap.h). psg_generate() reads every sound register
 * once, at its start, and then runs the channels for the frames requested;
 * a write takes effect at the next call. The platform calls it once per
 * frame after the VBlank handler, where m4a's CgbSound writes the registers,
 * so register changes land on frame boundaries, as the m4a engine intends.
 *
 * TRIGGERS. NRx4 bit 7 ("Initial (1=Restart Sound)") is write-only: on the
 * GBA each write of a 1 restarts the channel once, and the bit never reads
 * back as 1. Plain memory keeps the 1, so the module must remember what it
 * has acted on. It does so in the register itself: when a call finds the bit
 * set it triggers the channel and clears the bit in the I/O region, so the
 * register then reads as the hardware's does and the game's next write of the
 * bit is seen as a new trigger, even if the value written is the same. A
 * shadow copy compared between calls would not do: m4a writes NRx4 with bit 7
 * set on every volume change and every note (src/m4a.c, CgbSound, and
 * CgbOscOff's NRx4 = 80h), usually the same value as before, and each of those
 * writes restarts the envelope on hardware. Writes of the bit between two
 * calls count as one trigger. NRx4's other bits are left as written.
 *
 * OTHER WRITE-ONLY AND READ-ONLY BITS. The frequency (NRx3, NRx4 bits 0-2)
 * and length fields are write-only on hardware but are left as written here,
 * because the module reads them as its state. The length counter is loaded
 * from NRx1 on each trigger (GBATEK gives no other moment; on the Game Boy the
 * NRx1 write loads it, which a call cannot see). Channel 1's sweep writes its
 * new frequency back to NR13/NR14 as the hardware does. SOUNDCNT_X bits 0-3
 * are rewritten after every call with the channels' on flags (bit n-1 for
 * channel n), bits 4-6 read 0, and bit 7 is left as the game wrote it.
 *
 * MASTER ENABLE. While SOUNDCNT_X bit 7 is clear, GBATEK: "all PSG registers
 * at 4000060h..4000081h are reset to zero (and must be re-initialized after
 * re-enabling sound)". Each call that finds it clear stops every channel,
 * zeroes those registers in the I/O region (discarding anything written
 * while off) and outputs silence. Wave RAM and SOUNDCNT_H are kept. The first
 * call that finds it set powers the PSG on: the frame sequencer restarts so
 * its step 0 comes 1/512 s later (Pan Docs "Audio Details"), and every
 * channel stays off until triggered.
 *
 * TIMING. Everything counts system clock cycles, 2^24 Hz (PSG_SYSTEM_CLOCK_HZ).
 * The frame sequencer steps every 32768 cycles (512 Hz): length counters on
 * steps 0, 2, 4, 6 (256 Hz), sweep on steps 2 and 6 (128 Hz), envelopes on
 * step 7 (64 Hz). Each channel's frequency timer counts its own period in
 * cycles and reloads from the current frequency when it expires, so pitch
 * changes take effect at the next step.
 *
 * RESAMPLING. Output sample k is the channels' state at the end of its share
 * of time: the module runs the hardware for floor((k+1) * 2^24 / rate) -
 * floor(k * 2^24 / rate) cycles, carrying the remainder exactly in integers,
 * so rate samples always span exactly one second of hardware time, then
 * takes the instantaneous levels. This is point sampling (decimation with no
 * filter), not interpolation. Its aliasing: every component of the PSG's
 * output above rate/2 folds back into the audible band. The square and
 * noise channels are full of such components (a square wave's odd harmonics
 * fall off only as 1/k; the noise channel clocks at up to 524288 Hz), so high
 * notes gain inharmonic tones and edges jitter by up to one output sample.
 * The GBA itself resamples the PSG's 262.144 kHz output to its 32.768 kHz PWM
 * rate (GBATEK, "Resampling to 32.768kHz"), so at 32768 Hz this matches the
 * hardware's own sampling grid; at lower rates it aliases more than the
 * hardware. A band-limited synthesis (box filter or band-limited steps) can
 * replace mix_sample() later without changing the interface.
 *
 * OUTPUT SCALE. GBATEK "GBA Sound Control Registers", Max Output Levels:
 * each FIFO spans the whole output range, +/-200h units of the 10-bit output,
 * and each PSG channel "one QUARTER of the output range (+/-80h)", that is
 * 100h units over its 16 levels, 10h units per level at full volume. The GBA
 * mixes digitally and has no per-channel DACs (Pan Docs "Audio Details",
 * Game Boy Advance audio: a disabled DAC behaves as one receiving 0), so a
 * channel's contribution is level * 10h units: 0 when silent or off, never
 * negative. NR50's 0-7 scales it by (n+1)/8 (Pan Docs NR50) and SOUNDCNT_H's
 * 25/50/100% by 1/4, 2/4, 4/4. Four channels at level 15 and full volume are
 * 4 * 15 * 10h = 3C0h units. This module writes 16 int16_t steps per unit,
 * so its maximum is 3C0h * 16 = 15360 (PSG_MAX_OUTPUT_LEVEL), and the GBA's
 * whole digital mix, two FIFOs at +/-200h each plus the PSG's 0..3C0h, spans
 * -16384..31744 at the same scale: the audio module can add the FIFO output
 * at 16 steps per unit (a FIFO sample s at 100% is 4s units) and never
 * overflow int16_t. (The GBA clips that sum to its 10-bit range after adding
 * SOUNDBIAS; the host has more room, so nothing clips.) The output carries
 * the DC offset the hardware's mix does; the GBA's output capacitor removes
 * it, and so should the audio module, after mixing. SOUNDBIAS is not
 * modelled: its bias and PWM resolution act only on the PWM stage the host
 * replaces.
 *
 * AMBIGUITIES. SOUNDCNT_H bits 0-1 = 3 is "Prohibited"; it plays as 100% here
 * (mGBA's src/gba/audio.c computes a 200% shift for it; 100% keeps the
 * derived maximum). The other choices are in the channel files' headers.
 *
 * Not modelled: the Direct Sound FIFOs (the m4a mixer's output replaces
 * them), SOUNDBIAS, the Game Boy's power-on and trigger-timing quirks, and
 * its NR10 negate quirk (clearing the sweep direction bit after a decreasing
 * calculation stops channel 1; Pan Docs "Audio Details", Obscure Behavior).
 */
#include <string.h>

#include "gba_memmap.h"
#include "psg.h"
#include "psg_internal.h"

/* The game stores halfwords and words into the I/O region, and this module
 * reads their bytes by GBATEK's byte addresses (NR13 is the low byte of
 * SOUND1CNT_X): that needs the host to be little-endian, as the GBA is. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "psg reads register bytes little-endian");

/* Byte offsets of the sound registers in the I/O region, GBATEK "GBA Sound
 * Channel 1 - Tone & Sweep" to "GBA Sound Control Registers". */
#define REG_NR10 0x060u /* SOUND1CNT_L */
#define REG_NR11 0x062u /* SOUND1CNT_H, low byte */
#define REG_NR12 0x063u /* SOUND1CNT_H, high byte */
#define REG_NR13 0x064u /* SOUND1CNT_X, low byte */
#define REG_NR14 0x065u /* SOUND1CNT_X, high byte */
#define REG_NR21 0x068u /* SOUND2CNT_L */
#define REG_NR22 0x069u
#define REG_NR23 0x06Cu /* SOUND2CNT_H */
#define REG_NR24 0x06Du
#define REG_NR30 0x070u /* SOUND3CNT_L */
#define REG_NR31 0x072u /* SOUND3CNT_H */
#define REG_NR32 0x073u
#define REG_NR33 0x074u /* SOUND3CNT_X */
#define REG_NR34 0x075u
#define REG_NR41 0x078u /* SOUND4CNT_L */
#define REG_NR42 0x079u
#define REG_NR43 0x07Cu /* SOUND4CNT_H */
#define REG_NR44 0x07Du
#define REG_NR50 0x080u /* SOUNDCNT_L, low byte */
#define REG_NR51 0x081u /* SOUNDCNT_L, high byte */
#define REG_SOUNDCNT_H GBA_SOUNDCNT_H_OFFSET
#define REG_NR52 GBA_SOUNDCNT_X_OFFSET /* SOUNDCNT_X */
#define REG_WAVE_RAM 0x090u            /* WAVE_RAM0_L .. WAVE_RAM3_H */
/* GBATEK SOUNDCNT_X: the registers master disable resets, 4000060h-4000081h. */
#define REG_PSG_FIRST REG_NR10
#define REG_PSG_END (REG_NR51 + 1u)

_Static_assert(REG_WAVE_RAM + PSG_WAVE_BANK_BYTES <= GBA_IO_SIZE, "every sound register lies inside the I/O region");

/* SOUNDCNT_X: bits 0-3 "Sound 1-4 ON flag (Read Only)", bit 7 "PSG/FIFO
 * Master Enable". */
#define NR52_MASTER_ENABLE 0x80u
#define NR52_SOUND_1_ON 0x01u
#define NR52_SOUND_2_ON 0x02u
#define NR52_SOUND_3_ON 0x04u
#define NR52_SOUND_4_ON 0x08u

/* SOUNDCNT_L: NR50 bits 0-2 "Master Volume RIGHT", 4-6 "LEFT"; NR51 bits 0-3
 * enable channels 1-4 RIGHT, 4-7 LEFT. */
#define NR50_RIGHT_SHIFT 0u
#define NR50_LEFT_SHIFT 4u
#define NR50_VOLUME_MASK 0x07u
#define NR51_RIGHT_SHIFT 0u
#define NR51_LEFT_SHIFT 4u
#define NR51_ENABLE_MASK 0x0Fu

/* SOUNDCNT_H bits 0-1, "Sound # 1-4 Volume (0=25%, 1=50%, 2=100%,
 * 3=Prohibited)", in quarters; 3 plays as 100% (see AMBIGUITIES). */
#define SOUNDCNT_H_PSG_VOLUME_MASK 0x03u
#define PSG_VOLUME_QUARTERS_FULL 4u
static const uint8_t psg_volume_quarters[] = {1, 2, 4, 4};

/* OUTPUT SCALE, above. */
#define GBA_UNITS_PER_LEVEL 16u /* 100h units over 16 levels */
#define MASTER_VOLUME_STEPS 8u  /* NR50 n scales by (n+1)/8 */
#define INT16_PER_GBA_UNIT 16u
#define CHANNEL_COUNT 4u
#define GBA_FIFO_UNITS 0x200u /* one FIFO's full swing */
#define GBA_FIFO_COUNT 2u

_Static_assert(CHANNEL_COUNT *PSG_LEVEL_MAX *GBA_UNITS_PER_LEVEL *INT16_PER_GBA_UNIT == PSG_MAX_OUTPUT_LEVEL,
               "PSG_MAX_OUTPUT_LEVEL follows from the scale");
_Static_assert((GBA_FIFO_COUNT * GBA_FIFO_UNITS + CHANNEL_COUNT * PSG_LEVEL_MAX * GBA_UNITS_PER_LEVEL)
                       * INT16_PER_GBA_UNIT
                   <= INT16_MAX,
               "the whole GBA mix fits int16_t at this scale");

struct frame_sequencer {
    uint32_t countdown; /* cycles until the next step */
    uint8_t step;       /* the step that comes next, 0-7 */
};

/* NRx1-NRx4 offsets of a tone channel. */
struct square_registers_at {
    uint32_t nrx1, nrx2, nrx3, nrx4;
};

static const struct square_registers_at channel_1_at = {REG_NR11, REG_NR12, REG_NR13, REG_NR14};
static const struct square_registers_at channel_2_at = {REG_NR21, REG_NR22, REG_NR23, REG_NR24};

static struct {
    bool initialised;
    bool powered; /* SOUNDCNT_X bit 7 as last seen */
    uint32_t output_rate_hz;
    uint32_t cycle_remainder; /* fraction of a cycle owed, in 1/output_rate_hz */
    struct frame_sequencer sequencer;
    struct psg_square square_1, square_2;
    struct psg_sweep sweep;
    uint16_t channel_1_frequency_read; /* NR13/NR14 at the start of the call */
    struct psg_wave wave;
    struct psg_noise noise;
    uint8_t nr50, nr51, soundcnt_h;
} psg;

/* The I/O region is mapped at its GBA address (gba_memmap.h), so the
 * integer-to-pointer conversion is the identity. Flagged for the 64-bit
 * milestone like gba_memmap's addresses: GBA addresses fit in 32 bits. Every
 * offset used is a constant checked against GBA_IO_SIZE above. */
static uint8_t *io_register(uint32_t offset)
{
    return (uint8_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

static uint8_t read_register(uint32_t offset)
{
    return *io_register(offset);
}

static void write_register(uint32_t offset, uint8_t value)
{
    *io_register(offset) = value;
}

/* Whether NRx4 holds a trigger the module has not acted on; consumes it (see
 * TRIGGERS). */
static bool take_trigger(uint32_t nrx4)
{
    const uint8_t value = read_register(nrx4);
    if (!(value & PSG_NRX4_INITIAL))
        return false;
    write_register(nrx4, (uint8_t)(value & ~PSG_NRX4_INITIAL));
    return true;
}

/* --- Applying the registers ----------------------------------------------- */

static void power_on(void)
{
    psg.powered = true;
    psg.sequencer = (struct frame_sequencer){PSG_CYCLES_PER_SEQUENCER_STEP, 0};
}

static void power_off(void)
{
    psg.powered = false;
    psg.square_1 = (struct psg_square){0};
    psg.square_2 = (struct psg_square){0};
    psg.sweep = (struct psg_sweep){0};
    psg_wave_power_off(&psg.wave);
    psg.noise = (struct psg_noise){0};
    psg.nr50 = 0;
    psg.nr51 = 0;
    memset(io_register(REG_PSG_FIRST), 0, REG_PSG_END - REG_PSG_FIRST);
}

/* Applies a tone channel's registers; returns whether it was triggered. */
static bool apply_square(struct psg_square *square, const struct square_registers_at *at)
{
    const bool trigger = take_trigger(at->nrx4);
    const struct psg_square_registers registers = {
        read_register(at->nrx1),
        read_register(at->nrx2),
        read_register(at->nrx3),
        read_register(at->nrx4),
    };
    psg_square_update(square, &registers);
    if (trigger)
        psg_square_trigger(square, &registers);
    return trigger;
}

static void apply_channel_1(void)
{
    psg_sweep_update(&psg.sweep, read_register(REG_NR10));
    if (apply_square(&psg.square_1, &channel_1_at))
        psg_sweep_trigger(&psg.sweep, &psg.square_1);
    psg.channel_1_frequency_read = psg.square_1.frequency;
}

static void apply_wave(void)
{
    const bool trigger = take_trigger(REG_NR34);
    struct psg_wave_registers registers = {
        read_register(REG_NR30), read_register(REG_NR31), read_register(REG_NR32),
        read_register(REG_NR33), read_register(REG_NR34), {0},
    };
    memcpy(registers.window, io_register(REG_WAVE_RAM), PSG_WAVE_BANK_BYTES);
    psg_wave_update(&psg.wave, &registers);
    if (trigger)
        psg_wave_trigger(&psg.wave, &registers);
}

static void apply_noise(void)
{
    const bool trigger = take_trigger(REG_NR44);
    const struct psg_noise_registers registers = {
        read_register(REG_NR41),
        read_register(REG_NR42),
        read_register(REG_NR43),
        read_register(REG_NR44),
    };
    psg_noise_update(&psg.noise, &registers);
    if (trigger)
        psg_noise_trigger(&psg.noise, &registers);
}

static void apply_registers(void)
{
    psg.soundcnt_h = read_register(REG_SOUNDCNT_H);
    if (!(read_register(REG_NR52) & NR52_MASTER_ENABLE)) {
        power_off();
        return;
    }
    if (!psg.powered)
        power_on();
    apply_channel_1();
    apply_square(&psg.square_2, &channel_2_at);
    apply_wave();
    apply_noise();
    psg.nr50 = read_register(REG_NR50);
    psg.nr51 = read_register(REG_NR51);
}

/* What the hardware writes into the registers itself: the sweep's frequency
 * and the status bits. */
static void publish_registers(void)
{
    const uint16_t frequency = psg.square_1.frequency;
    if (psg.powered && frequency != psg.channel_1_frequency_read) {
        write_register(REG_NR13, (uint8_t)(frequency & PSG_NRX3_FREQUENCY_LOW_MASK));
        const uint8_t nr14 = read_register(REG_NR14);
        write_register(REG_NR14,
                       (uint8_t)((nr14 & ~PSG_NRX4_FREQUENCY_HIGH_MASK) | frequency >> PSG_NRX4_FREQUENCY_HIGH_SHIFT));
    }
    uint8_t status = read_register(REG_NR52) & NR52_MASTER_ENABLE;
    if (psg.square_1.channel.on)
        status |= NR52_SOUND_1_ON;
    if (psg.square_2.channel.on)
        status |= NR52_SOUND_2_ON;
    if (psg.wave.channel.on)
        status |= NR52_SOUND_3_ON;
    if (psg.noise.channel.on)
        status |= NR52_SOUND_4_ON;
    write_register(REG_NR52, status);
}

/* --- Running the hardware ------------------------------------------------- */

static void clock_frame_sequencer(void)
{
    const uint8_t step = psg.sequencer.step;
    if (step % (PSG_SEQUENCER_HZ / PSG_LENGTH_HZ) == 0) {
        psg_channel_clock_length(&psg.square_1.channel);
        psg_channel_clock_length(&psg.square_2.channel);
        psg_channel_clock_length(&psg.wave.channel);
        psg_channel_clock_length(&psg.noise.channel);
    }
    if (step == PSG_SEQUENCER_SWEEP_STEP_A || step == PSG_SEQUENCER_SWEEP_STEP_B)
        psg_sweep_clock(&psg.sweep, &psg.square_1);
    if (step == PSG_SEQUENCER_ENVELOPE_STEP) {
        psg_envelope_clock(&psg.square_1.envelope);
        psg_envelope_clock(&psg.square_2.envelope);
        psg_envelope_clock(&psg.noise.envelope);
    }
    psg.sequencer.step = (uint8_t)((step + 1u) % PSG_SEQUENCER_STEPS);
}

static void run_channels(uint32_t cycles)
{
    psg_square_run(&psg.square_1, cycles);
    psg_square_run(&psg.square_2, cycles);
    psg_wave_run(&psg.wave, cycles);
    psg_noise_run(&psg.noise, cycles);
}

/* Runs the channels and the frame sequencer for `cycles` cycles, stopping at
 * each sequencer step so its clocks land between the right channel steps. */
static void run_hardware(uint32_t cycles)
{
    while (cycles > 0) {
        const uint32_t span = cycles < psg.sequencer.countdown ? cycles : psg.sequencer.countdown;
        run_channels(span);
        cycles -= span;
        psg.sequencer.countdown -= span;
        if (psg.sequencer.countdown == 0) {
            clock_frame_sequencer();
            psg.sequencer.countdown = PSG_CYCLES_PER_SEQUENCER_STEP;
        }
    }
}

/* Cycles from the previous output sample to the next (RESAMPLING). */
static uint32_t cycles_to_next_sample(void)
{
    const uint32_t owed = psg.cycle_remainder + PSG_SYSTEM_CLOCK_HZ; /* below 2^25 */
    psg.cycle_remainder = owed % psg.output_rate_hz;
    return owed / psg.output_rate_hz;
}

/* One side of the mix (OUTPUT SCALE): the enabled channels' levels, scaled by
 * the side's master volume and SOUNDCNT_H. */
static int16_t mix_side(const uint8_t levels[CHANNEL_COUNT], unsigned enables, unsigned volume)
{
    uint32_t sum = 0;
    for (unsigned channel = 0; channel < CHANNEL_COUNT; channel++)
        if (enables >> channel & 1u)
            sum += levels[channel];
    const uint32_t quarters = psg_volume_quarters[psg.soundcnt_h & SOUNDCNT_H_PSG_VOLUME_MASK];
    const uint32_t scaled = sum * GBA_UNITS_PER_LEVEL * INT16_PER_GBA_UNIT * (volume + 1u) * quarters
                            / (MASTER_VOLUME_STEPS * PSG_VOLUME_QUARTERS_FULL);
    return (int16_t)scaled; /* at most PSG_MAX_OUTPUT_LEVEL, asserted above */
}

static void mix_sample(int16_t *left, int16_t *right)
{
    const uint8_t levels[CHANNEL_COUNT] = {
        psg_square_level(&psg.square_1),
        psg_square_level(&psg.square_2),
        psg_wave_level(&psg.wave),
        psg_noise_level(&psg.noise),
    };
    const unsigned nr50 = psg.nr50, nr51 = psg.nr51;
    *left = mix_side(levels, nr51 >> NR51_LEFT_SHIFT & NR51_ENABLE_MASK, nr50 >> NR50_LEFT_SHIFT & NR50_VOLUME_MASK);
    *right = mix_side(levels, nr51 >> NR51_RIGHT_SHIFT & NR51_ENABLE_MASK, nr50 >> NR50_RIGHT_SHIFT & NR50_VOLUME_MASK);
}

/* --- Public interface ------------------------------------------------------ */

int psg_init(uint32_t output_sample_rate_hz)
{
    psg_shutdown();
    if (output_sample_rate_hz == 0 || output_sample_rate_hz > PSG_MAX_OUTPUT_RATE_HZ)
        return PSG_ERR_OUTPUT_RATE;
    psg.output_rate_hz = output_sample_rate_hz;
    psg.initialised = true;
    return PSG_OK;
}

void psg_shutdown(void)
{
    memset(&psg, 0, sizeof psg);
}

size_t psg_generate(int16_t *stereo_out, size_t frames)
{
    if (!psg.initialised || (stereo_out == NULL && frames > 0))
        return 0;
    apply_registers();
    int16_t *out = stereo_out;
    for (size_t frame = 0; frame < frames; frame++) {
        const uint32_t cycles = cycles_to_next_sample();
        if (psg.powered)
            run_hardware(cycles);
        mix_sample(&out[0], &out[1]);
        out += 2;
    }
    publish_registers();
    return frames;
}
