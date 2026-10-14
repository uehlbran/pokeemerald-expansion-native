// deps: m4a/m4a_mixer.c m4a/m4a_render.c m4a/m4a_sequencer.c m4a/m4a_note.c m4a/m4a_commands.c m4a/m4a_clear64.c m4a/test_m4a_support.c m4a/test_m4a_c_reference.c ../src/m4a_tables.c
/*
 * Unit tests for the m4a engine's channel rendering (the sample loops of
 * src/m4a_1.s SoundMainRAM, SoundMainRAM_Unk1 and SoundMainRAM_Unk2),
 * translated to C in m4a_render.c: fixed-rate and interpolating mixing,
 * loops with overshoot, reversed and DPCM-compressed samples. Expected
 * values are derived by hand from the assembly's arithmetic or by the
 * small reference players below, which model the documented behaviour
 * with plain integer arithmetic rather than the engine's register tricks.
 */
#include <stddef.h>
#include <string.h>

#include "m4a/test_m4a_support.h"
#include "ptest.h"

static uint8_t sExpected[PCM_DMA_BUF_SIZE * 2];

static const uint8_t *PcmBytes(const struct SoundInfo *si)
{
    return (const uint8_t *)si->pcmBuffer;
}

/* Fills the ring and the expected ring with the same byte. */
static void FillPcm(struct SoundInfo *si, uint8_t value)
{
    memset(si->pcmBuffer, value, sizeof(si->pcmBuffer));
    memset(sExpected, value, sizeof(sExpected));
}

static uint32_t PointerField(const struct SoundChannel *chan)
{
    uintptr_t bits;

    memcpy(&bits, &chan->currentPointer, sizeof(bits));
    return (uint32_t)bits;
}

/* Reference interpolating player. */

struct RefPlayer {
    const int32_t *samples; // samples[-1] must exist for reverse play
    int64_t pos;
    int64_t count;
    uint32_t fw;
    uint32_t step;
    int64_t loopStart;
    int64_t loopLen; // 0: no loop
    int reverse;
    int stopped;
};

/* cur + fraction * (next - cur), the fraction 23 bits, rounded down. */
static int32_t RefOutput(const struct RefPlayer *p)
{
    int32_t cur = p->samples[p->pos];
    int32_t next = p->samples[p->reverse ? p->pos - 1 : p->pos + 1];

    return cur + (int32_t)M4aTestFloorDiv((int64_t)p->fw * (next - cur), 1 << 23);
}

/* Steps use at most 7 whole samples here, so the fraction mask is 23 bits. */
static void RefAdvance(struct RefPlayer *p)
{
    uint32_t whole;
    int64_t over;

    p->fw += p->step;
    whole = p->fw >> 23;
    p->fw &= 0x7FFFFFu;
    if (whole == 0)
        return;
    p->count -= whole;
    if (p->count > 0) {
        p->pos += p->reverse ? -(int64_t)whole : (int64_t)whole;
        return;
    }
    if (p->loopLen == 0) {
        p->stopped = 1;
        return;
    }
    over = -p->count;
    p->pos = p->loopStart + over % p->loopLen;
    p->count = p->loopLen - over % p->loopLen;
}

/* Mixes one frame of the reference into sExpected at frame 0. */
static void RefMixFrame(struct RefPlayer *p)
{
    for (uint32_t i = 0; i < SPV && !p->stopped; i++) {
        int32_t out = RefOutput(p);

        sExpected[i] = (uint8_t)(sExpected[i] + M4aTestMixDelta(ENV_RIGHT, out));
        sExpected[PCM_DMA_BUF_SIZE + i] = (uint8_t)(sExpected[PCM_DMA_BUF_SIZE + i] + M4aTestMixDelta(ENV_LEFT, out));
        RefAdvance(p);
    }
}

/* Reference DPCM decoder (the format wav2agb writes): 33-byte blocks of 64
 * samples; sample 0 is the first byte, sample 1 adds the delta of the second
 * byte's low nibble, every later byte gives two samples, high nibble first. */
static const int8_t kDpcmDeltas[16] = {0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1};

static void RefDecodeDpcm(const uint8_t *data, size_t numBlocks, int32_t *out)
{
    for (size_t b = 0; b < numBlocks; b++) {
        const uint8_t *block = data + b * 33;
        int64_t level = (int8_t)block[0];

        out[b * 64] = M4aTestWrapS8(level);
        level += kDpcmDeltas[block[1] & 0xF];
        out[b * 64 + 1] = M4aTestWrapS8(level);
        for (size_t j = 2; j < 64; j++) {
            uint8_t byte = block[j / 2 + 1];

            level += kDpcmDeltas[(j % 2 == 0) ? (byte >> 4) : (byte & 0xF)];
            out[b * 64 + j] = M4aTestWrapS8(level);
        }
    }
}

