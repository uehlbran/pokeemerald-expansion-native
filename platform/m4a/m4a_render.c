/*
 * m4a engine: rendering one Direct Sound channel. C translation of the
 * sample loops of src/m4a_1.s SoundMainRAM (from _081DD03A), of
 * SoundMainRAM_Unk1 (reversed and DPCM-compressed samples) and of
 * SoundMainRAM_Unk2 (the DPCM block decoder).
 *
 * Each output sample adds bits 8..15 of (envelope volume * sample) to its
 * byte in the right and the left half of the ring, modulo 256: what the
 * assembly's rotate-and-add of four bytes in one register does. A fixed
 * rate channel (TONEDATA_TYPE_FIX) takes one stored sample per output;
 * the others step a 9.23 fixed-point position by divFreq * frequency and
 * interpolate linearly toward the next sample. The channel's position
 * (SoundChannel.currentPointer, count, fw) carries over between frames;
 * for a compressed sample currentPointer holds a sample index, as in the
 * assembly.
 *
 * Followed: the assembly; the arithmetic is instruction for instruction.
 * The one departure (README.md): a compressed sample's blocks are not read
 * past either end, where the assembly reads whatever follows in ROM.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "m4a_render.h"

/* An output byte gains bits 8..15 of envelope volume * sample. */
#define MIX_SHIFT 8u

/* SoundChannel.fw: a 9.23 fixed-point position between two samples. */
#define FW_FRACTION_BITS 23u
/* bic r9, r9, #0x3F800000: the whole-sample bits cleared after a step. */
#define FW_WHOLE_MASK 0x3F800000u
/* SoundMainRAM_Unk1's step for TONEDATA_TYPE_FIX: one sample per output. */
#define FW_ONE_SAMPLE 0x800000u

/* DPCM blocks: 64 samples in 33 bytes, an initial sample and 32 bytes of
 * 4-bit indices into gDeltaEncodingTable (the second byte's high nibble is
 * unused). */
#define DPCM_BLOCK_SHIFT 6u
#define DPCM_BLOCK_SAMPLES (1u << DPCM_BLOCK_SHIFT)
#define DPCM_BLOCK_BYTES 33u
#define DPCM_NIBBLE_BITS 4u
#define DPCM_NIBBLE_MASK 0xFu
/* The cache key SoundMainRAM_Unk1 stores each frame: no block index is this large. */
#define DPCM_CACHE_INVALID 0xFF000000u

/* SoundMainRAM_Unk2's decoding buffer: one DPCM block, shared by all channels
 * (each channel invalidates its cache key every frame). */
static u8 sDecodingBuffer[DPCM_BLOCK_SAMPLES];

/* Where a channel stands in its sample: the assembly's r3, r2 and r9. */
struct MixCursor {
    u32 index; // the current sample, counted from WaveData.data
    u32 count; // samples left, the current one included
    u32 fw;    // the fraction toward the next sample
};

/* How a channel's samples are read: as stored, or DPCM-decoded. */
struct SampleSource {
    struct SoundChannel *chan;
    bool decodesDpcm;
};

enum MixResult {
    MIX_CONTINUES,
    MIX_ENDED,
};

/* Samples */

/* A stored sample. The index wraps like the assembly's address arithmetic:
 * index -1 is the last header byte, which reversed playback reads. */
