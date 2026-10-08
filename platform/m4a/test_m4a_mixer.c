/*
 * Unit tests for the mixer half of the m4a engine (src/m4a_1.s SoundMain,
 * SoundMainRAM, SoundMainRAM_Unk1, SoundMainRAM_Unk2), translated to C in
 * m4a_mixer.c. Expected values are derived by hand from the assembly's
 * arithmetic (worked numbers in the comments) or by the small reference
 * players below, which model the documented behaviour with plain integer
 * arithmetic rather than the engine's register tricks.
 */
#include <string.h>

#if __has_include("../test/ptest.h")
#include "../test/ptest.h"
#else
#include "ptest_stub.h"
#endif

#include "test_m4a_support.h"

/* Samples per frame the fixture configures (pcmSamplesPerVBlank). */
#define SPV 16u
/* The fixture sets divFreq to 1, so a channel's frequency is its step:
 * a 9.23 fixed-point number of source samples per output sample. */
#define STEP_ONE        0x800000u
#define STEP_HALF       0x400000u
#define STEP_3_QUARTERS 0x600000u
/* Envelope 255, masterVolume 15: (255 * ((255 * 16) >> 4)) >> 8 = 254 for
 * rightVolume 255, and (128 * 255) >> 8 = 127 for leftVolume 128. */
#define ENV_RIGHT 254u
#define ENV_LEFT  127u
/* WaveData.status with the loop flag (o_WaveData_flags 0x40 in its high byte). */
#define WAVE_STATUS_LOOP 0x4000u
/* constants/m4a_constants.inc values the C header does not define. */
#define TYPE_REV     0x10u
#define TYPE_CMP     0x20u
#define SF_SPECIAL   0x20u
/* Status after the first frame of a channel with attack 255: decay phase. */
#define SF_DECAY_AFTER_START 0x02u

static uint8_t sExpected[PCM_DMA_BUF_SIZE * 2];

static const uint8_t *PcmBytes(const struct SoundInfo *si)
{
    return (const uint8_t *)si->pcmBuffer;
}

static void FillPcm(struct SoundInfo *si, uint8_t value)
{
    memset(si->pcmBuffer, value, sizeof(si->pcmBuffer));
    memset(sExpected, value, sizeof(sExpected));
}

/* A Direct Sound channel at full envelope from its first frame on. */
static struct SoundChannel *StartChannel(struct SoundInfo *si, size_t index, struct WaveData *wav,
                                         uint8_t type, uint32_t step, uint32_t startOffset)
{
    struct SoundChannel *chan = &si->chans[index];

    memset(chan, 0, sizeof(*chan));
    chan->statusFlags = SOUND_CHANNEL_SF_START;
    chan->type = type;
    chan->wav = wav;
    chan->frequency = step;
    chan->count = startOffset;
    chan->rightVolume = 255;
    chan->leftVolume = 128;
    chan->attack = 255;
    chan->decay = 255;
    chan->sustain = 255;
    return chan;
}

static uint32_t PointerField(const struct SoundChannel *chan)
{
    uintptr_t bits;

    memcpy(&bits, &chan->currentPointer, sizeof(bits));
    return (uint32_t)bits;
}

static uint32_t LcgNext(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 24;
}

/* Reference interpolating player. */

struct RefPlayer
{
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
    if (p->count > 0)
    {
        p->pos += p->reverse ? -(int64_t)whole : (int64_t)whole;
        return;
    }
    if (p->loopLen == 0)
    {
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
    for (uint32_t i = 0; i < SPV && !p->stopped; i++)
    {
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
    for (size_t b = 0; b < numBlocks; b++)
    {
        const uint8_t *block = data + b * 33;
        int64_t level = (int8_t)block[0];

        out[b * 64] = M4aTestWrapS8(level);
        level += kDpcmDeltas[block[1] & 0xF];
        out[b * 64 + 1] = M4aTestWrapS8(level);
        for (size_t j = 2; j < 64; j++)
        {
            uint8_t byte = block[j / 2 + 1];

            level += kDpcmDeltas[(j % 2 == 0) ? (byte >> 4) : (byte & 0xF)];
            out[b * 64 + j] = M4aTestWrapS8(level);
        }
    }
}

/* SoundMain: the ident gate and the callbacks */

PTEST(SoundMain_does_nothing_when_ident_is_not_ID_NUMBER)
{
    static struct MusicPlayerInfo player;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->MPlayMainHead = TestMPlayMainHead;
    si->musicPlayerHead = &player;
    StartChannel(si, 0, NULL, 0, STEP_ONE, 0);
    FillPcm(si, 0x5A);
    si->ident = ID_NUMBER + 1;

    SoundMain();

    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER + 1);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));

    /* control: unlocked, the same state runs */
    si->ident = ID_NUMBER;
    si->chans[0].statusFlags = SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_STOP;
    SoundMain();
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 2);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
}

