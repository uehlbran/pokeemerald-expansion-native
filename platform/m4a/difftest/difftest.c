/*
 * Differential test driver for the m4a engine: random mixer and sequencer
 * scenarios, a digest of the engine's state after every frame. Built once
 * with src/m4a_1.s for ARM (run under qemu-arm) and once with the C
 * translation; the two outputs must be identical (see Makefile, target
 * difftest). Pointers are printed as the object they point to, so the
 * digests do not depend on where each target placed its data.
 *
 * The scenarios stay inside what the engine is defined for on both sides:
 * no compressed sample is read past either end (the C engine reads those
 * as 0, the assembly reads whatever follows in memory), channel counts are
 * 0..12 and every voice names a PSG channel 1..4.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gba/m4a_internal.h"
#include "m4a.h"
#include "difftest_platform.h"

extern MPlayFunc gMPlayJumpTable[];

/* constants/m4a_constants.inc values the C header lacks */
#define TYPE_REV 0x10u
#define TYPE_CMP 0x20u
#define WAVE_LOOP 0x4000u /* WaveData.status with the loop flag */

/* Track command bytes (sound/MPlayDef.s). */
enum {
    CMD_W01 = 0x81,
    CMD_FINE = 0xB1,
    CMD_GOTO = 0xB2,
    CMD_PATT = 0xB3,
    CMD_PEND = 0xB4,
    CMD_REPT = 0xB5,
    CMD_PRIO = 0xBA,
    CMD_TEMPO = 0xBB,
    CMD_KEYSH = 0xBC,
    CMD_VOICE = 0xBD,
    CMD_VOL = 0xBE,
    CMD_PAN = 0xBF,
    CMD_BEND = 0xC0,
    CMD_BENDR = 0xC1,
    CMD_LFOS = 0xC2,
    CMD_LFODL = 0xC3,
    CMD_MOD = 0xC4,
    CMD_MODT = 0xC5,
    CMD_TUNE = 0xC8,
    CMD_PORT = 0xCC,
    CMD_EOT = 0xCE,
    CMD_TIE = 0xCF,
    CMD_N01 = 0xD0,
    CMD_ARG_LIMIT = 0x80, /* argument bytes are below this */
    CENTRE = 0x40,        /* C_V, the centre of PAN, BEND and TUNE */
};

#define NUM_WAVES 8
#define MAX_WAVE_DATA 9216u
#define WAVE_HEADER_SIZE 16u
/* Compressed waves: 250 blocks of 33 bytes (8250 bytes); 15999 % 64 != 0,
 * so the sample after the last one is still inside the last block. */
#define CMP_WAVE_SAMPLES 15999u

#define MIXER_SEEDS 300
#define MIXER_FRAMES 8
#define SEQUENCER_SEEDS 60
#define SEQUENCER_FRAMES 150

#define NUM_PLAYERS 2
#define TRACKS_PER_PLAYER 4
#define SONG_BYTES 1024u
#define SUBROUTINE_OFFSET 768u

union DiffWave {
    struct WaveData wave;
    uint32_t align;
    uint8_t bytes[WAVE_HEADER_SIZE + MAX_WAVE_DATA + 16];
};

static union DiffWave sWaves[NUM_WAVES];
static struct SoundInfo sSoundInfo __attribute__((aligned(4)));
static struct CgbChannel sCgbChans[4];
static struct MusicPlayerInfo sPlayers[NUM_PLAYERS];
static struct MusicPlayerTrack sTracks[NUM_PLAYERS][TRACKS_PER_PLAYER];
static uint8_t sSongs[NUM_PLAYERS][TRACKS_PER_PLAYER][SONG_BYTES];
static struct ToneData sVoices[12];
static struct ToneData sDrums[128];
static struct ToneData sSplitVoices[4];
static uint8_t sSplitTable[128];
static uint8_t sCgbWaveRam[16];

static uint32_t sRng;
static uint32_t sCallbackHash;