static u32 WaveSample(const struct WaveData *wav, u32 index)
{
    const u8 *bytes = (const u8 *)wav;

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
    const u8 *src = (const u8 *)wav + offsetof(struct WaveData, data) + (size_t)block * DPCM_BLOCK_BYTES;
    u8 level = src[0];

    sDecodingBuffer[0] = level;
    level = (u8)(level + (u8)gDeltaEncodingTable[src[1] & DPCM_NIBBLE_MASK]);
    sDecodingBuffer[1] = level;
    for (u32 i = 2; i < DPCM_BLOCK_SAMPLES; i += 2) {
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

    if (block != DpcmCacheKey(chan)) {
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
 * sample the index itself (stored as a uintptr_t at either width). */
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

    for (;;) {
        bool positive = SumIsPositive(*count, loopLength);

        *count += loopLength;
        if (positive)
            return overshoot;
        overshoot -= loopLength;
    }
}

/* _081DD07C: TONEDATA_TYPE_FIX, one stored sample per output sample. */
static enum MixResult MixFixedRate(const struct WaveData *wav, const struct MixOutput *out, struct MixCursor *cursor,
                                   const struct LoopRegion *loop)
{
    for (u32 i = 0; i < out->samples; i++) {
        MixSample(out, i, WaveSample(wav, cursor->index));
        cursor->index++;
        cursor->count--;
        if (cursor->count == 0) {
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

    for (u32 i = 0; i < out->samples; i++) {
        u32 whole;

        MixSample(out, i, InterpolatedSample(current, delta, cursor->fw));
        whole = StepPosition(&cursor->fw, step);
        if (whole == 0)
            continue;
        if (CountRunsOut(cursor->count, whole)) {
            cursor->count -= whole;
            if (loop->length == 0)
                return MIX_ENDED;
            cursor->index = loop->start + WrapIntoLoop(&cursor->count, loop->length);
            current = FetchSample(source, cursor->index);
        } else {
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

    for (u32 i = 0; i < out->samples; i++) {
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

    if (MixFixedRate(chan->wav, out, &cursor, loop) == MIX_ENDED) {
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

    if (MixForward(&source, out, &cursor, step, loop) == MIX_ENDED) {
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

    if (!(chan->statusFlags & SOUND_CHANNEL_SF_SPECIAL)) {
        chan->statusFlags |= SOUND_CHANNEL_SF_SPECIAL;
        if (chan->type & TONEDATA_TYPE_REV) {
            cursor.index = wav->size - cursor.index;
            StoreIndex(&source, cursor.index);
        }
    }

    if (source.decodesDpcm)
        SetDpcmCacheKey(chan, DPCM_CACHE_INVALID);

    if (chan->type & TONEDATA_TYPE_REV) {
        cursor.index--;
        result = MixBackward(&source, out, &cursor, step);
        /* ended: the assembly's r3 is the next sample when decoding, else the current */
        if (result == MIX_ENDED)
            kept = source.decodesDpcm ? cursor.index - 1 : cursor.index;
        else
            kept = cursor.index + 1;
    } else if (source.decodesDpcm) {
        result = MixForward(&source, out, &cursor, step, loop);
        kept = result == MIX_ENDED ? cursor.index + 1 : cursor.index;
    } else {
        /* TONEDATA_TYPE_CMP on an uncompressed sample plays nothing */
        return;
    }

    if (result == MIX_ENDED) {
        chan->statusFlags = 0;
        cursor.count = 0;
    }
    chan->fw = cursor.fw;
    chan->count = cursor.count;
    StoreIndex(&source, kept);
}

void M4aRenderSetStart(struct SoundChannel *chan, u32 offset)
{
    // cppcheck-suppress constVariablePointer ; wav->data is stored in the non-const currentPointer
    struct WaveData *wav = chan->wav;

    /* The assembly stores wav->data + offset here, and SoundMainRAM_Unk1
     * turns it into a sample index for a compressed sample before any
     * other use. That pointer would lie past the compressed data, so a
     * compressed sample gets the index directly. */
    if ((chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV)) && wav->type != 0) {
        struct SampleSource source = {chan, true};

        StoreIndex(&source, offset);
    } else {
        chan->currentPointer = wav->data + offset;
    }
}

void M4aRenderChannel(struct SoundChannel *chan, const struct MixOutput *out, u32 divFreq,
                      const struct LoopRegion *loop)
{
    if (chan->type & (TONEDATA_TYPE_CMP | TONEDATA_TYPE_REV))
        PlaySpecial(chan, out, divFreq, loop);
    else if (chan->type & TONEDATA_TYPE_FIX)
        PlayFixedRate(chan, out, loop);
    else
        PlayResampled(chan, out, divFreq * chan->frequency, loop);
}
