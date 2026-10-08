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
 * files share: constants the C header lacks, the struct layout the
 * assembly hard-codes (asserted below), access to channel-chain nodes, and
 * the bit arithmetic the ARM gets from its barrel shifter.
 *
 * Endianness and alignment: the GBA and the native host (x86) are
 * little-endian, and the engine's data (songs, voicegroups, samples) is
 * assembled little-endian. The structs are naturally aligned; the
 * assembly's word accesses at the asserted offsets are aligned. Where the
 * assembly moves one 32-bit word across two 16-bit fields (SoundChannel
 * xpi and xpc) the C composes the two fields explicitly.
 *
 * Pointer size: this milestone is 32-bit. Every offset that depends on the
 * size of a pointer is marked "flagged 64-bit"; the 64-bit milestone will
 * revisit them together with the song and voicegroup data, which store
 * 4-byte pointers.
 */
#ifndef GUARD_PLATFORM_M4A_ENGINE_H
#define GUARD_PLATFORM_M4A_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "gba/m4a_internal.h"
#include "m4a.h"

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

/* SoundMainBTM and Clear64byte clear this many bytes (4 x stm of 4 words). */
#define M4A_CLEAR_SIZE           64

/* Number of PSG channels, gCgbChans[4] in src/m4a.c (GBATEK sound channels 1 to 4). */
#define NUM_CGB_CHANNELS         4

/*
 * Struct layout the assembly hard-codes (constants/m4a_constants.inc).
 * Each offset the engine depends on is asserted against the C header.
 */

/* struct WaveData: no pointers, the same on every target. */
_Static_assert(offsetof(struct WaveData, type) == 0x00, "o_WaveData_type");
_Static_assert(offsetof(struct WaveData, status) == 0x02, "o_WaveData_d1 and o_WaveData_flags");
_Static_assert(offsetof(struct WaveData, freq) == 0x04, "o_WaveData_freq");
_Static_assert(offsetof(struct WaveData, loopStart) == 0x08, "o_WaveData_loopStart");
_Static_assert(offsetof(struct WaveData, size) == 0x0C, "o_WaveData_size");
_Static_assert(offsetof(struct WaveData, data) == 0x10, "o_WaveData_data");

/* struct ToneData */
_Static_assert(offsetof(struct ToneData, type) == 0x00, "o_ToneData_type");
_Static_assert(offsetof(struct ToneData, key) == 0x01, "o_ToneData_key");
_Static_assert(offsetof(struct ToneData, length) == 0x02, "o_ToneData_length");
_Static_assert(offsetof(struct ToneData, pan_sweep) == 0x03, "o_ToneData_pan_sweep");
_Static_assert(offsetof(struct ToneData, wav) == 0x04, "o_ToneData_wav");
_Static_assert(offsetof(struct ToneData, attack) == 0x08, "o_ToneData_attack: flagged 64-bit");
_Static_assert(offsetof(struct ToneData, decay) == 0x09, "o_ToneData_decay: flagged 64-bit");
_Static_assert(offsetof(struct ToneData, sustain) == 0x0A, "o_ToneData_sustain: flagged 64-bit");
_Static_assert(offsetof(struct ToneData, release) == 0x0B, "o_ToneData_release: flagged 64-bit");
_Static_assert(sizeof(struct ToneData) == 0x0C, "ToneData_size, the voicegroup stride: flagged 64-bit");
/* A key-split voice keeps its table pointer in attack..release (voice_keysplit
 * in asm/macros/music_voice.inc). */
_Static_assert(sizeof(const u8 *) == 4, "key-split table pointer in ToneData.attack: flagged 64-bit");

