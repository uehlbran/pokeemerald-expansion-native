#include "global.h"
#include "battle_setup.h"
#include "constants/opponents.h"
#include "constants/script_commands.h"
#include "test/overworld_script.h"
#include "test/test.h"

// Native only: the trainerbattle macro (asm/macros/event.inc) writes its pointers as
// 4-byte words, which script bytecode keeps at 64 bits, while TrainerBattleParameter's
// are 8 bytes. TrainerBattleLoadArgs and TrainerBattleLoadArgsSecondTrainer must read
// the script's own layout at both widths (src/battle_setup.c). Before the fix, at 64
// bits, the rival on Route 103 faulted in ShowTrainerIntroSpeech: its NULL intro text
// was read together with the defeat text's word as one pointer.
#if PLATFORM_NATIVE

extern const u8 Route103_EventScript_OpenAlteringCave[];
extern const u8 Route103_EventScript_Rival[];
extern const u8 Route103_EventScript_RivalMay[];
extern const u8 Route103_EventScript_RivalBrendan[];
extern const u8 Route103_EventScript_StartMayBattleTreecko[];
extern const u8 Route103_EventScript_StartMayBattleTorchic[];
extern const u8 Route103_EventScript_StartMayBattleMudkip[];
extern const u8 Route103_EventScript_AfterMayBattle[];
extern const u8 Route103_EventScript_AfterBrendanBattle[];

// The size of the trainerbattle command's arguments as the macro lays them out.
#define TRAINERBATTLE_SCRIPT_ARGS_SIZE 40

static uintptr_t ReadScriptWord(const u8 *bytes)
{
    return (uintptr_t)((u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24));
}

TEST("trainerbattle_no_intro of the rival on Route 103 is read as the script lays it out")
{
    const u8 *script = Route103_EventScript_StartMayBattleTreecko;
    const u8 *after;

    ASSUME(script[0] == SCR_OP_TRAINERBATTLE);
    TrainerBattleLoadArgs(script + 1);

    EXPECT_EQ(TRAINER_BATTLE_PARAM.opponentA, TRAINER_MAY_ROUTE_103_TREECKO);
    EXPECT(TRAINER_BATTLE_PARAM.introTextA == NULL);
    EXPECT_EQ((uintptr_t)TRAINER_BATTLE_PARAM.defeatTextA, ReadScriptWord(script + 1 + 8));
    EXPECT(TRAINER_BATTLE_PARAM.defeatTextA != NULL);
    EXPECT(TRAINER_BATTLE_PARAM.battleScriptRetAddrA == NULL);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.opponentB, TRAINER_NONE);
    EXPECT(TRAINER_BATTLE_PARAM.introTextB == NULL);
    EXPECT(TRAINER_BATTLE_PARAM.victoryText == NULL);
    EXPECT(TRAINER_BATTLE_PARAM.cannotBattleText == NULL);
    EXPECT(TRAINER_BATTLE_PARAM.playMusicA);
    EXPECT(TRAINER_BATTLE_PARAM.continueScript);
    EXPECT(TRAINER_BATTLE_PARAM.skipFlagCheck);
    EXPECT(!TRAINER_BATTLE_PARAM.facePlayer);

    // The script goes on after the 40 bytes: goto Route103_EventScript_AfterMayBattle.
    after = BattleSetup_GetScriptAddrAfterBattle();
    EXPECT(after == script + 1 + TRAINERBATTLE_SCRIPT_ARGS_SIZE);
    EXPECT_EQ(after[0], SCR_OP_GOTO);
    EXPECT_EQ(ReadScriptWord(after + 1), (uintptr_t)Route103_EventScript_AfterMayBattle);
}

