// deps: m4a/m4a_mixer.c m4a/m4a_render.c m4a/m4a_sequencer.c m4a/m4a_note.c m4a/m4a_commands.c m4a/m4a_clear64.c m4a/test_m4a_support.c m4a/test_m4a_c_reference.c ../src/m4a_tables.c
/*
 * Unit tests for the m4a engine's ply_note (src/m4a_1.s ply_note and
 * ChnVolSetAsm), translated to C in m4a_note.c: note arguments, voice
 * selection through rhythm and key-split voicegroups, the choice of a
 * Direct Sound or PSG channel and the channel stealing order, and the
 * channel's start. Expected values are worked out by hand from the
 * assembly or by 64-bit reference arithmetic in the test.
 */
#include <string.h>

#include "m4a/test_m4a_support.h"
#include "ptest.h"

static struct MusicPlayerInfo sPlayer;
static struct MusicPlayerTrack sTracks[4];
static union M4aTestWave sNoteWave;

static struct SoundInfo *SetUpPlayer(uint8_t trackCount)
{
    return M4aTestSetUpPlayer(&sPlayer, sTracks, 4, trackCount);
}

/* ply_note */

static struct SoundInfo *SetUpNote(const uint8_t *args)
{
    struct SoundInfo *si = SetUpPlayer(4);

    if (si == NULL)
        return NULL;
    M4aTestMakeWave(&sNoteWave, 0, 0, NOTE_WAVE_FREQ, 0, 4);
    sPlayer.priority = 10;
    for (size_t i = 0; i < 4; i++) {
        sTracks[i].flags = MPT_FLG_EXIST;
        sTracks[i].priority = 5;
        sTracks[i].volMR = 64;
        sTracks[i].volML = 32;
        sTracks[i].key = 61;
        sTracks[i].velocity = 90;
        sTracks[i].tone.type = 0;
        sTracks[i].tone.key = 60;
        sTracks[i].tone.wav = &sNoteWave.wave;
        sTracks[i].tone.attack = 10;
        sTracks[i].tone.decay = 20;
        sTracks[i].tone.sustain = 30;
        sTracks[i].tone.release = 40;
        sTracks[i].pseudoEchoVolume = 7;
        sTracks[i].pseudoEchoLength = 8;
        sTracks[i].unk_3C = 3;
        sTracks[i].cmdPtr = (uint8_t *)(uintptr_t)args;
    }
    return si;
}

PTEST(ply_note_starts_a_direct_sound_channel)
{
    static const uint8_t args[] = {60, 100, 5};
    struct SoundInfo *si = SetUpNote(args);
    const struct SoundChannel *chan;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, args + 3);
    PT_ASSERT_EQ_U32(sTracks[0].key, 60);
    PT_ASSERT_EQ_U32(sTracks[0].velocity, 100);
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 24 + 5); /* gClockTable[24] = 24, plus 5 */
    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(chan->gateTime, 29);
    PT_ASSERT_EQ_U32(chan->midiKey, 60);
    PT_ASSERT_EQ_U32(chan->velocity, 100);
    PT_ASSERT_EQ_U32(chan->priority, 15); /* player 10 + track 5 */
    PT_ASSERT_EQ_U32(chan->key, 60);
    PT_ASSERT_EQ_U32(chan->rhythmPan, 0);
    PT_ASSERT_EQ_U32(chan->type, 0);
    PT_ASSERT_EQ_PTR(chan->wav, &sNoteWave.wave);
    PT_ASSERT_EQ_U32(chan->attack, 10);
    PT_ASSERT_EQ_U32(chan->decay, 20);
    PT_ASSERT_EQ_U32(chan->sustain, 30);
    PT_ASSERT_EQ_U32(chan->release, 40);
    PT_ASSERT_EQ_U32(chan->pseudoEchoVolume, 7);
    PT_ASSERT_EQ_U32(chan->pseudoEchoLength, 8);
    PT_ASSERT_EQ_U32(chan->count, 3);
    /* (128 * 100 * 64) >> 14 = 50, (127 * 100 * 32) >> 14 = 24 */
    PT_ASSERT_EQ_U32(chan->rightVolume, 50);
    PT_ASSERT_EQ_U32(chan->leftVolume, 24);
    /* key 60 is the sample's own rate: gFreqTable[0] >> 9 = 2^22, so
     * freq * 2^22 >> 32 = freq >> 10 = 13379 */
    PT_ASSERT_EQ_U32(chan->frequency, 13379);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, chan);
    PT_ASSERT_EQ_PTR(chan->track, &sTracks[0]);
    PT_ASSERT_EQ_PTR(chan->prevChannelPointer, NULL);
    PT_ASSERT_EQ_PTR(chan->nextChannelPointer, NULL);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);
    /* ClearChain, then TrkVolPitSet, then MidiKeyToFreq */
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 3);
    PT_ASSERT_EQ_INT(M4aTestCall(0)->kind, CALL_CLEAR_CHAIN);
    PT_ASSERT_EQ_PTR(M4aTestCall(0)->ptr0, chan);
    PT_ASSERT_EQ_INT(M4aTestCall(1)->kind, CALL_TRK_VOL_PIT_SET);
    PT_ASSERT_EQ_INT(M4aTestCall(2)->kind, CALL_MIDI_KEY_TO_FREQ);
    PT_ASSERT_EQ_U32(M4aTestCall(2)->arg0, 60);
    PT_ASSERT_EQ_U32(M4aTestCall(2)->arg1, 0);
}

