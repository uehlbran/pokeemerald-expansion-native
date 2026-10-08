/*
 * Unit tests for the m4a engine's mixer (src/m4a_1.s SoundMain, the frame,
 * reverb, channel-loop and envelope parts of SoundMainRAM, and
 * m4aSoundVSync), translated to C in m4a_mixer.c. Expected values are
 * derived by hand from the assembly's arithmetic (worked numbers in the
 * comments) or by small reference computations in the test.
 */
#include <string.h>

#if __has_include("../test/ptest.h")
#include "../test/ptest.h"
#else
#include "ptest_stub.h"
#endif

#include "test_m4a_support.h"

/* DMA control the VSync leaves on DMA1/DMA2: enable, FIFO timing, 32-bit,
 * repeat (0x8000 | 0x3000 | 0x0400 | 0x0200), GBATEK "GBA DMA Transfers". */
#define DMA_FIFO_CONTROL 0xB600u

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

/* SoundMain: the ident gate and the callbacks */

PTEST(SoundMain_does_nothing_when_ident_is_not_ID_NUMBER)
{
    static struct MusicPlayerInfo player;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->MPlayMainHead = TestMPlayMainHead;
    si->musicPlayerHead = &player;
    M4aTestStartChannel(si, 0, NULL, 0, STEP_ONE, 0);
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
    M4aTestStartChannel(si, 0, &w->wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
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
        sExpected[i] = (uint8_t)M4aTestLcgNext(&state);
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
    chan = M4aTestStartChannel(si, 0, &w->wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
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

/* The channel loop with several channels */

PTEST(Mixer_channel_off_is_skipped_and_later_channels_still_mix)
{
    static union M4aTestWave w;
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    M4aTestMakeWave(&w, 0, WAVE_STATUS_LOOP, 0, 0, 4);
    for (uint32_t i = 0; i < 5; i++)
        M4aTestSetWaveByte(&w, i, 64);
    si->maxChans = 3;
    M4aTestStartChannel(si, 2, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

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
    M4aTestStartChannel(si, 1, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
    M4aTestStartChannel(si, 2, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);

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
    M4aTestStartChannel(si, 11, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
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
    M4aTestStartChannel(si, 0, &w.wave, TONEDATA_TYPE_FIX, STEP_ONE, 0);
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

/* m4aSoundVSync */

PTEST(m4aSoundVSync_ignores_a_sound_info_that_is_not_running)
{
    static const uint32_t kIdents[] = {ID_NUMBER - 1, ID_NUMBER + 2, ID_NUMBER + 10, 0};

    for (size_t i = 0; i < sizeof(kIdents) / sizeof(kIdents[0]); i++)
    {
        struct SoundInfo *si = M4aTestSetUp();

        PT_ASSERT(si != NULL);
        si->ident = kIdents[i];
        si->pcmDmaCounter = 1;
        m4aSoundVSync();
        PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
        PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), 0);

        /* control: running, the same state counts down */
        si->ident = ID_NUMBER;
        si->pcmDmaCounter = 2;
        m4aSoundVSync();
        PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
    }
}

PTEST(m4aSoundVSync_before_SoundInit_does_nothing)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaCounter = 1;
    M4aTestWriteIo32(REG_OFFSET_DMA1CNT, (uint32_t)DMA_REPEAT << 16);
    SOUND_INFO_PTR = NULL;
    m4aSoundVSync();
    SOUND_INFO_PTR = si;
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), DMA_REPEAT);
}

PTEST(m4aSoundVSync_counts_down_without_touching_the_dma)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaCounter = 3;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 2);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), 0);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), 0);
}

PTEST(m4aSoundVSync_runs_while_SoundMain_holds_the_lock)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->ident = ID_NUMBER + 1;
    si->pcmDmaCounter = 3;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 2);
}

PTEST(m4aSoundVSync_restarts_the_fifo_dma_when_the_counter_expires)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaPeriod = 7;
    si->pcmDmaCounter = 1;
    /* DMA1 in repeat mode is first flushed with a 4-word transfer; DMA2 is not */
    M4aTestWriteIo32(REG_OFFSET_DMA1CNT, ((uint32_t)DMA_REPEAT << 16) | 0x1234u);
    M4aTestWriteIo32(REG_OFFSET_DMA2CNT, 0x5678u);

    m4aSoundVSync();

    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 7);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_L), 4);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), DMA_FIFO_CONTROL);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_L), 0x5678u);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), DMA_FIFO_CONTROL);
}

PTEST(m4aSoundVSync_counter_0_also_reloads)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaPeriod = 7;
    si->pcmDmaCounter = 0;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 7);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), DMA_FIFO_CONTROL);
}

PTEST_MAIN()