TEST("Every field of a trainerbattle is read from the script's offsets, for trainer A and trainer B")
{
    // Distinct values in every field: eight pointers, two local ids, two trainers, the flag bits and the rival flags.
    const u8 *script = OVERWORLD_SCRIPT(
        trainerbattle 7, TRAINER_MAY_ROUTE_103_TREECKO, Route103_EventScript_OpenAlteringCave, Route103_EventScript_Rival, Route103_EventScript_RivalMay, 9, TRAINER_MAY_ROUTE_103_TORCHIC, Route103_EventScript_RivalBrendan, Route103_EventScript_StartMayBattleTreecko, Route103_EventScript_StartMayBattleTorchic, Route103_EventScript_StartMayBattleMudkip, Route103_EventScript_AfterMayBattle, 1, 0, 1, 0, 1, 0, 1, 0, 0x5A;
        goto Route103_EventScript_AfterBrendanBattle;
    );

    TrainerBattleLoadArgs(script + 1);

    EXPECT_EQ(TRAINER_BATTLE_PARAM.objEventLocalIdA, 7);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.opponentA, TRAINER_MAY_ROUTE_103_TREECKO);
    EXPECT(TRAINER_BATTLE_PARAM.introTextA == Route103_EventScript_OpenAlteringCave);
    EXPECT(TRAINER_BATTLE_PARAM.defeatTextA == Route103_EventScript_Rival);
    EXPECT(TRAINER_BATTLE_PARAM.battleScriptRetAddrA == Route103_EventScript_RivalMay);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.objEventLocalIdB, 9);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.opponentB, TRAINER_MAY_ROUTE_103_TORCHIC);
    EXPECT(TRAINER_BATTLE_PARAM.introTextB == Route103_EventScript_RivalBrendan);
    EXPECT(TRAINER_BATTLE_PARAM.defeatTextB == Route103_EventScript_StartMayBattleTreecko);
    EXPECT(TRAINER_BATTLE_PARAM.battleScriptRetAddrB == Route103_EventScript_StartMayBattleTorchic);
    EXPECT(TRAINER_BATTLE_PARAM.victoryText == Route103_EventScript_StartMayBattleMudkip);
    EXPECT(TRAINER_BATTLE_PARAM.cannotBattleText == Route103_EventScript_AfterMayBattle);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.rivalBattleFlags, 0x5A);
    // The macro's flag arguments, in its order: isDouble 1, playMusicA 0, playMusicB 1, isRematch 0,
    // continueScript 1, facePlayer 0, earlyRival 1, skipFlagCheck 0.
    EXPECT(TRAINER_BATTLE_PARAM.isDoubleBattle);
    EXPECT(!TRAINER_BATTLE_PARAM.playMusicA);
    EXPECT(TRAINER_BATTLE_PARAM.playMusicB);
    EXPECT(!TRAINER_BATTLE_PARAM.isRematch);
    EXPECT(TRAINER_BATTLE_PARAM.continueScript);
    EXPECT(!TRAINER_BATTLE_PARAM.facePlayer);
    EXPECT(TRAINER_BATTLE_PARAM.earlyRival);
    EXPECT(!TRAINER_BATTLE_PARAM.skipFlagCheck);
    EXPECT(BattleSetup_GetScriptAddrAfterBattle() == script + 1 + TRAINERBATTLE_SCRIPT_ARGS_SIZE);
    EXPECT_EQ(script[1 + TRAINERBATTLE_SCRIPT_ARGS_SIZE], SCR_OP_GOTO);

    // A second approaching trainer's script: its trainer A fields become trainer B.
    TrainerBattleLoadArgsSecondTrainer(script + 1);

    EXPECT_EQ(TRAINER_BATTLE_PARAM.objEventLocalIdB, 7);
    EXPECT_EQ(TRAINER_BATTLE_PARAM.opponentB, TRAINER_MAY_ROUTE_103_TREECKO);
    EXPECT(TRAINER_BATTLE_PARAM.introTextB == Route103_EventScript_OpenAlteringCave);
    EXPECT(TRAINER_BATTLE_PARAM.defeatTextB == Route103_EventScript_Rival);
    EXPECT(TRAINER_BATTLE_PARAM.battleScriptRetAddrB == Route103_EventScript_RivalMay);
    EXPECT(!TRAINER_BATTLE_PARAM.playMusicB); // playMusicA's 0 over the 1 above
}

#endif
