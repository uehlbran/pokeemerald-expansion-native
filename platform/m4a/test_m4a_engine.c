/*
 * Unit tests for the sequencer half of the m4a engine (src/m4a_1.s
 * umul3232H32, SoundMainBTM, RealClearChain, MPlayJumpTableCopy,
 * m4aSoundVSync, MPlayMain, TrackStop, ChnVolSetAsm, ply_note and the
 * ply_* track commands), translated to C in m4a_sequencer.c, m4a_note.c,
 * m4a_commands.c and m4a_clear64.c. Expected values are worked out by hand
 * from the assembly (the arithmetic is in the comments) or by 64-bit
 * reference arithmetic in the test.
 */
#include <string.h>

#if __has_include("../test/ptest.h")
#include "../test/ptest.h"
#else
#include "ptest_stub.h"
#endif

#include "test_m4a_support.h"

_Static_assert(sizeof(void *) == 4, "song data in these tests holds 4-byte pointers (32-bit milestone)");

extern void *const gMPlayJumpTableTemplate[];

/* Track command bytes (src/m4a_tables.c, sound/MPlayDef.s). */
#define CMD_FINE  0xB1
#define CMD_TEMPO 0xBB
#define CMD_VOL   0xBE
#define CMD_W01   0x81
#define CMD_W02   0x82
#define CMD_N24   0xE7 /* note command 0xCF + 24 */
/* Status of a channel that is playing and not released (start or envelope bits). */
#define SF_PLAYING SOUND_CHANNEL_SF_ENV_ATTACK
/* MPlayMain's tempo accumulator threshold: one tick per 150 units. */
#define TEMPO_TICK 150
/* gClockTable: a length for each wait (0x80..0xB0) and note (0xCF..0xFF) command. */
#define MPLAY_CLOCK_TABLE_SIZE_FOR_TEST 49u
/* DMA control the VSync leaves on DMA1/DMA2: enable, FIFO timing, 32-bit, repeat
 * (0x8000 | 0x3000 | 0x0400 | 0x0200), GBATEK "GBA DMA Transfers". */
#define DMA_FIFO_CONTROL 0xB600u

static struct MusicPlayerInfo sPlayer;
static struct MusicPlayerTrack sTracks[4];
static union M4aTestWave sNoteWave;
/* 13379 Hz * 1024, the WaveData.freq of a sample recorded at 13379 Hz. */
#define NOTE_WAVE_FREQ (13379u << 10)

static void PutPointer(uint8_t *dst, const void *target)
{
    uintptr_t bits = (uintptr_t)target;

    for (size_t i = 0; i < 4; i++)
        dst[i] = (uint8_t)(bits >> (8 * i));
}

static void LinkChannels(struct MusicPlayerTrack *track, struct SoundChannel *const *chans, size_t n)
{
    track->chan = n > 0 ? chans[0] : NULL;
    for (size_t i = 0; i < n; i++)
    {
        chans[i]->track = track;
        chans[i]->prevChannelPointer = i > 0 ? chans[i - 1] : NULL;
        chans[i]->nextChannelPointer = i + 1 < n ? chans[i + 1] : NULL;
    }
}

static struct SoundInfo *SetUpPlayer(uint8_t trackCount)
{
    struct SoundInfo *si = M4aTestSetUp();

    memset(&sPlayer, 0, sizeof(sPlayer));
    memset(sTracks, 0, sizeof(sTracks));
    sPlayer.ident = ID_NUMBER;
    sPlayer.tracks = sTracks;
    sPlayer.trackCount = trackCount;
    sPlayer.tempoD = TEMPO_TICK;
    sPlayer.tempoU = 0x100;
    sPlayer.tempoI = TEMPO_TICK;
    return si;
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
    LinkChannels(&sTracks[0], chans, 3);
}

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

/* m4aSoundVSync */

PTEST(m4aSoundVSync_ignores_a_sound_info_that_is_not_running)
{
    static const uint32_t kIdents[] = {ID_NUMBER - 1, ID_NUMBER + 2, ID_NUMBER + 10, 0};

    for (size_t i = 0; i < sizeof(kIdents) / sizeof(kIdents[0]); i++)
    {
        struct SoundInfo *si = M4aTestSetUp();

        PT_ASSERT(si != NULL);
        si->ident = kIdents[i];
        si->pcmDmaCounter = 1;
        m4aSoundVSync();
        PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
        PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), 0);

        /* control: running, the same state counts down */
        si->ident = ID_NUMBER;
        si->pcmDmaCounter = 2;
        m4aSoundVSync();
        PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
    }
}

