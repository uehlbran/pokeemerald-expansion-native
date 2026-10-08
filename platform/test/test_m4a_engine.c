// deps: m4a/m4a_mixer.c m4a/m4a_render.c m4a/m4a_sequencer.c m4a/m4a_note.c m4a/m4a_commands.c m4a/m4a_clear64.c m4a/test_m4a_support.c m4a/test_m4a_c_reference.c ../src/m4a_tables.c
/*
 * Unit tests for the m4a engine's sequencer (src/m4a_1.s umul3232H32,
 * SoundMainBTM, RealClearChain, MPlayJumpTableCopy, TrackStop, MPlayMain
 * and ChnVolSetAsm), translated to C in m4a_sequencer.c, m4a_commands.c
 * and m4a_clear64.c. Expected values are worked out by hand from the
 * assembly (the arithmetic is in the comments) or by 64-bit reference
 * arithmetic in the test.
 */
#include <string.h>

#include "m4a/test_m4a_support.h"
#include "ptest.h"

extern void *const gMPlayJumpTableTemplate[];

static struct MusicPlayerInfo sPlayer;
static struct MusicPlayerTrack sTracks[4];
static union M4aTestWave sNoteWave;

static struct SoundInfo *SetUpPlayer(uint8_t trackCount)
{
    return M4aTestSetUpPlayer(&sPlayer, sTracks, 4, trackCount);
}

/* Three Direct Sound channels A, B, C chained in that order on track 0. */
static struct SoundChannel sChanA;
static struct SoundChannel sChanB;
static struct SoundChannel sChanC;

static void SetUpChain(void)
{
    struct SoundChannel *const chans[] = {&sChanA, &sChanB, &sChanC};

    memset(&sChanA, 0, sizeof(sChanA));
    memset(&sChanB, 0, sizeof(sChanB));
    memset(&sChanC, 0, sizeof(sChanC));
    memset(sTracks, 0, sizeof(sTracks));
    M4aTestLinkChannels(&sTracks[0], chans, 3);
}

/* umul3232H32 */

PTEST(umul3232H32_hand_values)
{
    PT_ASSERT_EQ_U32(umul3232H32(0, 0), 0);
    PT_ASSERT_EQ_U32(umul3232H32(0xFFFFFFFFu, 1), 0);
    PT_ASSERT_EQ_U32(umul3232H32(0x80000000u, 2), 1);
    PT_ASSERT_EQ_U32(umul3232H32(0x10000u, 0x10000u), 1);
    PT_ASSERT_EQ_U32(umul3232H32(0x80000000u, 0x80000000u), 0x40000000u);
    PT_ASSERT_EQ_U32(umul3232H32(0xFFFFFFFFu, 0xFFFFFFFFu), 0xFFFFFFFEu);
}

PTEST(umul3232H32_matches_64_bit_multiplication_at_boundaries)
{
    static const uint32_t kValues[] = {
        0, 1, 2, 3, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u,
        0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0x12345678u, 0x9ABCDEF0u, 0x400000u,
    };
    const size_t n = sizeof(kValues) / sizeof(kValues[0]);

    for (size_t i = 0; i < n; i++)
    {
        for (size_t j = 0; j < n; j++)
        {
            uint32_t expected = (uint32_t)(((uint64_t)kValues[i] * kValues[j]) >> 32);

            PT_ASSERT_EQ_U32(umul3232H32(kValues[i], kValues[j]), expected);
        }
    }
}

/* SoundMainBTM, reached as the game reaches it: Clear64byte -> gMPlayJumpTable[35] */

PTEST(SoundMainBTM_zeroes_exactly_64_bytes)
{
    static uint32_t words[18];

    PT_ASSERT(M4aTestSetUp() != NULL);
    for (size_t i = 0; i < 18; i++)
        words[i] = 0xA5A5A5A5u;

    Clear64byte(&words[1]);

    PT_ASSERT_EQ_U32(words[0], 0xA5A5A5A5u);
    for (size_t i = 1; i <= 16; i++)
        PT_ASSERT_EQ_U32(words[i], 0);
    PT_ASSERT_EQ_U32(words[17], 0xA5A5A5A5u);
}

/* RealClearChain */