PTEST(SoundMain_before_SoundInit_does_nothing)
{
    /* SOUND_INFO_PTR is 0 until SoundInit; the GBA then reads the BIOS's
     * first word as ident, which is never ID_NUMBER */
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    SOUND_INFO_PTR = NULL;
    SoundMain();
    SOUND_INFO_PTR = si;
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
}

PTEST(SoundMain_calls_MPlayMainHead_then_CgbSound_while_locked)
{
    static struct MusicPlayerInfo player;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->MPlayMainHead = TestMPlayMainHead;
    si->musicPlayerHead = &player;

    SoundMain();

    PT_ASSERT_EQ_U32(M4aTestCallCount(), 2);
    PT_ASSERT_EQ_INT(M4aTestCall(0)->kind, CALL_MPLAY_MAIN_HEAD);
    PT_ASSERT_EQ_PTR(M4aTestCall(0)->ptr0, &player);
    PT_ASSERT_EQ_U32(M4aTestCall(0)->soundIdentAtCall, ID_NUMBER + 1);
    PT_ASSERT_EQ_INT(M4aTestCall(1)->kind, CALL_CGB_SOUND);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->soundIdentAtCall, ID_NUMBER + 1);
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
}

PTEST(SoundMain_skips_a_null_MPlayMainHead_but_still_calls_CgbSound)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    SoundMain();

    PT_ASSERT_EQ_U32(M4aTestCallCount(), 1);
    PT_ASSERT_EQ_INT(M4aTestCall(0)->kind, CALL_CGB_SOUND);
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
}

/* maxLines: the deadline is VCOUNT at entry (plus 228 below line 160) plus
 * maxLines, taken before the callbacks; each channel checks VCOUNT again. */

static uint8_t sVcountAfterCallbacks;

static void SetVcountHook(void)
{
    M4aTestWriteIo8(REG_OFFSET_VCOUNT, sVcountAfterCallbacks);
}

static struct SoundInfo *SetUpDeadline(uint8_t maxLines, uint8_t vcountAtEntry, uint8_t vcountLater,
                                       union M4aTestWave *w)
{
    struct SoundInfo *si = M4aTestSetUp();

    if (si == NULL)
        return NULL;
    M4aTestMakeWave(w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    si->maxLines = maxLines;
    si->maxChans = 1;
    StartChannel(si, 0, &w->wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    M4aTestWriteIo8(REG_OFFSET_VCOUNT, vcountAtEntry);
    sVcountAfterCallbacks = vcountLater;
    M4aTestSetCgbSoundHook(SetVcountHook);
    return si;
}

PTEST(SoundMain_stops_mixing_when_the_maxLines_deadline_has_passed)
{
    static union M4aTestWave w;
    /* entry line 10 -> 238, deadline 243; line 20 -> 248 >= 243 */
    struct SoundInfo *si = SetUpDeadline(5, 10, 20, &w);

    PT_ASSERT(si != NULL);
    FillPcm(si, 0x5A);
    SoundMain();

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
    /* the callbacks ran and the frame was cleared before the deadline check */
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_CGB_SOUND), 1);
    PT_ASSERT_EQ_U32(PcmBytes(si)[0], 0);
}

PTEST(SoundMain_mixes_while_before_the_maxLines_deadline)
{
    static union M4aTestWave w;
    /* line 14 -> 242 < 243 */
    struct SoundInfo *si = SetUpDeadline(5, 10, 14, &w);

    PT_ASSERT(si != NULL);
    SoundMain();

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SF_DECAY_AFTER_START);
}

