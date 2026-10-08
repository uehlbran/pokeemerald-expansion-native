/*
 * m4a engine: the mixer. C translation of src/m4a_1.s SoundMain, of
 * SoundMainRAM up to each channel's samples (frame selection, reverb or
 * clearing, the channel loop and the envelope) and of m4aSoundVSync. The
 * samples themselves are rendered by m4a_render.c.
 *
 * On the GBA, SoundMain runs once per frame (m4aSoundMain, from VBlank),
 * calls the sequencer and the PSG driver, then renders every active Direct
 * Sound channel into SoundInfo.pcmBuffer: a ring of pcmDmaPeriod frames of
 * pcmSamplesPerVBlank signed 8-bit samples, the right channel in the first
 * PCM_DMA_BUF_SIZE bytes and the left channel in the second. DMA1 and DMA2
 * in sound FIFO mode copy the two halves to FIFO A and FIFO B, which Timer
 * 0 drains at pcmFreq (GBATEK "GBA Sound Controller", "DMA Sound";
 * src/m4a.c SoundInit and SampleFreqSet set this up). m4aSoundVSync counts
 * the frames and restarts both DMAs at the start of the ring every
 * pcmDmaPeriod frames. The native platform reads the same ring instead.
 *
 * Followed: the assembly, branch for branch. README.md lists the few
 * places where the C departs from it, all on state the game never makes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "m4a_render.h"

/* GBATEK "LCD I/O Interrupts and Status": VCOUNT reads 160..227 during VBlank. */
#define VCOUNT_VBLANK   160u
/* GBATEK "LCD Dimensions and Timings": 228 lines per frame. */
#define TOTAL_SCANLINES 228u

/* The mixer stores four samples per 32-bit word. */
#define SAMPLES_PER_WORD 4u
/* SoundMainRAM_NoReverb: a word per half if bit 2 of the sample count is
 * set, two if bit 3 is, then a loop of four words per half that runs at
 * least once. */
#define CLEAR_ONE_WORD_BIT  4u
#define CLEAR_TWO_WORDS_BIT 8u
#define CLEAR_PASS_BYTES    16u
#define CLEAR_PASS_SHIFT    4u

/* Reverb: four samples summed, times SoundInfo.reverb, shifted right by 9. */
#define REVERB_SHIFT      9u
/* Bit 7 of the reverb result; when set (a negative result) one is added. */
#define REVERB_ROUND_BIT  0x80u

/* Envelope volumes run 0..255; decay and release multiply by value / 256. */
#define ENVELOPE_MAX         0xFFu
#define ENVELOPE_SCALE_SHIFT 8u
/* The envelope is scaled by (masterVolume + 1) / 16 ... */
#define MASTER_VOLUME_SHIFT  4u
/* ... and by rightVolume / 256 and leftVolume / 256. */
#define CHANNEL_VOLUME_SHIFT 8u

/* WaveData.status: its high byte is o_WaveData_flags. */
#define WAVE_FLAGS_SHIFT 8u

/* DMAxCNT: DMA_REPEAT of the control half, seen in the 32-bit register. */
#define DMA_CNT_REPEAT ((u32)DMA_REPEAT << 16)
/* A 4-word immediate transfer that stops a FIFO DMA (GBATEK "GBA DMA Transfers"). */
#define DMA_CNT_FIFO_FLUSH ((((u32)DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_FIXED) << 16) | 4u)
/* Sound FIFO mode: started by the FIFO's request, 32-bit, repeating. */
#define DMA_CNT_H_FIFO (DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT)

/* Where this frame is mixed: offsets into each half of the ring. */
struct MixFrame
{
    u32 offset;
    u32 nextOffset; // the frame reverb reads ahead
    u32 samples;
};

/* SoundMain and SoundMainRAM: VCOUNT, made monotonic across VBlank. */
static u32 CurrentLine(void)
{
    u32 line = *(const volatile u8 *)(uintptr_t)REG_ADDR_VCOUNT;

    if (line < VCOUNT_VBLANK)
        line += TOTAL_SCANLINES;
    return line;
}

