/*
 * Fixtures and src/m4a.c doubles for the m4a engine unit tests; see
 * test_m4a_support.h.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "test_m4a_c_reference.h"
#include "test_m4a_support.h"

/* IWRAM page holding SOUND_INFO_PTR (0x03007FF0), GBATEK "GBA Memory Map". */
#define TEST_IWRAM_PAGE_ADDR 0x03007000u
/* I/O register page, REG_BASE (GBATEK "GBA I/O Map"). */
#define TEST_IO_PAGE_ADDR    0x04000000u
/* VCOUNT while the native game runs (docs/ARCHITECTURE.md, "The frame"). */
#define TEST_VCOUNT_IN_VBLANK 160u

_Static_assert(sizeof(MPlayFunc) == sizeof(void *), "jump table entries are copied as pointers");

/* m4a.c owns these in the game; the tests own them here. */
MPlayFunc gMPlayJumpTable[36];

static struct SoundInfo sSoundInfo ALIGNED(4);
static struct CgbChannel sCgbChans[TEST_NUM_CGB_CHANS];
static struct M4aCall sCalls[M4A_CALL_LOG_SIZE];
static size_t sCallCount;
static int sCallLogOverflow;
static int sPagesMapped;
static void (*sCgbSoundHook)(void);
static void (*sFadeOutBodyHook)(struct MusicPlayerInfo *);

static volatile uint8_t *IoByte(uint32_t offset)
{
    return (volatile uint8_t *)(uintptr_t)(TEST_IO_PAGE_ADDR + offset);
}

uint8_t M4aTestReadIo8(uint32_t offset)
{
    return *IoByte(offset);
}

void M4aTestWriteIo8(uint32_t offset, uint8_t value)
{
    *IoByte(offset) = value;
}

uint16_t M4aTestReadIo16(uint32_t offset)
{
    return (uint16_t)(M4aTestReadIo8(offset) | (M4aTestReadIo8(offset + 1) << 8));
}

void M4aTestWriteIo32(uint32_t offset, uint32_t value)
{
    for (uint32_t i = 0; i < 4; i++)
        M4aTestWriteIo8(offset + i, (uint8_t)(value >> (8 * i)));
}

uint8_t *M4aTestLowPage(void)
{
    return (uint8_t *)(uintptr_t)TEST_LOW_PAGE_ADDR;
}