PTEST(SoundMain_deadline_reached_exactly_stops_mixing_in_vblank)
{
    static union M4aTestWave w;
    /* entry line 200 is not adjusted: deadline 205; line 205 is not before it */
    struct SoundInfo *si = SetUpDeadline(5, 200, 205, &w);

    PT_ASSERT(si != NULL);
    FillPcm(si, 0x5A);
    SoundMain();

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(PcmBytes(si)[PCM_DMA_BUF_SIZE], 0);
}

PTEST(SoundMain_with_maxLines_zero_ignores_VCOUNT)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpDeadline(0, 10, 227, &w);

    PT_ASSERT(si != NULL);
    SoundMain();

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SF_DECAY_AFTER_START);
}

/* Frame selection: counter > 1 mixes frame (pcmDmaPeriod - counter + 1),
 * otherwise frame 0. With no reverb the frame is cleared in both halves. */

static void ExpectFrameCleared(uint32_t frame, uint32_t bytes)
{
    memset(sExpected + frame * SPV, 0, bytes);
    memset(sExpected + PCM_DMA_BUF_SIZE + frame * SPV, 0, bytes);
}

static int RunEmptyFrame(uint8_t counter, uint32_t expectedFrame)
{
    struct SoundInfo *si = M4aTestSetUp();

    if (si == NULL)
        return 0;
    FillPcm(si, 0x5A);
    si->pcmDmaCounter = counter;
    SoundMain();
    ExpectFrameCleared(expectedFrame, SPV);
    return memcmp(PcmBytes(si), sExpected, sizeof(sExpected)) == 0;
}

PTEST(SoundMain_mixes_frame_0_when_the_dma_counter_is_0_or_1)
{
    PT_ASSERT(RunEmptyFrame(0, 0));
    PT_ASSERT(RunEmptyFrame(1, 0));
}

PTEST(SoundMain_mixes_frame_1_when_the_dma_counter_equals_the_period)
{
    /* period 99: 99 - 99 + 1 = 1 */
    PT_ASSERT(RunEmptyFrame(99, 1));
}

PTEST(SoundMain_mixes_the_last_frame_when_the_dma_counter_is_2)
{
    /* 99 - 2 + 1 = 98, bytes 1568..1583, the end of each half */
    PT_ASSERT(RunEmptyFrame(2, 98));
}

PTEST(SoundMain_mixes_a_middle_frame)
{
    PT_ASSERT(RunEmptyFrame(50, 50));
}

PTEST(SoundMain_clears_the_frame_as_the_assembly_does_for_short_frames)
{
    /* The clear loop stores 4 bytes if bit 2 of the sample count is set, 8 if
     * bit 3 is, then 16 per iteration of a loop that runs at least once:
     * 8 samples clear 8 + 16 = 24 bytes. */
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmSamplesPerVBlank = 8;
    si->pcmDmaPeriod = PCM_DMA_BUF_SIZE / 8;
    FillPcm(si, 0x5A);

    SoundMain();

    memset(sExpected, 0, 24);
    memset(sExpected + PCM_DMA_BUF_SIZE, 0, 24);
    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST(SoundMain_clears_exactly_132_bytes_for_132_samples)
{
    /* 132 = 0x84: 4 (bit 2) + 0 (bit 3) + 8 iterations of 16 */
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmSamplesPerVBlank = 132;
    si->pcmDmaPeriod = PCM_DMA_BUF_SIZE / 132;
    FillPcm(si, 0x5A);
    si->pcmDmaCounter = 2; /* frame 12 - 2 + 1 = 11, the last: 1452..1583 */

    SoundMain();

    memset(sExpected + 1452, 0, 132);
    memset(sExpected + PCM_DMA_BUF_SIZE + 1452, 0, 132);
    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST(SoundMain_rejects_a_frame_size_that_is_not_whole_words)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmSamplesPerVBlank = 18;
    si->pcmDmaPeriod = PCM_DMA_BUF_SIZE / 18;
    FillPcm(si, 0x5A);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_CGB_SOUND), 1);
}

