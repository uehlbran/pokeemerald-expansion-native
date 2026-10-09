/*
 * m4a engine: notes. C translation of src/m4a_1.s ply_note, the handler of
 * the note commands (TIE and N1..N96, 0xCF..0xFF), reached through
 * SoundInfo.plynote.
 *
 * ply_note reads the note's key, velocity and gate-time extension, picks
 * the voice (one level into a rhythm or key-split voicegroup), finds a
 * channel for it and starts it. A PSG voice gets the PSG channel it names
 * unless that channel is busy at a higher priority. A Direct Sound voice
 * gets a free channel, else a released one, else the busy channel of
 * lowest priority if it does not outrank the note; ties go to the channel
 * whose track lies at the higher address, then to the later channel. The
 * started channel is linked at the head of the track's chain and flagged
 * SOUND_CHANNEL_SF_START for the mixer (Direct Sound) or CgbSound (PSG).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "m4a_engine.h"

/* MusicPlayerInfo.priority + MusicPlayerTrack.priority, saturated. */
#define PRIORITY_MAX 0xFFu

/* ToneData.pan_sweep of a rhythm voice: bit 7 marks a pan, and
 * (pan_sweep - TONEDATA_P_S_PAN) * 2 is the channel's rhythmPan. */
#define TONEDATA_PAN_FLAG 0x80u
#define RHYTHM_PAN_SHIFT 1u

/* ToneData.pan_sweep of a PSG voice is NR10 (GBATEK "Sound Channel 1 -
 * Tone & Sweep"): bits 4-6 the sweep time. Without a sweep time (or with a
 * pan instead) the channel gets 0x08: time 0, no sweep. */
#define PSG_SWEEP_TIME_MASK 0x70u
#define PSG_SWEEP_OFF 0x08u

/* The voice a note plays and the key and pan it plays it with. */
struct NoteVoice {
    const struct ToneData *tone;
    u8 key;
    u8 rhythmPan;
};

/*
 * The assembly compares track pointers as unsigned addresses to break
 * priority ties, so the tie-break follows the order in which the linker
 * placed the track arrays (sound/music_player_table.inc and src/m4a.c's
 * gPokemonCryTracks). Flagged 64-bit: a pointer converted to an integer.
 */
static uintptr_t TrackAddress(const struct MusicPlayerTrack *track)
{
    return (uintptr_t)track;
}

/*
 * voice_keysplit (asm/macros/music_voice.inc) stores the key-split table's
 * address as .4byte in attack..release, little-endian; the assembly loads
 * it as one word. Flagged 64-bit: a 4-byte address, which on a 64-bit host
 * holds only for data linked below 4 GiB, as the native build links it
 * (non-PIE, image at 0x40000000).
 */
static const u8 *KeySplitTable(const struct ToneData *tone)
{
    u32 address = ComposeLe32(tone->attack, tone->decay, tone->sustain, tone->release);

    return (const u8 *)(uintptr_t)address;
}

/* Key, velocity and gate-time extension, each present only while the
 * bytes stay below MPLAY_CMD_FIRST. */
static void ReadNoteArguments(struct MusicPlayerTrack *track)
{
    u8 *cmd = track->cmdPtr;

    if (*cmd >= MPLAY_CMD_FIRST)
        return;
    track->key = *cmd++;
    if (*cmd < MPLAY_CMD_FIRST) {
        track->velocity = *cmd++;
        if (*cmd < MPLAY_CMD_FIRST)
            track->gateTime = (u8)(track->gateTime + *cmd++);
    }
    track->cmdPtr = cmd;
}

/* The track's voice, or for a rhythm or key-split voice the entry it
 * points to. Returns false for a nested group, which plays nothing. */
