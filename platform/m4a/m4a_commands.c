/*
 * m4a engine: track commands. C translation of src/m4a_1.s
 * MPlayJumpTableCopy and the ply_* handlers it installs (ply_fine, goto,
 * patt, pend, rept, prio, tempo, keysh, voice, vol, pan, bend, bendr,
 * lfodl, modt, tune, port, lfos, mod, endtie), with their argument readers
 * ld_r3_tp_adr_i and chk_adr_r2.
 *
 * MPlayMain reads a command byte (sound/MPlayDef.s) and calls
 * MPlayJumpTable[cmd - 0xB1](mplayInfo, track); each handler reads its
 * arguments from track->cmdPtr and updates the track or the player.
 * ply_port is the one that touches hardware: it writes a byte to a sound
 * register (GBATEK "GBA Sound Controller"), REG_SOUND1CNT_L plus an offset.
 *
 * Followed: the assembly. Most argument reads go through chk_adr_r2, the
 * driver's guard against reading the BIOS: a refused read gives 0.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "m4a_engine.h"

/* GBATEK "GBA Memory Map": the BIOS and the unused area below EWRAM end at
 * 0x02000000; the BIOS ROM itself is 16 KiB. */
#define BIOS_REGION_END 0x02000000u
#define BIOS_ROM_SIZE   0x4000u

/* MPlayJumpTableCopy copies the 36 entries of gMPlayJumpTableTemplate. */
#define MPLAY_JUMP_TABLE_SIZE 36u

/* Song data holds addresses as 4 bytes, little-endian (flagged 64-bit). */
#define SONG_ADDRESS_SIZE 4u
/* REPT: a count byte, then the address to repeat from. */
#define REPT_ARGS_SIZE (1u + SONG_ADDRESS_SIZE)
/* PATT nests three deep (MusicPlayerTrack.patternStack). */
#define PATTERN_STACK_DEPTH 3u
/* PORT: a register offset, then the byte to write. */
#define PORT_ARGS_SIZE 2u

/*
 * chk_adr_r2: a read below 0x02000000 is refused unless it lies in the
 * BIOS ROM at or after gMPlayJumpTableTemplate (where the BIOS-resident
 * driver kept its table). The game's template is in ROM, so every address
 * below 0x02000000 is refused. Flagged 64-bit: a pointer compared as an
 * integer.
 */
static bool IsReadAllowed(const void *address)
{
    uintptr_t value = (uintptr_t)address;

    if (value >= BIOS_REGION_END)
        return true;
    if (value < (uintptr_t)gMPlayJumpTableTemplate)
        return false;
    return value < BIOS_ROM_SIZE;
}

/* ld_r3_tp_adr_i: the next argument byte, or 0 if the guard refuses it.
 * The assembly loads the byte before checking; a refused byte is not read
 * here, which gives the same result without touching the address. */
static u8 ReadCheckedByte(struct MusicPlayerTrack *track)
{
    const u8 *arg = track->cmdPtr++;

    return IsReadAllowed(arg) ? *arg : 0;
}

/* ld_r3_tp_adr_i_unchecked */
static u8 ReadByte(struct MusicPlayerTrack *track)
{
    return *track->cmdPtr++;
}

/* ply_goto_1: jumps to the 4-byte little-endian address at cmdPtr; only
 * its low byte goes through the guard. Flagged 64-bit: an address read
 * from song data becomes a pointer. */
static void JumpToAddressAt(struct MusicPlayerTrack *track)
{
    const u8 *arg = track->cmdPtr;
    u32 target = ((u32)arg[3] << 24) | ((u32)arg[2] << 16) | ((u32)arg[1] << 8)
               | (IsReadAllowed(arg) ? arg[0] : 0u);

    track->cmdPtr = (u8 *)(uintptr_t)target;
}

static void FlagChange(struct MusicPlayerTrack *track, u8 change)
{
    track->flags = (u8)(track->flags | change);
}

void MPlayJumpTableCopy(MPlayFunc *mplayJumpTable)
{
    for (u32 i = 0; i < MPLAY_JUMP_TABLE_SIZE; i++)
    {
        void *const *entry = &gMPlayJumpTableTemplate[i];

        if (IsReadAllowed(entry))
            memcpy(&mplayJumpTable[i], entry, sizeof(mplayJumpTable[i]));
        else
            mplayJumpTable[i] = NULL;
    }
}

/* FINE: releases every playing channel, detaches the chain, ends the track. */
void ply_fine(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;

    for (void *chan = track->chan; chan != NULL; chan = ChanNext(chan))
    {
        u8 status = ChanByte(chan, CHAN_STATUS_FLAGS);

        if (status & SOUND_CHANNEL_SF_ON)
            ChanSetByte(chan, CHAN_STATUS_FLAGS, (u8)(status | SOUND_CHANNEL_SF_STOP));
        RealClearChain(chan);
    }
    track->flags = 0;
}

void ply_goto(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    JumpToAddressAt(track);
}