PTEST(SoundMain_rejects_a_frame_outside_the_buffer)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaPeriod = 200; /* counter 2 selects frame 199, offset 3184 */
    si->pcmDmaCounter = 2;
    FillPcm(si, 0x5A);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->ident, ID_NUMBER);
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_CGB_SOUND), 1);
}

/* Reverb: each byte of the frame becomes
 *   v = (frame_r + frame_l + next_r + next_l) * reverb >> 9 (arithmetic),
 *   plus 1 if bit 7 of v is set (v negative), stored to both halves;
 * "next" is the following frame, or frame 0 when the counter is 2. */

static void FillPattern(struct SoundInfo *si, uint32_t seed)
{
    uint32_t state = seed;

    for (size_t i = 0; i < sizeof(si->pcmBuffer); i++)
        sExpected[i] = (uint8_t)LcgNext(&state);
    memcpy(si->pcmBuffer, sExpected, sizeof(sExpected));
}

static void RefReverb(uint32_t frame, uint32_t nextFrame, uint32_t reverb)
{
    for (uint32_t i = 0; i < SPV; i++)
    {
        uint32_t cur = frame * SPV + i;
        uint32_t next = nextFrame * SPV + i;
        int64_t sum = (int8_t)sExpected[cur] + (int8_t)sExpected[PCM_DMA_BUF_SIZE + cur]
                    + (int8_t)sExpected[next] + (int8_t)sExpected[PCM_DMA_BUF_SIZE + next];
        int64_t v = M4aTestFloorDiv(sum * reverb, 512);

        if (v < 0)
            v += 1;
        sExpected[cur] = (uint8_t)M4aTestWrapS8(v);
        sExpected[PCM_DMA_BUF_SIZE + cur] = (uint8_t)M4aTestWrapS8(v);
    }
}

PTEST(SoundMain_reverb_mixes_the_frame_with_the_next_frame)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->reverb = 100;
    si->pcmDmaCounter = 3; /* frame 97, next 98 */
    FillPattern(si, 1);
    RefReverb(97, 98, 100);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST(SoundMain_reverb_wraps_to_the_start_of_the_ring_from_the_last_frame)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->reverb = 127;
    si->pcmDmaCounter = 2; /* frame 98, the last; next is frame 0 */
    FillPattern(si, 2);
    RefReverb(98, 0, 127);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST(SoundMain_reverb_from_frame_0_reads_frame_1)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->reverb = 64;
    si->pcmDmaCounter = 0;
    FillPattern(si, 3);
    RefReverb(0, 1, 64);

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST(SoundMain_reverb_rounds_negative_results_toward_zero)
{
    /* 4 * 100 * 127 = 50800, >> 9 = 99. 4 * -100 * 127 = -50800, >> 9 = -100
     * (floor of -99.2), bit 7 set, + 1 = -99 = 0x9D. */
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->reverb = 127;
    memset(si->pcmBuffer, 100, sizeof(si->pcmBuffer));
    memset(si->pcmBuffer + 2, (uint8_t)-100, 1);
    memset(si->pcmBuffer + 16 + 2, (uint8_t)-100, 1);
    memset(si->pcmBuffer + PCM_DMA_BUF_SIZE + 2, (uint8_t)-100, 1);
    memset(si->pcmBuffer + PCM_DMA_BUF_SIZE + 16 + 2, (uint8_t)-100, 1);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], 99);
    PT_ASSERT_EQ_U32(PcmBytes(si)[PCM_DMA_BUF_SIZE], 99);
    PT_ASSERT_EQ_U32(PcmBytes(si)[2], 0x9D);
    PT_ASSERT_EQ_U32(PcmBytes(si)[PCM_DMA_BUF_SIZE + 2], 0x9D);
}

/* Envelope: attack adds, decay and release multiply by value/256, sustain
 * holds, the pseudo echo holds for its length; the result is scaled by
 * (masterVolume + 1) / 16 and by the channel's left and right volume. */

