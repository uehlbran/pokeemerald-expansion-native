/*
 * The src/m4a.c functions the engine calls, for the differential test,
 * built identically for both targets: MidiKeyToFreq and TrkVolPitSet as
 * m4a.c computes them (../test_m4a_c_reference.c), ClearChain and
 * Clear64byte dispatching through gMPlayJumpTable as m4a.c does, and the
 * fade and sample-rate entry points as no-ops (no scenario fades).
 */
#include "gba/m4a_internal.h"
#include "test_m4a_c_reference.h"

MPlayFunc gMPlayJumpTable[36];

void SampleFreqSet(u32 freq)
{
    (void)freq;
}

void FadeOutBody(struct MusicPlayerInfo *mplayInfo)
{
    (void)mplayInfo;
}

void ClearChain(void *x)
{
    void (*func)(void *) = gMPlayJumpTable[34];

    func(x);
}

void Clear64byte(void *addr)
{
    void (*func)(void *) = gMPlayJumpTable[35];

    func(addr);
}

void TrkVolPitSet(struct MusicPlayerInfo *mplayInfo, struct MusicPlayerTrack *track)
{
    (void)mplayInfo;
    M4aRefTrkVolPitSet(track);
}

u32 MidiKeyToFreq(struct WaveData *wav, u8 key, u8 fineAdjust)
{
    return M4aRefMidiKeyToFreq(wav, key, fineAdjust);
}
