#include "global.h"
#include "test/battle.h"

SINGLE_BATTLE_TEST("Lash Out damage is boosted if the user's stats are dropped the turn it is used")
{
    s16 damage[2];
    enum Move move = MOVE_NONE;

    PARAMETRIZE { move = MOVE_TACKLE; }
    PARAMETRIZE { move = MOVE_GROWL; }
    PARAMETRIZE { move = MOVE_LEER; }
    PARAMETRIZE { move = MOVE_STRING_SHOT; }
    PARAMETRIZE { move = MOVE_CONFIDE; }
    PARAMETRIZE { move = MOVE_SAND_ATTACK; }
    PARAMETRIZE { move = MOVE_SWEET_SCENT; }
    PARAMETRIZE { move = MOVE_CHARM; }
    PARAMETRIZE { move = MOVE_SCREECH; }
    PARAMETRIZE { move = MOVE_SCARY_FACE; }
    PARAMETRIZE { move = MOVE_CAPTIVATE; }
    PARAMETRIZE { move = MOVE_EERIE_IMPULSE; }
    PARAMETRIZE { move = MOVE_FAKE_TEARS; }
    PARAMETRIZE { move = MOVE_NOBLE_ROAR; }
    PARAMETRIZE { move = MOVE_LUNGE; }
    PARAMETRIZE { move = MOVE_FIRE_LASH; }
    PARAMETRIZE { move = MOVE_BULLDOZE; }
    PARAMETRIZE { move = MOVE_MYSTICAL_FIRE; }
    PARAMETRIZE { move = MOVE_BUG_BUZZ; }

    GIVEN {
        PLAYER(SPECIES_GRIMMSNARL) { Gender(MON_MALE); Speed(1); Moves(MOVE_LASH_OUT); }
        OPPONENT(SPECIES_GOLEM) { Gender(MON_FEMALE); Speed(2); Moves(move, MOVE_CELEBRATE); }
    } WHEN {
        TURN { MOVE(player, MOVE_LASH_OUT); MOVE(opponent, MOVE_CELEBRATE); }
        TURN { MOVE(player, MOVE_LASH_OUT); MOVE(opponent, move); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_CELEBRATE, opponent);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[0]);

        ANIMATION(ANIM_TYPE_MOVE, move, opponent);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[1]);
    } THEN {
        if (move == MOVE_TACKLE || move == MOVE_CHARM)
            EXPECT_EQ(damage[0], damage[1]);
        else if (move == MOVE_GROWL || move == MOVE_LUNGE || move == MOVE_NOBLE_ROAR)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(1.33), damage[1]);
        else
            EXPECT_MUL_EQ(damage[0], UQ_4_12(2.00), damage[1]);
    }
}

SINGLE_BATTLE_TEST("Lash Out damage is only boosted on the turn that Intimidate switches in")
{
#if PLATFORM_NATIVE
    // Known failing natively: an upstream test bug. EXPECT_EQ(damage[0],
    // damage[2]) ignores that Intimidate's Attack drop lasts: the battle gives
    // 54 on turn 1, 72 on turn 2 (power doubled at -1 Attack, 54 x 4/3) and
    // 36 on turn 3 (-1 Attack, no boost, 54 x 2/3), the same on the GBA.
    // On the GBA, and natively without the capture replay, THEN's call runs
    // the initialiser again over the captured values and compares zeros
    // (platform/test_runner/SUITE-LOG.md, class 11, has the measurements).
    KNOWN_FAILING;
#endif
#line 55
    s16 damage[3] = {0};
    enum Move move = MOVE_NONE;

    PARAMETRIZE { move = MOVE_LASH_OUT; }
    PARAMETRIZE { move = MOVE_SPLASH; }

    GIVEN {
        PLAYER(SPECIES_GRIMMSNARL) { Moves(move, MOVE_CELEBRATE, MOVE_LASH_OUT); }
        OPPONENT(SPECIES_INCINEROAR) { Ability(ABILITY_BLAZE); Moves(MOVE_CELEBRATE); }
        OPPONENT(SPECIES_INCINEROAR) { Ability(ABILITY_INTIMIDATE); Moves(MOVE_CELEBRATE); }
    } WHEN {
        TURN { MOVE(player, MOVE_LASH_OUT); MOVE(opponent, MOVE_CELEBRATE); }
        TURN { MOVE(player, move); SWITCH(opponent, 1); }
        TURN { MOVE(player, MOVE_LASH_OUT); MOVE(opponent, MOVE_CELEBRATE); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[0]);

        ANIMATION(ANIM_TYPE_MOVE, move, player);
        if (move == MOVE_LASH_OUT)
            HP_BAR(opponent, captureDamage: &damage[1]);

        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[2]);
    } THEN {
        EXPECT_EQ(damage[0], damage[2]);
        EXPECT_MUL_EQ(damage[0], (move == MOVE_LASH_OUT ? UQ_4_12(1.33) : UQ_4_12(0.00)), damage[1]);
    }
}