static uint32_t Rand(void)
{
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

static uint32_t RandBelow(uint32_t n)
{
    return n == 0 ? 0 : Rand() % n;
}

static uint32_t Fnv(uint32_t hash, const void *data, size_t size)
{
    const uint8_t *bytes = data;

    for (size_t i = 0; i < size; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

#define FNV_START 2166136261u

static void HashCallback(uint32_t tag, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t words[4] = {tag, a, b, c};

    sCallbackHash = Fnv(sCallbackHash, words, sizeof(words));
}

static void DummyCgbSound(void)
{
    HashCallback(1, 0, 0, 0);
}

static void DummyCgbOscOff(u8 chanNum)
{
    HashCallback(2, chanNum, 0, 0);
}

static u32 DummyMidiKeyToCgbFreq(u8 chanNum, u8 key, u8 fineAdjust)
{
    HashCallback(3, chanNum, key, fineAdjust);
    return 0x10000u * chanNum + 0x100u * key + fineAdjust;
}

/* Waves */

/* A wave of random sample bytes (or random DPCM blocks when type is set),
 * with random bytes around it for the reads one past either end. */
static struct WaveData *MakeWave(int id, uint16_t type, uint32_t size, int loop, uint32_t loopStart, uint32_t freq)
{
    union DiffWave *w = &sWaves[id];

    for (size_t i = 0; i < sizeof(w->bytes); i++)
        w->bytes[i] = (uint8_t)Rand();
    w->wave.type = type;
    w->wave.status = loop ? WAVE_LOOP : 0;
    w->wave.freq = freq;
    w->wave.loopStart = loopStart;
    w->wave.size = size;
    return &w->wave;
}

static int WaveId(const void *p)
{
    if (p == NULL)
        return -1;
    for (int i = 0; i < NUM_WAVES; i++) {
        if (p == &sWaves[i].wave)
            return i;
    }
    if (p == (const void *)sDrums)
        return 200;
    if (p == (const void *)sSplitVoices)
        return 201;
    if (p == (const void *)sCgbWaveRam)
        return 202;
    if ((uintptr_t)p < 0x10000u)
        return 1000 + (int)(uintptr_t)p;
    return -2;
}

static long ChanId(const void *p)
{
    if (p == NULL)
        return -1;
    for (int i = 0; i < MAX_DIRECTSOUND_CHANNELS; i++) {
        if (p == &sSoundInfo.chans[i])
            return i;
    }
    for (int i = 0; i < 4; i++) {
        if (p == &sCgbChans[i])
            return 100 + i;
    }
    return -2;
}

static long TrackId(const struct MusicPlayerTrack *t)
{
    if (t == NULL)
        return -1;
    for (int p = 0; p < NUM_PLAYERS; p++) {
        for (int i = 0; i < TRACKS_PER_PLAYER; i++) {
            if (t == &sTracks[p][i])
                return p * 16 + i;
        }
    }
    return -2;
}

static long SongOffset(const uint8_t *p)
{
    uintptr_t base = (uintptr_t)&sSongs[0][0][0];

    if (p == NULL)
        return -1;
    if ((uintptr_t)p < base || (uintptr_t)p >= base + sizeof(sSongs))
        return -2;
    return (long)((uintptr_t)p - base);
}

/* currentPointer holds a pointer into a sample, or for a compressed
 * reversed/compressed channel a sample index. A channel switched off before
 * it was ever mixed keeps whatever its previous voice left, so a pointer is
 * recognised by where it points (wave * 100000 + offset), not by the type. */
static long CursorOf(const struct SoundChannel *chan)
{
    uintptr_t raw;

    memcpy(&raw, &chan->currentPointer, sizeof(raw));
    if (raw == 0)
        return -88888;
    for (int i = 0; i < NUM_WAVES; i++) {
        uintptr_t start = (uintptr_t)&sWaves[i];

        if (raw >= start && raw < start + sizeof(sWaves[i]))
            return i * 100000L + (long)(raw - (uintptr_t)sWaves[i].wave.data);
    }
    return (long)(int32_t)(uint32_t)raw;
}

static void DigestChannels(void)
{
    for (int i = 0; i < MAX_DIRECTSOUND_CHANNELS; i++) {
        const struct SoundChannel *c = &sSoundInfo.chans[i];

        printf(" C%d %02x %02x %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %08x %08x %08x w%d p%ld t%ld <%ld "
               ">%ld %04x %04x\n",
               i, c->statusFlags, c->type, c->rightVolume, c->leftVolume, c->attack, c->decay, c->sustain, c->release,
               c->key, c->envelopeVolume, c->envelopeVolumeRight, c->envelopeVolumeLeft, c->pseudoEchoVolume,
               c->pseudoEchoLength, c->gateTime, c->midiKey, c->velocity, c->priority, (int)(int8_t)c->rhythmPan,
               (unsigned)c->count, (unsigned)c->fw, (unsigned)c->frequency, WaveId(c->wav), CursorOf(c),
               TrackId(c->track), ChanId(c->prevChannelPointer), ChanId(c->nextChannelPointer), c->xpi, c->xpc);
    }
}

static void DigestCgbChannels(void)
{
    for (int i = 0; i < 4; i++) {
        const struct CgbChannel *c = &sCgbChans[i];

        printf(" G%d %02x %02x %u %u %u %u %u %u %u %u %u %u %u %u %d %02x %u %u %08x w%d t%ld <%ld >%ld\n", i,
               c->statusFlags, c->type, c->rightVolume, c->leftVolume, c->attack, c->decay, c->sustain, c->release,
               c->key, c->pseudoEchoVolume, c->pseudoEchoLength, c->gateTime, c->midiKey, c->velocity,
               (int)(int8_t)c->rhythmPan, c->modify, c->length, c->sweep, (unsigned)c->frequency,
               WaveId(c->wavePointer), TrackId(c->track), ChanId(c->prevChannelPointer), ChanId(c->nextChannelPointer));
    }
}

static void DigestTracks(void)
{
    for (int p = 0; p < NUM_PLAYERS; p++) {
        const struct MusicPlayerInfo *m = &sPlayers[p];

        printf(" P%d %08x %u %u %u %u %u %u %08x\n", p, (unsigned)m->status, (unsigned)m->clock, m->tempoD, m->tempoU,
               m->tempoI, m->tempoC, m->cmd, (unsigned)m->ident);
        for (int i = 0; i < TRACKS_PER_PLAYER; i++) {
            const struct MusicPlayerTrack *t = &sTracks[p][i];
            unsigned adsr = (t->tone.type & TONEDATA_TYPE_SPL)
                                ? 0xFFFFFFFFu
                                : (unsigned)t->tone.attack | ((unsigned)t->tone.decay << 8)
                                      | ((unsigned)t->tone.sustain << 16) | ((unsigned)t->tone.release << 24);

            printf(" T%d.%d %02x %u %u %u %u %u %u %02x %u %u %d %d %d %u %d %u %u %u %u %u %d %d %d %u %u %u %u %u %u "
                   "%u %u %u"
                   " c%ld v%02x.%u.%u.%02x.w%d.%08x %u s%ld %ld %ld %ld\n",
                   p, i, t->flags, t->wait, t->patternLevel, t->repN, t->gateTime, t->key, t->velocity,
                   t->runningStatus, t->keyM, t->pitM, t->keyShift, t->keyShiftX, t->tune, t->pitX, t->bend,
                   t->bendRange, t->volMR, t->volML, t->vol, t->volX, t->pan, t->panX, t->modM, t->mod, t->modT,
                   t->lfoSpeed, t->lfoSpeedC, t->lfoDelay, t->lfoDelayC, t->priority, t->pseudoEchoVolume,
                   t->pseudoEchoLength, ChanId(t->chan), t->tone.type, t->tone.key, t->tone.length, t->tone.pan_sweep,
                   WaveId(t->tone.wav), adsr, (unsigned)t->unk_3C, SongOffset(t->cmdPtr),
                   SongOffset(t->patternStack[0]), SongOffset(t->patternStack[1]), SongOffset(t->patternStack[2]));
        }
    }
}

static void DigestFrame(int frame)
{
    const volatile uint8_t *io = (const volatile uint8_t *)(uintptr_t)REG_BASE;
    uint8_t ioCopy[0x100];

    for (size_t i = 0; i < sizeof(ioCopy); i++)
        ioCopy[i] = io[0x60 + i];
    printf("F%d pcm %08x io %08x cb %08x id %08x n %u\n", frame,
           (unsigned)Fnv(FNV_START, sSoundInfo.pcmBuffer, sizeof(sSoundInfo.pcmBuffer)),
           (unsigned)Fnv(FNV_START, ioCopy, sizeof(ioCopy)), (unsigned)sCallbackHash, (unsigned)sSoundInfo.ident,
           sSoundInfo.pcmDmaCounter);
}

/* SoundInfo as SoundInit, MPlayExtender and SampleFreqSet leave it. */
static void ResetSoundInfo(void)
{
    static const uint16_t kSamplesPerVBlank[] = {96, 132, 176, 224, 264, 304, 352, 448, 528, 608, 672, 704};
    uint32_t spv = kSamplesPerVBlank[RandBelow(12)];
    uint32_t pcmFreq = (597275u * spv + 5000u) / 10000u;

    memset(&sSoundInfo, 0, sizeof(sSoundInfo));
    memset(sCgbChans, 0, sizeof(sCgbChans));
    for (int i = 0; i < 4; i++)
        sCgbChans[i].type = (u8)(i + 1);
    memset((void *)(uintptr_t)REG_BASE, 0, 0x400);
    *(volatile uint8_t *)(uintptr_t)REG_ADDR_VCOUNT = 160;

    SOUND_INFO_PTR = &sSoundInfo;
    sSoundInfo.maxChans = (u8)RandBelow(13);
    sSoundInfo.masterVolume = (u8)RandBelow(16);
    sSoundInfo.reverb = RandBelow(2) ? (u8)(1 + RandBelow(127)) : 0;
    sSoundInfo.pcmSamplesPerVBlank = (s32)spv;
    sSoundInfo.pcmDmaPeriod = (u8)(PCM_DMA_BUF_SIZE / spv);
    sSoundInfo.pcmDmaCounter = (u8)RandBelow(sSoundInfo.pcmDmaPeriod + 1u);
    sSoundInfo.pcmFreq = (s32)pcmFreq;
    sSoundInfo.divFreq = (s32)((16777216u / pcmFreq + 1) >> 1);
    sSoundInfo.cgbChans = sCgbChans;
    sSoundInfo.CgbSound = DummyCgbSound;
    sSoundInfo.CgbOscOff = DummyCgbOscOff;
    sSoundInfo.MidiKeyToCgbFreq = DummyMidiKeyToCgbFreq;
    sSoundInfo.plynote = ply_note;
    MPlayJumpTableCopy(gMPlayJumpTable);
    sSoundInfo.MPlayJumpTable = gMPlayJumpTable;
    for (size_t i = 0; i < PCM_DMA_BUF_SIZE * 2; i++)
        sSoundInfo.pcmBuffer[i] = (s8)(uint8_t)Rand();
    sSoundInfo.ident = ID_NUMBER;
    sCallbackHash = FNV_START;
}

/* Mixer scenarios */

static void MakeMixerWaves(void)
{
    for (int id = 0; id < NUM_WAVES; id++) {
        int compressed = id >= 5;
        uint32_t size = compressed ? CMP_WAVE_SAMPLES : 1 + RandBelow(MAX_WAVE_DATA - 1);
        int loop = (int)RandBelow(2);
        uint32_t loopStart = RandBelow(size);

        if (id < 2)
            size = 1 + RandBelow(40); /* short samples end inside a frame */
        loopStart = RandBelow(size);
        MakeWave(id, compressed ? 1 : 0, size, loop, loopStart, Rand());
    }
}

static void StartMixerChannel(struct SoundChannel *c)
{
    static const uint8_t kTypes[] = {
        0,
        0,
        TONEDATA_TYPE_FIX,
        TYPE_REV,
        TYPE_REV | TONEDATA_TYPE_FIX,
        TYPE_CMP,
        TYPE_CMP | TONEDATA_TYPE_FIX,
        TYPE_CMP | TYPE_REV,
        TYPE_CMP | TYPE_REV | TONEDATA_TYPE_FIX,
    };
    uint8_t type = kTypes[RandBelow(sizeof(kTypes))];
    int compressedSample = (type & TYPE_CMP) && RandBelow(4) != 0;
    int id = compressedSample ? 5 + (int)RandBelow(3) : (int)RandBelow(5);
    struct WaveData *wav = &sWaves[id].wave;
    uint32_t divFreq = (uint32_t)sSoundInfo.divFreq;
    uint32_t maxStep = 0x1000000u; /* two samples per output */

    memset(c, 0, sizeof(*c));
    c->statusFlags = RandBelow(10) == 0 ? (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_STOP) : SOUND_CHANNEL_SF_START;
    c->type = type;
    c->wav = wav;
    c->rightVolume = (u8)Rand();
    c->leftVolume = (u8)Rand();
    c->attack = (u8)Rand();
    c->decay = (u8)Rand();
    c->sustain = (u8)Rand();
    c->release = (u8)Rand();
    c->pseudoEchoVolume = (u8)RandBelow(40);
    c->pseudoEchoLength = (u8)RandBelow(6);
    /* the uncompressed forward path also gets steps far above two samples,
     * and some of 2^30 to 2^32 on a looping sample, where the assembly's
     * fraction mask leaves bits 30 and 31 set from one output to the next */
    if (!(type & (TYPE_CMP | TYPE_REV | TONEDATA_TYPE_FIX)) && RandBelow(4) == 0)
        maxStep = 0x7FFFFFFFu;
    c->frequency = RandBelow(maxStep / divFreq + 1);
    if (maxStep == 0x7FFFFFFFu && RandBelow(2) == 0) {
        id = 2 + (int)RandBelow(3);
        wav = &sWaves[id].wave;
        wav->status = WAVE_LOOP;
        c->wav = wav;
        c->frequency = (0x40000000u + RandBelow(0xBFFFFFFFu)) / divFreq;
    }
    if (compressedSample && (type & TYPE_REV))
        c->count = RandBelow(1000); /* never reaches sample 0 within the frames */
    else
        c->count = RandBelow(wav->size);
}

static void RunMixerScenario(uint32_t seed)
{
    sRng = seed * 2654435761u + 1;
    ResetSoundInfo();
    MakeMixerWaves();
    for (int i = 0; i < MAX_DIRECTSOUND_CHANNELS; i++) {
        if (RandBelow(4) != 0)
            StartMixerChannel(&sSoundInfo.chans[i]);
    }
    printf("M%u maxChans %u mv %u reverb %u spv %d period %u\n", (unsigned)seed, sSoundInfo.maxChans,
           sSoundInfo.masterVolume, sSoundInfo.reverb, (int)sSoundInfo.pcmSamplesPerVBlank, sSoundInfo.pcmDmaPeriod);
    for (int frame = 0; frame < MIXER_FRAMES; frame++) {
        int i = (int)RandBelow(MAX_DIRECTSOUND_CHANNELS);

        /* release a playing channel (an ended one keeps stale state) */
        if (RandBelow(3) == 0 && sSoundInfo.chans[i].statusFlags != 0)
            sSoundInfo.chans[i].statusFlags |= SOUND_CHANNEL_SF_STOP;
        SoundMain();
        m4aSoundVSync();
        DigestFrame(frame);
        DigestChannels();
    }
}

/* Sequencer scenarios */

static void PutAddress(uint8_t *dst, const void *target)
{
    uintptr_t bits = (uintptr_t)target;

    for (int i = 0; i < 4; i++)
        dst[i] = (uint8_t)(bits >> (8 * i));
}

static void MakeVoices(void)
{
    struct WaveData *loopWave = MakeWave(0, 0, 500, 1, 100, 13379u << 10);
    struct WaveData *shortWave = MakeWave(1, 0, 300, 0, 0, 10512u << 10);
    struct WaveData *cmpWave = MakeWave(5, 1, CMP_WAVE_SAMPLES, 0, 0, 10512u << 10);
    struct WaveData *cmpLoopWave = MakeWave(6, 1, CMP_WAVE_SAMPLES, 1, 7000, 13379u << 10);

    memset(sVoices, 0, sizeof(sVoices));
    for (size_t i = 0; i < sizeof(sCgbWaveRam); i++)
        sCgbWaveRam[i] = (uint8_t)Rand();

    sVoices[0] = (struct ToneData){0, 60, 0, 0, loopWave, 255, 200, 128, 180};
    sVoices[1] = (struct ToneData){TONEDATA_TYPE_FIX, 60, 0, 0x80 | 20, shortWave, 200, 255, 255, 100};
    sVoices[2] = (struct ToneData){1, 60, 0, 0x25, (struct WaveData *)(uintptr_t)2, 0, 7, 10, 3};
    sVoices[3] = (struct ToneData){2, 60, 20, 0xC0 + 10, (struct WaveData *)(uintptr_t)1, 0, 2, 15, 0};
    sVoices[4] = (struct ToneData){3, 60, 0, 0, (struct WaveData *)(void *)sCgbWaveRam, 0, 0, 15, 0};
    sVoices[5] = (struct ToneData){4, 60, 0, 0, (struct WaveData *)(uintptr_t)0, 0, 1, 8, 2};
    sVoices[6] = (struct ToneData){TONEDATA_TYPE_RHY, 0, 0, 0, (struct WaveData *)(void *)sDrums, 0, 0, 0, 0};
    sVoices[7] = (struct ToneData){TONEDATA_TYPE_SPL, 0, 0, 0, (struct WaveData *)(void *)sSplitVoices, 0, 0, 0, 0};
    PutAddress(&sVoices[7].attack, sSplitTable); /* voice_keysplit: .4byte in attack..release */
    sVoices[8] = (struct ToneData){TYPE_REV, 60, 0, 0, shortWave, 255, 0, 255, 200};
    sVoices[9] = (struct ToneData){TYPE_CMP, 60, 0, 0, cmpWave, 255, 0, 255, 0};
    sVoices[10] = (struct ToneData){TYPE_CMP | TONEDATA_TYPE_FIX, 60, 0, 0, cmpLoopWave, 255, 250, 200, 150};
    sVoices[11] = (struct ToneData){0, 60, 0, 0, shortWave, 0, 0, 0, 0};

    for (int i = 0; i < 128; i++) {
        sDrums[i] = sVoices[(int)(i % 6 == 2 ? 2 : i % 6 == 4 ? 9 : i % 6 == 5 ? 8 : i % 2)];
        sDrums[i].key = (u8)(36 + i % 50);
        sDrums[i].pan_sweep = (u8)(i % 3 == 0 ? 0x80 + (i * 7) % 128 : i % 3 == 1 ? 0x20 : 0);
    }
    sDrums[127].type = TONEDATA_TYPE_SPL; /* a nested group plays nothing */
    for (int i = 0; i < 4; i++)
        sSplitVoices[i] = sVoices[i == 3 ? 3 : i];
    for (int i = 0; i < 128; i++)
        sSplitTable[i] = (uint8_t)(i < 40 ? 0 : i < 70 ? 1 : i < 100 ? 2 : 3);
}

/* Appends one random event; returns the new write position. */
static uint32_t PutEvent(uint8_t *song, uint32_t pos)
{
    switch (RandBelow(24)) {
    case 0:
        song[pos++] = CMD_VOICE;
        song[pos++] = (uint8_t)RandBelow(12);
        break;
    case 1:
        song[pos++] = CMD_VOL;
        song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        if (RandBelow(2))
            song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT); /* VOL again, by running status */
        break;
    case 2:
        song[pos++] = CMD_PAN;
        song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        break;
    case 3:
        song[pos++] = CMD_BEND;
        song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        break;
    case 4:
        song[pos++] = CMD_BENDR;
        song[pos++] = (uint8_t)RandBelow(13);
        break;
    case 5:
        song[pos++] = CMD_LFOS;
        song[pos++] = (uint8_t)RandBelow(80);
        break;
    case 6:
        song[pos++] = CMD_LFODL;
        song[pos++] = (uint8_t)RandBelow(8);
        break;
    case 7:
        song[pos++] = CMD_MOD;
        song[pos++] = (uint8_t)RandBelow(80);
        break;
    case 8:
        song[pos++] = CMD_MODT;
        song[pos++] = (uint8_t)RandBelow(3);
        break;
    case 9:
        song[pos++] = CMD_TUNE;
        song[pos++] = (uint8_t)(CENTRE + RandBelow(41) - 20);
        break;
    case 10:
        song[pos++] = CMD_KEYSH;
        song[pos++] = (uint8_t)(RandBelow(25) - 12);
        break;
    case 11:
        song[pos++] = CMD_PRIO;
        song[pos++] = (uint8_t)RandBelow(30);
        break;
    case 12:
        song[pos++] = CMD_TEMPO;
        song[pos++] = (uint8_t)(30 + RandBelow(170));
        break;
    case 13:
        song[pos++] = CMD_PORT; /* a sound register within 0x04000060..0x0400007F */
        song[pos++] = (uint8_t)RandBelow(0x20);
        song[pos++] = (uint8_t)Rand();
        break;
    case 14:
        song[pos++] = CMD_TIE;
        song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        song[pos++] = (uint8_t)(1 + RandBelow(CMD_ARG_LIMIT - 1));
        break;
    case 15:
        song[pos++] = CMD_EOT;
        if (RandBelow(2))
            song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        break;
    case 16:
    case 17:
    case 18:
    case 19:
        /* N1..N48: key, then optionally velocity, then optionally a gate extension */
        song[pos++] = (uint8_t)(CMD_N01 + RandBelow(48));
        song[pos++] = (uint8_t)RandBelow(CMD_ARG_LIMIT);
        if (RandBelow(4) != 0) {
            song[pos++] = (uint8_t)(1 + RandBelow(CMD_ARG_LIMIT - 1));
            if (RandBelow(2))
                song[pos++] = (uint8_t)RandBelow(4);
        }
        break;
    default:
        song[pos++] = (uint8_t)(CMD_W01 + RandBelow(24)); /* W01..W24 */
        break;
    }
    return pos;
}

/* A track: voice and volume, a body that starts with a wait (so no loop
 * spins without one), random events with the occasional PATT and REPT,
 * then GOTO the body or FINE. A subroutine for PATT sits at
 * SUBROUTINE_OFFSET. */
static void MakeSong(uint8_t *song)
{
    uint32_t pos = 0;
    uint32_t body;
    uint32_t sub = SUBROUTINE_OFFSET;
    uint32_t events = 10 + RandBelow(60);

    memset(song, CMD_FINE, SONG_BYTES);
    song[pos++] = CMD_VOICE;
    song[pos++] = (uint8_t)RandBelow(12);
    song[pos++] = CMD_VOL;
    song[pos++] = (uint8_t)(64 + RandBelow(64));
    body = pos;
    song[pos++] = CMD_W01 + 3;

    for (int i = 0; i < 6; i++)
        sub = PutEvent(song, sub);
    song[sub++] = CMD_W01 + 1;
    song[sub++] = CMD_PEND;

    for (uint32_t i = 0; i < events && pos < SUBROUTINE_OFFSET - 16; i++) {
        uint32_t extra = RandBelow(20);

        if (extra == 0) {
            song[pos++] = CMD_PATT;
            PutAddress(&song[pos], &song[SUBROUTINE_OFFSET]);
            pos += 4;
        } else if (extra == 1) {
            song[pos++] = CMD_REPT;
            song[pos++] = (uint8_t)(1 + RandBelow(3));
            PutAddress(&song[pos], &song[body]);
            pos += 4;
        } else {
            pos = PutEvent(song, pos);
        }
    }
    if (RandBelow(3) != 0) {
        song[pos++] = CMD_GOTO;
        PutAddress(&song[pos], &song[body]);
    } else {
        song[pos] = CMD_FINE;
    }
}

static void RunSequencerScenario(uint32_t seed)
{
    sRng = seed * 2246822519u + 7;
    ResetSoundInfo();
    if (sSoundInfo.maxChans == 0)
        sSoundInfo.maxChans = 4;
    MakeVoices();

    memset(sPlayers, 0, sizeof(sPlayers));
    memset(sTracks, 0, sizeof(sTracks));
    for (int p = 0; p < NUM_PLAYERS; p++) {
        struct MusicPlayerInfo *m = &sPlayers[p];
        uint8_t trackCount = (uint8_t)(1 + RandBelow(TRACKS_PER_PLAYER));

        m->tracks = sTracks[p];
        m->trackCount = trackCount;
        m->priority = (u8)RandBelow(20);
        m->tone = sVoices;
        m->tempoD = 150;
        m->tempoI = 150;
        m->tempoU = 0x100;
        m->ident = ID_NUMBER;
        for (int i = 0; i < trackCount; i++) {
            MakeSong(sSongs[p][i]);
            sTracks[p][i].flags = MPT_FLG_EXIST | MPT_FLG_START;
            sTracks[p][i].cmdPtr = sSongs[p][i];
        }
    }
    sPlayers[0].MPlayMainNext = MPlayMain;
    sPlayers[0].musicPlayerNext = &sPlayers[1];
    sSoundInfo.MPlayMainHead = MPlayMain;
    sSoundInfo.musicPlayerHead = &sPlayers[0];

    printf("S%u maxChans %u spv %d\n", (unsigned)seed, sSoundInfo.maxChans, (int)sSoundInfo.pcmSamplesPerVBlank);
    for (int frame = 0; frame < SEQUENCER_FRAMES; frame++) {
        SoundMain();
        m4aSoundVSync();
        DigestFrame(frame);
        DigestChannels();
        DigestCgbChannels();
        DigestTracks();
    }
}

int main(void)
{
    if (DifftestMapGbaPages() != 0)
        return 1;
    for (uint32_t seed = 0; seed < MIXER_SEEDS; seed++)
        RunMixerScenario(seed);
    for (uint32_t seed = 0; seed < SEQUENCER_SEEDS; seed++)
        RunSequencerScenario(seed);
    printf("end\n");
    DifftestUnmapGbaPages();
    return 0;
}
