/*
 * m4a engine: the mixer. C translation of src/m4a_1.s SoundMain,
 * SoundMainRAM, SoundMainRAM_Unk1 and SoundMainRAM_Unk2, and of
 * m4aSoundVSync.
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
 * The arithmetic follows the assembly instruction for instruction. Two ARM
 * devices are replaced by plain per-sample code with the same result: the
 * assembly mixes four samples at a time by rotating a 32-bit word of four
 * output bytes and adding each product into its top byte, and it counts
 * those four samples in the top two bits of the output pointer. Here each
 * product is added to its output byte directly, modulo 256, which is what
 * the rotate-and-add does. README.md lists the few places where the C
 * departs from the assembly on invalid data.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "m4a_engine.h"

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
/* An output byte gains bits 8..15 of envelope volume * sample. */
#define MIX_SHIFT            8u

/* SoundChannel.fw: a 9.23 fixed-point position between two samples. */
#define FW_FRACTION_BITS 23u
/* bic r9, r9, #0x3F800000: the whole-sample bits cleared after a step. */
#define FW_WHOLE_MASK    0x3F800000u
/* SoundMainRAM_Unk1's step for TONEDATA_TYPE_FIX: one sample per output. */
#define FW_ONE_SAMPLE    0x800000u

/* DPCM blocks: 64 samples in 33 bytes, an initial sample and 32 bytes of
 * 4-bit indices into gDeltaEncodingTable (the second byte's high nibble is
 * unused). */
#define DPCM_BLOCK_SHIFT   6u
#define DPCM_BLOCK_SAMPLES (1u << DPCM_BLOCK_SHIFT)
#define DPCM_BLOCK_BYTES   33u
#define DPCM_NIBBLE_BITS   4u
#define DPCM_NIBBLE_MASK   0xFu
/* The cache key SoundMainRAM_Unk1 stores each frame: no block index is this large. */
#define DPCM_CACHE_INVALID 0xFF000000u

/* WaveData.status: its high byte is o_WaveData_flags. */
#define WAVE_FLAGS_SHIFT 8u

/* DMAxCNT: DMA_REPEAT of the control half, seen in the 32-bit register. */
#define DMA_CNT_REPEAT ((u32)DMA_REPEAT << 16)
/* A 4-word immediate transfer that stops a FIFO DMA (GBATEK "GBA DMA Transfers"). */
#define DMA_CNT_FIFO_FLUSH ((((u32)DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_FIXED) << 16) | 4u)
/* Sound FIFO mode: started by the FIFO's request, 32-bit, repeating. */
#define DMA_CNT_H_FIFO (DMA_ENABLE | DMA_START_SPECIAL | DMA_32BIT | DMA_REPEAT)

/* SoundMainRAM_Unk2's decoding buffer: one DPCM block, shared by all channels
 * (each channel invalidates its cache key every frame). */
static u8 sDecodingBuffer[DPCM_BLOCK_SAMPLES];

/* Where this frame is mixed: offsets into each half of the ring. */
struct MixFrame
{
    u32 offset;
    u32 nextOffset; // the frame reverb reads ahead
    u32 samples;
};

/* One channel's output: its frame in each half and its envelope volumes. */
struct MixOutput
{
    u8 *right;
    u8 *left;
    u32 samples;
    u32 volumeRight;
    u32 volumeLeft;
};

/* The part of the sample the loop repeats; length 0 means no loop. */
struct LoopRegion
{
    u32 start;
    u32 length;
};

/* Where a channel stands in its sample: the assembly's r3, r2 and r9. */
struct MixCursor
{
    u32 index; // the current sample, counted from WaveData.data
    u32 count; // samples left, the current one included
    u32 fw;    // the fraction toward the next sample
};

/* How a channel's samples are read: as stored, or DPCM-decoded. */
struct SampleSource
{
    struct SoundChannel *chan;
    bool decodesDpcm;
};

enum MixResult
{
    MIX_CONTINUES,
    MIX_ENDED,
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
    /* The assembly stores wav->data + offset here, and SoundMainRAM_Unk1
     * turns it into a sample index for compressed samples before any
     * other use. That pointer would lie past the compressed data, so a
     * compressed sample gets the index directly. */
    if ((chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV)) && wav->type != 0)
    {
        uintptr_t index = offset;

        memcpy(&chan->currentPointer, &index, sizeof(index));
    }
    else
    {
        chan->currentPointer = wav->data + offset;
    }
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