/* Mixer: no resampling (TONEDATA_TYPE_FIX) */

PTEST(Mixer_fixed_rate_channels_add_and_a_finished_channel_leaves_the_rest_alone)
{
    /* Channel 0 loops four samples of 64: right +63 (254 * 64 / 256 = 63.5),
     * left +31 (127 * 64 / 256 = 31.75). Channel 1 plays six samples once:
     * right 254 * {10,-20,30,-40,50,-60} / 256 floored = {9,-20,29,-40,49,-60},
     * left 127 * ... = {4,-10,14,-20,24,-30}; it stops in the middle of the
     * second word and the rest of the frame keeps channel 0 alone. */
    static const uint8_t kRight[SPV] = {72, 43, 92, 23, 112, 3, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};
    static const uint8_t kLeft[SPV] = {35, 21, 45, 11, 55, 1, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31};
    static const int8_t kShort[] = {10, -20, 30, -40, 50, -60};
    static union M4aTestWave loopWave;
    static union M4aTestWave shortWave;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&loopWave, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&loopWave, i, 64);
    M4aTestMakeWave(&shortWave, 0, 0, 0, 0, 6);
    for (uint32_t i = 0; i < 6; i++)
        M4aTestSetWaveByte(&shortWave, i, (uint8_t)kShort[i]);
    si->maxChans = 2;
    M4aTestStartChannel(si, 0, &loopWave.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    M4aTestStartChannel(si, 1, &shortWave.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    FillPcm(si, 0x5A);
    memcpy(sExpected, kRight, SPV);
    memcpy(sExpected + PCM_DMA_BUF_SIZE, kLeft, SPV);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SF_DECAY_AFTER_START);
    PT_ASSERT_EQ_U32(si->chans[0].count, 4);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, loopWave.wave.data);
    PT_ASSERT_EQ_U32(si->chans[1].statusFlags, 0);
    /* a finished channel's position is left as the start of the frame set it */
    PT_ASSERT_EQ_U32(si->chans[1].count, 6);
    PT_ASSERT_EQ_PTR(si->chans[1].currentPointer, shortWave.wave.data);
}

PTEST(Mixer_fixed_rate_loop_restarts_at_the_loop_start)
{
    static const int8_t kWave[] = {100, 50, -50, -100};
    /* 100, 50, -50, -100, then the loop 50, -50, -100 repeats */
    static const int8_t kPlayed[SPV] = {100, 50, -50, -100, 50, -50, -100, 50, -50, -100, 50, -50, -100, 50, -50, -100};
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 1, 4);
    for (uint32_t i = 0; i < 4; i++)
        M4aTestSetWaveByte(&w, i, (uint8_t)kWave[i]);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < SPV; i++) {
        sExpected[i] = M4aTestMixDelta(ENV_RIGHT, kPlayed[i]);
        sExpected[PCM_DMA_BUF_SIZE + i] = M4aTestMixDelta(ENV_LEFT, kPlayed[i]);
    }

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    /* reloaded at the 16th sample: 3 left from the loop start */
    PT_ASSERT_EQ_U32(si->chans[0].count, 3);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data + 1);
}

PTEST(Mixer_bytes_wrap_without_saturation)
{
    /* two channels of 127: right 254 * 127 / 256 = 126.01 -> 126, twice = 252
     * = 0xFC; left 127 * 127 / 256 = 63.0 -> 63, twice = 126 */
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&w, i, 127);
    si->maxChans = 2;
    M4aTestStartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    M4aTestStartChannel(si, 1, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

    SoundMain();

    for (uint32_t i = 0; i < SPV; i++) {
        PT_ASSERT_EQ_U32(PcmBytes(si)[i], 0xFC);
        PT_ASSERT_EQ_U32(PcmBytes(si)[PCM_DMA_BUF_SIZE + i], 126);
    }
}

PTEST(Mixer_offset_start_plays_from_the_offset)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 40);
    for (uint32_t i = 0; i < 41; i++)
        M4aTestSetWaveByte(&w, i, (uint8_t)(i * 3));
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 10);

    SoundMain();

    for (uint32_t i = 0; i < SPV; i++)
        PT_ASSERT_EQ_U32(PcmBytes(si)[i], M4aTestMixDelta(ENV_RIGHT, (int32_t)((10 + i) * 3)));
    PT_ASSERT_EQ_U32(si->chans[0].count, 40 - 10 - SPV);
    /* The samples run past the one-element data[] of struct WaveData into the union's storage. */
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, (const s8 *)&w + offsetof(struct WaveData, data) + 10 + SPV);
}

