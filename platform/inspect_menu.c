/*
 * inspect_menu.c: the facts of the menus (inspect.h): the debug menu
 * (src/debug.c), the party menu and summary screen, the bag, PC boxes, and the
 * facts with arguments that read them. Split from inspect.c, which has the
 * game-state facts; the table of argument ranges is inspect_args.c.
 *
 * Followed: expansion's own headers for every layout; built with the game's
 * defines (see platform/Makefile).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "inspect_decode.h"
#include "inspect_internal.h"
#include "symbols.h"

#include "global.h"
#include "constants/species.h"
#include "item.h"
#include "load_save.h"
#include "party_menu.h"
#include "pokemon_summary_screen.h"
#include "pokemon.h"
#include "task.h"

/* ---- The debug menu (src/debug.c, DEBUG_OVERWORLD_MENU) ------------------ */

/* Every task of the debug menu runs a static function named DebugTask_* (the
 * list menus) or DebugAction_* (numeric entry, the selection steps); the
 * symbol table names them. */
#define DEBUG_MENU_TASK_PREFIX "Debug"
#define DEBUG_ENTRY_FUNCTION "DebugAction_Selection_StepUpdate"
/* src/debug.c: tInput is data[3] of the entry task (`#define tInput data[3]`). */
#define DEBUG_ENTRY_INPUT_DATA 3u
/* tSubstep is data[10], tStep data[11]. */
#define DEBUG_ENTRY_SUBSTEP_DATA 10u
#define DEBUG_ENTRY_STEP_DATA 11u

static bool task_function_has_prefix(size_t task, const char *prefix)
{
    const char *name = symbols_name_at((uintptr_t)gTasks[task].func);
    return name != NULL && strncmp(name, prefix, strlen(prefix)) == 0;
}

static void fact_debug_menu_open(char *out, size_t size)
{
    unsigned open = 0;
    for (size_t i = 0; i < NUM_TASKS; i++)
        open |= (gTasks[i].isActive && task_function_has_prefix(i, DEBUG_MENU_TASK_PREFIX)) ? 1u : 0u;
    snprintf(out, size, "%u", open);
}

/* The active numeric-entry task (src/debug.c: DebugAction_Selection_StepUpdate), or NULL. */
static const struct Task *debug_entry_task(void)
{
    for (size_t i = 0; i < NUM_TASKS; i++) {
        const char *name = symbols_name_at((uintptr_t)gTasks[i].func);
        if (gTasks[i].isActive && name != NULL && strcmp(name, DEBUG_ENTRY_FUNCTION) == 0)
            return &gTasks[i];
    }
    return NULL;
}

/* The number being entered in the debug menu's numeric entry (species, level,
 * item id, flag id, ...), "none" when no entry is waiting for input. */
static void fact_debug_input(char *out, size_t size)
{
    const struct Task *task = debug_entry_task();
    if (task == NULL)
        snprintf(out, size, "%s", inspect_none_text);
    else
        snprintf(out, size, "%u", (unsigned)(uint16_t)task->data[DEBUG_ENTRY_INPUT_DATA]);
}

/* Which step of the selection the entry is on, "STEP.SUBSTEP" (the complex Pokemon give
 * has twelve steps, the IVs and EVs six substeps each); "none" when no entry is
 * waiting for input. It tells a new step from the previous one that held the
 * same number. */
static void fact_debug_step(char *out, size_t size)
{
    const struct Task *task = debug_entry_task();
    if (task == NULL)
        snprintf(out, size, "%s", inspect_none_text);
    else
        snprintf(out, size, "%d.%d", task->data[DEBUG_ENTRY_STEP_DATA], task->data[DEBUG_ENTRY_SUBSTEP_DATA]);
}

/* ---- Party, bag, storage -------------------------------------------------- */

static unsigned party_species_at(unsigned slot)
{
    if (slot >= inspect_party_count() || slot >= PARTY_SIZE)
        return 0;
    return GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_SPECIES);
}

static void write_species_name(unsigned species, char *out, size_t size)
{
    if (species == SPECIES_NONE || species >= NUM_SPECIES) {
        snprintf(out, size, "%s", inspect_none_text);
        return;
    }
    (void)inspect_decode_text(gSpeciesInfo[species].speciesName, sizeof gSpeciesInfo[species].speciesName, out, size);
}

/* Every party slot's species, comma separated ("none" for an empty party). */
static void fact_party_species(char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (unsigned slot = 0; slot < inspect_party_count() && slot < PARTY_SIZE; slot++) {
        int written = snprintf(out + used, size - used, "%s%u", used > 0 ? "," : "", party_species_at(slot));
        if (written < 0 || (size_t)written >= size - used)
            return;
        used += (size_t)written;
    }
    if (used == 0)
        snprintf(out, size, "%s", inspect_none_text);
}

/* Pokemon in PC box `box`; 0 before the storage is allocated (title screen). */
static unsigned box_count(unsigned box)
{
    unsigned count = 0;
    if (gPokemonStoragePtr == NULL)
        return 0;
    for (unsigned position = 0; position < IN_BOX_COUNT; position++)
        count += GetBoxMonData(&gPokemonStoragePtr->boxes[box][position], MON_DATA_SANITY_HAS_SPECIES) ? 1u : 0u;
    return count;
}

static void fact_pc_box0_count(char *out, size_t size)
{
    snprintf(out, size, "%u", box_count(0));
}

