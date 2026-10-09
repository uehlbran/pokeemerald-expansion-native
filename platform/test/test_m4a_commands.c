// deps: m4a/m4a_mixer.c m4a/m4a_render.c m4a/m4a_sequencer.c m4a/m4a_note.c m4a/m4a_commands.c m4a/m4a_clear64.c m4a/test_m4a_support.c m4a/test_m4a_c_reference.c ../src/m4a_tables.c
/*
 * Unit tests for the m4a engine's track commands (src/m4a_1.s
 * MPlayJumpTableCopy's guard chk_adr_r2, ply_fine, goto, patt, pend, rept,
 * prio, tempo, keysh, voice, vol, pan, bend, bendr, lfodl, modt, tune,
 * port, lfos, mod and endtie), translated to C in m4a_commands.c.
 * Expected values are worked out by hand from the assembly.
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
    PT_ASSERT(M4aTestPutPointer(sSong, sTarget));
    ply_goto(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_PTR(sTracks[0].cmdPtr, sTarget);
}

PTEST(ply_goto_from_the_bios_region_reads_the_low_byte_as_0)
{
    uint8_t *low = M4aTestLowPage();
    uintptr_t target = (uintptr_t)sTarget | 0x5A;

    PT_ASSERT(SetUpCommand());
    PT_ASSERT(M4aTestPutPointer(low, (const void *)target));
    sTracks[0].cmdPtr = low;
    ply_goto(&sPlayer, &sTracks[0]);
    PT_ASSERT_EQ_U32((uint32_t)(uintptr_t)sTracks[0].cmdPtr, (uint32_t)(target & ~(uintptr_t)0xFF));
}

PTEST(ply_patt_calls_a_pattern_and_ply_pend_returns)
{
    PT_ASSERT(SetUpCommand());
    PT_ASSERT(M4aTestPutPointer(sSong, sTarget));

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
    for (int level = 0; level < 3; level++) {
        PT_ASSERT(M4aTestPutPointer(sSong + 8 * level, sSong + 8 * (level + 1)));
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
    PT_ASSERT(M4aTestPutPointer(sSong + 1, sTarget));

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
    PT_ASSERT(M4aTestPutPointer(sSong + 1, sTarget));
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
