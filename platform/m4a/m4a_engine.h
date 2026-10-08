/*
 * m4a engine: internal header shared by the files of the C translation of
 * src/m4a_1.s.
 *
 * What it stands in for: the ARM/Thumb half of the MusicPlayer2000 ("m4a")
 * sound driver. SoundMain and its mixer render the Direct Sound channels
 * into the PCM ring that DMA1/DMA2 feed to sound FIFOs A and B (GBATEK
 * "GBA Sound Controller"); MPlayMain and the ply_* commands run the song
 * sequencer. The C half of the driver is expansion's src/m4a.c, unchanged.
 *
 * Followed: the assembly itself (src/m4a_1.s, constants from
 * constants/m4a_constants.inc). The reference ports' C engines are not
 * used (docs/C-RULES.md, licensing rule). README.md maps every assembly
 * symbol to its C function.
 *
 * Public interface: none of its own. include/gba/m4a_internal.h and
 * include/m4a.h already declare every function the engine exports, with
 * the names the game links against. This header holds what the engine's
 * files share: constants the C header lacks, access to channel-chain
 * nodes, and the bit arithmetic the ARM gets from its barrel shifter. The
 * struct layout the assembly hard-codes is asserted in m4a_layout.h.
 *
 * Endianness and alignment: the GBA and the native host (x86) are
 * little-endian, and the engine's data (songs, voicegroups, samples) is
 * assembled little-endian. The structs are naturally aligned; the
 * assembly's word accesses at the asserted offsets are aligned. Where the
 * assembly moves one 32-bit word across two 16-bit fields (SoundChannel
 * xpi and xpc) the C composes the two fields explicitly.
 *
 * Pointer size: this milestone is 32-bit. Everything that depends on the
 * size of a pointer is marked "flagged 64-bit" (here and in m4a_layout.h).
 */
#ifndef GUARD_PLATFORM_M4A_ENGINE_H
#define GUARD_PLATFORM_M4A_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gba/m4a_internal.h"
#include "m4a.h"
#include "m4a_layout.h"

/* constants/m4a_constants.inc values that include/gba/m4a_internal.h lacks */
#define TONEDATA_TYPE_REV        0x10 // play the sample backwards
#define TONEDATA_TYPE_CMP        0x20 // the sample may be DPCM-compressed
#define SOUND_CHANNEL_SF_SPECIAL 0x20 // reversed/compressed playback initialised
#define WAVE_DATA_FLAG_LOOP      0xC0 // in o_WaveData_flags, the high byte of WaveData.status

/* Track command bytes, as MPlayMain decodes them (sound/MPlayDef.s). */
#define MPLAY_CMD_FIRST          0x80 // bytes below are arguments, or repeat the running status
#define MPLAY_CMD_WAIT_LAST      0xB0 // 0x80..0xB0: wait gClockTable[cmd - 0x80] ticks
#define MPLAY_CMD_TABLE_FIRST    0xB1 // 0xB1..0xCE: MPlayJumpTable[cmd - 0xB1] (FINE first)
#define MPLAY_CMD_RUNNING_FIRST  0xBD // commands from VOICE on are remembered as running status
#define MPLAY_CMD_NOTE_FIRST     0xCF // 0xCF..0xFF: plynote(cmd - 0xCF) (TIE first)
/* gClockTable holds a length for each wait (0x80..0xB0) and note (0xCF..0xFF) command. */
#define MPLAY_CLOCK_TABLE_SIZE   (MPLAY_CMD_WAIT_LAST - MPLAY_CMD_FIRST + 1)

/* The high nibble of MusicPlayerTrack.flags (EXIST, START) survives an update. */
#define MPT_FLG_KEEP_MASK        0xF0

/* Number of PSG channels, gCgbChans[4] in src/m4a.c (GBATEK sound channels 1 to 4). */
#define NUM_CGB_CHANNELS         4