PTEST(ply_note_takes_key_velocity_and_gate_only_while_bytes_are_data)
{
    static const uint8_t noArgs[] = {0x80};
    static const uint8_t keyOnly[] = {62, 0x90};
    const struct SoundInfo *si = SetUpNote(noArgs);

    PT_ASSERT(si != NULL);
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, noArgs);
    PT_ASSERT_EQ_U32(si->chans[0].midiKey, 61);
    PT_ASSERT_EQ_U32(si->chans[0].velocity, 90);
    PT_ASSERT_EQ_U32(si->chans[0].gateTime, 24);

    sTracks[1].cmdPtr = (uint8_t *)(uintptr_t)keyOnly;
    ply_note(0, &sPlayer, &sTracks[1]);
    PT_ASSERT_EQ_PTR(sTracks[1].cmdPtr, keyOnly + 1);
    PT_ASSERT_EQ_U32(si->chans[1].midiKey, 62);
    PT_ASSERT_EQ_U32(si->chans[1].velocity, 90);
    PT_ASSERT_EQ_U32(si->chans[1].gateTime, 0); /* gClockTable[0]: a tie */
}

PTEST(ply_note_puts_the_new_channel_at_the_head_of_the_chain)
{
    static const uint8_t args[] = {60, 100, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    ply_note(24, &sPlayer, &sTracks[0]);
    sTracks[0].cmdPtr = (uint8_t *)(uintptr_t)args;
    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].chan, &si->chans[1]);
    PT_ASSERT_EQ_PTR(si->chans[1].prevChannelPointer, NULL);
    PT_ASSERT_EQ_PTR(si->chans[1].nextChannelPointer, &si->chans[0]);
    PT_ASSERT_EQ_PTR(si->chans[0].prevChannelPointer, &si->chans[1]);
}

/* Busy channel k plays for track k at the given priority. */
static void OccupyChannel(struct SoundInfo *si, size_t k, uint8_t status, uint8_t priority, size_t track)
{
    struct SoundChannel *chan = &si->chans[k];
    struct SoundChannel *const one[] = {chan};

    chan->statusFlags = status;
    chan->priority = priority;
    M4aTestLinkChannels(&sTracks[track], one, 1);
}

PTEST(ply_note_takes_the_first_free_channel)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    OccupyChannel(si, 0, SF_PLAYING, 200, 1);

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].chan, &si->chans[1]);
    PT_ASSERT_EQ_U32(si->chans[0].priority, 200);
}