/* struct SoundChannel */
_Static_assert(offsetof(struct SoundChannel, statusFlags) == 0x00, "o_SoundChannel_statusFlags");
_Static_assert(offsetof(struct SoundChannel, type) == 0x01, "o_SoundChannel_type");
_Static_assert(offsetof(struct SoundChannel, rightVolume) == 0x02, "o_SoundChannel_rightVolume");
_Static_assert(offsetof(struct SoundChannel, leftVolume) == 0x03, "o_SoundChannel_leftVolume");
_Static_assert(offsetof(struct SoundChannel, attack) == 0x04, "o_SoundChannel_attack");
_Static_assert(offsetof(struct SoundChannel, decay) == 0x05, "o_SoundChannel_decay");
_Static_assert(offsetof(struct SoundChannel, sustain) == 0x06, "o_SoundChannel_sustain");
_Static_assert(offsetof(struct SoundChannel, release) == 0x07, "o_SoundChannel_release");
_Static_assert(offsetof(struct SoundChannel, key) == 0x08, "o_SoundChannel_key");
_Static_assert(offsetof(struct SoundChannel, envelopeVolume) == 0x09, "o_SoundChannel_envelopeVolume");
_Static_assert(offsetof(struct SoundChannel, envelopeVolumeRight) == 0x0A, "o_SoundChannel_envelopeVolumeRight");
_Static_assert(offsetof(struct SoundChannel, envelopeVolumeLeft) == 0x0B, "o_SoundChannel_envelopeVolumeLeft");
_Static_assert(offsetof(struct SoundChannel, pseudoEchoVolume) == 0x0C, "o_SoundChannel_pseudoEchoVolume");
_Static_assert(offsetof(struct SoundChannel, pseudoEchoLength) == 0x0D, "o_SoundChannel_pseudoEchoLength");
_Static_assert(offsetof(struct SoundChannel, gateTime) == 0x10, "o_SoundChannel_gateTime");
_Static_assert(offsetof(struct SoundChannel, midiKey) == 0x11, "o_SoundChannel_midiKey");
_Static_assert(offsetof(struct SoundChannel, velocity) == 0x12, "o_SoundChannel_velocity");
_Static_assert(offsetof(struct SoundChannel, priority) == 0x13, "o_SoundChannel_priority");
_Static_assert(offsetof(struct SoundChannel, rhythmPan) == 0x14, "o_SoundChannel_rhythmPan");
_Static_assert(offsetof(struct SoundChannel, count) == 0x18, "o_SoundChannel_count");
_Static_assert(offsetof(struct SoundChannel, fw) == 0x1C, "o_SoundChannel_fw");
_Static_assert(offsetof(struct SoundChannel, frequency) == 0x20, "o_SoundChannel_frequency");
_Static_assert(offsetof(struct SoundChannel, wav) == 0x24, "o_SoundChannel_wav");
_Static_assert(offsetof(struct SoundChannel, currentPointer) == 0x28, "o_SoundChannel_currentPointer: flagged 64-bit");
_Static_assert(offsetof(struct SoundChannel, track) == 0x2C, "o_SoundChannel_track: flagged 64-bit");
_Static_assert(offsetof(struct SoundChannel, prevChannelPointer) == 0x30, "o_SoundChannel_prevChannelPointer: flagged 64-bit");
_Static_assert(offsetof(struct SoundChannel, nextChannelPointer) == 0x34, "o_SoundChannel_nextChannelPointer: flagged 64-bit");
_Static_assert(offsetof(struct SoundChannel, xpi) == 0x3C, "o_SoundChannel_xpi: flagged 64-bit");
_Static_assert(offsetof(struct SoundChannel, xpc) == 0x3E, "o_SoundChannel_xpc: flagged 64-bit");
_Static_assert(sizeof(struct SoundChannel) == 0x40, "SoundChannel_size: flagged 64-bit");
/* A compressed channel keeps a sample index in currentPointer, as the
 * assembly does; it is stored there as a uintptr_t (flagged 64-bit). */
_Static_assert(sizeof(uintptr_t) == sizeof(s8 *), "sample index stored in SoundChannel.currentPointer");

/* struct CgbChannel */
_Static_assert(offsetof(struct CgbChannel, modify) == 0x1D, "o_CgbChannel_modify");
_Static_assert(offsetof(struct CgbChannel, length) == 0x1E, "o_CgbChannel_length");
_Static_assert(offsetof(struct CgbChannel, sweep) == 0x1F, "o_CgbChannel_sweep");
_Static_assert(offsetof(struct CgbChannel, frequency) == 0x20, "o_CgbChannel_frequency");
_Static_assert(offsetof(struct CgbChannel, wavePointer) == 0x24, "o_CgbChannel_wavePointer");
_Static_assert(offsetof(struct CgbChannel, track) == 0x2C, "o_CgbChannel_track: flagged 64-bit");
_Static_assert(offsetof(struct CgbChannel, prevChannelPointer) == 0x30, "o_CgbChannel_prevChannelPointer: flagged 64-bit");
_Static_assert(offsetof(struct CgbChannel, nextChannelPointer) == 0x34, "o_CgbChannel_nextChannelPointer: flagged 64-bit");
_Static_assert(sizeof(struct CgbChannel) == 0x40, "CgbChannel_size, the gCgbChans stride: flagged 64-bit");
/* ply_note stores ToneData.wav into a PSG channel's wavePointer word. */
_Static_assert(sizeof(((struct CgbChannel *)0)->wavePointer) == sizeof(((struct ToneData *)0)->wav),
               "CgbChannel.wavePointer receives ToneData.wav");