SINGLE_BATTLE_TEST("Lash Out damage is boosted on turn 1 by switch in abilities")
{
#if PLATFORM_NATIVE
    // Known failing natively: the test's expectation is wrong under either
    // reading of the mechanic. The battle gives 36 then 36 after Intimidate
    // and 88 then 88 after Supersweet Syrup, the same on the GBA: expansion
    // clears lashOutAffected after the battle-start events (TurnValuesCleanUp
    // at FIRST_TURN_EVENTS_END), so turn 1 is not boosted and the turns are
    // equal; Showdown keeps the battle-start drop for turn 1, which would make
    // turn 1 twice turn 2. The test wants turn 2 to be 1.33 and 2 times turn
    // 1, which neither gives. Whether turn 1 should be boosted is a
    // difference between expansion and Showdown, not this port's.
    // On the GBA, and natively without the capture replay, THEN's call runs
    // the initialiser again over the captured values and compares zeros
    // (platform/test_runner/SUITE-LOG.md, class 11, has the measurements).
    KNOWN_FAILING;
#endif
#line 87
    s16 damage[2] = {0};
    enum Species species = SPECIES_NONE;
    enum Ability ability = ABILITY_NONE;

    PARAMETRIZE { species = SPECIES_INCINEROAR, ability = ABILITY_BLAZE; }
    PARAMETRIZE { species = SPECIES_INCINEROAR, ability = ABILITY_INTIMIDATE; }
    PARAMETRIZE { species = SPECIES_HYDRAPPLE,  ability = ABILITY_REGENERATOR; }
    PARAMETRIZE { species = SPECIES_HYDRAPPLE,  ability = ABILITY_SUPERSWEET_SYRUP; }

    GIVEN {
        PLAYER(SPECIES_GRIMMSNARL) { Moves(MOVE_LASH_OUT); }
        OPPONENT(species) { Ability(ability); }
    } WHEN {
        TURN { MOVE(player, MOVE_LASH_OUT); }
        TURN { MOVE(player, MOVE_LASH_OUT); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[0]);

        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, player);
        HP_BAR(opponent, captureDamage: &damage[1]);

    } THEN {
        if (ability == ABILITY_INTIMIDATE)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(1.33) , damage[1]);
        else if (ability == ABILITY_SUPERSWEET_SYRUP)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(2.00) , damage[1]);
        else
            EXPECT_EQ(damage[0], damage[1]);
    }
}