PTEST(ply_note_steals_the_lowest_priority_channel_preferring_later_tracks)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sPlayer.priority = 0;
    sTracks[0].priority = 15;
    OccupyChannel(si, 0, SF_PLAYING, 20, 0);
    OccupyChannel(si, 1, SF_PLAYING, 12, 1);
    OccupyChannel(si, 2, SF_PLAYING, 12, 2);
    OccupyChannel(si, 3, SF_PLAYING, 30, 3);

    ply_note(24, &sPlayer, &sTracks[0]);

    /* 12 < 15 twice; the tie goes to the higher track address, track 2 */
    PT_ASSERT_EQ_PTR(si->chans[2].track, &sTracks[0]);
    PT_ASSERT_EQ_PTR(sTracks[2].chan, NULL);
    PT_ASSERT_EQ_U32(si->chans[2].priority, 15);
    PT_ASSERT_EQ_U32(si->chans[1].priority, 12);
}

PTEST(ply_note_plays_nothing_when_every_channel_outranks_it)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sPlayer.priority = 0;
    sTracks[0].priority = 15;
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_VOLCHG;
    for (size_t k = 0; k < 4; k++)
        OccupyChannel(si, k, SF_PLAYING, 20, k);

    ply_note(24, &sPlayer, &sTracks[0]);

    for (size_t k = 0; k < 4; k++) {
        PT_ASSERT_EQ_U32(si->chans[k].priority, 20);
        PT_ASSERT_EQ_PTR(si->chans[k].track, &sTracks[k]);
    }
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_VOLCHG);
    PT_ASSERT_EQ_U32(sTracks[0].key, 60);
}

PTEST(ply_note_equal_priority_steals_only_from_tracks_at_or_after_its_own)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sPlayer.priority = 0;
    for (size_t k = 0; k < 4; k++)
        sTracks[k].priority = 15;
    si->maxChans = 2;
    OccupyChannel(si, 0, SF_PLAYING, 15, 3);
    OccupyChannel(si, 1, SF_PLAYING, 15, 1);

    /* from track 0: channel 0 (track 3 >= 0) is chosen, channel 1's track 1
     * is below the best so far (3) */
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_PTR(si->chans[0].track, &sTracks[0]);
    PT_ASSERT_EQ_PTR(si->chans[1].track, &sTracks[1]);

    /* from track 2: channel 0 now plays track 0 (< 2), channel 1 track 1 (< 2) */
    sTracks[2].cmdPtr = (uint8_t *)(uintptr_t)args;
    ply_note(24, &sPlayer, &sTracks[2]);
    PT_ASSERT_EQ_PTR(si->chans[0].track, &sTracks[0]);
    PT_ASSERT_EQ_PTR(si->chans[1].track, &sTracks[1]);
    PT_ASSERT_EQ_PTR(sTracks[2].chan, NULL);
}

PTEST(ply_note_prefers_released_channels_whatever_their_priority)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    OccupyChannel(si, 0, SF_PLAYING, 1, 0);
    OccupyChannel(si, 1, SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_ENV_SUSTAIN, 200, 1);
    OccupyChannel(si, 2, SF_PLAYING, 1, 2);
    OccupyChannel(si, 3, SF_PLAYING, 1, 3);

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(si->chans[1].track, &sTracks[0]);
}

PTEST(ply_note_among_released_channels_takes_the_lowest_priority)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    OccupyChannel(si, 0, SF_PLAYING, 1, 0);
    OccupyChannel(si, 1, SOUND_CHANNEL_SF_STOP | SOUND_CHANNEL_SF_ENV_SUSTAIN, 9, 1);
    OccupyChannel(si, 2, SF_PLAYING, 1, 2);
    OccupyChannel(si, 3, SOUND_CHANNEL_SF_STOP, 5, 3);

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(si->chans[3].track, &sTracks[0]);
    PT_ASSERT_EQ_PTR(si->chans[1].track, &sTracks[1]);
}

