/*
 * m4a engine: the sequencer. C translation of src/m4a_1.s MPlayMain,
 * TrackStop, RealClearChain, ChnVolSetAsm, clear_modM and umul3232H32.
 *
 * On the GBA, SoundMain calls MPlayMain for each open music player once a
 * frame (through SoundInfo.MPlayMainHead and MusicPlayerInfo.MPlayMainNext).
 * MPlayMain advances the player's tracks by as many ticks as the tempo
 * accumulator allows, decoding track commands (sound/MPlayDef.s) into calls
 * of the ply_* handlers and plynote, counts down gate times, runs each
 * track's LFO, and finally pushes volume and pitch changes to the channels
 * the tracks are playing. None of this touches hardware directly; the PSG
 * side goes through the SoundInfo callbacks into src/m4a.c.
 *
 * Followed: the assembly, branch for branch. Loops the assembly tests at
 * the bottom (track and channel counts) keep that shape: a count of 0
 * still runs the first iteration.
 */
#include <stdbool.h>
#include <stdint.h>

#include "m4a_engine.h"

/* MPlayMain's tempo accumulator: one tick per 150 units of tempoI. */
#define TEMPO_TICK 150u

/* MPT_FLG_START: a starting track is cleared and given these defaults. */
#define TRACK_START_BEND_RANGE 2
#define TRACK_START_VOLX 64
#define TRACK_START_LFO_SPEED 22
#define TRACK_START_TONE_TYPE 1

/* The LFO is a triangle wave of the counter lfoSpeedC: bit 7 of
 * (counter - 64) picks the half that rises through 0 (the counter itself,
 * as a signed byte) or the half that falls (128 - counter). */
#define LFO_PHASE_OFFSET 0x40u
#define LFO_PHASE_BIT 0x80u
#define LFO_FALL_BASE 0x80u
/* depth * wave >> 6 */
#define LFO_DEPTH_SHIFT 6u

/* ChnVolSetAsm: (0x80 + pan) and (0x7F - pan) weight the right and left
 * volumes; velocity * weight * volMR >> 14, saturated to 255. */
#define PAN_RIGHT_BASE 0x80
#define PAN_LEFT_BASE 0x7F
#define CHANNEL_VOL_SHIFT 14u
#define CHANNEL_VOL_MAX 0xFFu

u32 umul3232H32(u32 multiplier, u32 multiplicand)
{
    return (u32)(((u64)multiplier * multiplicand) >> 32);
}

void RealClearChain(void *x)
{
    struct MusicPlayerTrack *track = ChanTrack(x);
    void *next;
    void *prev;

    if (track == NULL)
        return;
    next = ChanNext(x);
    prev = ChanPrev(x);
    if (prev != NULL)
        ChanSetNext(prev, next);
    else
        track->chan = next;
    if (next != NULL)
        ChanSetPrev(next, prev);
    ChanSetTrack(x, NULL);
}

void clear_modM(struct MusicPlayerTrack *track)
{
    track->modM = 0;
    track->lfoSpeedC = 0;
    track->flags = (u8)(track->flags | (track->modT == 0 ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG));
}

void ChnVolSetAsm(void *chan, const struct MusicPlayerTrack *track)
{
    u32 velocity = ChanByte(chan, CHAN_VELOCITY);
    s32 pan = LowByteS8(ChanByte(chan, CHAN_RHYTHM_PAN));
    u32 right = (u32)(PAN_RIGHT_BASE + pan) * velocity * track->volMR >> CHANNEL_VOL_SHIFT;
    u32 left = (u32)(PAN_LEFT_BASE - pan) * velocity * track->volML >> CHANNEL_VOL_SHIFT;

    ChanSetByte(chan, CHAN_RIGHT_VOLUME, (u8)(right > CHANNEL_VOL_MAX ? CHANNEL_VOL_MAX : right));
    ChanSetByte(chan, CHAN_LEFT_VOLUME, (u8)(left > CHANNEL_VOL_MAX ? CHANNEL_VOL_MAX : left));
}

void TrackStop(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;

    if (!(track->flags & MPT_FLG_EXIST))
        return;
    for (void *chan = track->chan; chan != NULL; chan = ChanNext(chan)) {
        if (ChanByte(chan, CHAN_STATUS_FLAGS) != 0) {
            u8 cgbChannel = ChanByte(chan, CHAN_TYPE) & TONEDATA_TYPE_CGB;

            if (cgbChannel != 0)
                SOUND_INFO_PTR->CgbOscOff(cgbChannel);
            ChanSetByte(chan, CHAN_STATUS_FLAGS, 0);
        }
        ChanSetTrack(chan, NULL);
    }
    track->chan = NULL;
}