/* PATT: calls a pattern, remembering where to return; a fourth level ends the track. */
void ply_patt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    if (track->patternLevel >= PATTERN_STACK_DEPTH)
    {
        ply_fine(mplayInfo, track);
        return;
    }
    track->patternStack[track->patternLevel] = track->cmdPtr + SONG_ADDRESS_SIZE;
    track->patternLevel++;
    JumpToAddressAt(track);
}

void ply_pend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;

    if (track->patternLevel == 0)
        return;
    track->patternLevel--;
    track->cmdPtr = track->patternStack[track->patternLevel];
}

/* REPT: a count of 0 repeats forever; otherwise the jump is taken until
 * repN reaches the count. repN is compared before it wraps at 256. */
void ply_rept(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 *args = track->cmdPtr;
    u32 repeats;

    (void)mplayInfo;

    if (*args == 0)
    {
        track->cmdPtr = args + 1;
        JumpToAddressAt(track);
        return;
    }
    repeats = (u32)track->repN + 1;
    track->repN = (u8)repeats;
    if (repeats < ReadCheckedByte(track))
    {
        JumpToAddressAt(track);
        return;
    }
    track->repN = 0;
    track->cmdPtr = args + REPT_ARGS_SIZE;
}

void ply_prio(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->priority = ReadCheckedByte(track);
}

/* TEMPO: tempoD is twice the byte (beats per minute), scaled by tempoU / 256. */
void ply_tempo(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 tempo = (u32)ReadCheckedByte(track) << 1;

    mplayInfo->tempoD = (u16)tempo;
    mplayInfo->tempoI = (u16)(tempo * mplayInfo->tempoU >> 8);
}

void ply_keysh(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->keyShift = LowByteS8(ReadCheckedByte(track));
    FlagChange(track, MPT_FLG_PITCHG);
}

/* VOICE: copies a voice from the player's voicegroup; all of it reads as 0
 * if the guard refuses the voice's address. */
void ply_voice(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    const struct ToneData *tone = &mplayInfo->tone[ReadByte(track)];

    if (IsReadAllowed(tone))
        track->tone = *tone;
    else
        memset(&track->tone, 0, sizeof(track->tone));
}

void ply_vol(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->vol = ReadCheckedByte(track);
    FlagChange(track, MPT_FLG_VOLCHG);
}

/* PAN, BEND and TUNE are stored relative to C_V, the centre value 0x40. */
void ply_pan(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->pan = LowByteS8((u32)ReadCheckedByte(track) - C_V);
    FlagChange(track, MPT_FLG_VOLCHG);
}

void ply_bend(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bend = LowByteS8((u32)ReadCheckedByte(track) - C_V);
    FlagChange(track, MPT_FLG_PITCHG);
}

void ply_bendr(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->bendRange = ReadCheckedByte(track);
    FlagChange(track, MPT_FLG_PITCHG);
}

void ply_lfodl(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->lfoDelay = ReadCheckedByte(track);
}

void ply_modt(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 modT = ReadCheckedByte(track);

    (void)mplayInfo;
    if (track->modT == modT)
        return;
    track->modT = modT;
    FlagChange(track, MPT_FLG_VOLCHG | MPT_FLG_PITCHG);
}

void ply_tune(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->tune = LowByteS8((u32)ReadCheckedByte(track) - C_V);
    FlagChange(track, MPT_FLG_PITCHG);
}

/* PORT: writes a byte to the sound register at REG_SOUND1CNT_L + offset.
 * With a byte offset this stays inside the I/O page (GBATEK "GBA I/O Map");
 * the native platform maps that page at REG_BASE. */
void ply_port(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 *args = track->cmdPtr;
    volatile u8 *reg = (volatile u8 *)(uintptr_t)(REG_ADDR_SOUND1CNT_L + args[0]);

    (void)mplayInfo;
    track->cmdPtr = args + PORT_ARGS_SIZE;
    *reg = IsReadAllowed(args + 1) ? args[1] : 0;
}

void ply_lfos(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->lfoSpeed = ReadByte(track);
    if (track->lfoSpeed == 0)
        clear_modM(track);
}

void ply_mod(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    track->mod = ReadByte(track);
    if (track->mod == 0)
        clear_modM(track);
}

/* EOT: releases the first channel still sounding the key (the byte, if it
 * is one, else the track's last key). */
void ply_endtie(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u8 key = *track->cmdPtr;

    (void)mplayInfo;
    if (key < MPLAY_CMD_FIRST)
    {
        track->key = key;
        track->cmdPtr++;
    }
    else
    {
        key = track->key;
    }

    for (void *chan = track->chan; chan != NULL; chan = ChanNext(chan))
    {
        u8 status = ChanByte(chan, CHAN_STATUS_FLAGS);

        if ((status & (SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_ENV))
            && !(status & SOUND_CHANNEL_SF_STOP)
            && ChanByte(chan, CHAN_MIDI_KEY) == key)
        {
            ChanSetByte(chan, CHAN_STATUS_FLAGS, (u8)(status | SOUND_CHANNEL_SF_STOP));
            return;
        }
    }
}
