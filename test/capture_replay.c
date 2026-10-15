#include "global.h"
#include "test/battle.h"

// Native only: the battle runner's capture replay (test/test_runner_battle.c,
// "A capture"; docs/UPSTREAM_MODIFICATIONS.md). Natively a capture reaches
// THEN in a variable of THEN's own call, whether or not it has an
// initialiser (on the GBA an initialiser hides it: platform/test_runner/
// SUITE-LOG.md, class 11), at every width a capture has; and a SCENE that
// does not queue the same events when it runs again for THEN ends the test
// with ERROR instead of handing THEN values written nowhere.
#if PLATFORM_NATIVE

SINGLE_BATTLE_TEST("Capture replay: HP_BAR's captureDamage reaches THEN in a variable with an initialiser")
{
    s16 damage = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &damage);
    } THEN {
        EXPECT_GT(damage, 0);
        EXPECT_EQ(damage, opponent->maxHP - opponent->hp);
    }
}

SINGLE_BATTLE_TEST("Capture replay: a heal's captureDamage reaches THEN negative")
{
    s16 heal = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { HP(100); }
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_RECOVER); }
    } SCENE {
        HP_BAR(player, captureDamage: &heal);
    } THEN {
        EXPECT_LT(heal, 0);
        EXPECT_EQ(heal, 100 - player->hp);
    }
}

SINGLE_BATTLE_TEST("Capture replay: HP_BAR's captureHP reaches THEN")
{
    u16 hp = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        HP_BAR(opponent, captureHP: &hp);
    } THEN {
        EXPECT_GT(hp, 0);
        EXPECT_EQ(hp, opponent->hp);
    }
}

SINGLE_BATTLE_TEST("Capture replay: SUB_HIT's captureDamage reaches THEN")
{
    u16 damage = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SUBSTITUTE); MOVE(opponent, MOVE_SCRATCH); }
    } SCENE {
        SUB_HIT(player, captureDamage: &damage);
    } THEN {
        EXPECT_GT(damage, 0);
    }
}

WILD_BATTLE_TEST("Capture replay: EXPERIENCE_BAR's captureGainedExp reaches THEN")
{
    s32 exp = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET) { Level(20); }
        OPPONENT(SPECIES_CATERPIE) { Level(5); HP(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        MESSAGE("The wild Caterpie fainted!");
        EXPERIENCE_BAR(player, captureGainedExp: &exp);
    } THEN {
        EXPECT_GT(exp, 0);
    }
}

WILD_BATTLE_TEST("Capture replay: CATCHING_CHANCE reaches THEN")
{
    u32 odds = 0;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CLEFFA);
    } WHEN {
        TURN { USE_ITEM(player, ITEM_POKE_BALL); }
    } SCENE {
        CATCHING_CHANCE(&odds);
    } THEN {
        EXPECT_GT(odds, 0);
    }
}

// The SCENE below queues an HP_BAR of the opponent the first time and one of
// the player when it runs again for THEN.
SINGLE_BATTLE_TEST("Capture replay: a SCENE that queues another event when it runs again ends the test with ERROR")
{
    static u32 sceneRuns;
    s16 damage;
    Test_ExpectedResult(TEST_RESULT_ERROR);
    GIVEN {
        sceneRuns = 0;
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        if (sceneRuns++ == 0)
            HP_BAR(opponent, captureDamage: &damage);
        else
            HP_BAR(player, captureDamage: &damage);
    } THEN {
        EXPECT(FALSE); // Not reached: CloseScene ends the test first.
    }
}

SINGLE_BATTLE_TEST("Capture replay: a SCENE that queues more events when it runs again ends the test with ERROR")
{
    static u32 sceneRuns;
    s16 damage;
    Test_ExpectedResult(TEST_RESULT_ERROR);
    GIVEN {
        sceneRuns = 0;
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &damage);
        if (sceneRuns++ != 0)
            MESSAGE("Wobbuffet used Scratch!");
    } THEN {
        EXPECT(FALSE); // Not reached: CloseScene ends the test first.
    }
}

// SCENE runs in GIVEN's call (the second) but not in THEN's (the third): the
// first call only counts the parameters.
SINGLE_BATTLE_TEST("Capture replay: a SCENE that does not run again for THEN ends the test with ERROR")
{
    static u32 calls;
    s16 damage;
    Test_ExpectedResult(TEST_RESULT_ERROR);
    GIVEN {
        calls = 1;
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_WOBBUFFET);
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    }
    if (calls++ == 1)
    {
        SCENE {
            HP_BAR(opponent, captureDamage: &damage);
        }
    }
    THEN {
        EXPECT(TRUE);
    }
}

#endif