/* Mixer: resampling with linear interpolation */

static void FillRandomWave(union M4aTestWave *w, uint32_t count, uint32_t seed, int32_t *samples)
{
    uint32_t state = seed;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t byte = (uint8_t)M4aTestLcgNext(&state);

        M4aTestSetWaveByte(w, i, byte);
        samples[i] = (int8_t)byte;
    }
}

PTEST(Mixer_resamples_at_three_quarter_speed_across_frames)
{
    static union M4aTestWave w;
    static int32_t samples[41];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 40);
    FillRandomWave(&w, 41, 7, samples);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, 0, STEP_3_QUARTERS, 0);
    ref.samples = samples;
    ref.count = 40;
    ref.step = STEP_3_QUARTERS;

    for (int frame = 0; frame < 3; frame++) {
        FillPcm(si, 0);
        RefMixFrame(&ref);
        SoundMain();
        PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
        PT_ASSERT(!ref.stopped);
        PT_ASSERT_EQ_U32(si->chans[0].fw, ref.fw);
        PT_ASSERT_EQ_U32(si->chans[0].count, (uint32_t)ref.count);
        PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data + ref.pos);
    }
}

PTEST(Mixer_interpolation_hand_values)
{
    /* half speed over 0, 100, -100: 0; 0 + floor(0.5 * 100) = 50; 100;
     * 100 + floor(0.5 * -200) = 0; -100; ... */
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 20);
    M4aTestSetWaveByte(&w, 0, 0);
    M4aTestSetWaveByte(&w, 1, 100);
    M4aTestSetWaveByte(&w, 2, (uint8_t)-100);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, 0, STEP_HALF, 0);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], M4aTestMixDelta(ENV_RIGHT, 0));
    PT_ASSERT_EQ_U32(PcmBytes(si)[1], M4aTestMixDelta(ENV_RIGHT, 50));
    PT_ASSERT_EQ_U32(PcmBytes(si)[2], M4aTestMixDelta(ENV_RIGHT, 100));
    PT_ASSERT_EQ_U32(PcmBytes(si)[3], M4aTestMixDelta(ENV_RIGHT, 0));
    PT_ASSERT_EQ_U32(PcmBytes(si)[4], M4aTestMixDelta(ENV_RIGHT, -100));
}

PTEST(Mixer_step_is_divFreq_times_frequency_and_small_fractions_floor_negative_deltas)
{
    /* divFreq 627 (13379 Hz) * frequency 13379 = 8388633 = 0x800019: one
     * sample per output plus 25/2^23. At output i the fraction is 25 * i; a
     * delta of -200 gives floor(-5000 * i / 2^23) = -1 for i >= 1, a delta
     * of +200 gives 0. */
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 40);
    for (uint32_t i = 0; i < 41; i++)
        M4aTestSetWaveByte(&w, i, (uint8_t)(i % 2 == 0 ? 100 : -100));
    si->maxChans = 1;
    si->divFreq = 627;
    M4aTestStartChannel(si, 0, &w.wave, 0, 13379, 0);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], M4aTestMixDelta(ENV_RIGHT, 100));
    for (uint32_t i = 1; i < SPV; i++) {
        int32_t expected = (i % 2 == 0) ? 99 : -100;

        PT_ASSERT_EQ_U32(PcmBytes(si)[i], M4aTestMixDelta(ENV_RIGHT, expected));
    }
    PT_ASSERT_EQ_U32(si->chans[0].fw, 16u * 25u);
    PT_ASSERT_EQ_U32(si->chans[0].count, 40 - SPV);
}

PTEST(Mixer_loop_wrap_carries_the_overshoot_into_the_loop)
{
    /* step 3 over five samples looping from 2 (loop length 3), and step 7,
     * which overshoots the loop more than once */
    static const uint32_t kSteps[] = {0x1800000u, 0x3800000u};

    for (size_t s = 0; s < sizeof(kSteps) / sizeof(kSteps[0]); s++) {
        static union M4aTestWave w;
        static int32_t samples[6];
        struct SoundInfo *si = M4aTestSetUp();
        struct RefPlayer ref = {0};

        PT_ASSERT(si != NULL);
        M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 2, 5);
        FillRandomWave(&w, 6, (uint32_t)(11 + s), samples);
        si->maxChans = 1;
        M4aTestStartChannel(si, 0, &w.wave, 0, kSteps[s], 0);
        ref.samples = samples;
        ref.count = 5;
        ref.step = kSteps[s];
        ref.loopStart = 2;
        ref.loopLen = 3;

        for (int frame = 0; frame < 2; frame++) {
            FillPcm(si, 0);
            RefMixFrame(&ref);
            SoundMain();
            PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
            PT_ASSERT_EQ_U32(si->chans[0].count, (uint32_t)ref.count);
            PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data + ref.pos);
        }
    }
}

