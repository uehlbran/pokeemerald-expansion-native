#include "global.h"
#include "trig.h"
#include "test/test.h"

// Native only: reads one past a table that the GBA serves from the next bytes
// of its ROM, which natively are whatever the link places after the table. The
// tables carry the GBA's value as an extra entry (docs/UPSTREAM_MODIFICATIONS.md).
#if PLATFORM_NATIVE

extern const s8 gDoomDesireLightBeamCoordTable[];
extern const u8 gDoomDesireLightBeamDelayTable[];

// Index 320 of gSineTable: Extrasensory's stage 1 and Cos with an unmasked angle.
#define SINE_TABLE_FIRST_PAST_END 320
#define SINE_TABLE_GBA_READ_PAST_END 0x041E

TEST("gSineTable's index one past its 320 entries is the word the GBA reads")
{
    EXPECT_EQ(gSineTable[SINE_TABLE_FIRST_PAST_END], SINE_TABLE_GBA_READ_PAST_END);
    EXPECT_EQ(gSineTable[SINE_TABLE_FIRST_PAST_END - 1], 0x00FF);
}

TEST("Cos with an angle whose index + 64 is 320 reads that entry")
{
    EXPECT_EQ(Cos(SINE_TABLE_FIRST_PAST_END - 64, 256), SINE_TABLE_GBA_READ_PAST_END);
}

TEST("Doom Desire's fifth beam run reads 0, the first byte of the delay table the GBA reads")
{
    EXPECT_EQ(gDoomDesireLightBeamCoordTable[3], 0x00);
    EXPECT_EQ(gDoomDesireLightBeamCoordTable[4], gDoomDesireLightBeamDelayTable[0]);
    EXPECT_EQ(gDoomDesireLightBeamCoordTable[4], 0);
}

#endif