static struct SoundInfo *SetUpEnvelope(union M4aTestWave *w, uint8_t attack, uint8_t decay,
                                       uint8_t sustain, uint8_t release)
{
    struct SoundInfo *si = M4aTestSetUp();
    struct SoundChannel *chan;

    if (si == NULL)
        return NULL;
    M4aTestMakeWave(w, 0, WAVE_STATUS_LOOP, 0, 0, 4); /* four silent samples, looping */
    si->maxChans = 1;
    chan = StartChannel(si, 0, &w->wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    chan->attack = attack;
    chan->decay = decay;
    chan->sustain = sustain;
    chan->release = release;
    return si;
}

PTEST(Envelope_attack_decay_sustain_release_steps)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 100, 128, 64, 200);
    struct SoundChannel *chan;
    uint32_t env;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];

    SoundMain(); /* attack: 0 + 100 */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 100);
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_ATTACK);
    SoundMain(); /* 200 */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 200);
    SoundMain(); /* 300 >= 255: clamp, decay */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 255);
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY);
    SoundMain(); /* 255 * 128 >> 8 = 127 > 64 */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 127);
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY);
    SoundMain(); /* 127 * 128 >> 8 = 63 <= 64: sustain at 64 */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 64);
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_SUSTAIN);
    SoundMain();
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 64);
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_SUSTAIN);

    chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
    env = 64;
    while (env != 0)
    {
        env = env * 200 >> 8;
        SoundMain();
        if (env == 0)
            break;
        PT_ASSERT_EQ_U32(chan->envelopeVolume, env);
        PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_ENV_SUSTAIN);
    }
    /* released to 0 with no pseudo echo: the channel is off */
    PT_ASSERT_EQ_U32(chan->statusFlags, 0);
}

PTEST(Envelope_volume_is_scaled_by_master_and_channel_volume)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 200, 255, 255, 0);

    PT_ASSERT(si != NULL);
    si->masterVolume = 7;
    SoundMain();
    /* (200 * 8) >> 4 = 100; right (255 * 100) >> 8 = 99; left (128 * 100) >> 8 = 50 */
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 200);
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolumeRight, 99);
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolumeLeft, 50);
}

PTEST(Envelope_attack_255_reaches_decay_in_the_first_frame)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 255, 255, 0);

    PT_ASSERT(si != NULL);
    SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 255);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY);
}

PTEST(Envelope_attack_254_stays_in_attack_until_the_next_frame)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 254, 255, 255, 0);

    PT_ASSERT(si != NULL);
    SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 254);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_ATTACK);
    SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 255);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_DECAY);
}

PTEST(Envelope_attack_0_never_leaves_attack)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 0, 255, 255, 0);

    PT_ASSERT(si != NULL);
    for (int i = 0; i < 4; i++)
        SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 0);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_ENV_ATTACK);
}

PTEST(Envelope_decay_to_sustain_0_without_echo_turns_the_channel_off)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 0, 0, 0);

    PT_ASSERT(si != NULL);
    SoundMain(); /* 255, decay */
    SoundMain(); /* 255 * 0 >> 8 = 0 <= 0: sustain 0, no echo: off */
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
}

PTEST(Envelope_decay_to_sustain_0_with_echo_enters_the_pseudo_echo)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 0, 0, 0);

    PT_ASSERT(si != NULL);
    si->chans[0].pseudoEchoVolume = 20;
    si->chans[0].pseudoEchoLength = 5;
    SoundMain();
    SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].envelopeVolume, 20);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags,
                     SOUND_CHANNEL_SF_LOOP | SOUND_CHANNEL_SF_IEC | SOUND_CHANNEL_SF_ENV_DECAY);
}

PTEST(Envelope_release_into_the_pseudo_echo_holds_for_its_length)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 255, 255, 0);
    struct SoundChannel *chan;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];
    chan->pseudoEchoVolume = 10;
    chan->pseudoEchoLength = 3;
    SoundMain(); /* 255, decay */
    chan->statusFlags |= SOUND_CHANNEL_SF_STOP;
    SoundMain(); /* release 0: 0 <= 10, echo volume 10, IEC */
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 10);
    PT_ASSERT(chan->statusFlags & SOUND_CHANNEL_SF_IEC);
    SoundMain(); /* length 3 -> 2, held */
    PT_ASSERT_EQ_U32(chan->pseudoEchoLength, 2);
    PT_ASSERT_EQ_U32(chan->envelopeVolume, 10);
    PT_ASSERT(chan->statusFlags & SOUND_CHANNEL_SF_IEC);
    SoundMain(); /* 2 -> 1, held */
    PT_ASSERT_EQ_U32(chan->pseudoEchoLength, 1);
    PT_ASSERT(chan->statusFlags != 0);
    SoundMain(); /* 1 -> 0: off */
    PT_ASSERT_EQ_U32(chan->pseudoEchoLength, 0);
    PT_ASSERT_EQ_U32(chan->statusFlags, 0);
}