PTEST(RealClearChain_unlinks_a_middle_channel)
{
    SetUpChain();
    RealClearChain(&sChanB);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &sChanA);
    PT_ASSERT_EQ_PTR(sChanA.nextChannelPointer, &sChanC);
    PT_ASSERT_EQ_PTR(sChanC.prevChannelPointer, &sChanA);
    PT_ASSERT_EQ_PTR(sChanB.track, NULL);
    /* the removed channel's own links are left as they were */
    PT_ASSERT_EQ_PTR(sChanB.prevChannelPointer, &sChanA);
    PT_ASSERT_EQ_PTR(sChanB.nextChannelPointer, &sChanC);
}

PTEST(RealClearChain_unlinks_the_head)
{
    SetUpChain();
    RealClearChain(&sChanA);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &sChanB);
    PT_ASSERT_EQ_PTR(sChanB.prevChannelPointer, NULL);
    PT_ASSERT_EQ_PTR(sChanA.track, NULL);
}

PTEST(RealClearChain_unlinks_the_tail)
{
    SetUpChain();
    RealClearChain(&sChanC);
    PT_ASSERT_EQ_PTR(sChanB.nextChannelPointer, NULL);
    PT_ASSERT_EQ_PTR(sChanC.track, NULL);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &sChanA);
}

PTEST(RealClearChain_ignores_a_channel_without_a_track)
{
    SetUpChain();
    sChanB.track = NULL;
    RealClearChain(&sChanB);
    PT_ASSERT_EQ_PTR(sChanA.nextChannelPointer, &sChanB);
    PT_ASSERT_EQ_PTR(sChanC.prevChannelPointer, &sChanB);

    /* control: with its track back it is unlinked */
    sChanB.track = &sTracks[0];
    RealClearChain(&sChanB);
    PT_ASSERT_EQ_PTR(sChanA.nextChannelPointer, &sChanC);
}

PTEST(ClearChain_reaches_RealClearChain_and_unlinks_cgb_channels)
{
    struct CgbChannel *cgb;

    PT_ASSERT(M4aTestSetUp() != NULL);
    cgb = &M4aTestCgbChans()[1];
    SetUpChain();
    sTracks[0].chan = (struct SoundChannel *)(void *)cgb;
    cgb->track = &sTracks[0];
    cgb->prevChannelPointer = NULL;
    cgb->nextChannelPointer = &sChanA;
    sChanA.prevChannelPointer = cgb;

    ClearChain(&sChanA);
    PT_ASSERT_EQ_PTR(cgb->nextChannelPointer, &sChanB);
    PT_ASSERT_EQ_PTR(sChanB.prevChannelPointer, cgb);

    ClearChain(cgb);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &sChanB);
    PT_ASSERT_EQ_PTR(cgb->track, NULL);
}

/* MPlayJumpTableCopy */

PTEST(MPlayJumpTableCopy_copies_the_36_template_entries)
{
    static MPlayFunc table[37];

    memset(table, 0, sizeof(table));
    table[36] = TestMPlayNext;

    MPlayJumpTableCopy(table);

    for (size_t i = 0; i < 36; i++)
        PT_ASSERT_MEM_EQ(&table[i], &gMPlayJumpTableTemplate[i], sizeof(void *));
    PT_ASSERT(table[36] == TestMPlayNext);
}

/* TrackStop */

PTEST(TrackStop_ignores_a_track_that_does_not_exist)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    SetUpChain();
    sChanA.statusFlags = SF_PLAYING;
    sTracks[0].flags = 0;

    TrackStop(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sChanA.statusFlags, SF_PLAYING);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, &sChanA);
    PT_ASSERT_EQ_PTR(sChanA.track, &sTracks[0]);

    /* control: once the track exists it is stopped */
    sTracks[0].flags = MPT_FLG_EXIST;
    TrackStop(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sChanA.statusFlags, 0);
}