PTEST(m4aSoundVSync_before_SoundInit_does_nothing)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaCounter = 1;
    M4aTestWriteIo32(REG_OFFSET_DMA1CNT, (uint32_t)DMA_REPEAT << 16);
    SOUND_INFO_PTR = NULL;
    m4aSoundVSync();
    SOUND_INFO_PTR = si;
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 1);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), DMA_REPEAT);
}

PTEST(m4aSoundVSync_counts_down_without_touching_the_dma)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaCounter = 3;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 2);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), 0);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), 0);
}

PTEST(m4aSoundVSync_runs_while_SoundMain_holds_the_lock)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->ident = ID_NUMBER + 1;
    si->pcmDmaCounter = 3;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 2);
}

PTEST(m4aSoundVSync_restarts_the_fifo_dma_when_the_counter_expires)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaPeriod = 7;
    si->pcmDmaCounter = 1;
    /* DMA1 in repeat mode is first flushed with a 4-word transfer; DMA2 is not */
    M4aTestWriteIo32(REG_OFFSET_DMA1CNT, ((uint32_t)DMA_REPEAT << 16) | 0x1234u);
    M4aTestWriteIo32(REG_OFFSET_DMA2CNT, 0x5678u);

    m4aSoundVSync();

    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 7);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_L), 4);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA1CNT_H), DMA_FIFO_CONTROL);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_L), 0x5678u);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), DMA_FIFO_CONTROL);
}

PTEST(m4aSoundVSync_counter_0_also_reloads)
{
    struct SoundInfo *si = M4aTestSetUp();

    PT_ASSERT(si != NULL);
    si->pcmDmaPeriod = 7;
    si->pcmDmaCounter = 0;
    m4aSoundVSync();
    PT_ASSERT_EQ_U32(si->pcmDmaCounter, 7);
    PT_ASSERT_EQ_U32(M4aTestReadIo16(REG_OFFSET_DMA2CNT_H), DMA_FIFO_CONTROL);
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
    PT_ASSERT_EQ_U32(sTracks[0].pan, 0);
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

    for (size_t i = 0; i < M4aTestCallCount(); i++)
    {
        if (M4aTestCall(i)->kind == CALL_MIDI_KEY_TO_FREQ)
            PT_ASSERT_EQ_U32(M4aTestCall(i)->arg0, 0);
        if (M4aTestCall(i)->kind == CALL_MIDI_KEY_TO_CGB_FREQ)
            PT_ASSERT_EQ_U32(M4aTestCall(i)->arg1, 0); /* 70 - 70 = 0 */
    }
    PT_ASSERT_EQ_U32(M4aTestCountCalls(CALL_MIDI_KEY_TO_FREQ), 1);
}

/* ply_note */

static const uint8_t *sNoteArgs;

static struct SoundInfo *SetUpNote(const uint8_t *args)
{
    struct SoundInfo *si = SetUpPlayer(4);

