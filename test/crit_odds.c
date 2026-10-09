#include "global.h"
#include "battle_util.h"
#include "constants/battle_util.h"
#include "test/test.h"

// Native only: GetCriticalHitOdds is asked about CRITICAL_HIT_BLOCKED by
// ShouldCalcCritDamage (battle_ai_util.c). The GBA reads the word before
// sGen7CriticalHitOdds, 0x02000301, which is not 1: no guaranteed crit.
#if PLATFORM_NATIVE

TEST("GetCriticalHitOdds of CRITICAL_HIT_BLOCKED is the word the GBA reads, not 1")
{
    u32 odds = GetCriticalHitOdds((u32)CRITICAL_HIT_BLOCKED);
    EXPECT_NE(odds, 1);
    EXPECT_EQ(odds, 0x02000301);
}

TEST("GetCriticalHitOdds of a stage inside the table is unchanged")
{
    EXPECT_EQ(GetCriticalHitOdds(0), 24);
    EXPECT_EQ(GetCriticalHitOdds(3), 1);
}

#endif