/* struct MusicPlayerTrack */
_Static_assert(offsetof(struct MusicPlayerTrack, flags) == 0x00, "o_MusicPlayerTrack_flags");
_Static_assert(offsetof(struct MusicPlayerTrack, wait) == 0x01, "o_MusicPlayerTrack_wait");
_Static_assert(offsetof(struct MusicPlayerTrack, patternLevel) == 0x02, "o_MusicPlayerTrack_patternLevel");
_Static_assert(offsetof(struct MusicPlayerTrack, repN) == 0x03, "o_MusicPlayerTrack_repN");
_Static_assert(offsetof(struct MusicPlayerTrack, gateTime) == 0x04, "o_MusicPlayerTrack_gateTime");
_Static_assert(offsetof(struct MusicPlayerTrack, key) == 0x05, "o_MusicPlayerTrack_key");
_Static_assert(offsetof(struct MusicPlayerTrack, velocity) == 0x06, "o_MusicPlayerTrack_velocity");
_Static_assert(offsetof(struct MusicPlayerTrack, runningStatus) == 0x07, "o_MusicPlayerTrack_runningStatus");
_Static_assert(offsetof(struct MusicPlayerTrack, keyM) == 0x08, "o_MusicPlayerTrack_keyM");
_Static_assert(offsetof(struct MusicPlayerTrack, pitM) == 0x09, "o_MusicPlayerTrack_pitM");
_Static_assert(offsetof(struct MusicPlayerTrack, keyShift) == 0x0A, "o_MusicPlayerTrack_keyShift");
_Static_assert(offsetof(struct MusicPlayerTrack, tune) == 0x0C, "o_MusicPlayerTrack_tune");
_Static_assert(offsetof(struct MusicPlayerTrack, bend) == 0x0E, "o_MusicPlayerTrack_bend");
_Static_assert(offsetof(struct MusicPlayerTrack, bendRange) == 0x0F, "o_MusicPlayerTrack_bendRange");
_Static_assert(offsetof(struct MusicPlayerTrack, volMR) == 0x10, "o_MusicPlayerTrack_volMR");
_Static_assert(offsetof(struct MusicPlayerTrack, volML) == 0x11, "o_MusicPlayerTrack_volML");
_Static_assert(offsetof(struct MusicPlayerTrack, vol) == 0x12, "o_MusicPlayerTrack_vol");
_Static_assert(offsetof(struct MusicPlayerTrack, volX) == 0x13, "o_MusicPlayerTrack_volX");
_Static_assert(offsetof(struct MusicPlayerTrack, pan) == 0x14, "o_MusicPlayerTrack_pan");
_Static_assert(offsetof(struct MusicPlayerTrack, modM) == 0x16, "o_MusicPlayerTrack_modM");
_Static_assert(offsetof(struct MusicPlayerTrack, mod) == 0x17, "o_MusicPlayerTrack_mod");
_Static_assert(offsetof(struct MusicPlayerTrack, modT) == 0x18, "o_MusicPlayerTrack_modT");
_Static_assert(offsetof(struct MusicPlayerTrack, lfoSpeed) == 0x19, "o_MusicPlayerTrack_lfoSpeed");
_Static_assert(offsetof(struct MusicPlayerTrack, lfoSpeedC) == 0x1A, "o_MusicPlayerTrack_lfoSpeedC");
_Static_assert(offsetof(struct MusicPlayerTrack, lfoDelay) == 0x1B, "o_MusicPlayerTrack_lfoDelay");
_Static_assert(offsetof(struct MusicPlayerTrack, lfoDelayC) == 0x1C, "o_MusicPlayerTrack_lfoDelayC");
_Static_assert(offsetof(struct MusicPlayerTrack, priority) == 0x1D, "o_MusicPlayerTrack_priority");
_Static_assert(offsetof(struct MusicPlayerTrack, pseudoEchoVolume) == 0x1E, "o_MusicPlayerTrack_pseudoEchoVolume");
_Static_assert(offsetof(struct MusicPlayerTrack, pseudoEchoLength) == 0x1F, "o_MusicPlayerTrack_pseudoEchoLength");
_Static_assert(offsetof(struct MusicPlayerTrack, chan) == 0x20, "o_MusicPlayerTrack_chan");
_Static_assert(offsetof(struct MusicPlayerTrack, tone) == 0x24, "o_MusicPlayerTrack_ToneData_type: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerTrack, unk_3C) == 0x3C, "o_MusicPlayerTrack_unk_3C: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) == 0x40, "o_MusicPlayerTrack_cmdPtr: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerTrack, patternStack) == 0x44, "o_MusicPlayerTrack_patternStack: flagged 64-bit");
_Static_assert(sizeof(struct MusicPlayerTrack) == 0x50, "MusicPlayerTrack_size, TRACK_SIZE in sound/music_player_table.inc: flagged 64-bit");
/* MPlayMain clears a starting track with Clear64byte, which must stop short of cmdPtr. */
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) == M4A_CLEAR_SIZE, "Clear64byte keeps cmdPtr: flagged 64-bit");

