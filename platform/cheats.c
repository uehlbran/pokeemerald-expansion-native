/*
 * cheats.c: the bubble menu's cheats (cheats.h), as calls to expansion's own
 * functions. Compiled with the game's defines (platform/Makefile,
 * GAME_FLAG_OBJS), as inspect.c is, because it reads the game's variables
 * and calls its functions.
 */
#include "cheats.h"

#include <stddef.h>

#include "global.h"
#include "config/overworld.h"
#include "config/wild_encounter.h"
#include "constants/items.h"
#include "event_data.h"
#include "item.h"
#include "main.h"
#include "money.h"
#include "overworld.h"
#include "script_pokemon_util.h"

bool cheats_is_toggle(enum cheat cheat)
{
    return cheat == CHEAT_WALK_THROUGH_WALLS || cheat == CHEAT_NO_ENCOUNTERS || cheat == CHEAT_TRAINERS_IGNORE;
}

bool cheats_available(void)
{
    return gMain.callback2 == CB2_Overworld && gSaveBlock1Ptr != NULL && gSaveBlock2Ptr != NULL;
}

/* The flag a toggle flips, or 0 where the config gives it none. */
static u16 toggle_flag(enum cheat cheat)
{
    switch (cheat) {
    case CHEAT_WALK_THROUGH_WALLS:
        return OW_FLAG_NO_COLLISION;
    case CHEAT_NO_ENCOUNTERS:
        return WE_FLAG_NO_ENCOUNTER;
    case CHEAT_TRAINERS_IGNORE:
        return OW_FLAG_NO_TRAINER_SEE;
    default:
        return 0;
    }
}

static enum cheat_result add_items(u16 item)
{
    return AddBagItem(item, CHEATS_ITEM_COUNT) ? CHEAT_DONE : CHEAT_BAG_FULL;
}

enum cheat_result cheats_run(enum cheat cheat)
{
    if ((unsigned)cheat >= CHEAT_COUNT)
        return CHEAT_NO_SUCH_CHEAT;
    if (!cheats_available())
        return CHEAT_NOT_NOW;
    switch (cheat) {
    case CHEAT_HEAL_PARTY:
        HealPlayerParty();
        return CHEAT_DONE;
    case CHEAT_MAX_MONEY:
        SetMoney(&gSaveBlock1Ptr->money, MAX_MONEY);
        return CHEAT_DONE;
    case CHEAT_RARE_CANDIES:
        return add_items(ITEM_RARE_CANDY);
    case CHEAT_MASTER_BALLS:
        return add_items(ITEM_MASTER_BALL);
    default:
        break;
    }
    const u16 flag = toggle_flag(cheat);
    if (flag == 0)
        return CHEAT_UNSUPPORTED;
    FlagToggle(flag);
    return CHEAT_DONE;
}

bool cheats_on(enum cheat cheat)
{
    const u16 flag = cheats_is_toggle(cheat) ? toggle_flag(cheat) : 0;
    return flag != 0 && cheats_available() && FlagGet(flag);
}