static bool SelectVoice(const struct MusicPlayerTrack *track, struct NoteVoice *voice)
{
    const struct ToneData *tone = &track->tone;
    const struct ToneData *group;
    u8 index = track->key;

    voice->tone = tone;
    voice->key = track->key;
    voice->rhythmPan = 0;
    if (!(tone->type & (TONEDATA_TYPE_RHY | TONEDATA_TYPE_SPL)))
        return true;

    if (tone->type & TONEDATA_TYPE_SPL)
        index = KeySplitTable(tone)[track->key];
    /* a rhythm or key-split voice's wav points to its voicegroup */
    group = (const struct ToneData *)(const void *)tone->wav;
    voice->tone = &group[index];
    if (voice->tone->type & (TONEDATA_TYPE_SPL | TONEDATA_TYPE_RHY))
        return false;
    if (tone->type & TONEDATA_TYPE_RHY) {
        if (voice->tone->pan_sweep & TONEDATA_PAN_FLAG)
            voice->rhythmPan = (u8)(((u32)voice->tone->pan_sweep - TONEDATA_P_S_PAN) << RHYTHM_PAN_SHIFT);
        voice->key = voice->tone->key;
    }
    return true;
}

/* _081DDBC4. A voice naming a PSG channel beyond the four src/m4a.c
 * provides would index past gCgbChans on the GBA; it plays nothing here. */
static struct CgbChannel *FindCgbChannel(struct SoundInfo *soundInfo, u8 cgbChannel, u32 priority,
                                         const struct MusicPlayerTrack *track)
{
    struct CgbChannel *chan;

    if (soundInfo->cgbChans == NULL || cgbChannel > NUM_CGB_CHANNELS)
        return NULL;
    chan = &soundInfo->cgbChans[cgbChannel - 1];
    if ((chan->statusFlags & SOUND_CHANNEL_SF_ON) && !(chan->statusFlags & SOUND_CHANNEL_SF_STOP)) {
        if (chan->priority > priority)
            return NULL;
        if (chan->priority == priority && TrackAddress(chan->track) < TrackAddress(track))
            return NULL;
    }
    return chan;
}

/* _081DDBEC */
static struct SoundChannel *FindDirectChannel(struct SoundInfo *soundInfo, u32 priority,
                                              const struct MusicPlayerTrack *track)
{
    struct SoundChannel *chosen = NULL;
    u32 bestPriority = priority;
    uintptr_t bestTrack = TrackAddress(track);
    bool releasedFound = false;
    u32 channels = DirectChannelsToScan(soundInfo);

    for (u32 i = 0; i < channels; i++) {
        struct SoundChannel *chan = &soundInfo->chans[i];
        u8 status = chan->statusFlags;

        if (!(status & SOUND_CHANNEL_SF_ON))
            return chan;
        if (status & SOUND_CHANNEL_SF_STOP) {
            if (!releasedFound) {
                /* the first released channel is taken whatever its priority */
                releasedFound = true;
                bestPriority = chan->priority;
                bestTrack = TrackAddress(chan->track);
                chosen = chan;
                continue;
            }
        } else if (releasedFound) {
            continue;
        }

        if (chan->priority < bestPriority) {
            bestPriority = chan->priority;
            bestTrack = TrackAddress(chan->track);
            chosen = chan;
        } else if (chan->priority == bestPriority && TrackAddress(chan->track) >= bestTrack) {
            bestTrack = TrackAddress(chan->track);
            chosen = chan;
        }
    }
    return chosen;
}

/* _081DDC40: out of its old chain, in at the head of the track's. */
static void LinkChannel(void *chan, struct MusicPlayerTrack *track)
{
    void *head;

    ClearChain(chan);
    ChanSetPrev(chan, NULL);
    head = track->chan;
    ChanSetNext(chan, head);
    if (head != NULL)
        ChanSetPrev(head, chan);
    track->chan = chan;
    ChanSetTrack(chan, track);
}