static u8 *PcmRight(struct SoundInfo *soundInfo, u32 offset)
{
    return (u8 *)soundInfo->pcmBuffer + offset;
}

static u8 *PcmLeft(struct SoundInfo *soundInfo, u32 offset)
{
    return (u8 *)soundInfo->pcmBuffer + PCM_DMA_BUF_SIZE + offset;
}

static u32 ClearLength(u32 samples)
{
    u32 passes = samples >> CLEAR_PASS_SHIFT;

    if (passes == 0)
        passes = 1;
    return (samples & CLEAR_ONE_WORD_BIT) + (samples & CLEAR_TWO_WORDS_BIT) + passes * CLEAR_PASS_BYTES;
}

/*
 * SoundMain_5: the frame after the one DMA is playing, frame
 * pcmDmaPeriod - counter + 1, or frame 0 while the counter is 0 or 1.
 * Reverb reads the following frame, wrapping to frame 0 from the last.
 * Rejects a configuration the assembly would mix outside the ring, or a
 * frame its 4-sample words cannot mix exactly; every configuration
 * SampleFreqSet makes passes.
 */
static bool SelectFrame(const struct SoundInfo *soundInfo, struct MixFrame *frame)
{
    u32 samples = (u32)soundInfo->pcmSamplesPerVBlank;
    u32 counter = soundInfo->pcmDmaCounter;
    u32 period = soundInfo->pcmDmaPeriod;
    u64 index = 0;
    u64 offset;
    u64 nextOffset;
    u64 extent = samples;

    if (samples == 0 || samples % SAMPLES_PER_WORD != 0 || samples > PCM_DMA_BUF_SIZE)
        return false;
    if (counter > 1)
    {
        if (counter - 1 > period)
            return false;
        index = period - (counter - 1);
    }
    offset = index * samples;
    nextOffset = counter == 2 ? 0 : offset + samples;
    if (soundInfo->reverb == 0)
    {
        if (ClearLength(samples) > extent)
            extent = ClearLength(samples);
    }
    else if (nextOffset + samples > PCM_DMA_BUF_SIZE)
    {
        return false;
    }
    if (offset + extent > PCM_DMA_BUF_SIZE)
        return false;

    frame->offset = (u32)offset;
    frame->nextOffset = (u32)nextOffset;
    frame->samples = samples;
    return true;
}

/* SoundMainRAM_NoReverb */
static void ClearFrame(struct SoundInfo *soundInfo, const struct MixFrame *frame)
{
    u32 length = ClearLength(frame->samples);

    memset(PcmRight(soundInfo, frame->offset), 0, length);
    memset(PcmLeft(soundInfo, frame->offset), 0, length);
}

/* SoundMainRAM_Reverb */
static void ApplyReverb(struct SoundInfo *soundInfo, const struct MixFrame *frame)
{
    u8 *right = PcmRight(soundInfo, frame->offset);
    u8 *left = PcmLeft(soundInfo, frame->offset);
    const u8 *nextRight = PcmRight(soundInfo, frame->nextOffset);
    const u8 *nextLeft = PcmLeft(soundInfo, frame->nextOffset);
    u32 reverb = soundInfo->reverb;

    for (u32 i = 0; i < frame->samples; i++)
    {
        u32 sum = SignExtend8(left[i]) + SignExtend8(right[i])
                + SignExtend8(nextLeft[i]) + SignExtend8(nextRight[i]);
        u32 value = AsrU32(sum * reverb, REVERB_SHIFT);

        if (value & REVERB_ROUND_BIT)
            value++;
        left[i] = (u8)value;
        right[i] = (u8)value;
    }
}

/* Envelope (SoundMainRAM _081DCF60 to _081DD006) */

static void SetStatus(struct SoundChannel *chan, u8 *status, u8 value)
{
    *status = value;
    chan->statusFlags = value;
}

static bool TurnOff(struct SoundChannel *chan)
{
    chan->statusFlags = 0;
    return false;
}