/* Samples */

/* A stored sample. The index wraps like the assembly's address arithmetic:
 * index -1 is the last header byte, which reversed playback reads. */
static u32 WaveSample(const struct WaveData *wav, u32 index)
{
    const u8 *bytes = (const u8 *)(const void *)wav;

    return SignExtend8(bytes[(u32)(offsetof(struct WaveData, data) + index)]);
}

static u32 DpcmCacheKey(const struct SoundChannel *chan)
{
    return (u32)chan->xpi | ((u32)chan->xpc << 16);
}

static void SetDpcmCacheKey(struct SoundChannel *chan, u32 key)
{
    chan->xpi = (u16)key;
    chan->xpc = (u16)(key >> 16);
}

static bool DpcmBlockInWave(const struct WaveData *wav, u32 block)
{
    return wav->size != 0 && block <= (wav->size - 1) >> DPCM_BLOCK_SHIFT;
}

/* SoundMainRAM_Unk2's decoder: one 64-sample block into sDecodingBuffer. */
static void DecodeDpcmBlock(const struct WaveData *wav, u32 block)
{
    const u8 *src = (const u8 *)(const void *)wav + offsetof(struct WaveData, data) + block * DPCM_BLOCK_BYTES;
    u8 level = src[0];

    sDecodingBuffer[0] = level;
    level = (u8)(level + (u8)gDeltaEncodingTable[src[1] & DPCM_NIBBLE_MASK]);
    sDecodingBuffer[1] = level;
    for (u32 i = 2; i < DPCM_BLOCK_SAMPLES; i += 2)
    {
        u8 byte = src[i / 2 + 1];

        level = (u8)(level + (u8)gDeltaEncodingTable[byte >> DPCM_NIBBLE_BITS]);
        sDecodingBuffer[i] = level;
        level = (u8)(level + (u8)gDeltaEncodingTable[byte & DPCM_NIBBLE_MASK]);
        sDecodingBuffer[i + 1] = level;
    }
}

/*
 * SoundMainRAM_Unk2: a compressed sample, decoding its block unless the
 * channel's cache key says it is already in the buffer. A block outside the
 * sample (the index one past either end) is not read: the assembly reads
 * whatever lies there in ROM; the C engine returns 0 (README.md).
 */
static u32 DpcmSample(struct SoundChannel *chan, u32 index)
{
    u32 block = index >> DPCM_BLOCK_SHIFT;

    if (block != DpcmCacheKey(chan))
    {
        if (!DpcmBlockInWave(chan->wav, block))
            return 0;
        SetDpcmCacheKey(chan, block);
        DecodeDpcmBlock(chan->wav, block);
    }
    return SignExtend8(sDecodingBuffer[index & (DPCM_BLOCK_SAMPLES - 1)]);
}

static u32 FetchSample(const struct SampleSource *source, u32 index)
{
    if (source->decodesDpcm)
        return DpcmSample(source->chan, index);
    return WaveSample(source->chan->wav, index);
}

/* currentPointer holds a pointer into the sample, or for a compressed
 * sample the index itself (flagged 64-bit: stored as a uintptr_t). */
static u32 LoadIndex(const struct SampleSource *source)
{
    const struct SoundChannel *chan = source->chan;
    uintptr_t index;

    if (!source->decodesDpcm)
        return (u32)(chan->currentPointer - chan->wav->data);
    memcpy(&index, &chan->currentPointer, sizeof(index));
    return (u32)index;
}

static void StoreIndex(const struct SampleSource *source, u32 index)
{
    struct SoundChannel *chan = source->chan;
    uintptr_t bits = index;

    if (!source->decodesDpcm)
        chan->currentPointer = chan->wav->data + index;
    else
        memcpy(&chan->currentPointer, &bits, sizeof(bits));
}

/* Mixing loops */