PTEST(ply_note_with_maxChans_0_still_looks_at_channel_0)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    si->maxChans = 0;
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &si->chans[0]);

    /* channel 0 now outranks the next note and channel 1 is never examined */
    sTracks[1].priority = 0;
    sPlayer.priority = 0;
    sTracks[1].cmdPtr = (uint8_t *)(uintptr_t)args;
    ply_note(24, &sPlayer, &sTracks[1]);
    PT_ASSERT_EQ_PTR(sTracks[1].chan, NULL);
    PT_ASSERT_EQ_U32(si->chans[1].statusFlags, 0);
}

PTEST(ply_note_scans_at_most_the_12_channels_SoundInfo_holds)
{
    /* maxChans can be set up to 15 (m4aSoundMode's 4-bit field); the
     * assembly would scan "channels" 12..14 inside pcmBuffer. */
    static const uint8_t args[] = {60, CMD_FINE};
    static uint8_t before[64];
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    si->maxChans = 15;
    sPlayer.priority = 0;
    sTracks[0].priority = 0;
    for (size_t k = 0; k < MAX_DIRECTSOUND_CHANNELS; k++) {
        si->chans[k].statusFlags = SF_PLAYING;
        si->chans[k].priority = 50;
    }
    memset(si->pcmBuffer, 0, sizeof(before)); /* channel 12 would look free */
    memcpy(before, si->pcmBuffer, sizeof(before));

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    PT_ASSERT_MEM_EQ(si->pcmBuffer, before, sizeof(before));
    PT_ASSERT_EQ_U32(sTracks[0].key, 60);
}

PTEST(ply_note_beyond_the_clock_table_plays_nothing)
{
    /* MPlayMain passes 0..48; a larger value would index past gClockTable */
    static const uint8_t args[] = {60, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].gateTime = 7;
    ply_note(CLOCK_TABLE_SIZE, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 7);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, args);
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);

    ply_note(CLOCK_TABLE_SIZE - 1, &sPlayer, &sTracks[0]); /* the last valid one */
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 96);             /* gClockTable[48] */
}

PTEST(ply_note_starts_a_psg_channel)
{
    static const uint8_t args[] = {60, 100, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);
    struct CgbChannel *cgb;

    PT_ASSERT(si != NULL);
    cgb = &M4aTestCgbChans()[0];
    sTracks[0].tone.type = 1;
    sTracks[0].tone.length = 0x22;
    sTracks[0].tone.pan_sweep = 0x35;
    sTracks[0].keyM = (uint8_t)-2;
    sTracks[0].pitM = 7;

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(cgb->statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(cgb->type, 1);
    PT_ASSERT_EQ_U32(cgb->length, 0x22);
    PT_ASSERT_EQ_U32(cgb->sweep, 0x35);
    PT_ASSERT_EQ_U32(cgb->key, 60);
    PT_ASSERT_EQ_U32(cgb->priority, 15);
    PT_ASSERT_EQ_U32(cgb->attack, 10);
    PT_ASSERT_EQ_U32(cgb->release, 40);
    PT_ASSERT_EQ_PTR(cgb->wavePointer, &sNoteWave.wave);
    PT_ASSERT_EQ_U32(cgb->rightVolume, 50);
    PT_ASSERT_EQ_U32(cgb->leftVolume, 24);
    PT_ASSERT_EQ_U32(cgb->frequency, M4aTestCgbFreqFor(1, 58, 7));
    PT_ASSERT_EQ_PTR(sTracks[0].chan, (void *)cgb);
    PT_ASSERT_EQ_PTR(cgb->track, &sTracks[0]);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, 0);
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_MIDI_KEY_TO_FREQ), 0);
}

PTEST(ply_note_psg_sweep_defaults_to_8)
{
    static const uint8_t args[] = {60, CMD_FINE};
    static const uint8_t kPanSweep[] = {0x80, 0xB5, 0x05, 0x00};
    static const uint8_t kSweep[] = {8, 8, 8, 8};

    for (size_t i = 0; i < sizeof(kPanSweep); i++) {
        const struct SoundInfo *si = SetUpNote(args);

        PT_ASSERT(si != NULL);
        sTracks[0].tone.type = 2;
        sTracks[0].tone.pan_sweep = kPanSweep[i];
        ply_note(24, &sPlayer, &sTracks[0]);
        PT_ASSERT_EQ_U32(M4aTestCgbChans()[1].sweep, kSweep[i]);
    }
}

