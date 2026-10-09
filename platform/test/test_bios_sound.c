// deps: bios_internal.c transfer_bounds.c gba_memmap.c
/*
 * Tests of the BIOS sound functions, from GBATEK "BIOS Sound Functions":
 * SoundBias (SWI 19h: "0=Level 000h, any other value=Level 200h", "The
 * upper bits of the register are kept unchanged") and MidiKey2Freq (SWI 1Fh:
 * key mk plus fp/256 halftones; GBATEK's BIOS-reading formula
 * "a = (MidiKey2Freq(i-(((i AND 3)+1)OR 3), 168, 0) * 2) SHR 24" means key
 * 168 returns half the wave's freq word, read at wa+4). Expected rates are
 * worked by hand from freq * 2^((mk + fp/256 - 180) / 12), with the 2^(n/12)
 * factors round(2^31 * 2^(n/12)) and linear interpolation between keys.
 */
#include <gba/types.h>
#include <gba/defines.h>
#include <gba/io_reg.h>

#include <stddef.h>
#include <stdint.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

/* The head of expansion's struct WaveData (include/gba/m4a_internal.h). */
struct WaveData {
    uint16_t type;
    uint16_t status;
    /* Written here, read by MidiKey2Freq through the byte offset (checked below). */
    // cppcheck-suppress unusedStructMember
    uint32_t freq;
    uint32_t loopStart;
    uint32_t size;
};
_Static_assert(offsetof(struct WaveData, type) == 0 && offsetof(struct WaveData, status) == 2
                   && offsetof(struct WaveData, freq) == 4 && offsetof(struct WaveData, loopStart) == 8
                   && offsetof(struct WaveData, size) == 12,
               "expansion's WaveData head layout");

/* m4a stores a sample's rate times 1024 (key 60 = 180 - 120: 2^-10). */
#define RATE_13379 (13379u * 1024u)

static void setup(void)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
}

static void teardown(void)
{
    transfer_bounds_shutdown();
}

static uint32_t rate(uint32_t freq, uint8_t mk, uint8_t fp)
{
    const struct WaveData wave = { .type = 0, .status = 0, .freq = freq, .loopStart = 0, .size = 0 };
    return MidiKey2Freq(&wave, mk, fp);
}

/* Middle C (key 60) plays the sample at its own rate: freq / 2^10. */
PTEST(midikey2freq_middle_c)
{
    setup();
    PT_ASSERT_EQ_U32(rate(RATE_13379, 60, 0), 13379);
    teardown();
}

PTEST(midikey2freq_octaves)
{
    setup();
    PT_ASSERT_EQ_U32(rate(RATE_13379, 72, 0), 26758); /* x2 */
    PT_ASSERT_EQ_U32(rate(RATE_13379, 48, 0), 6689);  /* /2 = 6689.5 */
    PT_ASSERT_EQ_U32(rate(RATE_13379, 0, 0), 418);    /* / 2^15 = 418.09 */
    teardown();
}

PTEST(midikey2freq_halftones)
{
    setup();
    PT_ASSERT_EQ_U32(rate(RATE_13379, 61, 0), 14174); /* 13379 * 2^(1/12) = 14174.55 */
    PT_ASSERT_EQ_U32(rate(RATE_13379, 69, 0), 22500); /* 13379 * 2^(9/12) = 22500.68 */
    PT_ASSERT_EQ_U32(rate(RATE_13379, 71, 0), 25256); /* 13379 * 2^(11/12) = 25256.13 */
    teardown();
}

/* fp/256 of a halftone, interpolated linearly between keys 60 and 61:
 * 13379 + (14174.55 - 13379) / 2 = 13776.8. */
PTEST(midikey2freq_fine_adjust)
{
    setup();
    PT_ASSERT_EQ_U32(rate(RATE_13379, 60, 128), 13776);
    PT_ASSERT_EQ_U32(rate(RATE_13379, 59, 255), 13376); /* just below key 60 */
    teardown();
}

/* GBATEK's BIOS-reading formula relies on key 168 halving the word. */
PTEST(midikey2freq_gbatek_byte_formula)
{
    setup();
    uint32_t half = rate(UINT32_C(0x89ABCDEF), 168, 0);
    PT_ASSERT_EQ_U32(half, UINT32_C(0x44D5E6F7));
    PT_ASSERT_EQ_U32((half * 2) >> 24, 0x89);
    teardown();
}

/* Key 180's factor (2^32) does not fit; keys above 178 play as 178 + 255/256. */
PTEST(midikey2freq_keys_above_178_clamp)
{
    setup();
    uint32_t top = rate(RATE_13379, 178, 255);
    PT_ASSERT_EQ_U32(top, 12928333);
    PT_ASSERT_EQ_U32(rate(RATE_13379, 179, 0), top);
    PT_ASSERT_EQ_U32(rate(RATE_13379, 180, 0), top);
    PT_ASSERT_EQ_U32(rate(RATE_13379, 255, 7), top);
    PT_ASSERT_EQ_U32(rate(UINT32_MAX, 178, 255), UINT32_C(4053020521)); /* no overflow */
    teardown();
}

PTEST(midikey2freq_unreadable_wave_is_zero)
{
    setup();
    /* An address no GBA region or native object covers. */
    PT_ASSERT_EQ_U32(MidiKey2Freq((const struct WaveData *)(uintptr_t)0x10, 60, 0), 0);
    teardown();
}

PTEST(soundbias_levels_keep_upper_bits)
{
    setup();
    REG_SOUNDBIAS = 0xC2AB;
    SoundBiasChange(0);
    PT_ASSERT_EQ_U32(REG_SOUNDBIAS, 0xC000);
    SoundBiasChange(5);
    PT_ASSERT_EQ_U32(REG_SOUNDBIAS, 0xC200);
    REG_SOUNDBIAS = 0x41FF;
    SoundBiasReset();
    PT_ASSERT_EQ_U32(REG_SOUNDBIAS, 0x4000);
    SoundBiasSet();
    PT_ASSERT_EQ_U32(REG_SOUNDBIAS, 0x4200);
    REG_SOUNDBIAS = 0;
    teardown();
}

PTEST_MAIN()
