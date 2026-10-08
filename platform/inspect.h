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
 *   player_name       gSaveBlock2Ptr->playerName as ASCII
 *   player_gender     MALE or FEMALE
 *   map               gSaveBlock1Ptr->location as "group.number"
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
 */
#ifndef PLATFORM_INSPECT_H
#define PLATFORM_INSPECT_H

#include <stddef.h>

/* The longest value a fact has, including the NUL. */
#define INSPECT_VALUE_SIZE 64u

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