/* Attack adds to the volume; at 255 it moves on to decay. */
static u32 Attack(struct SoundChannel *chan, u8 *status, u32 volume)
{
    volume += chan->attack;
    if (volume >= ENVELOPE_MAX)
    {
        volume = ENVELOPE_MAX;
        SetStatus(chan, status, (u8)(*status - 1));
    }
    return volume;
}

/* _081DCFC8: hold at the pseudo-echo volume, or end if it is 0. */
static bool EnterPseudoEcho(struct SoundChannel *chan, u8 *status, u32 *envelope)
{
    u32 volume = chan->pseudoEchoVolume;

    if (volume == 0)
        return TurnOff(chan);
    SetStatus(chan, status, (u8)(*status | SOUND_CHANNEL_SF_IEC));
    *envelope = volume;
    return true;
}

/* A started channel: plays from count (the start offset) with its envelope at 0. */
static u8 StartChannel(struct SoundChannel *chan)
{
    struct WaveData *wav = chan->wav;
    u32 offset = chan->count;
    u8 status = SOUND_CHANNEL_SF_ENV_ATTACK;

    chan->statusFlags = status;
    M4aRenderSetStart(chan, offset);
    chan->count = wav->size - offset;
    chan->envelopeVolume = 0;
    chan->fw = 0;
    if ((u8)(wav->status >> WAVE_FLAGS_SHIFT) & WAVE_DATA_FLAG_LOOP)
        SetStatus(chan, &status, (u8)(status | SOUND_CHANNEL_SF_LOOP));
    return status;
}

/* One frame of a running channel's envelope. Returns false if it ended. */
static bool StepEnvelope(struct SoundChannel *chan, u8 *status, u32 *envelope)
{
    u32 volume = chan->envelopeVolume;

    if (*status & SOUND_CHANNEL_SF_IEC)
    {
        u8 length = chan->pseudoEchoLength;

        chan->pseudoEchoLength = (u8)(length - 1);
        if (length < 2)
            return TurnOff(chan);
    }
    else if (*status & SOUND_CHANNEL_SF_STOP)
    {
        volume = volume * chan->release >> ENVELOPE_SCALE_SHIFT;
        if (volume <= chan->pseudoEchoVolume)
            return EnterPseudoEcho(chan, status, envelope);
    }
    else if ((*status & SOUND_CHANNEL_SF_ENV) == SOUND_CHANNEL_SF_ENV_DECAY)
    {
        volume = volume * chan->decay >> ENVELOPE_SCALE_SHIFT;
        if (volume <= chan->sustain)
        {
            volume = chan->sustain;
            if (volume == 0)
                return EnterPseudoEcho(chan, status, envelope);
            SetStatus(chan, status, (u8)(*status - 1));
        }
    }
    else if ((*status & SOUND_CHANNEL_SF_ENV) == SOUND_CHANNEL_SF_ENV_ATTACK)
    {
        volume = Attack(chan, status, volume);
    }
    *envelope = volume;
    return true;
}

/* _081DD006: the envelope scaled by the master volume and split left/right. */
static void SetEnvelopeVolumes(const struct SoundInfo *soundInfo, struct SoundChannel *chan, u32 envelope)
{
    u32 scaled = ((u32)soundInfo->masterVolume + 1) * envelope >> MASTER_VOLUME_SHIFT;

    chan->envelopeVolume = (u8)envelope;
    chan->envelopeVolumeRight = (u8)(chan->rightVolume * scaled >> CHANNEL_VOLUME_SHIFT);
    chan->envelopeVolumeLeft = (u8)(chan->leftVolume * scaled >> CHANNEL_VOLUME_SHIFT);
}