/* The track loops run at least once (the assembly tests the count after the body). */
static u32 TracksToRun(const struct MusicPlayerInfo *mplayInfo)
{
    return mplayInfo->trackCount != 0 ? mplayInfo->trackCount : 1;
}

/* _081DD892: a playing channel's gate time runs down to a release; an ended
 * channel leaves the chain. */
static void CountDownGateTimes(const struct MusicPlayerTrack *track)
{
    for (void *chan = track->chan; chan != NULL; chan = ChanNext(chan)) {
        u8 status = ChanByte(chan, CHAN_STATUS_FLAGS);
        u8 gateTime;

        if (!(status & SOUND_CHANNEL_SF_ON)) {
            ClearChain(chan);
            continue;
        }
        gateTime = ChanByte(chan, CHAN_GATE_TIME);
        if (gateTime == 0)
            continue;
        gateTime--;
        ChanSetByte(chan, CHAN_GATE_TIME, gateTime);
        if (gateTime == 0)
            ChanSetByte(chan, CHAN_STATUS_FLAGS, (u8)(status | SOUND_CHANNEL_SF_STOP));
    }
}

static void StartTrack(struct MusicPlayerTrack *track)
{
    Clear64byte(track);
    track->flags = MPT_FLG_EXIST;
    track->bendRange = TRACK_START_BEND_RANGE;
    track->volX = TRACK_START_VOLX;
    track->lfoSpeed = TRACK_START_LFO_SPEED;
    track->tone.type = TRACK_START_TONE_TYPE;
}

/*
 * _081DD8E0: decodes and runs one command. Returns false if a jump table
 * command ended the track (flags 0). A data byte with no running status
 * would index gClockTable 128 bytes before its start on the GBA; mid2agb
 * never writes one, and the C engine ends the track instead (README.md).
 */
static bool RunCommand(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    u32 cmd = *track->cmdPtr;

    if (cmd < MPLAY_CMD_FIRST) {
        cmd = track->runningStatus;
    } else {
        track->cmdPtr++;
        if (cmd >= MPLAY_CMD_RUNNING_FIRST)
            track->runningStatus = (u8)cmd;
    }

    if (cmd >= MPLAY_CMD_NOTE_FIRST) {
        soundInfo->plynote(cmd - MPLAY_CMD_NOTE_FIRST, mplayInfo, track);
        return true;
    }
    if (cmd >= MPLAY_CMD_TABLE_FIRST) {
        mplayInfo->cmd = (u8)(cmd - MPLAY_CMD_TABLE_FIRST);
        /* The table's type, MPlayFunc, has no prototype (its entries take
         * different arguments); the commands' entries are XcmdFunc's. */
        ((XcmdFunc)soundInfo->MPlayJumpTable[cmd - MPLAY_CMD_TABLE_FIRST])(mplayInfo, track);
        return track->flags != 0;
    }
    if (cmd < MPLAY_CMD_FIRST) {
        ply_fine(mplayInfo, track);
        return false;
    }
    track->wait = gClockTable[cmd - MPLAY_CMD_FIRST];
    return true;
}

/* _081DD95A: one LFO step; a changed output flags the volume or pitch it modulates. */
static void StepLfo(struct MusicPlayerTrack *track)
{
    u32 counter;
    u32 wave;
    u32 output;

    if (track->lfoSpeed == 0 || track->mod == 0)
        return;
    if (track->lfoDelayC != 0) {
        track->lfoDelayC--;
        return;
    }
    /* the stored counter wraps at 256, the wave uses the unwrapped sum */
    counter = (u32)track->lfoSpeedC + track->lfoSpeed;
    track->lfoSpeedC = (u8)counter;
    if ((counter - LFO_PHASE_OFFSET) & LFO_PHASE_BIT)
        wave = SignExtend8((u8)counter);
    else
        wave = LFO_FALL_BASE - counter;
    output = AsrU32(track->mod * wave, LFO_DEPTH_SHIFT);
    if ((u8)output == (u8)track->modM)
        return;
    track->modM = LowByteS8(output);
    track->flags = (u8)(track->flags | (track->modT == 0 ? MPT_FLG_PITCHG : MPT_FLG_VOLCHG));
}

/* _081DD87C: one tick of one existing track. */
static void TickTrack(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    CountDownGateTimes(track);
    if (track->flags & MPT_FLG_START)
        StartTrack(track);
    while (track->wait == 0) {
        if (!RunCommand(soundInfo, mplayInfo, track))
            return;
    }
    track->wait--;
    StepLfo(track);
}

