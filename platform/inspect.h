/*
 * inspect.h: named facts about the running game, for the replay's `expect`
 * and `wait-until` and the `dump` files.
 *
 * Not hardware: the debugger's view. Each fact is a short string read from
 * expansion's own variables through their declared symbols; nothing is
 * written, nothing of expansion is modified. inspect.c is a documented
 * exception to the rule that platform files do not include expansion's
 * headers (like m4a_output.c): reading the game's data structures is its
 * whole job. Call it from the game's thread, between frames (the replay hook
 * runs there).
 *
 * The facts, in the order `dump` writes them:
 *   frame             frames the platform has run (frame_count)
 *   callback2         gMain.callback2 by name: a table of the game's
 *                     globally declared callbacks first, then any function
 *                     of the executable by its ELF symbol (symbols.h), else
 *                     "unknown:0x<address>"; "none" when NULL
 *   task_count        active tasks (gTasks)
 *   task_first        the function of the active task with the lowest id,
 *                     named as callback2 is ("none" without one)
 *   task_last         the function of the active task with the highest id (the most recently
 *                     created menu or message task, usually)
 *   tasks             every active task's function, by id, comma separated
 *                     (cut with "..." at the value's size)
 *   fade_active       whether a palette fade is in progress, 0 or 1
 *   intro_state, town_state, rival_state
 *                     VAR_LITTLEROOT_INTRO_STATE, VAR_LITTLEROOT_TOWN_STATE and
 *                     VAR_LITTLEROOT_RIVAL_STATE: the opening's progress
 *   keys_held         the keys the game's last ReadKeys saw held (gMain.heldKeysRaw), "0x%04X"
 *   keys_new          the keys it saw newly pressed that frame (gMain.newKeysRaw)
 *   player_name       gSaveBlock2Ptr->playerName as ASCII, trailing spaces cut
 *   player_gender     MALE or FEMALE
 *   map               gSaveBlock1Ptr->location as "group.number"
 *   map_name          the MAP_* constant of map, for the maps the replay visits ("unknown" otherwise)
 *   pos               gSaveBlock1Ptr->pos as "x,y" (map tiles)
 *   party_count       gPartiesCount[B_TRAINER_PLAYER]
 *   party0_species    the species number of the first party Pokemon (0: none)
 *   party0_name       its species name ("none" without one)
 *   in_battle         gMain.inBattle, 0 or 1
 *   script_running    whether the global script context runs, 0 or 1
 *   controls_locked   whether the player's field controls are locked, 0 or 1
 *   save_counter      gSaveCounter, the save slot's counter
 *   save_status       gSaveFileStatus
 *   flash_dirty_writes flash_write_count: programs and erases the game has done
 *   party_species     every party slot's species number, comma separated ("none" without a party)
 *   pc_box0_count     Pokemon in PC box 1 (index 0)
 *   summary_species   species shown by the summary screen while it is open from the party menu
 *                     (the party menu's slot at the time it was opened), else 0
 *   debug_menu_open   whether a task of the debug menu (src/debug.c) is active, 0 or 1
 *   debug_input       the number in the debug menu's numeric entry ("none" when no entry waits for input)
 *   debug_step        its step "STEP.SUBSTEP" ("none" likewise); a new step is how a script knows a
 *                     confirmed number was taken
 *   party_slot        gPartyMenu.slotId, the party menu's cursor
 *
 * Facts with an argument, "name:argument" (the argument is plain decimal and in range, or the
 * fact does not exist). They are read by `expect` and `wait-until` and are not in `dump`:
 *   party_species:N   species of party slot N (0 to 5; 0 when the slot is empty)
 *   party_name:N      its species name ("none" when empty)
 *   party_level:N     its level
 *   party_extras:N    "shiny=S dynamax=D gmax=G tera=T": what the debug menu's complex give sets
 *   bag_item_count:ID quantity of item ID in the bag (CountTotalItemQuantityInBag)
 *   pc_box_count:N    Pokemon in PC box N (0 to 13)
 *   pc_box_species:N  species in PC box N, by position, comma separated ("none" when empty)
 *   flag:ID           FlagGet(ID), 0 or 1 (ID below FLAGS_COUNT)
 *   var:ID            VarGet(ID) (ID from VARS_START to 65535)
 */
#ifndef PLATFORM_INSPECT_H
#define PLATFORM_INSPECT_H

#include <stddef.h>

/* The longest value a fact has, including the NUL. */
#define INSPECT_VALUE_SIZE 256u

/* Loads the executable's symbols for naming callbacks. A failure is
 * reported and is not fatal: callbacks outside the table then read
 * "unknown:0x...". Returns 0 or -1 for that. */
int inspect_init(void);

/* Releases what inspect_init loaded. Safe before it and twice. */
void inspect_shutdown(void);

/* Writes the fact's value to out (size bytes, NUL-terminated, cut if too
 * long). 0, or -1 if there is no fact of that name. */
int inspect_get(const char *name, char *out, size_t size);

/* The name of fact `index`, NULL past the last. */
const char *inspect_fact_name(size_t index);

#endif /* PLATFORM_INSPECT_H */