PTEST(Mixer_resampled_channel_stops_at_the_end_and_keeps_its_last_position)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 3);
    M4aTestSetWaveByte(&w, 0, 10);
    M4aTestSetWaveByte(&w, 1, 20);
    M4aTestSetWaveByte(&w, 2, 30);
    M4aTestSetWaveByte(&w, 3, 40); /* the sample past the end the format appends */
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, 0, STEP_ONE, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < 3; i++) {
        sExpected[i] = M4aTestMixDelta(ENV_RIGHT, (int32_t)(10 * (i + 1)));
        sExpected[PCM_DMA_BUF_SIZE + i] = M4aTestMixDelta(ENV_LEFT, (int32_t)(10 * (i + 1)));
    }

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
    PT_ASSERT_EQ_U32(si->chans[0].count, 3);
    PT_ASSERT_EQ_U32(si->chans[0].fw, 0);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data);
}

/* Mixer: reversed and compressed samples (SoundMainRAM_Unk1/Unk2) */

PTEST(Mixer_reverse_plays_backwards_and_reads_the_header_byte_before_the_data)
{
    /* half speed from the end of 10..80: 80, 75, 70, ..., 10, and finally
     * 10 + floor(0.5 * (0 - 10)) = 5, the byte before sample 0 being the
     * top byte of size (0). The 16th output's advance ends the sample. */
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 8);
    for (uint32_t i = 0; i < 8; i++)
        M4aTestSetWaveByte(&w, i, (uint8_t)(10 * (i + 1)));
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_REV, STEP_HALF, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < SPV; i++) {
        int32_t out = 80 - 5 * (int32_t)i;

        sExpected[i] = M4aTestMixDelta(ENV_RIGHT, out);
        sExpected[PCM_DMA_BUF_SIZE + i] = M4aTestMixDelta(ENV_LEFT, out);
    }

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
    PT_ASSERT_EQ_U32(si->chans[0].count, 0);
    PT_ASSERT_EQ_U32(si->chans[0].fw, 0);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data);
}

PTEST(Mixer_reverse_keeps_its_position_one_past_the_current_sample)
{
    static union M4aTestWave w;
    static int32_t samples[1 + 41];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 40);
    FillRandomWave(&w, 40, 21, samples + 1);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_REV, STEP_3_QUARTERS, 4);
    /* reversed from offset 4: the first sample is index 40 - 4 - 1 */
    ref.samples = samples + 1;
    ref.pos = 35;
    ref.count = 36;
    ref.step = STEP_3_QUARTERS;
    ref.reverse = 1;

    for (int frame = 0; frame < 2; frame++) {
        FillPcm(si, 0);
        RefMixFrame(&ref);
        SoundMain();
        PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
        /* decay after the first frame, then 255 * 255 >> 8 = 254 <= 255: sustain */
        PT_ASSERT_EQ_U32(si->chans[0].statusFlags,
                         SF_SPECIAL | (frame == 0 ? SF_DECAY_AFTER_START : SOUND_CHANNEL_SF_ENV_SUSTAIN));
        PT_ASSERT_EQ_U32(si->chans[0].count, (uint32_t)ref.count);
        PT_ASSERT_EQ_U32(si->chans[0].fw, ref.fw);
        PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data + ref.pos + 1);
    }
}

#define DPCM_BLOCKS 2
#define DPCM_SAMPLES (DPCM_BLOCKS * 64)

static void MakeCompressedWave(union M4aTestWave *w, int32_t *decoded, uint32_t seed)
{
    uint8_t data[DPCM_BLOCKS * 33];
    uint32_t state = seed;

    M4aTestMakeWave(w, 1, 0, 0, 0, DPCM_SAMPLES);
    for (size_t i = 0; i < sizeof(data); i++) {
        data[i] = (uint8_t)M4aTestLcgNext(&state);
        M4aTestSetWaveByte(w, (uint32_t)i, data[i]);
    }
    RefDecodeDpcm(data, DPCM_BLOCKS, decoded);
}

static uint32_t CacheKey(const struct SoundChannel *chan)
{
    return (uint32_t)chan->xpi | ((uint32_t)chan->xpc << 16);
}