/*
 * Channel chains.
 *
 * A track's chain links struct SoundChannel (Direct Sound) and struct
 * CgbChannel (PSG) nodes through prevChannelPointer and nextChannelPointer;
 * the assembly walks it with SoundChannel offsets whatever each node is.
 * The two structs share those offsets (asserted below). A node of unknown
 * kind is read and written only through these helpers: its u8 fields as
 * bytes (a character type may access any object) and its pointer fields
 * with memcpy, so no CgbChannel is accessed through a SoundChannel lvalue.
 * Once a node's type byte shows which struct it is (type & 7 is the PSG
 * channel, 0 for Direct Sound), the code uses that struct directly.
 */
enum ChanByteField
{
    CHAN_STATUS_FLAGS        = offsetof(struct SoundChannel, statusFlags),
    CHAN_TYPE                = offsetof(struct SoundChannel, type),
    CHAN_RIGHT_VOLUME        = offsetof(struct SoundChannel, rightVolume),
    CHAN_LEFT_VOLUME         = offsetof(struct SoundChannel, leftVolume),
    CHAN_ATTACK              = offsetof(struct SoundChannel, attack),
    CHAN_DECAY               = offsetof(struct SoundChannel, decay),
    CHAN_SUSTAIN             = offsetof(struct SoundChannel, sustain),
    CHAN_RELEASE             = offsetof(struct SoundChannel, release),
    CHAN_KEY                 = offsetof(struct SoundChannel, key),
    CHAN_PSEUDO_ECHO_VOLUME  = offsetof(struct SoundChannel, pseudoEchoVolume),
    CHAN_PSEUDO_ECHO_LENGTH  = offsetof(struct SoundChannel, pseudoEchoLength),
    CHAN_GATE_TIME           = offsetof(struct SoundChannel, gateTime),
    CHAN_MIDI_KEY            = offsetof(struct SoundChannel, midiKey),
    CHAN_VELOCITY            = offsetof(struct SoundChannel, velocity),
    CHAN_PRIORITY            = offsetof(struct SoundChannel, priority),
    CHAN_RHYTHM_PAN          = offsetof(struct SoundChannel, rhythmPan),
};