/* struct MusicPlayerInfo: everything after songHeader moves with the pointer size. */
_Static_assert(offsetof(struct MusicPlayerInfo, status) == 0x04, "o_MusicPlayerInfo_status: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, trackCount) == 0x08, "o_MusicPlayerInfo_trackCount: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, priority) == 0x09, "o_MusicPlayerInfo_priority: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, cmd) == 0x0A, "o_MusicPlayerInfo_cmd: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, clock) == 0x0C, "o_MusicPlayerInfo_clock: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoD) == 0x1C, "o_MusicPlayerInfo_tempoD: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoU) == 0x1E, "o_MusicPlayerInfo_tempoU: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoI) == 0x20, "o_MusicPlayerInfo_tempoI: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoC) == 0x22, "o_MusicPlayerInfo_tempoC: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tracks) == 0x2C, "o_MusicPlayerInfo_tracks: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, tone) == 0x30, "o_MusicPlayerInfo_tone: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, ident) == 0x34, "o_MusicPlayerInfo_ident: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, MPlayMainNext) == 0x38, "o_MusicPlayerInfo_func: flagged 64-bit");
_Static_assert(offsetof(struct MusicPlayerInfo, musicPlayerNext) == 0x3C, "o_MusicPlayerInfo_intp: flagged 64-bit");
_Static_assert(sizeof(struct MusicPlayerInfo) == M4A_CLEAR_SIZE, "MusicPlayerInfo_size, cleared by Clear64byte in MPlayOpen: flagged 64-bit");

/* struct SoundInfo */
_Static_assert(offsetof(struct SoundInfo, ident) == 0x00, "o_SoundInfo_ident");
_Static_assert(offsetof(struct SoundInfo, pcmDmaCounter) == 0x04, "o_SoundInfo_pcmDmaCounter");
_Static_assert(offsetof(struct SoundInfo, reverb) == 0x05, "o_SoundInfo_reverb");
_Static_assert(offsetof(struct SoundInfo, maxChans) == 0x06, "o_SoundInfo_maxChans");
_Static_assert(offsetof(struct SoundInfo, masterVolume) == 0x07, "o_SoundInfo_masterVolume");
/* SoundMainRAM reads masterVolume as [soundInfo, #o_SoundChannel_release]. */
_Static_assert(offsetof(struct SoundInfo, masterVolume) == offsetof(struct SoundChannel, release),
               "masterVolume read through o_SoundChannel_release");
_Static_assert(offsetof(struct SoundInfo, pcmDmaPeriod) == 0x0B, "o_SoundInfo_pcmDmaPeriod");
_Static_assert(offsetof(struct SoundInfo, maxLines) == 0x0C, "o_SoundInfo_maxLines");
_Static_assert(offsetof(struct SoundInfo, pcmSamplesPerVBlank) == 0x10, "o_SoundInfo_pcmSamplesPerVBlank");
_Static_assert(offsetof(struct SoundInfo, divFreq) == 0x18, "o_SoundInfo_divFreq");
_Static_assert(offsetof(struct SoundInfo, cgbChans) == 0x1C, "o_SoundInfo_cgbChans");
_Static_assert(offsetof(struct SoundInfo, MPlayMainHead) == 0x20, "o_SoundInfo_MPlayMainHead: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, musicPlayerHead) == 0x24, "o_SoundInfo_musicPlayerHead: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, CgbSound) == 0x28, "o_SoundInfo_CgbSound: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, CgbOscOff) == 0x2C, "o_SoundInfo_CgbOscOff: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, MidiKeyToCgbFreq) == 0x30, "o_SoundInfo_MidiKeyToCgbFreq: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, MPlayJumpTable) == 0x34, "o_SoundInfo_MPlayJumpTable: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, plynote) == 0x38, "o_SoundInfo_plynote: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, chans) == 0x50, "o_SoundInfo_chans: flagged 64-bit");
_Static_assert(offsetof(struct SoundInfo, pcmBuffer) == 0x350, "o_SoundInfo_pcmBuffer: flagged 64-bit");
_Static_assert(sizeof(((struct SoundInfo *)0)->pcmBuffer) == 2 * PCM_DMA_BUF_SIZE, "the ring's two halves");
_Static_assert(sizeof(struct SoundInfo) == 0xFB0, "SoundInfo_size: flagged 64-bit");

/* MPlayJumpTableCopy copies the template entry by entry as pointers. */
_Static_assert(sizeof(MPlayFunc) == sizeof(void *), "jump table entries are pointer-sized");

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