static int PlayPsgAgainst(uint8_t status, uint8_t priority, size_t ownerTrack, size_t noteTrack)
{
    static const uint8_t args[] = {60, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);
    struct CgbChannel *cgb;

    if (si == NULL)
        return -1;
    cgb = &M4aTestCgbChans()[0];
    cgb->statusFlags = status;
    cgb->priority = priority;
    cgb->track = &sTracks[ownerTrack];
    sTracks[ownerTrack].chan = (struct SoundChannel *)(void *)cgb;
    sTracks[noteTrack].tone.type = 1;
    ply_note(24, &sPlayer, &sTracks[noteTrack]);
    return cgb->track == &sTracks[noteTrack];
}

PTEST(ply_note_psg_channel_priority_rules)
{
    /* the note's priority is 15 */
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING, 16, 3, 0), 0);
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING, 14, 3, 0), 1);
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING, 15, 0, 2), 0); /* owner track below */
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING, 15, 2, 2), 1); /* same track */
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING, 15, 3, 2), 1); /* owner track above */
    PT_ASSERT_EQ_INT(PlayPsgAgainst(SF_PLAYING | SOUND_CHANNEL_SF_STOP, 255, 3, 0), 1);
    PT_ASSERT_EQ_INT(PlayPsgAgainst(0, 255, 3, 0), 1);
}

PTEST(ply_note_psg_without_cgb_channels_plays_nothing)
{
    static const uint8_t args[] = {60, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    si->cgbChans = NULL;
    sTracks[0].tone.type = 1;
    ply_note(24, &sPlayer, &sTracks[0]);
    /* the note's arguments are still read */
    PT_ASSERT_EQ_U32(sTracks[0].key, 60);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, args + 1);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
}

PTEST(ply_note_psg_type_beyond_the_four_channels_plays_nothing)
{
    /* type & 7 = 5..7 would index past gCgbChans[4] on the GBA; the C engine
     * rejects it (README.md, "Departures from the assembly") */
    static const uint8_t args[] = {60, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].tone.type = 5;
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 24);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, args + 1);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
}

static struct ToneData sSubVoices[3];