PTEST(TrackStop_silences_and_detaches_every_channel)
{
    struct CgbChannel *cgb;

    PT_ASSERT(SetUpPlayer(1) != NULL);
    cgb = &M4aTestCgbChans()[2];
    SetUpChain();
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].chan = (struct SoundChannel *)(void *)cgb;
    cgb->statusFlags = SF_PLAYING;
    cgb->type = 3;
    cgb->track = &sTracks[0];
    cgb->nextChannelPointer = &sChanA;
    sChanA.prevChannelPointer = cgb;
    sChanA.statusFlags = SOUND_CHANNEL_SF_START;
    sChanB.statusFlags = 0;
    sChanC.statusFlags = SOUND_CHANNEL_SF_LOOP;

    TrackStop(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(cgb->statusFlags, 0);
    PT_ASSERT_EQ_U32(sChanA.statusFlags, 0);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, 0);
    PT_ASSERT_EQ_U32(sChanC.statusFlags, 0);
    PT_ASSERT_EQ_PTR(cgb->track, NULL);
    PT_ASSERT_EQ_PTR(sChanA.track, NULL);
    PT_ASSERT_EQ_PTR(sChanB.track, NULL);
    PT_ASSERT_EQ_PTR(sChanC.track, NULL);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    /* only the sounding PSG channel's oscillator is turned off */
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_CGB_OSC_OFF), 1);
    PT_ASSERT_EQ_U32(M4aTestCall(0)->arg0, 3);
}

/* MPlayMain: gate, chaining, tempo */

PTEST(MPlayMain_does_nothing_when_ident_is_not_ID_NUMBER)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sPlayer.ident = ID_NUMBER + 1;
    sPlayer.MPlayMainNext = TestMPlayNext;
    sPlayer.musicPlayerNext = &sPlayer;
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 5;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);
    PT_ASSERT_EQ_U32(sPlayer.ident, ID_NUMBER + 1);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 5);

    /* control: unlocked, the player ticks */
    sPlayer.ident = ID_NUMBER;
    MPlayMain(&sPlayer);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 4);
}

PTEST(MPlayMain_runs_the_next_player_first_and_stops_when_paused)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sPlayer.MPlayMainNext = TestMPlayNext;
    sPlayer.musicPlayerNext = &sPlayer;
    sPlayer.status = MUSICPLAYER_STATUS_PAUSE;
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 5;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(M4aTestCallCount(), 1);
    PT_ASSERT_EQ_INT(M4aTestCall(0)->kind, CALL_MPLAY_NEXT);
    PT_ASSERT_EQ_PTR(M4aTestCall(0)->ptr0, &sPlayer);
    /* the player is locked while the next one runs */
    PT_ASSERT_EQ_U32(M4aTestCall(0)->playerIdentAtCall, ID_NUMBER + 1);
    PT_ASSERT_EQ_U32(sPlayer.ident, ID_NUMBER);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 5);
}

static void PauseHook(struct MusicPlayerInfo *mplayInfo)
{
    mplayInfo->status = MUSICPLAYER_STATUS_PAUSE;
}

PTEST(MPlayMain_stops_when_the_fade_pauses_the_player)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 5;
    M4aTestSetFadeOutBodyHook(PauseHook);

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_FADE_OUT_BODY), 1);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 5);
    PT_ASSERT_EQ_U32(sPlayer.clock, 0);
    PT_ASSERT_EQ_U32(sPlayer.ident, ID_NUMBER);
}

PTEST(MPlayMain_ticks_once_per_150_tempo_units)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sPlayer.tempoI = 75;

    MPlayMain(&sPlayer); /* 75: no tick */
    PT_ASSERT_EQ_U32(sPlayer.tempoC, 75);
    PT_ASSERT_EQ_U32(sPlayer.clock, 0);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 100);

    MPlayMain(&sPlayer); /* 150: one tick */
    PT_ASSERT_EQ_U32(sPlayer.tempoC, 0);
    PT_ASSERT_EQ_U32(sPlayer.clock, 1);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 99);

    sPlayer.tempoI = 300; /* two ticks in one call */
    MPlayMain(&sPlayer);
    PT_ASSERT_EQ_U32(sPlayer.tempoC, 0);
    PT_ASSERT_EQ_U32(sPlayer.clock, 3);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 97);

    sPlayer.tempoC = 140; /* 140 + 20 = 160: one tick, 10 carried */
    sPlayer.tempoI = 20;
    MPlayMain(&sPlayer);
    PT_ASSERT_EQ_U32(sPlayer.tempoC, 10);
    PT_ASSERT_EQ_U32(sPlayer.clock, 4);
}

