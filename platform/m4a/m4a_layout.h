/*
 * m4a engine: the struct-layout contract.
 *
 * src/m4a_1.s addresses SoundInfo, SoundChannel, CgbChannel,
 * MusicPlayerInfo, MusicPlayerTrack, ToneData and WaveData by the numeric
 * offsets in constants/m4a_constants.inc, and sound/music_player_table.inc
 * reserves tracks by size. The C translation uses the C structs of
 * include/gba/m4a_internal.h, which stays as upstream has it; this file
 * asserts that every offset and size the assembly depends on is what the
 * C header gives. A failure here means the C engine would no longer read
 * the same memory as the GBA engine.
 *
 * Two groups. The first holds on every target: offsets that come before
 * any pointer, and the relations the C code itself relies on. The second
 * is the assembly's exact layout, which the C structs have only where a
 * pointer is 4 bytes. At 64 bits the assembled sound data is emitted in the
 * C structs' layout (asm/macros/m4a.inc and music_voice.inc under
 * NATIVE_POINTER_SIZE_8) and the 64-bit link compares it with them
 * (platform/build_glue/check_data_layout.py; platform/m4a/README.md, "The
 * 64-bit layout"). The engine is little-endian like the GBA; nothing here
 * depends on alignment beyond the structs' natural alignment.
 */
#ifndef GUARD_PLATFORM_M4A_LAYOUT_H
#define GUARD_PLATFORM_M4A_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "gba/m4a_internal.h"

/* SoundMainBTM and Clear64byte clear this many bytes (4 x stm of 4 words). */
#define M4A_CLEAR_SIZE 64

/* On every target */

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
/* A compressed channel keeps a sample index in currentPointer, as the
 * assembly does; it is stored there as a uintptr_t (either width). */
_Static_assert(sizeof(uintptr_t) == sizeof(s8 *), "sample index stored in SoundChannel.currentPointer");

/* struct CgbChannel */
_Static_assert(offsetof(struct CgbChannel, modify) == 0x1D, "o_CgbChannel_modify");
_Static_assert(offsetof(struct CgbChannel, length) == 0x1E, "o_CgbChannel_length");
_Static_assert(offsetof(struct CgbChannel, sweep) == 0x1F, "o_CgbChannel_sweep");
_Static_assert(offsetof(struct CgbChannel, frequency) == 0x20, "o_CgbChannel_frequency");
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
/* MPlayMain clears a starting track with Clear64byte, which must stop short of cmdPtr. */
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) >= M4A_CLEAR_SIZE, "Clear64byte stops short of cmdPtr");

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
_Static_assert(sizeof(((struct SoundInfo *)0)->pcmBuffer) == 2 * PCM_DMA_BUF_SIZE, "the ring's two halves");

/* MPlayJumpTableCopy copies the template entry by entry as pointers. */
_Static_assert(sizeof(MPlayFunc) == sizeof(void *), "jump table entries are pointer-sized");

/* The assembly's layout, where pointers are 4 bytes. At 64 bits the data is laid out as the C structs
 * (build_glue/check_data_layout.py compares it with them), so these hold for 32-bit pointers only. */

#if UINTPTR_MAX == UINT32_MAX

/* struct ToneData */
_Static_assert(offsetof(struct ToneData, wav) == 0x04, "o_ToneData_wav");
_Static_assert(offsetof(struct ToneData, attack) == 0x08, "o_ToneData_attack");
_Static_assert(offsetof(struct ToneData, decay) == 0x09, "o_ToneData_decay");
_Static_assert(offsetof(struct ToneData, sustain) == 0x0A, "o_ToneData_sustain");
_Static_assert(offsetof(struct ToneData, release) == 0x0B, "o_ToneData_release");
_Static_assert(sizeof(struct ToneData) == 0x0C, "ToneData_size, the voicegroup stride");

/* struct SoundChannel */
_Static_assert(offsetof(struct SoundChannel, wav) == 0x24, "o_SoundChannel_wav");
_Static_assert(offsetof(struct SoundChannel, currentPointer) == 0x28, "o_SoundChannel_currentPointer");
_Static_assert(offsetof(struct SoundChannel, track) == 0x2C, "o_SoundChannel_track");
_Static_assert(offsetof(struct SoundChannel, prevChannelPointer) == 0x30, "o_SoundChannel_prevChannelPointer");
_Static_assert(offsetof(struct SoundChannel, nextChannelPointer) == 0x34, "o_SoundChannel_nextChannelPointer");
_Static_assert(offsetof(struct SoundChannel, xpi) == 0x3C, "o_SoundChannel_xpi");
_Static_assert(offsetof(struct SoundChannel, xpc) == 0x3E, "o_SoundChannel_xpc");
_Static_assert(sizeof(struct SoundChannel) == 0x40, "SoundChannel_size");

