/*
 * The parts of src/m4a.c the m4a engine tests need, shared by the unit
 * tests and the differential test. src/m4a.c itself cannot be compiled
 * standalone (it includes global.h and an ARM swi), so the two functions
 * whose results flow back into the engine are transcribed here, with
 * shifts of negative values written as arithmetic.
 */
#ifndef GUARD_PLATFORM_M4A_TEST_C_REFERENCE_H
#define GUARD_PLATFORM_M4A_TEST_C_REFERENCE_H

#include "gba/m4a_internal.h"

/* src/m4a.c TrkVolPitSet */
void M4aRefTrkVolPitSet(struct MusicPlayerTrack *track);
/* src/m4a.c MidiKeyToFreq */
u32 M4aRefMidiKeyToFreq(const struct WaveData *wav, u8 key, u8 fineAdjust);

#endif // GUARD_PLATFORM_M4A_TEST_C_REFERENCE_H