/* What the assembly's rotate-and-add does to one output byte per side. */
static void MixSample(const struct MixOutput *out, u32 i, u32 sample)
{
    out->right[i] = (u8)(out->right[i] + (u8)(out->volumeRight * sample >> MIX_SHIFT));
    out->left[i] = (u8)(out->left[i] + (u8)(out->volumeLeft * sample >> MIX_SHIFT));
}

/* lr = r0 + asr(r9 * r1, 23): linear interpolation toward the next sample. */
static u32 InterpolatedSample(u32 current, u32 delta, u32 fw)
{
    return current + AsrU32(fw * delta, FW_FRACTION_BITS);
}

/* Moves the fraction on by one output; returns the whole samples crossed. */
static u32 StepPosition(u32 *fw, u32 step)
{
    u32 whole;

    *fw += step;
    whole = *fw >> FW_FRACTION_BITS;
    if (whole != 0)
        *fw &= ~FW_WHOLE_MASK;
    return whole;
}

/* subs r2, r2, lr; ble: the samples left run out (signed, as the flags see it). */
static bool CountRunsOut(u32 count, u32 whole)
{
    return (int64_t)AsS32(count) - (int64_t)whole <= 0;
}

/* adds r2, r0, r2; bgt */
static bool SumIsPositive(u32 a, u32 b)
{
    return (int64_t)AsS32(a) + (int64_t)AsS32(b) > 0;
}

/* _081DD134 and _081DD398: carries the overshoot past the end into the
 * loop. Returns the offset from the loop start. */
static u32 WrapIntoLoop(u32 *count, u32 loopLength)
{
    u32 overshoot = 0 - *count;

    for (;;)
    {
        bool positive = SumIsPositive(*count, loopLength);

        *count += loopLength;
        if (positive)
            return overshoot;
        overshoot -= loopLength;
    }
}

/* _081DD07C: TONEDATA_TYPE_FIX, one stored sample per output sample. */
static enum MixResult MixFixedRate(const struct WaveData *wav, const struct MixOutput *out,
                                   struct MixCursor *cursor, const struct LoopRegion *loop)
{
    for (u32 i = 0; i < out->samples; i++)
    {
        MixSample(out, i, WaveSample(wav, cursor->index));
        cursor->index++;
        cursor->count--;
        if (cursor->count == 0)
        {
            if (loop->length == 0)
                return MIX_ENDED;
            cursor->index = loop->start;
            cursor->count = loop->length;
        }
    }
    return MIX_CONTINUES;
}

/* _081DD19C and SoundMainRAM_Unk1 _081DD308: forward, interpolating. On
 * MIX_ENDED the cursor is left at the last sample played. */
static enum MixResult MixForward(const struct SampleSource *source, const struct MixOutput *out,
                                 struct MixCursor *cursor, u32 step, const struct LoopRegion *loop)
{
    u32 current = FetchSample(source, cursor->index);
    u32 delta = FetchSample(source, cursor->index + 1) - current;

    for (u32 i = 0; i < out->samples; i++)
    {
        u32 whole;

        MixSample(out, i, InterpolatedSample(current, delta, cursor->fw));
        whole = StepPosition(&cursor->fw, step);
        if (whole == 0)
            continue;
        if (CountRunsOut(cursor->count, whole))
        {
            cursor->count -= whole;
            if (loop->length == 0)
                return MIX_ENDED;
            cursor->index = loop->start + WrapIntoLoop(&cursor->count, loop->length);
            current = FetchSample(source, cursor->index);
        }
        else
        {
            cursor->count -= whole;
            cursor->index += whole;
            current = whole == 1 ? current + delta : FetchSample(source, cursor->index);
        }
        delta = FetchSample(source, cursor->index + 1) - current;
    }
    return MIX_CONTINUES;
}

/* SoundMainRAM_Unk1 _081DD3C0 and _081DD468: backward, interpolating
 * toward the previous sample, never looping. */
static enum MixResult MixBackward(const struct SampleSource *source, const struct MixOutput *out,
                                  struct MixCursor *cursor, u32 step)
{
    u32 current = FetchSample(source, cursor->index);
    u32 delta = FetchSample(source, cursor->index - 1) - current;