DOUBLE_BATTLE_TEST("Lash Out damage is boosted by Cotton Down activation in doubles")
{
#if PLATFORM_NATIVE
    // Known failing natively: an upstream test bug, EXPECT_MUL_EQ's operands
    // the wrong way round. Cotton Down's Speed drop boosts turn 1 (150), not
    // turn 2 (76), the same on the GBA; the test wants turn 2 to be twice
    // turn 1 (EXPECT_MUL_EQ(damage[1], UQ_4_12(2.00), damage[0]) holds).
    // On the GBA, and natively without the capture replay, THEN's call runs
    // the initialiser again over the captured values and compares zeros
    // (platform/test_runner/SUITE-LOG.md, class 11, has the measurements).
    KNOWN_FAILING;
#endif
#line 121
    s16 damage[2] = {0};
    enum Ability ability = ABILITY_NONE;

    PARAMETRIZE { ability = ABILITY_REGENERATOR; }
    PARAMETRIZE { ability = ABILITY_COTTON_DOWN; }

    GIVEN {
        PLAYER(SPECIES_GRIMMSNARL)  { Speed(1); Moves(MOVE_LASH_OUT); }
        PLAYER(SPECIES_RATTATA)     { Speed(2); Moves(MOVE_TACKLE, MOVE_CELEBRATE); }
        OPPONENT(SPECIES_ELDEGOSS)  { Speed(3); Ability(ability); }
        OPPONENT(SPECIES_GOLEM)     { Speed(4); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_LASH_OUT, target:opponentRight); MOVE(playerRight, MOVE_TACKLE, target:opponentLeft); }
        TURN { MOVE(playerLeft, MOVE_LASH_OUT, target:opponentRight); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_TACKLE, playerRight);
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, playerLeft);
        HP_BAR(opponentRight, captureDamage: &damage[0]);

        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, playerLeft);
        HP_BAR(opponentRight, captureDamage: &damage[1]);

    } THEN {
        if (ability == ABILITY_COTTON_DOWN)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(2.00) , damage[1]);
        else
            EXPECT_EQ(damage[0], damage[1]);
    }
}

DOUBLE_BATTLE_TEST("Lash Out damage is not boosted by Treasure of Ruin ability activation in doubles")
{
    s16 damage[2] = {0};
    enum Species species = SPECIES_NONE;
    enum Ability ability = ABILITY_NONE;

    PARAMETRIZE { species = SPECIES_KANGASKHAN, ability = ABILITY_INNER_FOCUS; }
    PARAMETRIZE { species = SPECIES_HYDRAPPLE,  ability = ABILITY_SUPERSWEET_SYRUP; }
    PARAMETRIZE { species = SPECIES_WO_CHIEN,   ability = ABILITY_TABLETS_OF_RUIN; }
    PARAMETRIZE { species = SPECIES_CHIEN_PAO,  ability = ABILITY_SWORD_OF_RUIN; }
    PARAMETRIZE { species = SPECIES_TING_LU,    ability = ABILITY_VESSEL_OF_RUIN; }
    PARAMETRIZE { species = SPECIES_CHI_YU,     ability = ABILITY_BEADS_OF_RUIN; }

    GIVEN {
        PLAYER(SPECIES_GRIMMSNARL)  { Moves(MOVE_LASH_OUT); }
        PLAYER(SPECIES_RATTATA);
        OPPONENT(SPECIES_WOBBUFFET);
        OPPONENT(SPECIES_GOLEM);
        OPPONENT(species)  { Ability(ability); }
    } WHEN {
        TURN { MOVE(playerLeft, MOVE_LASH_OUT, target:opponentRight); }
        TURN { MOVE(playerLeft, MOVE_LASH_OUT, target:opponentRight); SWITCH(opponentLeft, 2); }
    } SCENE {
        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, playerLeft);
        HP_BAR(opponentRight, captureDamage: &damage[0]);

        ANIMATION(ANIM_TYPE_MOVE, MOVE_LASH_OUT, playerLeft);
        HP_BAR(opponentRight, captureDamage: &damage[1]);

    } THEN {
        if (ability == ABILITY_SUPERSWEET_SYRUP)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(2.00) , damage[1]);
        else if (ability == ABILITY_TABLETS_OF_RUIN)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(0.75) , damage[1]);
        else if (ability == ABILITY_SWORD_OF_RUIN)
            EXPECT_MUL_EQ(damage[0], UQ_4_12(1.33) , damage[1]);
        else
            EXPECT_EQ(damage[0], damage[1]);
    }
}