    if (si == NULL)
        return NULL;
    M4aTestMakeWave(&sNoteWave, 0, 0, NOTE_WAVE_FREQ, 0, 4);
    sPlayer.priority = 10;
    for (size_t i = 0; i < 4; i++)
    {
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
    sNoteArgs = args;
    return si;
}

PTEST(ply_note_starts_a_direct_sound_channel)
{
    static const uint8_t args[] = {60, 100, 5};
    struct SoundInfo *si = SetUpNote(args);
    struct SoundChannel *chan;

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
    struct SoundInfo *si = SetUpNote(noArgs);

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
    LinkChannels(&sTracks[track], one, 1);
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

    for (size_t k = 0; k < 4; k++)
    {
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
    for (size_t k = 0; k < MAX_DIRECTSOUND_CHANNELS; k++)
    {
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
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].gateTime = 7;
    ply_note(MPLAY_CLOCK_TABLE_SIZE_FOR_TEST, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 7);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, args);
    PT_ASSERT_EQ_U32(M4aTestCallCount(), 0);

    ply_note(MPLAY_CLOCK_TABLE_SIZE_FOR_TEST - 1, &sPlayer, &sTracks[0]); /* the last valid one */
    PT_ASSERT_EQ_U32(sTracks[0].gateTime, 96); /* gClockTable[48] */
}

PTEST(ply_note_starts_a_psg_channel)
{
    static const uint8_t args[] = {60, 100, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);
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

    for (size_t i = 0; i < sizeof(kPanSweep); i++)
    {
        struct SoundInfo *si = SetUpNote(args);

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
    struct SoundInfo *si = SetUpNote(args);
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
    struct SoundInfo *si = SetUpNote(args);

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
    struct SoundChannel *chan;

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
    struct SoundChannel *chan;

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
    struct SoundInfo *si = SetUpNote(args);

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
    const uint8_t *tablePtr = splitTable;
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    memset(splitTable, 0, sizeof(splitTable));
    splitTable[60] = 1;
    memset(sSubVoices, 0, sizeof(sSubVoices));
    sSubVoices[1].key = 99;
    sSubVoices[1].wav = &sNoteWave.wave;
    sSubVoices[1].attack = 77;
    sTracks[0].tone.type = TONEDATA_TYPE_SPL;
    sTracks[0].tone.wav = (struct WaveData *)(void *)sSubVoices;
    memcpy((uint8_t *)&sTracks[0].tone + offsetof(struct ToneData, attack), &tablePtr, sizeof(tablePtr));

    ply_note(24, &sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_U32(si->chans[0].key, 60); /* a split keeps the played key */
    PT_ASSERT_EQ_U32(si->chans[0].attack, 77);
}

PTEST(ply_note_nested_split_or_rhythm_plays_nothing)
{
    static const uint8_t args[] = {1, 100, CMD_FINE};
    struct SoundInfo *si = SetUpNote(args);

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
    struct SoundInfo *si = SetUpNote(args);

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
    struct SoundInfo *si = SetUpNote(args);

    PT_ASSERT(si != NULL);
    sTracks[0].modM = 33;
    sTracks[0].lfoDelayC = 4;
    ply_note(24, &sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(si->chans[0].statusFlags, SOUND_CHANNEL_SF_START);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 33);
    PT_ASSERT_EQ_U32(sTracks[0].lfoDelayC, 0);
}

/* Track commands */

static uint8_t sSong[32];
static uint8_t sTarget[8];

static int SetUpCommand(void)
{
    if (SetUpPlayer(1) == NULL)
        return 0;
    memset(sSong, 0, sizeof(sSong));
    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].cmdPtr = sSong;
    return 1;
}

PTEST(ply_goto_jumps_to_the_little_endian_address)
{
    PT_ASSERT(SetUpCommand());
    PutPointer(sSong, sTarget);
    ply_goto(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sTarget);
}

PTEST(ply_goto_from_the_bios_region_reads_the_low_byte_as_0)
{
    uint8_t *low = M4aTestLowPage();
    uintptr_t target = (uintptr_t)sTarget | 0x5A;

    PT_ASSERT(SetUpCommand());
    PutPointer(low, (const void *)target);
    sTracks[0].cmdPtr = low;
    ply_goto(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32((uint32_t)(uintptr_t)sTracks[0].cmdPtr, (uint32_t)(target & ~(uintptr_t)0xFF));
}

PTEST(ply_patt_calls_a_pattern_and_ply_pend_returns)
{
    PT_ASSERT(SetUpCommand());
    PutPointer(sSong, sTarget);

    ply_patt(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].patternLevel, 1);
    PT_ASSERT_EQ_PTR(sTracks[0].patternStack[0], sSong + 4);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sTarget);

    ply_pend(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].patternLevel, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 4);

    ply_pend(&sPlayer, &sTracks[0]); /* at level 0: nothing */
    PT_ASSERT_EQ_U32(sTracks[0].patternLevel, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 4);
}

PTEST(ply_patt_nests_three_deep_and_a_fourth_ends_the_track)
{
    PT_ASSERT(SetUpCommand());
    for (int level = 0; level < 3; level++)
    {
        PutPointer(sSong + 8 * level, sSong + 8 * (level + 1));
        ply_patt(&sPlayer, &sTracks[0]);
        PT_ASSERT_EQ_U32(sTracks[0].patternLevel, (uint32_t)(level + 1));
        PT_ASSERT_EQ_PTR(sTracks[0].patternStack[level], sSong + 8 * level + 4);
    }
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 24);

    ply_patt(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].flags, 0);
    PT_ASSERT_EQ_U32(sTracks[0].patternLevel, 3);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 24);
}

PTEST(ply_rept_loops_count_times_then_falls_through)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 2;
    PutPointer(sSong + 1, sTarget);

    ply_rept(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].repN, 1);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sTarget);

    sTracks[0].cmdPtr = sSong;
    ply_rept(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].repN, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 5);
}

