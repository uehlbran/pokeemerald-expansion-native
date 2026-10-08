/*
 * Fixtures for the m4a engine unit tests.
 *
 * The engine reaches its state the way the GBA code does: SoundInfo through
 * SOUND_INFO_PTR (IWRAM 0x03007FF0) and the hardware through the I/O
 * registers at 0x04000000 (GBATEK "GBA Memory Map"). The fixture maps those
 * two pages at their GBA addresses, as the native platform will, plus one
 * page below 0x02000000 so the BIOS-region read guard can be exercised.
 *
 * It also provides the src/m4a.c functions the engine calls, each recording
 * the call: MidiKeyToFreq and TrkVolPitSet then compute as m4a.c does
 * (test_m4a_c_reference.c), ClearChain and Clear64byte dispatch through
 * gMPlayJumpTable as m4a.c does, and the rest only record.
 */
#ifndef GUARD_PLATFORM_M4A_TEST_SUPPORT_H
#define GUARD_PLATFORM_M4A_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "gba/m4a_internal.h"
#include "m4a.h"

/* A page below 0x02000000, the region the engine's read guard treats as BIOS. */
#define TEST_LOW_PAGE_ADDR 0x01000000u
#define TEST_PAGE_SIZE     0x1000u

/* Number of CGB channels MPlayExtender sets up (gCgbChans[4] in m4a.c). */
#define TEST_NUM_CGB_CHANS 4

enum M4aCallKind
{
    CALL_MPLAY_MAIN_HEAD,
    CALL_MPLAY_NEXT,
    CALL_CGB_SOUND,
    CALL_CGB_OSC_OFF,
    CALL_MIDI_KEY_TO_CGB_FREQ,
    CALL_MIDI_KEY_TO_FREQ,
    CALL_TRK_VOL_PIT_SET,
    CALL_FADE_OUT_BODY,
    CALL_CLEAR_CHAIN,
    CALL_CLEAR_64_BYTE,
    CALL_PLY_NOTE,
    CALL_SAMPLE_FREQ_SET,
};

struct M4aCall
{
    enum M4aCallKind kind;
    const void *ptr0;
    const void *ptr1;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t soundIdentAtCall;  // SOUND_INFO_PTR->ident when the call happened
    uint32_t playerIdentAtCall; // ident of the MusicPlayerInfo argument, if any
};

/* Maximum number of calls the log keeps; more is reported as overflow. */
#define M4A_CALL_LOG_SIZE 256

/*
 * Resets the fixture: maps the pages on first use, zeroes the SoundInfo,
 * points SOUND_INFO_PTR at it with ident ID_NUMBER, installs the recording
 * callbacks, copies the jump table template into gMPlayJumpTable, sets
 * VCOUNT to 160 and clears the DMA registers and the call log. Returns the
 * SoundInfo, or NULL if a page could not be mapped.
 */
struct SoundInfo *M4aTestSetUp(void);

/* The four CGB channels SoundInfo.cgbChans points at after M4aTestSetUp. */
struct CgbChannel *M4aTestCgbChans(void);

size_t M4aTestCallCount(void);
int M4aTestCallLogOverflowed(void);
const struct M4aCall *M4aTestCall(size_t index);
/* Number of logged calls of one kind. */
size_t M4aTestCountCalls(enum M4aCallKind kind);

/* Hooks run inside the recording doubles, after the call is logged. */
void M4aTestSetCgbSoundHook(void (*hook)(void));
void M4aTestSetFadeOutBodyHook(void (*hook)(struct MusicPlayerInfo *));

/* Value TestMidiKeyToCgbFreq returns: recognisable and derived from its arguments. */
uint32_t M4aTestCgbFreqFor(uint8_t chanNum, uint8_t key, uint8_t fineAdjust);

/* Recording doubles installed as SoundInfo callbacks or used by tests directly. */
void TestMPlayMainHead(struct MusicPlayerInfo *mplayInfo);
void TestMPlayNext(struct MusicPlayerInfo *mplayInfo);
void TestPlyNote(u32 noteCmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track);

/* The I/O register byte at REG_BASE + offset, as the engine sees it. */
uint8_t M4aTestReadIo8(uint32_t offset);
void M4aTestWriteIo8(uint32_t offset, uint8_t value);
uint16_t M4aTestReadIo16(uint32_t offset);
void M4aTestWriteIo32(uint32_t offset, uint32_t value);

/* The low page at TEST_LOW_PAGE_ADDR (mapped by M4aTestSetUp). */
uint8_t *M4aTestLowPage(void);

/* Test data */

/* Offset of the samples in a WaveData (the 16-byte header, o_WaveData_data). */
#define TEST_WAVE_HEADER_SIZE offsetof(struct WaveData, data)
/* Sample bytes a test wave can hold. */
#define TEST_WAVE_MAX_DATA 256

/*
 * A WaveData followed by its sample bytes, contiguous as the assembler
 * emits sound data (the C struct's data[1] is only the first sample).
 */
union M4aTestWave
{
    struct WaveData wave;
    uint32_t align;
    uint8_t bytes[TEST_WAVE_HEADER_SIZE + TEST_WAVE_MAX_DATA];
};

/* Fills the header and zeroes the samples. */
struct WaveData *M4aTestMakeWave(union M4aTestWave *w, uint16_t type, uint16_t status,
                                 uint32_t freq, uint32_t loopStart, uint32_t size);
/* Sets sample byte dataIndex (0 is the first byte after the header). */
void M4aTestSetWaveByte(union M4aTestWave *w, uint32_t dataIndex, uint8_t value);

/* Reference arithmetic, written independently of the engine. */

/* Floor of a / b for b > 0: what an arithmetic shift right gives for a power-of-two b. */
int64_t M4aTestFloorDiv(int64_t a, int64_t b);
/* What one sample adds to an output byte: floor(volume * sample / 256), as a byte. */
uint8_t M4aTestMixDelta(uint32_t volume, int32_t sample);
/* A value wrapped to the signed 8-bit range, as a byte store and signed reload give. */
int32_t M4aTestWrapS8(int64_t value);

#endif // GUARD_PLATFORM_M4A_TEST_SUPPORT_H