PTEST(Mixer_compressed_forward_decodes_blocks_across_a_boundary)
{
    static union M4aTestWave w;
    static int32_t decoded[DPCM_SAMPLES + 1];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    MakeCompressedWave(&w, decoded, 31);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_CMP, STEP_ONE, 56);
    ref.samples = decoded;
    ref.pos = 56;
    ref.count = DPCM_SAMPLES - 56;
    ref.step = STEP_ONE;
    FillPcm(si, 0);
    RefMixFrame(&ref);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SF_SPECIAL | SF_DECAY_AFTER_START);
    PT_ASSERT_EQ_U32(si->chans[0].count, DPCM_SAMPLES - 56 - SPV);
    /* compressed channels keep a sample index in currentPointer */
    PT_ASSERT_EQ_U32(PointerField(&si->chans[0]), 56 + SPV);
    /* the last block decoded held sample 73: block 1 */
    PT_ASSERT_EQ_U32(CacheKey(&si->chans[0]), 1);
}

PTEST(Mixer_compressed_forward_interpolates_over_frames)
{
    static union M4aTestWave w;
    static int32_t decoded[DPCM_SAMPLES + 1];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    MakeCompressedWave(&w, decoded, 37);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_CMP, STEP_3_QUARTERS, 0);
    ref.samples = decoded;
    ref.count = (int64_t)DPCM_SAMPLES;
    ref.step = STEP_3_QUARTERS;

    for (int frame = 0; frame < 3; frame++) {
        FillPcm(si, 0);
        RefMixFrame(&ref);
        SoundMain();
        PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
        PT_ASSERT_EQ_U32(si->chans[0].count, (uint32_t)ref.count);
        PT_ASSERT_EQ_U32(si->chans[0].fw, ref.fw);
        PT_ASSERT_EQ_U32(PointerField(&si->chans[0]), (uint32_t)ref.pos);
    }
}

PTEST(Mixer_compressed_reverse_plays_backwards)
{
    static union M4aTestWave w;
    static int32_t decoded[1 + DPCM_SAMPLES];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    MakeCompressedWave(&w, decoded + 1, 41);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_CMP | TYPE_REV, STEP_ONE, 0);
    ref.samples = decoded + 1;
    ref.pos = DPCM_SAMPLES - 1;
    ref.count = (int64_t)DPCM_SAMPLES;
    ref.step = STEP_ONE;
    ref.reverse = 1;
    FillPcm(si, 0);
    RefMixFrame(&ref);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].count, DPCM_SAMPLES - SPV);
    /* current sample 111, kept as 112 */
    PT_ASSERT_EQ_U32(PointerField(&si->chans[0]), DPCM_SAMPLES - SPV);
    /* the last block decoded held sample 110: block 1 */
    PT_ASSERT_EQ_U32(CacheKey(&si->chans[0]), 1);
}

PTEST(Mixer_compressed_reverse_to_sample_0_reads_nothing_before_the_data)
{
    /* At sample 0 the assembly reads "sample -1" from block 0x3FFFFFF, an
     * address 2 GiB past the wave on the GBA. The C engine treats samples
     * outside the wave's blocks as 0 (documented in README.md). */
    static union M4aTestWave w;
    static int32_t decoded[1 + DPCM_SAMPLES];
    struct SoundInfo *si = M4aTestSetUp();
    struct RefPlayer ref = {0};

    PT_ASSERT(si != NULL);
    MakeCompressedWave(&w, decoded + 1, 43);
    decoded[0] = 0;
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_CMP | TYPE_REV, STEP_HALF, DPCM_SAMPLES - 4);
    ref.samples = decoded + 1;
    ref.pos = 3;
    ref.count = 4;
    ref.step = STEP_HALF;
    ref.reverse = 1;
    FillPcm(si, 0);
    RefMixFrame(&ref);

    SoundMain();

    PT_ASSERT(ref.stopped);
    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
    PT_ASSERT_EQ_U32(si->chans[0].count, 0);
    /* stopped with the next index at -1 */
    PT_ASSERT_EQ_U32(PointerField(&si->chans[0]), 0xFFFFFFFFu);
}

PTEST(Mixer_compressed_type_on_an_uncompressed_forward_wave_is_silent)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, 0, 0, 0, 20);
    for (uint32_t i = 0; i < 21; i++)
        M4aTestSetWaveByte(&w, i, 77);
    si->maxChans = 1;
    M4aTestStartChannel(si, 0, &w.wave, TYPE_CMP, STEP_ONE, 0);
    FillPcm(si, 0);

    SoundMain();
    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SF_SPECIAL | SOUND_CHANNEL_SF_ENV_SUSTAIN);
    PT_ASSERT_EQ_U32(si->chans[0].count, 20);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data);
}

PTEST_MAIN()