PTEST(ply_rept_with_count_0_loops_forever)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 0;
    PutPointer(sSong + 1, sTarget);
    sTracks[0].repN = 7;

    ply_rept(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sTarget);
    PT_ASSERT_EQ_U32(sTracks[0].repN, 7);
}

PTEST(ply_prio_sets_the_track_priority)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 9;
    ply_prio(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].priority, 9);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 1);
}

PTEST(ply_tempo_doubles_the_byte_and_scales_by_tempoU)
{
    PT_ASSERT(SetUpCommand());
    sPlayer.tempoU = 0x80;
    sSong[0] = 75;
    sSong[1] = 255;
    ply_tempo(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sPlayer.tempoD, 150);
    PT_ASSERT_EQ_U32(sPlayer.tempoI, 75); /* 150 * 128 >> 8 */
    ply_tempo(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sPlayer.tempoD, 510);
    PT_ASSERT_EQ_U32(sPlayer.tempoI, 255);
}

PTEST(ply_keysh_sets_a_signed_shift_and_flags_pitch)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 0xFE;
    ply_keysh(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].keyShift, -2);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_PITCHG);
}

PTEST(ply_voice_copies_the_tone)
{
    static struct ToneData tones[3];

    PT_ASSERT(SetUpCommand());
    memset(tones, 0, sizeof(tones));
    tones[2].type = 8;
    tones[2].key = 61;
    tones[2].length = 3;
    tones[2].pan_sweep = 0xC4;
    tones[2].wav = &sNoteWave.wave;
    tones[2].attack = 11;
    tones[2].decay = 22;
    tones[2].sustain = 33;
    tones[2].release = 44;
    sPlayer.tone = tones;
    sSong[0] = 2;

    ply_voice(&sPlayer, &sTracks[0]);

    PT_ASSERT_MEM_EQ(&sTracks[0].tone, &tones[2], sizeof(struct ToneData));
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 1);
}

PTEST(ply_voice_from_the_bios_region_reads_zeros)
{
    struct ToneData *lowTones = (struct ToneData *)(void *)M4aTestLowPage();

    PT_ASSERT(SetUpCommand());
    lowTones[0].type = 8;
    lowTones[0].key = 61;
    lowTones[0].wav = &sNoteWave.wave;
    lowTones[0].attack = 11;
    sTracks[0].tone.type = 3;
    sTracks[0].tone.attack = 9;
    sPlayer.tone = lowTones;
    sSong[0] = 0;

    ply_voice(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sTracks[0].tone.type, 0);
    PT_ASSERT_EQ_U32(sTracks[0].tone.key, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].tone.wav, NULL);
    PT_ASSERT_EQ_U32(sTracks[0].tone.attack, 0);
}

PTEST(ply_checked_reads_from_the_bios_region_read_0_but_unchecked_ones_do_not)
{
    uint8_t *low = M4aTestLowPage();

    PT_ASSERT(SetUpCommand());
    low[0] = 9;
    low[1] = 33;
    sTracks[0].cmdPtr = low;

    ply_prio(&sPlayer, &sTracks[0]); /* checked */
    PT_ASSERT_EQ_U32(sTracks[0].priority, 0);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, low + 1);

    ply_lfos(&sPlayer, &sTracks[0]); /* unchecked */
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeed, 33);
}

PTEST(ply_vol_pan_bend_bendr_tune)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 100;
    sSong[1] = 0x50;
    sSong[2] = 0x00;
    sSong[3] = 0x7F;
    sSong[4] = 12;
    sSong[5] = 0x30;

    ply_vol(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].vol, 100);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_VOLCHG);

    ply_pan(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].pan, 16);
    ply_pan(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].pan, -64);

    sTracks[0].flags = MPT_FLG_EXIST;
    ply_bend(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].bend, 63);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_PITCHG);

    sTracks[0].flags = MPT_FLG_EXIST;
    ply_bendr(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].bendRange, 12);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_PITCHG);

    sTracks[0].flags = MPT_FLG_EXIST;
    ply_tune(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_INT(sTracks[0].tune, -16);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_PITCHG);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 6);
}

PTEST(ply_lfodl_and_modt)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 5;
    sSong[1] = 1;
    sSong[2] = 1;

    ply_lfodl(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].lfoDelay, 5);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);

    ply_modt(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].modT, 1);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_VOLCHG | MPT_FLG_PITCHG);

    sTracks[0].flags = MPT_FLG_EXIST;
    ply_modt(&sPlayer, &sTracks[0]); /* unchanged: no flags */
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 3);
}

