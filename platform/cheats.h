/*
 * cheats.h: the cheats the bubble menu (bubble_menu.h) offers with one
 * click, done by calling expansion's own functions between two frames.
 *
 * Not hardware: a GBA had no cheats but the codes a GameShark wrote into its
 * RAM, and those are written for the retail ROM's addresses, which mean
 * nothing in expansion's. Each cheat here is a call expansion's own debug
 * menu makes too (src/debug.c): HealPlayerParty, SetMoney to MAX_MONEY,
 * AddBagItem, and FlagToggle on the flags its config gives the overworld's
 * "no collision", "no wild encounters" and "trainers do not see you"
 * (include/config/overworld.h and wild_encounter.h, which the native build
 * points at three unused flags: docs/UPSTREAM_MODIFICATIONS.md). They act
 * only in the overworld (the field's callback, CB2_Overworld, with the save
 * blocks there), where the debug menu acts too; anywhere else a cheat
 * changes nothing and says so.
 *
 * This header names no game type, so that the menu and its tests can use
 * it; cheats.c is compiled with the game's defines (platform/Makefile,
 * GAME_FLAG_OBJS) and runs on the main thread, from the frame loop's
 * events, between two of the game's frames.
 */
#ifndef PLATFORM_CHEATS_H
#define PLATFORM_CHEATS_H

#include <stdbool.h>

/* How many of an item the item cheats add. */
#define CHEATS_ITEM_COUNT 99u

enum cheat {
    CHEAT_HEAL_PARTY,
    CHEAT_MAX_MONEY,
    CHEAT_RARE_CANDIES,
    CHEAT_MASTER_BALLS,
    CHEAT_WALK_THROUGH_WALLS, /* a toggle */
    CHEAT_NO_ENCOUNTERS,      /* a toggle */
    CHEAT_TRAINERS_IGNORE,    /* a toggle */
    CHEAT_COUNT
};

enum cheat_result {
    CHEAT_DONE,
    CHEAT_NOT_NOW,      /* not in the overworld */
    CHEAT_BAG_FULL,     /* AddBagItem found no room */
    CHEAT_UNSUPPORTED,  /* a toggle whose flag the config leaves at 0 */
    CHEAT_NO_SUCH_CHEAT /* not one of the enum */
};

/* Whether toggles are toggles (the others act once). Pure. */
bool cheats_is_toggle(enum cheat cheat);

/* Whether the cheats can act now: the overworld, with the save blocks. */
bool cheats_available(void);

/* Does the cheat, or says why not. */
enum cheat_result cheats_run(enum cheat cheat);

/* Whether a toggle is on now; false for the others and outside the
 * overworld. */
bool cheats_on(enum cheat cheat);

#endif /* PLATFORM_CHEATS_H */