#define ASSERT_SHARED_CHAN_FIELD(field) \
    _Static_assert(offsetof(struct SoundChannel, field) == offsetof(struct CgbChannel, field), \
                   "chain nodes share " #field)
ASSERT_SHARED_CHAN_FIELD(statusFlags);
ASSERT_SHARED_CHAN_FIELD(type);
ASSERT_SHARED_CHAN_FIELD(rightVolume);
ASSERT_SHARED_CHAN_FIELD(leftVolume);
ASSERT_SHARED_CHAN_FIELD(attack);
ASSERT_SHARED_CHAN_FIELD(decay);
ASSERT_SHARED_CHAN_FIELD(sustain);
ASSERT_SHARED_CHAN_FIELD(release);
ASSERT_SHARED_CHAN_FIELD(key);
ASSERT_SHARED_CHAN_FIELD(pseudoEchoVolume);
ASSERT_SHARED_CHAN_FIELD(pseudoEchoLength);
ASSERT_SHARED_CHAN_FIELD(gateTime);
ASSERT_SHARED_CHAN_FIELD(midiKey);
ASSERT_SHARED_CHAN_FIELD(velocity);
ASSERT_SHARED_CHAN_FIELD(priority);
ASSERT_SHARED_CHAN_FIELD(rhythmPan);
ASSERT_SHARED_CHAN_FIELD(frequency);
ASSERT_SHARED_CHAN_FIELD(track);
ASSERT_SHARED_CHAN_FIELD(prevChannelPointer);
ASSERT_SHARED_CHAN_FIELD(nextChannelPointer);
#undef ASSERT_SHARED_CHAN_FIELD
/* ply_note stores the voice's wav at the same offset in either kind. */
_Static_assert(offsetof(struct SoundChannel, wav) == offsetof(struct CgbChannel, wavePointer),
               "chain nodes share the wave pointer word");

static inline u8 ChanByte(const void *node, enum ChanByteField field)
{
    return ((const u8 *)node)[field];
}

static inline void ChanSetByte(void *node, enum ChanByteField field, u8 value)
{
    ((u8 *)node)[field] = value;
}

static inline void *ChanNext(const void *node)
{
    void *next;

    memcpy(&next, (const u8 *)node + offsetof(struct SoundChannel, nextChannelPointer), sizeof(next));
    return next;
}

static inline void ChanSetNext(void *node, void *next)
{
    memcpy((u8 *)node + offsetof(struct SoundChannel, nextChannelPointer), &next, sizeof(next));
}

static inline void *ChanPrev(const void *node)
{
    void *prev;

    memcpy(&prev, (const u8 *)node + offsetof(struct SoundChannel, prevChannelPointer), sizeof(prev));
    return prev;
}

static inline void ChanSetPrev(void *node, void *prev)
{
    memcpy((u8 *)node + offsetof(struct SoundChannel, prevChannelPointer), &prev, sizeof(prev));
}

static inline struct MusicPlayerTrack *ChanTrack(const void *node)
{
    struct MusicPlayerTrack *track;

    memcpy(&track, (const u8 *)node + offsetof(struct SoundChannel, track), sizeof(track));
    return track;
}

static inline void ChanSetTrack(void *node, struct MusicPlayerTrack *track)
{
    memcpy((u8 *)node + offsetof(struct SoundChannel, track), &track, sizeof(track));
}

/*
 * ARM arithmetic in well-defined C. Values the assembly keeps in signed
 * registers are held as u32 two's-complement patterns, so wrap-around is
 * defined; these helpers give the signed views without shifting negative
 * numbers or converting out-of-range values to signed types.
 */

/* ldrsb: a byte sign-extended to 32 bits. */
static inline u32 SignExtend8(u8 value)
{
    return (u32)value - ((u32)(value & 0x80u) << 1);
}

/* asr: shift right copying the sign bit, for shift 1..31. */
static inline u32 AsrU32(u32 value, unsigned shift)
{
    u32 signFill = (value & 0x80000000u) ? ~(0xFFFFFFFFu >> shift) : 0;

    return (value >> shift) | signFill;
}

/* The signed value of a 32-bit two's-complement pattern. */
static inline s32 AsS32(u32 value)
{
    return value <= 0x7FFFFFFFu ? (s32)value : -(s32)~value - 1;
}

/* strb of a signed value: its low byte, read back as a signed byte. */
static inline s8 LowByteS8(u32 value)
{
    u32 low = value & 0xFFu;

    return (s8)(low >= 0x80u ? (s32)low - 0x100 : (s32)low);
}

/* SoundMainRAM and ply_note scan maxChans Direct Sound channels, testing the
 * count after the first one (so 0 still scans channel 0). A count above the
 * 12 channels SoundInfo holds, which the assembly would run past, is cut. */
static inline u32 DirectChannelsToScan(const struct SoundInfo *soundInfo)
{
    u32 channels = soundInfo->maxChans;

    if (channels == 0)
        return 1;
    return channels > MAX_DIRECTSOUND_CHANNELS ? MAX_DIRECTSOUND_CHANNELS : channels;
}

/* ply_note and MPlayMain: the key a channel sounds, its own key plus the
 * track's keyM read as signed, no lower than 0. The sum is at most 255 + 127;
 * MidiKeyToFreq takes a u8, so a sum above 255 (unreachable with voicegroup
 * keys below 128) would wrap here where the GBA passes the full register. */
static inline u8 SoundingKey(u8 channelKey, u8 keyM)
{
    s32 key = channelKey + LowByteS8(keyM);

    return (u8)(key < 0 ? 0 : key);
}

/* Definitions in src/m4a.c and src/m4a_tables.c that the assembly uses by
 * name and the C headers do not declare. */
extern const u8 gClockTable[];
extern const s8 gDeltaEncodingTable[];
extern void *const gMPlayJumpTableTemplate[];
u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust);

/* Engine functions shared between its files, under their assembly names. */

/* ChnVolSetAsm: a channel's left and right volume from its velocity and
 * rhythm pan and the track's volMR and volML. The channel may be either kind. */
void ChnVolSetAsm(void *chan, const struct MusicPlayerTrack *track);

/* clear_modM: resets the LFO output and flags the parameter it modulates. */
void clear_modM(struct MusicPlayerTrack *track);

#endif // GUARD_PLATFORM_M4A_ENGINE_H
