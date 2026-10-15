#include "global.h"
#include "test/battle.h"

// Native only: the battle runner's capture replay (test/test_runner_battle.c,
// "A capture"; docs/UPSTREAM_MODIFICATIONS.md). Natively a capture reaches
// THEN in a variable of THEN's own call, whether or not it has an
// initialiser (on the GBA an initialiser hides it: platform/test_runner/
// SUITE-LOG.md, class 11), at every width a capture has and with every bit
// of it; a SCENE that does not queue the same events when it runs again for
// THEN (another, more or fewer events, or another grouping) ends the test
// with ERROR instead of handing THEN values written nowhere; and every
// trial's battle matches the events SCENE queued the first time.
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

// A level 1 Wobbuffet holding a Lucky Egg that defeats a level 100 Blissey
// gains more than 0xFFFF experience (B_SCALED_EXP's formula), so the value
// THEN reads is right only if all 32 bits of it were written.
WILD_BATTLE_TEST("Capture replay: EXPERIENCE_BAR's captureGainedExp reaches THEN")
{
    s32 exp = 0;
    GIVEN {
        WITH_CONFIG(B_SCALED_EXP, GEN_5);
        PLAYER(SPECIES_WOBBUFFET) { Level(1); Item(ITEM_LUCKY_EGG); }
        OPPONENT(SPECIES_BLISSEY) { Level(100); HP(1); Moves(MOVE_CELEBRATE); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); }
    } SCENE {
        MESSAGE("The wild Blissey fainted!");
        EXPERIENCE_BAR(player, captureGainedExp: &exp);
    } THEN {
        EXPECT_GT(exp, 0xFFFF);
        EXPECT_EQ(exp, GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_EXP) - GetMonData(&PLAYER_PARTY[0], MON_DATA_EXP));
    }
}

// CATCHING_CHANCE leaves the variable as it is until the battle writes it,
// and the odds of this throw fit in 16 bits: THEN sees the initialiser's
// upper half gone only if all 32 bits were written.
WILD_BATTLE_TEST("Capture replay: CATCHING_CHANCE reaches THEN")
{
    u32 odds = 0xA5A5A5A5;
    GIVEN {
        PLAYER(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_CLEFFA);
    } WHEN {
        TURN { USE_ITEM(player, ITEM_POKE_BALL); }
    } SCENE {
        CATCHING_CHANCE(&odds);
    } THEN {
        EXPECT_GT(odds, 0);
        EXPECT_LT(odds, 0x10000);
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

// The SCENE below queues the player's HP_BAR after the opponent's the first
// time only: the events it queues again are the first ones, one short.
SINGLE_BATTLE_TEST("Capture replay: a SCENE that queues fewer events when it runs again ends the test with ERROR")
{
    static u32 sceneRuns;
    s16 damage;
    Test_ExpectedResult(TEST_RESULT_ERROR);
    GIVEN {
        sceneRuns = 0;
        PLAYER(SPECIES_WOBBUFFET) { Speed(2); }
        OPPONENT(SPECIES_WOBBUFFET) { Speed(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); MOVE(opponent, MOVE_SCRATCH); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &damage);
        if (sceneRuns++ == 0)
            HP_BAR(player);
    } THEN {
        EXPECT(FALSE); // Not reached: CloseScene ends the test first.
    }
}

// The same HP_BAR, on the same line, in a ONE_OF the first time and on its
// own when SCENE runs again.
SINGLE_BATTLE_TEST("Capture replay: a SCENE that queues an event outside the group it was in when it runs again ends the test with ERROR")
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
        if (sceneRuns++ == 0) ONE_OF { HP_BAR(opponent, captureDamage: &damage); } else HP_BAR(opponent, captureDamage: &damage);
    } THEN {
        EXPECT(FALSE); // Not reached: CloseScene ends the test first.
    }
}

// The same two events, on the same line, in one ONE_OF of two the first time
// and in a ONE_OF of one, the MESSAGE after it, when SCENE runs again.
SINGLE_BATTLE_TEST("Capture replay: a SCENE whose group holds fewer events when it runs again ends the test with ERROR")
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
        if (sceneRuns++ == 0) ONE_OF { HP_BAR(opponent, captureDamage: &damage); MESSAGE("Wobbuffet used Scratch!"); } else { ONE_OF { HP_BAR(opponent, captureDamage: &damage); } MESSAGE("Wobbuffet used Scratch!"); }
    } THEN {
        EXPECT(FALSE); // Not reached: CloseScene ends the test first.
    }
}

// PASSES_RANDOMLY without a tag runs the battle 50 times (trials) after one
// call for GIVEN, WHEN and SCENE, each battle against the events SCENE
// queued then. SCENE's run for THEN queues the player's HP_BAR with the HP
// that only GIVEN's body sets, so 0 there: the next trial must not see it.
SINGLE_BATTLE_TEST("Capture replay: each trial's battle matches the events SCENE queued the first time")
{
    u16 hpAfter = 0;
    s16 damage = 0;
    PASSES_RANDOMLY(1, 1);
    GIVEN {
        hpAfter = 80;
        PLAYER(SPECIES_WOBBUFFET) { HP(100); Speed(2); }
        OPPONENT(SPECIES_WOBBUFFET) { Speed(1); }
    } WHEN {
        TURN { MOVE(player, MOVE_SCRATCH); MOVE(opponent, MOVE_SONIC_BOOM); }
    } SCENE {
        HP_BAR(opponent, captureDamage: &damage);
        HP_BAR(player, hp: hpAfter);
    } THEN {
        EXPECT_GT(damage, 0);
        EXPECT_EQ(player->hp, 80);
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