/* SoundMainRAM_ChanLoop: one channel's envelope and samples for this frame. */
static void MixChannel(struct SoundInfo *soundInfo, struct SoundChannel *chan, const struct MixFrame *frame)
{
    u8 status = chan->statusFlags;
    u32 envelope;
    struct MixOutput out;
    struct LoopRegion loop = {0, 0};

    if (!(status & SOUND_CHANNEL_SF_ON))
        return;
    if (status & SOUND_CHANNEL_SF_START)
    {
        if (status & SOUND_CHANNEL_SF_STOP)
        {
            chan->statusFlags = 0;
            return;
        }
        status = StartChannel(chan);
        envelope = Attack(chan, &status, 0);
    }
    else if (!StepEnvelope(chan, &status, &envelope))
    {
        return;
    }
    SetEnvelopeVolumes(soundInfo, chan, envelope);

    if (status & SOUND_CHANNEL_SF_LOOP)
    {
        loop.start = chan->wav->loopStart;
        loop.length = chan->wav->size - chan->wav->loopStart;
    }
    out.right = PcmRight(soundInfo, frame->offset);
    out.left = PcmLeft(soundInfo, frame->offset);
    out.samples = frame->samples;
    out.volumeRight = chan->envelopeVolumeRight;
    out.volumeLeft = chan->envelopeVolumeLeft;

    M4aRenderChannel(chan, &out, (u32)soundInfo->divFreq, &loop);
}

/*
 * SoundMainRAM: reverb or clear the frame, then mix the channels. With
 * maxLines set, mixing stops at the first channel reached after the
 * deadline line.
 */
static void SoundMainRAM_Mix(struct SoundInfo *soundInfo, const struct MixFrame *frame, u32 deadline)
{
    u32 channels = DirectChannelsToScan(soundInfo);

    if (soundInfo->reverb != 0)
        ApplyReverb(soundInfo, frame);
    else
        ClearFrame(soundInfo, frame);

    for (u32 i = 0; i < channels; i++)
    {
        if (deadline != 0 && CurrentLine() >= deadline)
            return;
        MixChannel(soundInfo, &soundInfo->chans[i], frame);
    }
}

/*
 * Until SoundInit sets SOUND_INFO_PTR it is 0, and the assembly reads its
 * ident from the BIOS at address 0, which never holds ID_NUMBER; natively
 * there is nothing at 0, so the two entry points the interrupt handlers
 * reach test for it.
 */
static bool SoundInfoReady(const struct SoundInfo *soundInfo)
{
    return soundInfo != NULL;
}

void SoundMain(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 deadline;
    struct MixFrame frame;

    if (!SoundInfoReady(soundInfo) || soundInfo->ident != ID_NUMBER)
        return;
    soundInfo->ident++;

    deadline = soundInfo->maxLines;
    if (deadline != 0)
        deadline += CurrentLine();

    if (soundInfo->MPlayMainHead != NULL)
        soundInfo->MPlayMainHead(soundInfo->musicPlayerHead);
    soundInfo->CgbSound();

    if (SelectFrame(soundInfo, &frame))
        SoundMainRAM_Mix(soundInfo, &frame, deadline);

    soundInfo->ident = ID_NUMBER;
}

/*
 * m4aSoundVSync: runs while the engine is idle or inside SoundMain (ident
 * ID_NUMBER or ID_NUMBER + 1). When the frame counter runs out it reloads
 * it and restarts DMA1 and DMA2 from the start of the ring: a DMA still in
 * repeat mode is stopped by a 4-word immediate transfer, both are disabled,
 * and both are re-enabled in sound FIFO mode (GBATEK "GBA DMA Transfers").
 */
void m4aSoundVSync(void)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    u32 counter;

    if (!SoundInfoReady(soundInfo) || soundInfo->ident - ID_NUMBER > 1)
        return;

    counter = (u32)soundInfo->pcmDmaCounter - 1;
    soundInfo->pcmDmaCounter = (u8)counter;
    if (AsS32(counter) > 0)
        return;
    soundInfo->pcmDmaCounter = soundInfo->pcmDmaPeriod;

    if (REG_DMA1CNT & DMA_CNT_REPEAT)
        REG_DMA1CNT = DMA_CNT_FIFO_FLUSH;
    if (REG_DMA2CNT & DMA_CNT_REPEAT)
        REG_DMA2CNT = DMA_CNT_FIFO_FLUSH;
    REG_DMA1CNT_H = DMA_32BIT;
    REG_DMA2CNT_H = DMA_32BIT;
    REG_DMA1CNT_H = DMA_CNT_H_FIFO;
    REG_DMA2CNT_H = DMA_CNT_H_FIFO;
}