/* MPlayMain: command decoding */

PTEST(MPlayMain_starts_a_track_runs_commands_and_pauses_at_the_end)
{
    static uint8_t song[] = {CMD_VOL, 100, CMD_W02, CMD_FINE};

    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_START;
    sTracks[0].cmdPtr = song;
    sTracks[0].runningStatus = 0x55;
    sTracks[0].pan = 9;

    MPlayMain(&sPlayer); /* tick 1: start, VOL 100, wait 2 - 1 */
    PT_ASSERT_EQ_U32(M4aTestCall(0)->kind, CALL_FADE_OUT_BODY);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->kind, CALL_CLEAR_64_BYTE);
    PT_ASSERT_EQ_PTR(M4aTestCall(1)->ptr0, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].pan, 0); /* s8, relative to C_V: compared sign-extended */
    PT_ASSERT_EQ_U32(sTracks[0].bendRange, 2);
    PT_ASSERT_EQ_U32(sTracks[0].volX, 64);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeed, 22);
    PT_ASSERT_EQ_U32(sTracks[0].tone.type, 1);
    PT_ASSERT_EQ_U32(sTracks[0].vol, 100);
    PT_ASSERT_EQ_U32(sTracks[0].runningStatus, CMD_VOL);
    PT_ASSERT_EQ_U32(sPlayer.cmd, CMD_VOL - CMD_FINE);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 1);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, song + 3);
    PT_ASSERT_EQ_U32(sPlayer.status, 1);
    PT_ASSERT_EQ_U32(sPlayer.clock, 1);
    /* the volume change reached TrkVolPitSet: vol 100 * volX 64 >> 5 = 200,
     * centred: right (128 * 200) >> 8 = 100, left (127 * 200) >> 8 = 99 */
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_TRK_VOL_PIT_SET), 1);
    PT_ASSERT_EQ_U32(sTracks[0].volMR, 100);
    PT_ASSERT_EQ_U32(sTracks[0].volML, 99);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);

    MPlayMain(&sPlayer); /* tick 2: wait 1 -> 0 */
    PT_ASSERT_EQ_U32(sTracks[0].wait, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, song + 3);

    MPlayMain(&sPlayer); /* tick 3: FINE ends the track; it still counted as playing */
    PT_ASSERT_EQ_U32(sTracks[0].flags, 0);
    PT_ASSERT_EQ_U32(sPlayer.status, 1);

    MPlayMain(&sPlayer); /* tick 4: nothing plays: paused */
    PT_ASSERT_EQ_U32(sPlayer.status, MUSICPLAYER_STATUS_PAUSE);
    PT_ASSERT_EQ_U32(sPlayer.clock, 4);

    MPlayMain(&sPlayer); /* paused: no tick */
    PT_ASSERT_EQ_U32(sPlayer.clock, 4);
    PT_ASSERT_EQ_U32(sPlayer.ident, ID_NUMBER);
}

PTEST(MPlayMain_data_bytes_repeat_the_running_status_command)
{
    static uint8_t song[] = {CMD_VOL, 100, 50, CMD_W01, CMD_FINE};

    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].cmdPtr = song;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sTracks[0].vol, 50);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, song + 4);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 0);
}

PTEST(MPlayMain_commands_below_VOICE_do_not_become_the_running_status)
{
    static uint8_t song[] = {CMD_TEMPO, 100, CMD_W01};

    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].cmdPtr = song;
    sTracks[0].runningStatus = CMD_VOL;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sTracks[0].runningStatus, CMD_VOL);
    PT_ASSERT_EQ_U32(sPlayer.tempoD, 200); /* 100 * 2 */
    PT_ASSERT_EQ_U32(sPlayer.tempoI, 200); /* 200 * 0x100 >> 8 */
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, song + 3);
}