/* _081DD874: one tick of the player. Returns false when no track is left
 * playing, after pausing the player. */
static bool Tick(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo)
{
    struct MusicPlayerTrack *track = mplayInfo->tracks;
    u32 tracks = TracksToRun(mplayInfo);
    u32 bit = 1;
    u32 playing = 0;

    for (u32 i = 0; i < tracks; i++, track++, bit <<= 1) {
        if (!(track->flags & MPT_FLG_EXIST))
            continue;
        playing |= bit;
        TickTrack(soundInfo, mplayInfo, track);
    }
    mplayInfo->clock++;
    if (playing == 0) {
        mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
        return false;
    }
    mplayInfo->status = playing;
    return true;
}

/* _081DD9F6: pushes a track's volume and pitch change to one channel. */
static void UpdateChannel(struct SoundInfo *soundInfo, struct MusicPlayerTrack *track, void *chan)
{
    u8 cgbChannel = ChanByte(chan, CHAN_TYPE) & TONEDATA_TYPE_CGB;
    u8 key;

    if (track->flags & MPT_FLG_VOLCHG) {
        ChnVolSetAsm(chan, track);
        if (cgbChannel != 0) {
            struct CgbChannel *cgb = chan;

            cgb->modify |= CGB_CHANNEL_MO_VOL;
        }
    }
    if (!(track->flags & MPT_FLG_PITCHG))
        return;
    key = SoundingKey(ChanByte(chan, CHAN_KEY), track->keyM);
    if (cgbChannel != 0) {
        struct CgbChannel *cgb = chan;

        cgb->frequency = soundInfo->MidiKeyToCgbFreq(cgbChannel, key, track->pitM);
        cgb->modify |= CGB_CHANNEL_MO_PIT;
    } else {
        struct SoundChannel *direct = chan;

        direct->frequency = MidiKeyToFreq(direct->wav, key, track->pitM);
    }
}

/* _081DD9C4: after the ticks, every track with a volume or pitch change
 * recomputes it (TrkVolPitSet) and passes it to its channels. */
static void UpdateVolumeAndPitch(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo)
{
    struct MusicPlayerTrack *track = mplayInfo->tracks;
    u32 tracks = TracksToRun(mplayInfo);

    for (u32 i = 0; i < tracks; i++, track++) {
        if (!(track->flags & MPT_FLG_EXIST) || !(track->flags & (MPT_FLG_VOLCHG | MPT_FLG_PITCHG)))
            continue;
        TrkVolPitSet(mplayInfo, track);
        for (void *chan = track->chan; chan != NULL; chan = ChanNext(chan)) {
            if (!(ChanByte(chan, CHAN_STATUS_FLAGS) & SOUND_CHANNEL_SF_ON))
                ClearChain(chan);
            else
                UpdateChannel(soundInfo, track, chan);
        }
        track->flags &= MPT_FLG_KEEP_MASK;
    }
}

/* _081DD86C: as many ticks as the tempo allows, then the volume and pitch
 * updates, unless the player stopped. tempoC is stored as 16 bits but
 * compared before truncation, as the assembly does. */
static void RunPlayer(struct SoundInfo *soundInfo, struct MusicPlayerInfo *mplayInfo)
{
    u32 tempo = (u32)mplayInfo->tempoC + mplayInfo->tempoI;

    for (;;) {
        mplayInfo->tempoC = (u16)tempo;
        if (tempo < TEMPO_TICK)
            break;
        if (!Tick(soundInfo, mplayInfo))
            return;
        tempo = (u32)mplayInfo->tempoC - TEMPO_TICK;
    }
    UpdateVolumeAndPitch(soundInfo, mplayInfo);
}

void MPlayMain(struct MusicPlayerInfo *mplayInfo)
{
    if (mplayInfo->ident != ID_NUMBER)
        return;
    mplayInfo->ident++;

    if (mplayInfo->MPlayMainNext != NULL)
        mplayInfo->MPlayMainNext(mplayInfo->musicPlayerNext);

    if (!(mplayInfo->status & MUSICPLAYER_STATUS_PAUSE)) {
        struct SoundInfo *soundInfo = SOUND_INFO_PTR;

        FadeOutBody(mplayInfo);
        if (!(mplayInfo->status & MUSICPLAYER_STATUS_PAUSE))
            RunPlayer(soundInfo, mplayInfo);
    }
    mplayInfo->ident = ID_NUMBER;
}