PTEST(ply_lfos_and_mod_of_0_clear_the_modulation)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 30;
    sSong[1] = 0;
    sSong[2] = 0;
    sTracks[0].modM = 12;
    sTracks[0].lfoSpeedC = 40;

    ply_lfos(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeed, 30);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 12);

    ply_lfos(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeed, 0);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 0);
    PT_ASSERT_EQ_U32(sTracks[0].lfoSpeedC, 0);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_PITCHG);

    sTracks[0].flags = MPT_FLG_EXIST;
    sTracks[0].modT = 2;
    sTracks[0].modM = 5;
    ply_mod(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].mod, 0);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 0);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST | MPT_FLG_VOLCHG);
}

PTEST(ply_mod_nonzero_sets_the_depth_only)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 40;
    sTracks[0].modM = 5;
    ply_mod(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(sTracks[0].mod, 40);
    PT_ASSERT_EQ_INT(sTracks[0].modM, 5);
    PT_ASSERT_EQ_U32(sTracks[0].flags, MPT_FLG_EXIST);
}

PTEST(ply_port_writes_a_sound_register)
{
    PT_ASSERT(SetUpCommand());
    sSong[0] = 0x02;
    sSong[1] = 0x77;
    ply_port(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32(M4aTestReadIo8(REG_OFFSET_SOUND1CNT_L + 2), 0x77);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 2);
}

PTEST(ply_endtie_releases_the_first_matching_playing_channel)
{
    PT_ASSERT(SetUpCommand());
    SetUpChain();
    sTracks[0].cmdPtr = sSong;
    sSong[0] = 60;
    sChanA.statusFlags = SOUND_CHANNEL_SF_IEC; /* echo only: not matched */
    sChanA.midiKey = 60;
    sChanB.statusFlags = SOUND_CHANNEL_SF_START;
    sChanB.midiKey = 60;
    sChanC.statusFlags = SF_PLAYING;
    sChanC.midiKey = 60;

    ply_endtie(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sTracks[0].key, 60);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong + 1);
    PT_ASSERT_EQ_U32(sChanA.statusFlags, SOUND_CHANNEL_SF_IEC);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_STOP);
    PT_ASSERT_EQ_U32(sChanC.statusFlags, SF_PLAYING);
}

PTEST(ply_endtie_without_a_key_uses_the_track_key_and_skips_released_channels)
{
    PT_ASSERT(SetUpCommand());
    SetUpChain();
    sTracks[0].cmdPtr = sSong;
    sTracks[0].key = 50;
    sSong[0] = CMD_W01;
    sChanA.statusFlags = SF_PLAYING | SOUND_CHANNEL_SF_STOP;
    sChanA.midiKey = 50;
    sChanB.statusFlags = SF_PLAYING;
    sChanB.midiKey = 51;
    sChanC.statusFlags = SF_PLAYING;
    sChanC.midiKey = 50;

    ply_endtie(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sSong);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, SF_PLAYING);
    PT_ASSERT_EQ_U32(sChanC.statusFlags, SF_PLAYING | SOUND_CHANNEL_SF_STOP);
}

PTEST(ply_fine_releases_and_detaches_every_channel_and_ends_the_track)
{
    PT_ASSERT(SetUpCommand());
    SetUpChain();
    sTracks[0].flags = MPT_FLG_EXIST | MPT_FLG_VOLCHG;
    sChanA.statusFlags = SF_PLAYING;
    sChanB.statusFlags = 0;
    sChanC.statusFlags = SOUND_CHANNEL_SF_START;

    ply_fine(&sPlayer, &sTracks[0]);

    PT_ASSERT_EQ_U32(sChanA.statusFlags, SF_PLAYING | SOUND_CHANNEL_SF_STOP);
    PT_ASSERT_EQ_U32(sChanB.statusFlags, 0);
    PT_ASSERT_EQ_U32(sChanC.statusFlags, SOUND_CHANNEL_SF_START | SOUND_CHANNEL_SF_STOP);
    PT_ASSERT_EQ_PTR(sChanA.track, NULL);
    PT_ASSERT_EQ_PTR(sChanB.track, NULL);
    PT_ASSERT_EQ_PTR(sChanC.track, NULL);
    PT_ASSERT_EQ_PTR(sTracks[0].chan, NULL);
    PT_ASSERT_EQ_U32(sTracks[0].flags, 0);
}

PTEST_MAIN()