/* _081DDC66: the fields both kinds of channel take from the note. */
static void SetNoteFields(void *chan, const struct MusicPlayerTrack *track, const struct NoteVoice *voice, u32 priority)
{
    const struct ToneData *tone = voice->tone;

    /* The assembly copies gateTime, key, velocity and runningStatus as one
     * word into gateTime, midiKey, velocity and priority, then overwrites
     * priority. */
    ChanSetByte(chan, CHAN_GATE_TIME, track->gateTime);
    ChanSetByte(chan, CHAN_MIDI_KEY, track->key);
    ChanSetByte(chan, CHAN_VELOCITY, track->velocity);
    ChanSetByte(chan, CHAN_PRIORITY, (u8)priority);
    ChanSetByte(chan, CHAN_KEY, voice->key);
    ChanSetByte(chan, CHAN_RHYTHM_PAN, voice->rhythmPan);
    ChanSetByte(chan, CHAN_TYPE, tone->type);
    /* SoundChannel.wav or CgbChannel.wavePointer: the voice's word as it is */
    // NOLINTNEXTLINE(bugprone-sizeof-expression): the pointer word is what is copied
    memcpy((u8 *)chan + offsetof(struct SoundChannel, wav), &tone->wav, sizeof(tone->wav));
    ChanSetByte(chan, CHAN_ATTACK, tone->attack);
    ChanSetByte(chan, CHAN_DECAY, tone->decay);
    ChanSetByte(chan, CHAN_SUSTAIN, tone->sustain);
    ChanSetByte(chan, CHAN_RELEASE, tone->release);
    ChanSetByte(chan, CHAN_PSEUDO_ECHO_VOLUME, track->pseudoEchoVolume);
    ChanSetByte(chan, CHAN_PSEUDO_ECHO_LENGTH, track->pseudoEchoLength);
}

static u8 PsgSweep(u8 panSweep)
{
    if ((panSweep & TONEDATA_PAN_FLAG) || !(panSweep & PSG_SWEEP_TIME_MASK))
        return PSG_SWEEP_OFF;
    return panSweep;
}

void ply_note(u32 note_cmd, struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    struct SoundInfo *soundInfo = SOUND_INFO_PTR;
    struct NoteVoice voice;
    u32 priority;
    u8 cgbChannel;
    void *chan;
    u8 key;

    /* MPlayMain passes 0..48; gClockTable has a length for each */
    if (note_cmd >= MPLAY_CLOCK_TABLE_SIZE)
        return;
    track->gateTime = gClockTable[note_cmd];
    ReadNoteArguments(track);
    if (!SelectVoice(track, &voice))
        return;

    priority = (u32)mplayInfo->priority + track->priority;
    if (priority > PRIORITY_MAX)
        priority = PRIORITY_MAX;
    cgbChannel = voice.tone->type & TONEDATA_TYPE_CGB;
    if (cgbChannel != 0)
        chan = FindCgbChannel(soundInfo, cgbChannel, priority, track);
    else
        chan = FindDirectChannel(soundInfo, priority, track);
    if (chan == NULL)
        return;

    LinkChannel(chan, track);
    track->lfoDelayC = track->lfoDelay;
    if (track->lfoDelay != 0)
        clear_modM(track);
    TrkVolPitSet(mplayInfo, track);
    SetNoteFields(chan, track, &voice, priority);
    ChnVolSetAsm(chan, track);

    key = SoundingKey(ChanByte(chan, CHAN_KEY), track->keyM);
    if (cgbChannel != 0) {
        struct CgbChannel *cgb = chan;

        cgb->length = voice.tone->length;
        cgb->sweep = PsgSweep(voice.tone->pan_sweep);
        cgb->frequency = soundInfo->MidiKeyToCgbFreq(cgbChannel, key, track->pitM);
    } else {
        struct SoundChannel *direct = chan;

        direct->count = track->unk_3C;
        direct->frequency = MidiKeyToFreq(direct->wav, key, track->pitM);
    }
    ChanSetByte(chan, CHAN_STATUS_FLAGS, SOUND_CHANNEL_SF_START);
    track->flags &= MPT_FLG_KEEP_MASK;
}