/* The species of the Pokemon the summary screen shows. The screen's own state is
 * static in pokemon_summary_screen.c; gInitialSummaryScreenCallback is set
 * while it is open from the party or PC menu, and the party menu's slot is the
 * Pokemon it was opened for (moving to another Pokemon inside the summary
 * screen is not followed). 0 when it is not open. */
static void fact_summary_species(char *out, size_t size)
{
    unsigned species = 0;
    if (gInitialSummaryScreenCallback != NULL)
        species = party_species_at((unsigned)(uint8_t)gPartyMenu.slotId); /* -1 (none) becomes 255: no such slot */
    snprintf(out, size, "%u", species);
}

/* The party menu's cursor slot (gPartyMenu.slotId), -1 when none. */
static void fact_party_slot(char *out, size_t size)
{
    snprintf(out, size, "%d", gPartyMenu.slotId);
}

/* ---- Facts with an argument: "name:argument" -------------------------------- */

static void arg_party_species(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t slot = arguments[0];
    snprintf(out, size, "%u", party_species_at(slot));
}

static void arg_party_name(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t slot = arguments[0];
    write_species_name(party_species_at(slot), out, size);
}

static void arg_party_level(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t slot = arguments[0];
    unsigned level = 0;
    if (slot < inspect_party_count() && slot < PARTY_SIZE)
        level = GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_LEVEL);
    snprintf(out, size, "%u", level);
}

static void arg_bag_item_count(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t item = arguments[0];
    /* the bag's pockets point into the save block, which does not exist before the game starts */
    unsigned count = gSaveBlock1Ptr != NULL ? CountTotalItemQuantityInBag((enum Item)item) : 0;
    snprintf(out, size, "%u", count);
}

static void arg_pc_box_count(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t box = arguments[0];
    snprintf(out, size, "%u", box_count(box));
}

/* The species in PC box `box`, by position, comma separated ("none" for an empty box). */
static void arg_pc_box_species(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t box = arguments[0];
    size_t used = 0;
    out[0] = '\0';
    if (gPokemonStoragePtr != NULL) {
        for (unsigned position = 0; position < IN_BOX_COUNT; position++) {
            struct BoxPokemon *mon = &gPokemonStoragePtr->boxes[box][position];
            if (!GetBoxMonData(mon, MON_DATA_SANITY_HAS_SPECIES))
                continue;
            int written = snprintf(out + used, size - used, "%s%u", used > 0 ? "," : "",
                                   (unsigned)GetBoxMonData(mon, MON_DATA_SPECIES));
            if (written < 0 || (size_t)written >= size - used)
                return;
            used += (size_t)written;
        }
    }
    if (used == 0)
        snprintf(out, size, "%s", inspect_none_text);
}

/* The properties the debug menu's complex give sets, of a party Pokemon: "shiny=0 dynamax=0 gmax=0 tera=1". */
static void arg_party_extras(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t slot = arguments[0];
    if (slot >= inspect_party_count() || slot >= PARTY_SIZE) {
        snprintf(out, size, "%s", inspect_none_text);
        return;
    }
    struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][slot];
    snprintf(out, size, "shiny=%u dynamax=%u gmax=%u tera=%u", (unsigned)GetMonData(mon, MON_DATA_IS_SHINY),
             (unsigned)GetMonData(mon, MON_DATA_DYNAMAX_LEVEL), (unsigned)GetMonData(mon, MON_DATA_GIGANTAMAX_FACTOR),
             (unsigned)GetMonData(mon, MON_DATA_TERA_TYPE));
}

/* Move MOVE (0 to 3) of party slot SLOT: its move number, 0 for an empty slot or move. */
static void arg_party_move(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    const uint32_t slot = arguments[0];
    const uint32_t move = arguments[1];
    unsigned id = 0;
    if (slot < inspect_party_count() && slot < PARTY_SIZE)
        id = GetMonData(&gParties[B_TRAINER_PLAYER][slot], MON_DATA_MOVE1 + (s32)move);
    snprintf(out, size, "%u", id);
}

const struct inspect_arg_reader inspect_menu_arg_readers[] = {
    { INSPECT_ARG_PARTY_SPECIES, arg_party_species },
    { INSPECT_ARG_PARTY_NAME, arg_party_name },
    { INSPECT_ARG_PARTY_LEVEL, arg_party_level },
    { INSPECT_ARG_PARTY_EXTRAS, arg_party_extras },
    { INSPECT_ARG_PARTY_MOVE, arg_party_move },
    { INSPECT_ARG_BAG_ITEM_COUNT, arg_bag_item_count },
    { INSPECT_ARG_PC_BOX_COUNT, arg_pc_box_count },
    { INSPECT_ARG_PC_BOX_SPECIES, arg_pc_box_species },
};
const size_t inspect_menu_arg_reader_count = sizeof inspect_menu_arg_readers / sizeof inspect_menu_arg_readers[0];

/* In dump order, after the game-state facts. */
const struct inspect_fact inspect_menu_facts[] = {
    { "party_species", fact_party_species, NULL },
    { "pc_box0_count", fact_pc_box0_count, NULL },
    { "summary_species", fact_summary_species, NULL },
    { "debug_menu_open", fact_debug_menu_open, NULL },
    { "debug_input", fact_debug_input, NULL },
    { "debug_step", fact_debug_step, NULL },
    { "party_slot", fact_party_slot, NULL },
};
const size_t inspect_menu_fact_count = sizeof inspect_menu_facts / sizeof inspect_menu_facts[0];
