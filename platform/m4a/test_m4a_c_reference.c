/*
 * src/m4a.c's TrkVolPitSet and MidiKeyToFreq transcribed for the tests,
 * and the definitions src/m4a_tables.c needs to link (its gXcmdTable
 * points at m4a.c's extended commands, gPokemonCrySongTemplate at
 * voicegroup_dummy); the engine never calls those.
 */
#include "test_m4a_c_reference.h"

// NOLINTBEGIN(readability-magic-numbers): a transcription of src/m4a.c, whose literals are the original's

/* Floor of x / 256, which is what an arithmetic shift right by 8 gives. */
static s32 FloorDiv256(s32 x)
{
    return x >= 0 ? x / 256 : -((-x + 255) / 256);
}

void M4aRefTrkVolPitSet(struct MusicPlayerTrack *track)
{
    if (track->flags & MPT_FLG_VOLSET) {
        s32 x = (s32)((u32)(track->vol * track->volX) >> 5);
        s32 y;

        if (track->modT == 1)
            x = (s32)((u32)(x * (track->modM + 128)) >> 7);

        y = 2 * track->pan + track->panX;

        if (track->modT == 2)
            y += track->modM;

        if (y < -128)
            y = -128;
        else if (y > 127)
            y = 127;

        track->volMR = (u8)((u32)((y + 128) * x) >> 8);
        track->volML = (u8)((u32)((127 - y) * x) >> 8);
    }

    if (track->flags & MPT_FLG_PITSET) {
        s32 bend = track->bend * track->bendRange;
        s32 x = (track->tune + bend) * 4 + track->keyShift * 256 + track->keyShiftX * 256 + track->pitX;

        if (track->modT == 0)
            x += 16 * track->modM;

        track->keyM = (u8)FloorDiv256(x);
        track->pitM = (u8)x;
    }

    track->flags &= (u8) ~(MPT_FLG_PITSET | MPT_FLG_VOLSET);
}

u32 M4aRefMidiKeyToFreq(const struct WaveData *wav, u8 key, u8 fineAdjust)
{
    u32 val1;
    u32 val2;
    u32 fineAdjustShifted = (u32)fineAdjust << 24;

    if (key > 178) {
        key = 178;
        fineAdjustShifted = 255u << 24;
    }

    val1 = gScaleTable[key];
    val1 = gFreqTable[val1 & 0xF] >> (val1 >> 4);

    val2 = gScaleTable[key + 1];
    val2 = gFreqTable[val2 & 0xF] >> (val2 >> 4);

    return umul3232H32(wav->freq, val1 + umul3232H32(val2 - val1, fineAdjustShifted));
}

const struct ToneData voicegroup_dummy = {0};

#define DEFINE_UNUSED_XCMD(name)                                                 \
    void name(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track) \
    {                                                                            \
        (void)mplayInfo;                                                         \
        (void)track;                                                             \
    }

DEFINE_UNUSED_XCMD(ply_xxx)
DEFINE_UNUSED_XCMD(ply_xwave)
DEFINE_UNUSED_XCMD(ply_xtype)
DEFINE_UNUSED_XCMD(ply_xatta)
DEFINE_UNUSED_XCMD(ply_xdeca)
DEFINE_UNUSED_XCMD(ply_xsust)
DEFINE_UNUSED_XCMD(ply_xrele)
DEFINE_UNUSED_XCMD(ply_xiecv)
DEFINE_UNUSED_XCMD(ply_xiecl)
DEFINE_UNUSED_XCMD(ply_xleng)
DEFINE_UNUSED_XCMD(ply_xswee)
DEFINE_UNUSED_XCMD(ply_xwait)
DEFINE_UNUSED_XCMD(ply_xcmd_0D)
// NOLINTEND(readability-magic-numbers)