static int MapPageAt(uint32_t address)
{
    void *wanted = (void *)(uintptr_t)address;
    void *got = mmap(wanted, TEST_PAGE_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (got == MAP_FAILED)
    {
        fprintf(stderr, "test_m4a_support: mmap at 0x%08x failed: %s\n",
                (unsigned)address, strerror(errno));
        return -1;
    }
    if (got != wanted)
    {
        fprintf(stderr, "test_m4a_support: mmap at 0x%08x returned %p\n",
                (unsigned)address, got);
        if (munmap(got, TEST_PAGE_SIZE) != 0)
            fprintf(stderr, "test_m4a_support: munmap failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static void UnmapPageAt(uint32_t address)
{
    if (munmap((void *)(uintptr_t)address, TEST_PAGE_SIZE) != 0)
        fprintf(stderr, "test_m4a_support: munmap at 0x%08x failed: %s\n",
                (unsigned)address, strerror(errno));
}

__attribute__((destructor)) static void UnmapPages(void)
{
    if (!sPagesMapped)
        return;
    UnmapPageAt(TEST_IWRAM_PAGE_ADDR);
    UnmapPageAt(TEST_IO_PAGE_ADDR);
    UnmapPageAt(TEST_LOW_PAGE_ADDR);
    sPagesMapped = 0;
}

static int MapPages(void)
{
    if (sPagesMapped)
        return 0;
    if (MapPageAt(TEST_IWRAM_PAGE_ADDR) != 0)
        return -1;
    if (MapPageAt(TEST_IO_PAGE_ADDR) != 0)
    {
        UnmapPageAt(TEST_IWRAM_PAGE_ADDR);
        return -1;
    }
    if (MapPageAt(TEST_LOW_PAGE_ADDR) != 0)
    {
        UnmapPageAt(TEST_IO_PAGE_ADDR);
        UnmapPageAt(TEST_IWRAM_PAGE_ADDR);
        return -1;
    }
    sPagesMapped = 1;
    return 0;
}

static void LogCall(enum M4aCallKind kind, const void *ptr0, const void *ptr1,
                    uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t playerIdent)
{
    struct M4aCall *call;

    if (sCallCount >= M4A_CALL_LOG_SIZE)
    {
        sCallLogOverflow = 1;
        return;
    }
    call = &sCalls[sCallCount++];
    call->kind = kind;
    call->ptr0 = ptr0;
    call->ptr1 = ptr1;
    call->arg0 = arg0;
    call->arg1 = arg1;
    call->arg2 = arg2;
    call->soundIdentAtCall = SOUND_INFO_PTR->ident;
    call->playerIdentAtCall = playerIdent;
}

size_t M4aTestCallCount(void)
{
    return sCallCount;
}

int M4aTestCallLogOverflowed(void)
{
    return sCallLogOverflow;
}

const struct M4aCall *M4aTestCall(size_t index)
{
    static const struct M4aCall sNoCall = {0};

    if (index >= sCallCount)
        return &sNoCall;
    return &sCalls[index];
}

size_t M4aTestCountCalls(enum M4aCallKind kind)
{
    size_t count = 0;

    for (size_t i = 0; i < sCallCount; i++)
    {
        if (sCalls[i].kind == kind)
            count++;
    }
    return count;
}

void M4aTestSetCgbSoundHook(void (*hook)(void))
{
    sCgbSoundHook = hook;
}

void M4aTestSetFadeOutBodyHook(void (*hook)(struct MusicPlayerInfo *))
{
    sFadeOutBodyHook = hook;
}

struct CgbChannel *M4aTestCgbChans(void)
{
    return sCgbChans;
}

uint32_t M4aTestCgbFreqFor(uint8_t chanNum, uint8_t key, uint8_t fineAdjust)
{
    return 0xC0000000u | ((uint32_t)chanNum << 16) | ((uint32_t)key << 8) | fineAdjust;
}

/* SoundInfo callbacks */

static void TestCgbSound(void)
{
    LogCall(CALL_CGB_SOUND, NULL, NULL, 0, 0, 0, 0);
    if (sCgbSoundHook != NULL)
        sCgbSoundHook();
}

static void TestCgbOscOff(u8 chanNum)
{
    LogCall(CALL_CGB_OSC_OFF, NULL, NULL, chanNum, 0, 0, 0);
}

static u32 TestMidiKeyToCgbFreq(u8 chanNum, u8 key, u8 fineAdjust)
{
    LogCall(CALL_MIDI_KEY_TO_CGB_FREQ, NULL, NULL, chanNum, key, fineAdjust, 0);
    return M4aTestCgbFreqFor(chanNum, key, fineAdjust);
}

void TestMPlayMainHead(struct MusicPlayerInfo *mplayInfo)
{
    LogCall(CALL_MPLAY_MAIN_HEAD, mplayInfo, NULL, 0, 0, 0,
            mplayInfo != NULL ? mplayInfo->ident : 0);
}

void TestMPlayNext(struct MusicPlayerInfo *mplayInfo)
{
    LogCall(CALL_MPLAY_NEXT, mplayInfo, NULL, 0, 0, 0,
            mplayInfo != NULL ? mplayInfo->ident : 0);
}

void TestPlyNote(u32 noteCmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    LogCall(CALL_PLY_NOTE, mplayInfo, track, noteCmd, 0, 0, mplayInfo->ident);
}

/* src/m4a.c doubles */

void SampleFreqSet(u32 freq)
{
    LogCall(CALL_SAMPLE_FREQ_SET, NULL, NULL, freq, 0, 0, 0);
}

void FadeOutBody(struct MusicPlayerInfo *mplayInfo)
{
    LogCall(CALL_FADE_OUT_BODY, mplayInfo, NULL, 0, 0, 0, mplayInfo->ident);
    if (sFadeOutBodyHook != NULL)
        sFadeOutBodyHook(mplayInfo);
}

void ClearChain(void *x)
{
    void (*func)(void *) = gMPlayJumpTable[34];

    LogCall(CALL_CLEAR_CHAIN, x, NULL, 0, 0, 0, 0);
    if (func != NULL)
        func(x);
}

void Clear64byte(void *x)
{
    void (*func)(void *) = gMPlayJumpTable[35];

    LogCall(CALL_CLEAR_64_BYTE, x, NULL, 0, 0, 0, 0);
    if (func != NULL)
        func(x);
}

/* src/m4a.c's TrkVolPitSet and MidiKeyToFreq, logged, then as m4a.c computes them. */
void TrkVolPitSet(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    LogCall(CALL_TRK_VOL_PIT_SET, mplayInfo, track, track->flags, 0, 0, mplayInfo->ident);
    M4aRefTrkVolPitSet(track);
}

u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust)
{
    LogCall(CALL_MIDI_KEY_TO_FREQ, wav, NULL, key, fineAdjust, 0, 0);
    return M4aRefMidiKeyToFreq(wav, key, fineAdjust);
}

/* Test data */

struct WaveData *M4aTestMakeWave(union M4aTestWave *w, uint16_t type, uint16_t status,
                                 uint32_t freq, uint32_t loopStart, uint32_t size)
{
    memset(w, 0, sizeof(*w));
    w->wave.type = type;
    w->wave.status = status;
    w->wave.freq = freq;
    w->wave.loopStart = loopStart;
    w->wave.size = size;
    return &w->wave;
}

void M4aTestSetWaveByte(union M4aTestWave *w, uint32_t dataIndex, uint8_t value)
{
    if (dataIndex >= TEST_WAVE_MAX_DATA)
    {
        fprintf(stderr, "test_m4a_support: wave byte %u out of range\n", (unsigned)dataIndex);
        return;
    }
    w->bytes[TEST_WAVE_HEADER_SIZE + dataIndex] = value;
}

int64_t M4aTestFloorDiv(int64_t a, int64_t b)
{
    if (a >= 0)
        return a / b;
    return -((-a + b - 1) / b);
}

uint8_t M4aTestMixDelta(uint32_t volume, int32_t sample)
{
    int64_t delta = M4aTestFloorDiv((int64_t)volume * sample, 256);

    return (uint8_t)(((delta % 256) + 256) % 256);
}

int32_t M4aTestWrapS8(int64_t value)
{
    int64_t wrapped = ((value % 256) + 256) % 256;

    return (int32_t)(wrapped >= 128 ? wrapped - 256 : wrapped);
}

/* Fixture */

struct SoundInfo *M4aTestSetUp(void)
{
    extern void *const gMPlayJumpTableTemplate[];

    if (MapPages() != 0)
        return NULL;

    memset(&sSoundInfo, 0, sizeof(sSoundInfo));
    memset(sCgbChans, 0, sizeof(sCgbChans));
    for (size_t i = 0; i < TEST_NUM_CGB_CHANS; i++)
        sCgbChans[i].type = (u8)(i + 1);

    for (size_t i = 0; i < 36; i++)
        memcpy(&gMPlayJumpTable[i], &gMPlayJumpTableTemplate[i], sizeof(MPlayFunc));

    memset(sCalls, 0, sizeof(sCalls));
    sCallCount = 0;
    sCallLogOverflow = 0;
    sCgbSoundHook = NULL;
    sFadeOutBodyHook = NULL;

    memset((void *)(uintptr_t)TEST_IO_PAGE_ADDR, 0, TEST_PAGE_SIZE);
    memset((void *)(uintptr_t)TEST_LOW_PAGE_ADDR, 0, TEST_PAGE_SIZE);
    M4aTestWriteIo8(REG_OFFSET_VCOUNT, TEST_VCOUNT_IN_VBLANK);

    SOUND_INFO_PTR = &sSoundInfo;
    sSoundInfo.ident = ID_NUMBER;
    sSoundInfo.maxChans = 4;
    sSoundInfo.masterVolume = 15;
    sSoundInfo.pcmSamplesPerVBlank = 16;
    sSoundInfo.pcmDmaPeriod = PCM_DMA_BUF_SIZE / 16;
    sSoundInfo.divFreq = 1;
    sSoundInfo.cgbChans = sCgbChans;
    sSoundInfo.CgbSound = TestCgbSound;
    sSoundInfo.CgbOscOff = TestCgbOscOff;
    sSoundInfo.MidiKeyToCgbFreq = TestMidiKeyToCgbFreq;
    sSoundInfo.MPlayJumpTable = gMPlayJumpTable;
    sSoundInfo.plynote = ply_note;
    return &sSoundInfo;
}
