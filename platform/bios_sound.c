/*
 * bios_sound.c: SoundBias (SWI 19h) and MidiKey2Freq (SWI 1Fh).
 *
 * Stands in for the two BIOS sound functions outside the BIOS's own sound
 * driver (which expansion replaces with its m4a engine; bios.h). GBATEK
 * "BIOS Sound Functions", https://problemkaputt.de/gbatek.htm#biossoundfunctions.
 *
 * SoundBias moves SOUNDBIAS's level to 000h or 200h "with short delays",
 * keeping the upper bits; the delays only avoid a click on the real DAC, so
 * the level is set at once.
 *
 * MidiKey2Freq returns the value the BIOS sound driver assigns a channel's
 * frequency when it plays wave wa at MIDI key mk plus fp/256 halftones.
 * GBATEK gives the inputs and a formula built on it for reading BIOS bytes,
 * a = (MidiKey2Freq(wa, 168, 0) * 2) >> 24 with the wave's 32-bit freq word
 * at wa+4, which needs key 168 to return freq/2: with 12 halftones to the
 * octave that fixes the rate as freq * 2^((mk + fp/256 - 180) / 12). The
 * method is the GBA sound library's own, in expansion's src/m4a.c
 * (MidiKeyToFreq) and src/m4a_tables.c: a factor 2^31 * 2^(n/12) per
 * halftone n of the octave (round() of it; the table below was computed
 * independently and equals gFreqTable), shifted right by the octaves below
 * the top one, interpolated linearly by fp/256 towards the next key, then
 * freq times the factor divided by 2^32. Keys above 178 play as 178 +
 * 255/256, as m4a.c clamps: key 180's factor would be 2^32.
 */
#include <stdio.h>

#include "bios.h"
#include "bios_internal.h"
#include "gba_memmap.h"

/* GBATEK "GBA Sound Controller": 4000088h SOUNDBIAS, bits 0-9 the bias
 * level (bit 0 unused), bits 10-15 kept by SoundBias. SoundBias: "0=Level
 * 000h, any other value=Level 200h". */
#define IO_SOUNDBIAS UINT32_C(0x088)
#define SOUNDBIAS_LEVEL_MASK UINT16_C(0x03FF)
#define SOUNDBIAS_LEVEL_SET UINT16_C(0x0200)
#define SOUNDBIAS_LEVEL_ZERO UINT16_C(0x0000)

/* The wave's frequency word (GBATEK's formula reads [wa + 4]). */
#define WAVE_FREQ_OFFSET 4u
#define WAVE_FREQ_SIZE 4u

#define HALFTONES_PER_OCTAVE 12u
#define HIGHEST_KEY 178u        /* the factor of key 179 is the last below 2^32 */
#define HIGHEST_FINE 255u
/* Key 168 is the top octave's first key: 168 / 12 = 14 octaves above key 0,
 * whose factor is 2^31 >> 14. */
#define TOP_OCTAVE 14u
#define FINE_STEPS_SHIFT 8u     /* fp is in 1/256 halftones */
#define FACTOR_SHIFT 32u        /* the factor is a 0.32 fraction of 2 */

/* round(2^31 * 2^(n/12)), n = 0..11. */
static const uint32_t halftone_factor[HALFTONES_PER_OCTAVE] = {
    2147483648u, 2275179671u, 2410468894u, 2553802834u, 2705659852u, 2866546760u,
    3037000500u, 3217589947u, 3408917802u, 3611622603u, 3826380858u, 4053909305u,
};

static volatile uint16_t *soundbias(void)
{
    return (volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + IO_SOUNDBIAS);
}

void SoundBiasChange(uint32_t level)
{
    uint16_t kept = (uint16_t)(*soundbias() & ~SOUNDBIAS_LEVEL_MASK);
    *soundbias() = (uint16_t)(kept | (level != 0 ? SOUNDBIAS_LEVEL_SET : SOUNDBIAS_LEVEL_ZERO));
}

void SoundBiasReset(void)
{
    SoundBiasChange(0);
}

void SoundBiasSet(void)
{
    SoundBiasChange(1);
}

/* 2^31 * 2^((key - 168) / 12), key <= 179. */
static uint32_t key_factor(unsigned key)
{
    return halftone_factor[key % HALFTONES_PER_OCTAVE] >> (TOP_OCTAVE - key / HALFTONES_PER_OCTAVE);
}

uint32_t MidiKey2Freq(const struct WaveData *wa, uint8_t mk, uint8_t fp)
{
    if (!bios_range_ok("MidiKey2Freq", "wave", wa, WAVE_FREQ_OFFSET + WAVE_FREQ_SIZE, TRANSFER_READ)) {
        return 0;
    }
    const uint8_t *freq_word = (const uint8_t *)wa + WAVE_FREQ_OFFSET;
    unsigned key = mk;
    unsigned fine = fp;
    if (key > HIGHEST_KEY) {
        key = HIGHEST_KEY;
        fine = HIGHEST_FINE;
    }
    uint32_t low = key_factor(key);
    uint32_t step = key_factor(key + 1) - low;
    uint32_t factor = low + (uint32_t)(((uint64_t)step * fine) >> FINE_STEPS_SHIFT);
    return (uint32_t)(((uint64_t)bios_load32(freq_word) * factor) >> FACTOR_SHIFT);
}