PTEST(ply_note_rhythm_voice_uses_the_drum_key_and_pan)
{
    static const uint8_t args[] = {2, 100, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);
    const struct SoundChannel *chan;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[2].type = 0;
    sSubVoices[2].key = 72;
    sSubVoices[2].pan_sweep = 0xCA; /* (0xCA - 0xC0) * 2 = 20 */
    sSubVoices[2].wav = &sNoteWave.wave;
    sSubVoices[2].attack = 1;
    sSubVoices[2].release = 4;
    sTracks[0].tone.type = TONEDATA_TYPE_RHY;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(chan->statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(chan->midiKey, 2);
    PT_ASSERT_EQ_U32(chan->key, 72);
    PT_ASSERT_EQ_U32(chan->rhythmPan, 20);
    PT_ASSERT_EQ_U32(chan->attack, 1);
    PT_ASSERT_EQ_U32(chan->release, 4);
    /* right ((128 + 20) * 100 * 64) >> 14 = 57, left ((127 - 20) * 100 * 32) >> 14 = 20 */
    PT_ASSERT_EQ_U32(chan->rightVolume, 57);
    PT_ASSERT_EQ_U32(chan->leftVolume, 20);
    PT_ASSERT_EQ_U32(M4aTestCall(2)->arg0, 72);
}

PTEST(ply_note_rhythm_pan_extremes)
{
    static const uint8_t args[] = {2, 100, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);
    const struct SoundChannel *chan;

    PT_ASSERT(si != NULL);
    chan = &si->chans[0];
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[2].key = 72;
    sSubVoices[2].pan_sweep = 0x80; /* (0x80 - 0xC0) * 2 = -128 */
    sSubVoices[2].wav = &sNoteWave.wave;
    sTracks[0].tone.type = TONEDATA_TYPE_RHY;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;

    ply_note(24, &sPlayer, &sTracks[0]);

    /* right (0 * ...) = 0; left ((127 + 128) * 100 * 32) >> 14 = 49 */
    PT_ASSERT_EQ_U32(chan->rhythmPan, 0x80);
    PT_ASSERT_EQ_U32(chan->rightVolume, 0);
    PT_ASSERT_EQ_U32(chan->leftVolume, 49);

    sSubVoices[2].pan_sweep = 0x40; /* bit 7 clear: no pan */
    sTracks[1].tone = sTracks[0].tone;
    sTracks[1].cmdPtr = (uint8_t *)(uintptr_t)args;
    ply_note(24, &sPlayer, &sTracks[1]);
    PT_ASSERT_EQ_U32(si->chans[1].rhythmPan, 0);
}

PTEST(ply_note_volume_saturates_at_255)
{
    /* (128 + 127) * 127 * 255 >> 14 = 504 -> 255 */
    static const uint8_t args[] = {2, 127, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[2].pan_sweep = 0xFF; /* (0xFF - 0xC0) * 2 = 126 */
    sSubVoices[2].wav = &sNoteWave.wave;
    sTracks[0].tone.type = TONEDATA_TYPE_RHY;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;
    sTracks[0].volMR = 255;
    sTracks[0].volML = 255;

    ply_note(24, &sPlayer, &sTracks[0]);

    /* right (254 * 127 * 255) >> 14 = 502 -> 255; left (1 * 127 * 255) >> 14 = 1 */
    PT_ASSERT_EQ_U32(si->chans[0].rightVolume, 255);
    PT_ASSERT_EQ_U32(si->chans[0].leftVolume, 1);
}

PTEST(ply_note_key_split_picks_the_voice_from_the_table)
{
    static const uint8_t args[] = {60, 100, CMD_FINE};
    static uint8_t splitTable[128];
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    memset(splitTable, 0, sizeof(splitTable));
    splitTable[60] = 1;
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[1].key = 99;
    sSubVoices[1].wav = &sNoteWave.wave;
    sSubVoices[1].attack = 77;
    sTracks[0].tone.type = TONEDATA_TYPE_SPL;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;
    /* voice_keysplit: the table's address as .4byte in attack..release */
    PT_ASSERT(M4aTestPutPointer(&sTracks[0].tone.attack, splitTable));

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(si->chans[0].key, 60); /* a split keeps the played key */
    PT_ASSERT_EQ_U32(si->chans[0].attack, 77);
}

PTEST(ply_note_nested_split_or_rhythm_plays_nothing)
{
    static const uint8_t args[] = {1, 100, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[1].type = TONEDATA_TYPE_SPL;
    sTracks[0].tone.type = TONEDATA_TYPE_RHY;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sTracks[0].key, 1);
    PT_ASSERT_EQ_U32(sTracks[0].velocity, 100);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
}

PTEST(ply_note_restarts_the_lfo_delay)
{
    static const uint8_t args[] = {60, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].lfoDelay = 9;
    sTracks[0].modM = 33;
    sTracks[0].lfoSpeedC = 44;

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sTracks[0].lfoDelayC, 9);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 0);
    /* clear_modM asked for a pitch update; TrkVolPitSet saw it */
    PT_ASSERT_EQ_U32(M4aTestCall(1)->arg0, MPT_FLG_EXIST | MPT_FLG_PITCHG);
}

PTEST(ply_note_without_lfo_delay_keeps_the_modulation)
{
    static const uint8_t args[] = {60, CMD_FINE};
    const struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].modM = 33;
    sTracks[0].lfoDelayC = 4;
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 33);
    PT_ASSERT_EQ_U32(sTracks[0].lfoDelayC, 0);
}

PTEST_MAIN()
