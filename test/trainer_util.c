#include "global.h"
#include "data.h"
#include "random.h"
#include "trainer_util.h"
#include "constants/battle_ai.h"
#include "constants/items.h"
#include "constants/trainers.h"
#include "test/test.h"

// Native only: the party seed (src/trainer_util.c, TrainerDataCrc32) hashes the
// trainer's bytes without the party table's address, so it is a property of the
// trainer's data and the player's ID. The test build has no trainer table
// (src/data.c), so these are copies of three early trainers' data (Calvin,
// the Route 103 Brendan, Roxanne) with a dummy party. The constants come from
// the 32-bit build and pin its value; the 64-bit build, whose struct is 4
// bytes bigger, must hash the same bytes and so match them.
#if PLATFORM_NATIVE

static const struct TrainerMon sDummyPartyA[1] = { { .lvl = 5 } };
static const struct TrainerMon sDummyPartyB[1] = { { .lvl = 6 } };

static const struct Trainer sCalvin =
{
    .trainerName = _("CALVIN"),
    .trainerClass = TRAINER_CLASS_YOUNGSTER,
    .trainerPic = TRAINER_PIC_YOUNGSTER,
    .gender = TRAINER_GENDER_MALE,
    .encounterMusic = TRAINER_ENCOUNTER_MUSIC_MALE,
    .battleType = TRAINER_BATTLE_TYPE_SINGLES,
    .aiFlags = AI_FLAG_CHECK_BAD_MOVE,
    .multiTeamSize = MULTI_TEAM_SIZE_FULL,
    .partySize = 1,
    .party = sDummyPartyA,
};

static const struct Trainer sBrendan =
{
    .trainerName = _("BRENDAN"),
    .trainerClass = TRAINER_CLASS_RIVAL,
    .trainerPic = TRAINER_PIC_BRENDAN,
    .gender = TRAINER_GENDER_MALE,
    .encounterMusic = TRAINER_ENCOUNTER_MUSIC_MALE,
    .battleType = TRAINER_BATTLE_TYPE_SINGLES,
    .aiFlags = AI_FLAG_CHECK_BAD_MOVE | AI_FLAG_TRY_TO_FAINT | AI_FLAG_FORCE_SETUP_FIRST_TURN,
    .multiTeamSize = MULTI_TEAM_SIZE_FULL,
    .partySize = 1,
    .party = sDummyPartyA,
};

static const struct Trainer sRoxanne =
{
    .trainerName = _("ROXANNE"),
    .trainerClass = TRAINER_CLASS_LEADER,
    .trainerPic = TRAINER_PIC_LEADER_ROXANNE,
    .gender = TRAINER_GENDER_FEMALE,
    .encounterMusic = TRAINER_ENCOUNTER_MUSIC_FEMALE,
    .items = { ITEM_POTION, ITEM_POTION },
    .battleType = TRAINER_BATTLE_TYPE_SINGLES,
    .aiFlags = AI_FLAG_BASIC_TRAINER,
    .multiTeamSize = MULTI_TEAM_SIZE_FULL,
    .partySize = 3,
    .party = sDummyPartyA,
};

static void SetPlayerId(void)
{
    gSaveBlock2Ptr->playerTrainerId[0] = 0x12;
    gSaveBlock2Ptr->playerTrainerId[1] = 0x34;
    gSaveBlock2Ptr->playerTrainerId[2] = 0x56;
    gSaveBlock2Ptr->playerTrainerId[3] = 0x78;
}

static void ExpectSeed(const struct Trainer *trainer, u32 a, u32 b, u32 c, u32 ctr)
{
    rng_value_t seed = GeneratePartySeed(trainer);
    EXPECT_EQ(seed.a, a);
    EXPECT_EQ(seed.b, b);
    EXPECT_EQ(seed.c, c);
    EXPECT_EQ(seed.ctr, ctr);
}

TEST("The trainer party seed is the same at 32 and 64 bits")
{
    SetPlayerId();
    ExpectSeed(&sCalvin, 2902204328u, 46989602u, 2644691145u, 17);
    ExpectSeed(&sBrendan, 3741775284u, 933305786u, 2072414657u, 17);
    ExpectSeed(&sRoxanne, 2912847067u, 4275315168u, 3306250338u, 17);
}

TEST("The trainer party seed does not depend on the party table's address")
{
    struct Trainer moved = sCalvin;
    SetPlayerId();
    moved.party = sDummyPartyB;
    EXPECT_EQ(GeneratePartySeed(&moved).a, GeneratePartySeed(&sCalvin).a);
    EXPECT_EQ(GeneratePartySeed(&moved).b, GeneratePartySeed(&sCalvin).b);
}

#endif