    for (u32 i = 0; i < out->samples; i++)
    {
        u32 whole;

        MixSample(out, i, InterpolatedSample(current, delta, cursor->fw));
        whole = StepPosition(&cursor->fw, step);
        if (whole == 0)
            continue;
        if (CountRunsOut(cursor->count, whole))
            return MIX_ENDED;
        cursor->count -= whole;
        cursor->index -= whole;
        current = whole == 1 ? current + delta : FetchSample(source, cursor->index);
        delta = FetchSample(source, cursor->index - 1) - current;
    }
    return MIX_CONTINUES;
}

/* Playing one channel */

/* Fixed rate and resampled channels: their position is kept only while they play. */
static void PlayFixedRate(struct SoundChannel *chan, const struct MixOutput *out, const struct LoopRegion *loop)
{
    struct SampleSource source = {chan, false};
    struct MixCursor cursor = {LoadIndex(&source), chan->count, 0};

    if (MixFixedRate(chan->wav, out, &cursor, loop) == MIX_ENDED)
    {
        chan->statusFlags = 0;
        return;
    }
    chan->count = cursor.count;
    StoreIndex(&source, cursor.index);
}

static void PlayResampled(struct SoundChannel *chan, const struct MixOutput *out, u32 step,
                          const struct LoopRegion *loop)
{
    struct SampleSource source = {chan, false};
    struct MixCursor cursor = {LoadIndex(&source), chan->count, chan->fw};

    if (MixForward(&source, out, &cursor, step, loop) == MIX_ENDED)
    {
        chan->statusFlags = 0;
        return;
    }
    chan->fw = cursor.fw;
    chan->count = cursor.count;
    StoreIndex(&source, cursor.index);
}

/*
 * SoundMainRAM_Unk1: reversed (TONEDATA_TYPE_REV) or compressed
 * (TONEDATA_TYPE_CMP with a compressed WaveData) samples. On its first
 * frame a reversed channel's position is mirrored to the end of the
 * sample. Between frames it keeps one past the current sample in playing
 * order, except a forward compressed channel, which keeps the current
 * one. Its position is kept even when it ends, with count 0.
 */
static void PlaySpecial(struct SoundChannel *chan, const struct MixOutput *out, u32 divFreq,
                        const struct LoopRegion *loop)
{
    const struct WaveData *wav = chan->wav;
    struct SampleSource source = {chan, wav->type != 0};
    struct MixCursor cursor = {LoadIndex(&source), chan->count, chan->fw};
    u32 step = (chan->type & TONEDATA_TYPE_FIX) ? FW_ONE_SAMPLE : divFreq * chan->frequency;
    u32 kept;
    enum MixResult result;

    if (!(chan->statusFlags & SOUND_CHANNEL_SF_SPECIAL))
    {
        chan->statusFlags |= SOUND_CHANNEL_SF_SPECIAL;
        if (chan->type & TONEDATA_TYPE_REV)
        {
            cursor.index = wav->size - cursor.index;
            StoreIndex(&source, cursor.index);
        }
    }

    if (source.decodesDpcm)
        SetDpcmCacheKey(chan, DPCM_CACHE_INVALID);

    if (chan->type & TONEDATA_TYPE_REV)
    {
        cursor.index--;
        result = MixBackward(&source, out, &cursor, step);
        /* ended: the assembly's r3 is the next sample when decoding, else the current */
        if (result == MIX_ENDED)
            kept = source.decodesDpcm ? cursor.index - 1 : cursor.index;
        else
            kept = cursor.index + 1;
    }
    else if (source.decodesDpcm)
    {
        result = MixForward(&source, out, &cursor, step, loop);
        kept = result == MIX_ENDED ? cursor.index + 1 : cursor.index;
    }
    else
    {
        /* TONEDATA_TYPE_CMP on an uncompressed sample plays nothing */
        return;
    }

    if (result == MIX_ENDED)
    {
        chan->statusFlags = 0;
        cursor.count = 0;
    }
    chan->fw = cursor.fw;
    chan->count = cursor.count;
    StoreIndex(&source, kept);
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

    if (chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV))
        PlaySpecial(chan, &out, (u32)soundInfo->divFreq, &loop);
    else if (chan->type & TONEDATA_TYPE_FIX)
        PlayFixedRate(chan, &out, &loop);
    else
        PlayResampled(chan, &out, (u32)soundInfo->divFreq * chan->frequency, &loop);
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