PTEST(MPlayMain_note_commands_go_to_plynote)
{
    static uint8_t song[] = {CMD_N24, CMD_W01};
    struct SoundInfo *si = SetUpPlayer(1);

    PT_ASSERT(si != NULL);
    si->plynote = TestPlyNote;
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].cmdPtr = song;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_PLY_NOTE), 1);
    PT_ASSERT_EQ_INT(M4aTestCall(1)->kind, CALL_PLY_NOTE);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->arg0, 24);
    PT_ASSERT_EQ_PTR(M4aTestCall(1)->ptr0, &sPlayer);
    PT_ASSERT_EQ_PTR(M4aTestCall(1)->ptr1, &sTracks[0]);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->playerIdentAtCall, ID_NUMBER + 1);
    PT_ASSERT_EQ_U32(sTracks[0].runningStatus, CMD_N24);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 0);
}

PTEST(MPlayMain_a_data_byte_before_any_running_status_ends_the_track)
{
    /* The assembly would wait gClockTable[0x30 - 0x80], 80 bytes before the
     * table; the C engine ends the track (README.md, departures). */
    static uint8_t song[] = {0x30, CMD_W01};

    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_START;
    sTracks[0].cmdPtr = song;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sTracks[0].flags, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, song);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 0);
    PT_ASSERT_EQ_U32(sPlayer.status, 1);
}

PTEST(MPlayMain_status_has_one_bit_per_existing_track)
{
    PT_ASSERT(SetUpPlayer(3) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 5;
    sTracks[2].flags = MPT_FLG_EXIST;
    sTracks[2].wait = 5;
    sTracks[1].wait = 5;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sPlayer.status, 5);
    PT_ASSERT_EQ_U32(sTracks[0].wait, 4);
    PT_ASSERT_EQ_U32(sTracks[1].wait, 5);
    PT_ASSERT_EQ_U32(sTracks[2].wait, 4);
}

PTEST(MPlayMain_with_track_count_0_still_runs_the_first_track)
{
    /* both track loops in the assembly test the count after the body */
    PT_ASSERT(SetUpPlayer(0) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 5;
    sTracks[1].flags = MPT_FLG_EXIST;
    sTracks[1].wait = 5;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sTracks[0].wait, 4);
    PT_ASSERT_EQ_U32(sTracks[1].wait, 5);
    PT_ASSERT_EQ_U32(sPlayer.status, 1);
}

/* MPlayMain: gate times and channel chains */

PTEST(MPlayMain_counts_gate_times_down_and_releases_at_zero)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    SetUpChain();
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 10;
    sChanA.statusFlags = SF_PLAYING;
    sChanA.gateTime = 2;
    sChanB.statusFlags = SF_PLAYING;
    sChanB.gateTime = 0; /* a tie: never released by the gate */
    sChanC.statusFlags = 0; /* finished: unlinked */

    MPlayMain(&sPlayer);
    PT_ASSERT_EQ_U32(sChanA.gateTime, 1);
    PT_ASSERT_EQ_U32(sChanA.statusFlags, SF_PLAYING);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, SF_PLAYING);
    PT_ASSERT_EQ_PTR(sChanB.nextChannelPointer, NULL);
    PT_ASSERT_EQ_PTR(sChanC.track, NULL);

    MPlayMain(&sPlayer);
    PT_ASSERT_EQ_U32(sChanA.gateTime, 0);
    PT_ASSERT_EQ_U32(sChanA.statusFlags, SF_PLAYING | SOUND_CHANNEL_SF_STOP);
    PT_ASSERT_EQ_U32(sChanB.gateTime, 0);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, SF_PLAYING);
}

/* MPlayMain: the LFO */

static int8_t RunLfoTick(void)
{
    MPlayMain(&sPlayer);
    return sTracks[0].modM;
}

PTEST(MPlayMain_lfo_is_a_triangle_wave)
{
    /* speed 64, depth 10: counter 64 -> 128 - 64 = 64 -> 10 * 64 >> 6 = 10;
     * 128 -> 0 -> 0; 192 -> (s8)192 = -64 -> -10; 256 -> 0 -> 0; 64 -> 10 */
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sTracks[0].lfoSpeed = 64;
    sTracks[0].mod = 10;

    PT_ASSERT_EQ_INT(RunLfoTick(), 10);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 64);
    PT_ASSERT_EQ_INT(RunLfoTick(), 0);
    PT_ASSERT_EQ_INT(RunLfoTick(), -10);
    PT_ASSERT_EQ_INT(RunLfoTick(), 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 0);
    PT_ASSERT_EQ_INT(RunLfoTick(), 10);
    /* each change asked for a pitch update, which then ran and was cleared */
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_TRK_VOL_PIT_SET), 5);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->arg0, MPT_FLG_EXIST | MPT_FLG_PITCHG);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);
}