PTEST(Envelope_pseudo_echo_of_length_0_wraps_and_stops_at_once)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 255, 255, 0);
    struct SoundChannel *chan;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];
    SoundMain();
    chan->statusFlags |= SOUND_CHANNEL_SF_IEC;
    chan->pseudoEchoLength = 0;
    SoundMain();
    PT_ASSERT_EQ_U32(chan->pseudoEchoLength, 255);
    PT_ASSERT_EQ_U32(chan->statusFlags, 0);
}

PTEST(Envelope_start_with_stop_set_turns_the_channel_off_unmixed)
{
    static union M4aTestWave w;
    struct SoundInfo *si = SetUpEnvelope(&w, 255, 255, 255, 0);

    PT_ASSERT(si != NULL);
    si->chans[0].statusFlags = SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_STOP;
    M4aTestSetWaveByte(&w, 0, 100);
    SoundMain();
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
    PT_ASSERT_EQ_U32(PcmBytes(si)[0], 0);
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
    StartChannel(si, 0, &loopWave.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    StartChannel(si, 1, &shortWave.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
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
    StartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < SPV; i++)
    {
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
    StartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    StartChannel(si, 1, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

    SoundMain();

    for (uint32_t i = 0; i < SPV; i++)
    {
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
    StartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 10);

    SoundMain();

    for (uint32_t i = 0; i < SPV; i++)
        PT_ASSERT_EQ_U32(PcmBytes(si)[i], M4aTestMixDelta(ENV_RIGHT, (int32_t)((10 + i) * 3)));
    PT_ASSERT_EQ_U32(si->chans[0].count, 40 - 10 - SPV);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data + 10 + SPV);
}

/* Mixer: resampling with linear interpolation */

static void FillRandomWave(union M4aTestWave *w, uint32_t count, uint32_t seed, int32_t *samples)
{
    uint32_t state = seed;

    for (uint32_t i = 0; i < count; i++)
    {
        uint8_t byte = (uint8_t)LcgNext(&state);

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
    StartChannel(si, 0, &w.wave, 0, STEP_3_QUARTERS, 0);
    ref.samples = samples;
    ref.count = 40;
    ref.step = STEP_3_QUARTERS;

    for (int frame = 0; frame < 3; frame++)
    {
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
    StartChannel(si, 0, &w.wave, 0, STEP_HALF, 0);

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
    StartChannel(si, 0, &w.wave, 0, 13379, 0);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], M4aTestMixDelta(ENV_RIGHT, 100));
    for (uint32_t i = 1; i < SPV; i++)
    {
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

    for (size_t s = 0; s < sizeof(kSteps) / sizeof(kSteps[0]); s++)
    {
        static union M4aTestWave w;
        static int32_t samples[6];
        struct SoundInfo *si = M4aTestSetUp();
        struct RefPlayer ref = {0};

        PT_ASSERT(si != NULL);
        M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 2, 5);
        FillRandomWave(&w, 6, (uint32_t)(11 + s), samples);
        si->maxChans = 1;
        StartChannel(si, 0, &w.wave, 0, kSteps[s], 0);
        ref.samples = samples;
        ref.count = 5;
        ref.step = kSteps[s];
        ref.loopStart = 2;
        ref.loopLen = 3;

        for (int frame = 0; frame < 2; frame++)
        {
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
    StartChannel(si, 0, &w.wave, 0, STEP_ONE, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < 3; i++)
    {
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
    StartChannel(si, 0, &w.wave, TYPE_REV, STEP_HALF, 0);
    FillPcm(si, 0);
    for (uint32_t i = 0; i < SPV; i++)
    {
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
    StartChannel(si, 0, &w.wave, TYPE_REV, STEP_3_QUARTERS, 4);
    /* reversed from offset 4: the first sample is index 40 - 4 - 1 */
    ref.samples = samples + 1;
    ref.pos = 35;
    ref.count = 36;
    ref.step = STEP_3_QUARTERS;
    ref.reverse = 1;

    for (int frame = 0; frame < 2; frame++)
    {
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
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)LcgNext(&state);
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
    StartChannel(si, 0, &w.wave, TYPE_CMP, STEP_ONE, 56);
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
    StartChannel(si, 0, &w.wave, TYPE_CMP, STEP_3_QUARTERS, 0);
    ref.samples = decoded;
    ref.count = DPCM_SAMPLES;
    ref.step = STEP_3_QUARTERS;

    for (int frame = 0; frame < 3; frame++)
    {
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
    StartChannel(si, 0, &w.wave, TYPE_CMP | TYPE_REV, STEP_ONE, 0);
    ref.samples = decoded + 1;
    ref.pos = DPCM_SAMPLES - 1;
    ref.count = DPCM_SAMPLES;
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
    StartChannel(si, 0, &w.wave, TYPE_CMP | TYPE_REV, STEP_HALF, DPCM_SAMPLES - 4);
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
    StartChannel(si, 0, &w.wave, TYPE_CMP, STEP_ONE, 0);
    FillPcm(si, 0);

    SoundMain();
    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SF_SPECIAL | SOUND_CHANNEL_SF_ENV_SUSTAIN);
    PT_ASSERT_EQ_U32(si->chans[0].count, 20);
    PT_ASSERT_EQ_PTR(si->chans[0].currentPointer, w.wave.data);
}

PTEST(Mixer_channel_off_is_skipped_and_later_channels_still_mix)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&w, i, 64);
    si->maxChans = 3;
    StartChannel(si, 2, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], 63);
    PT_ASSERT_EQ_U32(PcmBytes(si)[PCM_DMA_BUF_SIZE + 15], 31);
}

PTEST(Mixer_only_the_first_maxChans_channels_are_mixed)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&w, i, 64);
    si->maxChans = 2;
    StartChannel(si, 1, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    StartChannel(si, 2, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

    SoundMain();

    PT_ASSERT_EQ_U32(PcmBytes(si)[0], 63); /* channel 1 only */
    PT_ASSERT_EQ_U32(si->chans[2].statusFlags, SOUND_CHANNEL_SF_START);
}

PTEST(Mixer_maxChans_above_12_mixes_the_12_channels_SoundInfo_holds)
{
    /* the assembly would mix "channels" 12..14 out of pcmBuffer, which
     * follows chans[11]; frame 1 leaves those bytes uncleared */
    static union M4aTestWave w;
    static uint8_t before[3 * sizeof(struct SoundChannel)];
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    si->maxChans = 15;
    si->pcmDmaCounter = si->pcmDmaPeriod; /* frame 1 */
    StartChannel(si, 11, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    memset(si->pcmBuffer, 0, sizeof(before));
    si->pcmBuffer[0] = (s8)SOUND_CHANNEL_SF_STOP; /* "channel 12": releasing, envelope 0 */
    memcpy(before, si->pcmBuffer, sizeof(before));

    SoundMain();

    PT_ASSERT_EQ_U32(si->chans[11].statusFlags, SOUND_CHANNEL_SF_LOOP | SF_DECAY_AFTER_START);
    PT_ASSERT_MEM_EQ(si->pcmBuffer, before, sizeof(before));
}

PTEST(Mixer_reverb_and_a_channel_combine)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&w, i, 64);
    si->maxChans = 1;
    si->reverb = 90;
    StartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    FillPattern(si, 5);
    RefReverb(0, 1, 90);
    for (uint32_t i = 0; i < SPV; i++)
    {
        sExpected[i] = (uint8_t)(sExpected[i] + 63);
        sExpected[PCM_DMA_BUF_SIZE + i] = (uint8_t)(sExpected[PCM_DMA_BUF_SIZE + i] + 31);
    }

    SoundMain();

    PT_ASSERT_MEM_EQ(PcmBytes(si), sExpected, sizeof(sExpected));
}

PTEST_MAIN()