/* struct CgbChannel */
_Static_assert(offsetof(struct CgbChannel, wavePointer) == 0x24, "o_CgbChannel_wavePointer");
_Static_assert(offsetof(struct CgbChannel, track) == 0x2C, "o_CgbChannel_track");
_Static_assert(offsetof(struct CgbChannel, prevChannelPointer) == 0x30, "o_CgbChannel_prevChannelPointer");
_Static_assert(offsetof(struct CgbChannel, nextChannelPointer) == 0x34, "o_CgbChannel_nextChannelPointer");
_Static_assert(sizeof(struct CgbChannel) == 0x40, "CgbChannel_size, the gCgbChans stride");

/* struct MusicPlayerTrack */
_Static_assert(offsetof(struct MusicPlayerTrack, tone) == 0x24, "o_MusicPlayerTrack_ToneData_type");
_Static_assert(offsetof(struct MusicPlayerTrack, unk_3C) == 0x3C, "o_MusicPlayerTrack_unk_3C");
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) == 0x40, "o_MusicPlayerTrack_cmdPtr");
_Static_assert(offsetof(struct MusicPlayerTrack, patternStack) == 0x44, "o_MusicPlayerTrack_patternStack");
_Static_assert(sizeof(struct MusicPlayerTrack) == 0x50,
               "MusicPlayerTrack_size, TRACK_SIZE in sound/music_player_table.inc");
_Static_assert(offsetof(struct MusicPlayerTrack, cmdPtr) == M4A_CLEAR_SIZE, "Clear64byte keeps cmdPtr");

/* struct MusicPlayerInfo: everything after songHeader moves with the pointer size. */
_Static_assert(offsetof(struct MusicPlayerInfo, status) == 0x04, "o_MusicPlayerInfo_status");
_Static_assert(offsetof(struct MusicPlayerInfo, trackCount) == 0x08, "o_MusicPlayerInfo_trackCount");
_Static_assert(offsetof(struct MusicPlayerInfo, priority) == 0x09, "o_MusicPlayerInfo_priority");
_Static_assert(offsetof(struct MusicPlayerInfo, cmd) == 0x0A, "o_MusicPlayerInfo_cmd");
_Static_assert(offsetof(struct MusicPlayerInfo, clock) == 0x0C, "o_MusicPlayerInfo_clock");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoD) == 0x1C, "o_MusicPlayerInfo_tempoD");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoU) == 0x1E, "o_MusicPlayerInfo_tempoU");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoI) == 0x20, "o_MusicPlayerInfo_tempoI");
_Static_assert(offsetof(struct MusicPlayerInfo, tempoC) == 0x22, "o_MusicPlayerInfo_tempoC");
_Static_assert(offsetof(struct MusicPlayerInfo, tracks) == 0x2C, "o_MusicPlayerInfo_tracks");
_Static_assert(offsetof(struct MusicPlayerInfo, tone) == 0x30, "o_MusicPlayerInfo_tone");
_Static_assert(offsetof(struct MusicPlayerInfo, ident) == 0x34, "o_MusicPlayerInfo_ident");
_Static_assert(offsetof(struct MusicPlayerInfo, MPlayMainNext) == 0x38, "o_MusicPlayerInfo_func");
_Static_assert(offsetof(struct MusicPlayerInfo, musicPlayerNext) == 0x3C, "o_MusicPlayerInfo_intp");
_Static_assert(sizeof(struct MusicPlayerInfo) == M4A_CLEAR_SIZE,
               "MusicPlayerInfo_size, cleared by Clear64byte in MPlayOpen");

/* struct SoundInfo */
_Static_assert(offsetof(struct SoundInfo, cgbChans) == 0x1C, "o_SoundInfo_cgbChans");
_Static_assert(offsetof(struct SoundInfo, MPlayMainHead) == 0x20, "o_SoundInfo_MPlayMainHead");
_Static_assert(offsetof(struct SoundInfo, musicPlayerHead) == 0x24, "o_SoundInfo_musicPlayerHead");
_Static_assert(offsetof(struct SoundInfo, CgbSound) == 0x28, "o_SoundInfo_CgbSound");
_Static_assert(offsetof(struct SoundInfo, CgbOscOff) == 0x2C, "o_SoundInfo_CgbOscOff");
_Static_assert(offsetof(struct SoundInfo, MidiKeyToCgbFreq) == 0x30, "o_SoundInfo_MidiKeyToCgbFreq");
_Static_assert(offsetof(struct SoundInfo, MPlayJumpTable) == 0x34, "o_SoundInfo_MPlayJumpTable");
_Static_assert(offsetof(struct SoundInfo, plynote) == 0x38, "o_SoundInfo_plynote");
_Static_assert(offsetof(struct SoundInfo, chans) == 0x50, "o_SoundInfo_chans");
_Static_assert(offsetof(struct SoundInfo, pcmBuffer) == 0x350, "o_SoundInfo_pcmBuffer");
_Static_assert(sizeof(struct SoundInfo) == 0xFB0, "SoundInfo_size");

#endif // UINTPTR_MAX == UINT32_MAX

#endif // GUARD_PLATFORM_M4A_LAYOUT_H