PTEST(MPlayMain_lfo_on_volume_flags_a_volume_change)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sTracks[0].lfoSpeed = 64;
    sTracks[0].mod = 10;
    sTracks[0].modT = 1;

    PT_ASSERT_EQ_INT(RunLfoTick(), 10);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->kind, CALL_TRK_VOL_PIT_SET);
    PT_ASSERT_EQ_U32(M4aTestCall(1)->arg0, MPT_FLG_EXIST | MPT_FLG_VOLCHG);
}

PTEST(MPlayMain_lfo_waits_for_its_delay)
{
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sTracks[0].lfoSpeed = 64;
    sTracks[0].mod = 10;
    sTracks[0].lfoDelayC = 2;

    PT_ASSERT_EQ_INT(RunLfoTick(), 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoDelayC, 1);
    PT_ASSERT_EQ_INT(RunLfoTick(), 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoDelayC, 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 0);
    PT_ASSERT_EQ_INT(RunLfoTick(), 10);
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_TRK_VOL_PIT_SET), 1);
}

PTEST(MPlayMain_lfo_uses_the_unwrapped_counter_sum)
{
    /* speed 200 from 200: sum 400, stored 144; bit 7 of (400 - 64) & 0xFF is
     * clear, so the value is 128 - 400 = -272 (not 128 - 144);
     * 10 * -272 = -2720, >> 6 = -43 (floor of -42.5) */
    PT_ASSERT(SetUpPlayer(1) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sTracks[0].lfoSpeed = 200;
    sTracks[0].lfoSpeedC = 200;
    sTracks[0].mod = 10;

    PT_ASSERT_EQ_INT(RunLfoTick(), -43);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 144);
}

PTEST(MPlayMain_lfo_is_off_without_speed_or_depth)
{
    PT_ASSERT(SetUpPlayer(2) != NULL);
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].wait = 100;
    sTracks[0].lfoSpeed = 64;
    sTracks[1].flags = MPT_FLG_EXIST;
    sTracks[1].wait = 100;
    sTracks[1].mod = 10;

    MPlayMain(&sPlayer);

    PT_ASSERT_EQ_U32(sTracks[0].wait, 99);
    PT_ASSERT_EQ_U32(sTracks[1].wait, 99);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 0);
    PT_ASSERT_EQ_U32(sTracks[1].lfoSpeedC, 0);
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_TRK_VOL_PIT_SET), 0);
}

/* MPlayMain: volume and pitch updates reach the channels */

static struct SoundInfo *SetUpVolPitch(struct CgbChannel **cgbOut)
{
    struct SoundInfo *si = SetUpPlayer(1);
    struct CgbChannel *cgb;

    if (si == NULL)
        return NULL;
    SetUpChain();
    M4aTestMakeWave(&sNoteWave, 0, 0, NOTE_WAVE_FREQ, 0, 4);
    cgb = &M4aTestCgbChans()[1];
    /* chain: direct A, CGB channel 2, direct B (finished) */
    sChanA.nextChannelPointer = cgb;
    cgb->prevChannelPointer = &sChanA;
    cgb->nextChannelPointer = &sChanB;
    sChanB.prevChannelPointer = cgb;
    sChanB.nextChannelPointer = NULL;
    cgb->track = &sTracks[0];
    sChanA.statusFlags = SF_PLAYING;
    sChanA.velocity = 127;
    sChanA.key = 60;
    sChanA.wav = &sNoteWave.wave;
    cgb->statusFlags = SF_PLAYING;
    cgb->type = 2;
    cgb->velocity = 127;
    cgb->key = 70;
    sChanB.statusFlags = 0;
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_VOLCHG | MPT_FLG_PITCHG;
    sTracks[0].wait = 10;
    sTracks[0].vol = 127;
    sTracks[0].volX = 64;
    *cgbOut = cgb;
    return si;
}

PTEST(MPlayMain_volume_and_pitch_changes_update_every_channel)
{
    struct CgbChannel *cgb = NULL;
    uint32_t val1;

    PT_ASSERT(SetUpVolPitch(&cgb) != NULL);
    sTracks[0].keyShift = 1; /* TrkVolPitSet: keyM 1, pitM 0 */

    MPlayMain(&sPlayer);

    /* TrkVolPitSet: x = 127 * 64 >> 5 = 254; volMR (128 * 254) >> 8 = 127,
     * volML (127 * 254) >> 8 = 126. ChnVolSet at velocity 127, centred:
     * right (128 * 127 * 127) >> 14 = 126, left (127 * 127 * 126) >> 14 = 124 */
    PT_ASSERT_EQ_U32(sTracks[0].volMR, 127);
    PT_ASSERT_EQ_U32(sTracks[0].volML, 126);
    PT_ASSERT_EQ_U32(sChanA.rightVolume, 126);
    PT_ASSERT_EQ_U32(sChanA.leftVolume, 124);
    PT_ASSERT_EQ_U32(cgb->rightVolume, 126);
    PT_ASSERT_EQ_U32(cgb->leftVolume, 124);
    PT_ASSERT_EQ_U32(cgb->modify, CGB_CHANNEL_MO_VOL | CGB_CHANNEL_MO_PIT);

    /* key 60 + 1: gScaleTable[61] = 0x91, gFreqTable[1] >> 9; times freq >> 32 */
    val1 = gFreqTable[1] >> 9;
    PT_ASSERT_EQ_U32(sChanA.frequency, (uint32_t)(((uint64_t)NOTE_WAVE_FREQ * val1) >> 32));
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_MIDI_KEY_TO_FREQ), 1);
    PT_ASSERT_EQ_U32(cgb->frequency, M4aTestCgbFreqFor(2, 71, 0));
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_MIDI_KEY_TO_CGB_FREQ), 1);

    PT_ASSERT_EQ_PTR(sChanB.track, NULL);
    PT_ASSERT_EQ_PTR(cgb->nextChannelPointer, NULL);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);
}

PTEST(MPlayMain_pitch_bend_and_fine_tuning_reach_MidiKeyToFreq)
{
    /* bend 16 * range 2 = 32, * 4 = 128: keyM 0, pitM 128. MidiKeyToFreq
     * interpolates between keys 60 and 61 by 128/256. */
    struct CgbChannel *cgb = NULL;
    uint32_t val1 = gFreqTable[0] >> 9;
    uint32_t val2 = gFreqTable[1] >> 9;
    uint32_t between;

    PT_ASSERT(SetUpVolPitch(&cgb) != NULL);
    sTracks[0].bend = 16;
    sTracks[0].bendRange = 2;
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_PITCHG;

    MPlayMain(&sPlayer);

    between = val1 + (uint32_t)(((uint64_t)(val2 - val1) * (128u << 24)) >> 32);
    PT_ASSERT_EQ_U32(sChanA.frequency, (uint32_t)(((uint64_t)NOTE_WAVE_FREQ * between) >> 32));
    PT_ASSERT_EQ_U32(cgb->frequency, M4aTestCgbFreqFor(2, 70, 128));
    PT_ASSERT_EQ_U32(cgb->modify, CGB_CHANNEL_MO_PIT);
}

PTEST(MPlayMain_pitch_below_key_0_is_clamped_to_key_0)
{
    struct CgbChannel *cgb = NULL;

    PT_ASSERT(SetUpVolPitch(&cgb) != NULL);
    sTracks[0].keyShift = -70; /* keyM -70: 60 - 70 < 0 */
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_PITCHG;

    MPlayMain(&sPlayer);

    for (uint32_t i = 0; i < M4aTestCallCount(); i++)
    {
        if (M4aTestCall(i)->kind == CALL_MIDI_KEY_TO_FREQ)
            PT_ASSERT_EQ_U32(M4aTestCall(i)->arg0, 0);
        if (M4aTestCall(i)->kind == CALL_MIDI_KEY_TO_CGB_FREQ)
            PT_ASSERT_EQ_U32(M4aTestCall(i)->arg1, 0); /* 70 - 70 = 0 */
    }
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_MIDI_KEY_TO_FREQ), 1);
}

PTEST_MAIN()
