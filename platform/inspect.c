/*
 * inspect.c: the game-state facts (inspect.h), read from expansion's variables, the
 * dispatch of all facts, and the flag and var facts. The facts of the menus are
 * inspect_menu.c, the ranges of the arguments inspect_args.c.
 *
 * Followed: expansion's own headers for every layout (global.h, main.h,
 * task.h, pokemon.h, save.h); built with the game's defines (see
 * platform/Makefile) so the structures are the ones the game compiled.
 */
#include "inspect.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash.h"
#include "frame.h"
#include "inspect_decode.h"
#include "inspect_internal.h"
#include "symbols.h"

#include "global.h"
#include "battle.h"
#include "constants/battle.h"
#include "constants/map_groups.h"
#include "constants/species.h"
#include "intro.h"
#include "item.h"
#include "load_save.h"
#include "item_menu.h"
#include "event_data.h"
#include "constants/vars.h"
#include "main.h"
#include "main_menu.h"
#include "option_menu.h"
#include "overworld.h"
#include "party_menu.h"
#include "pokemon_summary_screen.h"
#include "palette.h"
#include "pokemon.h"
#include "save.h"
#include "script.h"
#include "battle_main.h"
#include "task.h"
#include "title_screen.h"
#include "wallclock.h"

const char inspect_none_text[] = "none";

/* 64-bit flag: the (uintptr_t) casts of function pointers in this file (this
 * table and name_function's callers) compare code addresses as integers with
 * the symbol table's values; uintptr_t is wide enough on both widths, the cast
 * is the one place a function pointer becomes a number. */

/* Every callback the game declares in a header, by its own name. The rest
 * (most are static) come from the symbol table. */
static const struct inspect_known_callback known_callbacks[] = {
    { (uintptr_t)CB2_InitCopyrightScreenAfterBootup, "CB2_InitCopyrightScreenAfterBootup" },
    { (uintptr_t)CB2_InitCopyrightScreenAfterTitleScreen, "CB2_InitCopyrightScreenAfterTitleScreen" },
    { (uintptr_t)MainCB2_Intro, "MainCB2_Intro" },
    { (uintptr_t)CB2_InitTitleScreen, "CB2_InitTitleScreen" },
    { (uintptr_t)CB2_InitMainMenu, "CB2_InitMainMenu" },
    { (uintptr_t)CB2_InitOptionMenu, "CB2_InitOptionMenu" },
    { (uintptr_t)CB2_NewGame, "CB2_NewGame" },
    { (uintptr_t)CB2_ContinueSavedGame, "CB2_ContinueSavedGame" },
    { (uintptr_t)CB2_LoadMap, "CB2_LoadMap" },
    { (uintptr_t)CB2_Overworld, "CB2_Overworld" },
    { (uintptr_t)CB2_ReturnToField, "CB2_ReturnToField" },
    { (uintptr_t)CB2_ReturnToFieldContinueScript, "CB2_ReturnToFieldContinueScript" },
    { (uintptr_t)CB2_ReturnToFieldWithOpenMenu, "CB2_ReturnToFieldWithOpenMenu" },
    { (uintptr_t)CB2_WhiteOut, "CB2_WhiteOut" },
    { (uintptr_t)CB2_InitBattle, "CB2_InitBattle" },
    { (uintptr_t)BattleMainCB2, "BattleMainCB2" },
    { (uintptr_t)CB2_BagMenuFromStartMenu, "CB2_BagMenuFromStartMenu" },
    { (uintptr_t)CB2_StartWallClock, "CB2_StartWallClock" },
};

/* A function pointer's name: known table, then symbol table, else unknown. */
static void name_function(uintptr_t address, char *out, size_t size)
{
    if (address == 0) {
        snprintf(out, size, "%s", inspect_none_text);
        return;
    }
    const char *name = inspect_find_known_callback(known_callbacks, sizeof known_callbacks / sizeof known_callbacks[0],
                                                   address);
    if (name == NULL)
        name = symbols_name_at(address);
    if (name != NULL)
        snprintf(out, size, "%s", name);
    else
        inspect_format_unknown_callback(address, out, size);
}

static void fact_frame(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)frame_count());
}

static void fact_callback2(char *out, size_t size)
{
    name_function((uintptr_t)gMain.callback2, out, size);
}

static void fact_task_count(char *out, size_t size)
{
    unsigned count = 0;
    for (size_t i = 0; i < NUM_TASKS; i++)
        count += gTasks[i].isActive ? 1u : 0u;
    snprintf(out, size, "%u", count);
}

static void fact_task_first(char *out, size_t size)
{
    for (size_t i = 0; i < NUM_TASKS; i++) {
        if (gTasks[i].isActive) {
            name_function((uintptr_t)gTasks[i].func, out, size);
            return;
        }
    }
    snprintf(out, size, "%s", inspect_none_text);
}

static void fact_task_last(char *out, size_t size)
{
    for (size_t i = NUM_TASKS; i > 0; i--) {
        if (gTasks[i - 1].isActive) {
            name_function((uintptr_t)gTasks[i - 1].func, out, size);
            return;
        }
    }
    snprintf(out, size, "%s", inspect_none_text);
}

/* Every active task's function, by task id, comma separated; cut at the end
 * of the value ("..." marks a cut). */
static void fact_tasks(char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (size_t i = 0; i < NUM_TASKS; i++) {
        char name[INSPECT_VALUE_SIZE];
        if (!gTasks[i].isActive)
            continue;
        name_function((uintptr_t)gTasks[i].func, name, sizeof name);
        int written = snprintf(out + used, size - used, "%s%s", used > 0 ? "," : "", name);
        if (written < 0 || (size_t)written >= size - used) {
            snprintf(out + (size > 4 ? size - 4 : 0), size > 4 ? 4 : size, "...");
            return;
        }
        used += (size_t)written;
    }
    if (used == 0)
        snprintf(out, size, "%s", inspect_none_text);
}

/* The save blocks do not exist before the game's boot has set them up (a script's first
 * frames). The facts that read them carry the value they give until then (struct inspect_fact),
 * and inspect_get does not call their reader before the blocks exist. */
static bool save_blocks_ready(void)
{
    return gSaveBlock1Ptr != NULL && gSaveBlock2Ptr != NULL;
}

/* A flag or variable stored in the save block reads 0 until the block exists; the special
 * variables (SPECIAL_VARS_START and up) are not stored there. */
static unsigned var_value(uint32_t var)
{
    if (var < SPECIAL_VARS_START && !save_blocks_ready())
        return 0;
    return (unsigned)VarGet((u16)var);
}

/* The story variables of the opening (data/maps/LittlerootTown/scripts.inc
 * describes their states). */
static void fact_intro_state(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)VarGet(VAR_LITTLEROOT_INTRO_STATE));
}

static void fact_town_state(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)VarGet(VAR_LITTLEROOT_TOWN_STATE));
}

static void fact_rival_state(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)VarGet(VAR_LITTLEROOT_RIVAL_STATE));
}

static void fact_keys_held(char *out, size_t size)
{
    snprintf(out, size, "0x%04X", (unsigned)gMain.heldKeysRaw);
}

static void fact_keys_new(char *out, size_t size)
{
    snprintf(out, size, "0x%04X", (unsigned)gMain.newKeysRaw);
}

static void fact_fade_active(char *out, size_t size)
{
    snprintf(out, size, "%u", gPaletteFade.active ? 1u : 0u);
}

static void fact_player_name(char *out, size_t size)
{
    size_t length = inspect_decode_text(gSaveBlock2Ptr->playerName, sizeof gSaveBlock2Ptr->playerName, out, size);
    while (length > 0 && out[length - 1] == ' ')
        out[--length] = '\0'; /* a name is padded with spaces (byte 0) on a new game */
}

static void fact_player_gender(char *out, size_t size)
{
    snprintf(out, size, "%s", gSaveBlock2Ptr->playerGender == MALE ? "MALE" : "FEMALE");
}

static void fact_map(char *out, size_t size)
{
    snprintf(out, size, "%d.%d", gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum);
}

/* The maps the replay passes through, by their constants (the game has no
 * table of names); other maps read "unknown". */
struct known_map {
    uint16_t id; /* MAP_*: number | group << 8 */
    const char *name;
};

static const struct known_map known_maps[] = {
    { MAP_INSIDE_OF_TRUCK, "MAP_INSIDE_OF_TRUCK" },
    { MAP_LITTLEROOT_TOWN, "MAP_LITTLEROOT_TOWN" },
    { MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F, "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_1F" },
    { MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F, "MAP_LITTLEROOT_TOWN_BRENDANS_HOUSE_2F" },
    { MAP_LITTLEROOT_TOWN_MAYS_HOUSE_1F, "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_1F" },
    { MAP_LITTLEROOT_TOWN_MAYS_HOUSE_2F, "MAP_LITTLEROOT_TOWN_MAYS_HOUSE_2F" },
    { MAP_ROUTE101, "MAP_ROUTE101" },
    { MAP_ROUTE102, "MAP_ROUTE102" },
    { MAP_OLDALE_TOWN, "MAP_OLDALE_TOWN" },
    { MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB, "MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB" },
};

static void fact_map_name(char *out, size_t size)
{
    const unsigned id = ((unsigned)gSaveBlock1Ptr->location.mapGroup << 8) | (unsigned)gSaveBlock1Ptr->location.mapNum;
    for (size_t i = 0; i < sizeof known_maps / sizeof known_maps[0]; i++) {
        if (known_maps[i].id == id) {
            snprintf(out, size, "%s", known_maps[i].name);
            return;
        }
    }
    snprintf(out, size, "unknown");
}

static void fact_pos(char *out, size_t size)
{
    snprintf(out, size, "%d,%d", gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
}

unsigned inspect_party_count(void)
{
    return gPartiesCount[B_TRAINER_PLAYER];
}

static void fact_party_count(char *out, size_t size)
{
    snprintf(out, size, "%u", inspect_party_count());
}

/* The first party Pokemon's species, 0 without one. */
static unsigned party0_species(void)
{
    if (inspect_party_count() == 0)
        return 0;
    return GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_SPECIES);
}

static void fact_party0_species(char *out, size_t size)
{
    snprintf(out, size, "%u", party0_species());
}

static void fact_party0_name(char *out, size_t size)
{
    unsigned species = party0_species();
    if (species == SPECIES_NONE || species >= NUM_SPECIES) {
        snprintf(out, size, "%s", inspect_none_text);
        return;
    }
    (void)inspect_decode_text(gSpeciesInfo[species].speciesName, sizeof gSpeciesInfo[species].speciesName, out, size);
}

static void fact_in_battle(char *out, size_t size)
{
    snprintf(out, size, "%u", gMain.inBattle ? 1u : 0u);
}

/* gBattleMainFunc by name: the battle's own state machine (DoBattleIntro, ...,
 * HandleTurnActionSelectionState while it waits for the player's choice, ...). */
static void fact_battle_main(char *out, size_t size)
{
    name_function((uintptr_t)gBattleMainFunc, out, size);
}

static void fact_script_running(char *out, size_t size)
{
    snprintf(out, size, "%u", ScriptContext_IsEnabled() ? 1u : 0u);
}

static void fact_controls_locked(char *out, size_t size)
{
    snprintf(out, size, "%u", ArePlayerFieldControlsLocked() ? 1u : 0u);
}

static void fact_save_counter(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)gSaveCounter);
}

static void fact_save_status(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)gSaveFileStatus);
}

static void fact_flash_dirty_writes(char *out, size_t size)
{
    snprintf(out, size, "%u", (unsigned)flash_write_count());
}

static void arg_flag(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    snprintf(out, size, "%u", (save_blocks_ready() && FlagGet((u16)arguments[0])) ? 1u : 0u);
}

static void arg_var(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size)
{
    snprintf(out, size, "%u", var_value(arguments[0]));
}

static const struct inspect_arg_reader state_arg_readers[] = {
    { INSPECT_ARG_FLAG, arg_flag },
    { INSPECT_ARG_VAR, arg_var },
};

static const struct inspect_fact facts[] = {
    { "frame", fact_frame, NULL },
    { "callback2", fact_callback2, NULL },
    { "task_count", fact_task_count, NULL },
    { "task_first", fact_task_first, NULL },
    { "task_last", fact_task_last, NULL },
    { "tasks", fact_tasks, NULL },
    { "fade_active", fact_fade_active, NULL },
    { "intro_state", fact_intro_state, "0" },
    { "town_state", fact_town_state, "0" },
    { "rival_state", fact_rival_state, "0" },
    { "keys_held", fact_keys_held, NULL },
    { "keys_new", fact_keys_new, NULL },
    { "player_name", fact_player_name, inspect_none_text },
    { "player_gender", fact_player_gender, inspect_none_text },
    { "map", fact_map, inspect_none_text },
    { "map_name", fact_map_name, inspect_none_text },
    { "pos", fact_pos, inspect_none_text },
    { "party_count", fact_party_count, NULL },
    { "party0_species", fact_party0_species, NULL },
    { "party0_name", fact_party0_name, NULL },
    { "in_battle", fact_in_battle, NULL },
    { "battle_main", fact_battle_main, NULL },
    { "script_running", fact_script_running, NULL },
    { "controls_locked", fact_controls_locked, NULL },
    { "save_counter", fact_save_counter, NULL },
    { "save_status", fact_save_status, NULL },
    { "flash_dirty_writes", fact_flash_dirty_writes, NULL },
};
#define STATE_FACT_COUNT (sizeof facts / sizeof facts[0])

int inspect_init(void)
{
    return symbols_load(SYMBOLS_EXE_PATH);
}

void inspect_shutdown(void)
{
    symbols_unload();
}

static const struct inspect_arg_reader *find_reader(const struct inspect_arg_reader *readers, size_t count,
                                                    enum inspect_arg_fact fact)
{
    for (size_t i = 0; i < count; i++) {
        if (readers[i].fact == fact)
            return &readers[i];
    }
    return NULL;
}

/* "name:argument[:argument]": 0 and the value, or -1 (unknown fact, malformed or out-of-range argument). */
static int read_argument_fact(const char *name, size_t name_length, const char *argument, char *out, size_t size)
{
    const struct inspect_arg_spec *spec = inspect_arg_find(name, name_length);
    uint32_t values[INSPECT_MAX_ARGUMENTS] = { 0 };
    if (spec == NULL || inspect_arg_parse(spec, argument, values) != 0)
        return -1;
    const enum inspect_arg_fact fact = inspect_arg_id(spec);
    const struct inspect_arg_reader *reader =
        find_reader(state_arg_readers, sizeof state_arg_readers / sizeof state_arg_readers[0], fact);
    if (reader == NULL)
        reader = find_reader(inspect_menu_arg_readers, inspect_menu_arg_reader_count, fact);
    if (reader == NULL)
        return -1;
    reader->read(values, out, size);
    return 0;
}

/* The fact's value, or its pre-boot value while the save blocks do not exist. */
static void read_fact(const struct inspect_fact *fact, char *out, size_t size)
{
    if (fact->value_without_save_blocks != NULL && !save_blocks_ready())
        snprintf(out, size, "%s", fact->value_without_save_blocks);
    else
        fact->read(out, size);
}

int inspect_get(const char *name, char *out, size_t size)
{
    const char *argument;
    const size_t name_length = inspect_split_name(name, &argument);
    if (out == NULL || size == 0)
        return -1;
    if (argument != NULL)
        return read_argument_fact(name, name_length, argument, out, size);
    for (size_t i = 0; i < STATE_FACT_COUNT; i++) {
        if (strcmp(facts[i].name, name) == 0) {
            read_fact(&facts[i], out, size);
            return 0;
        }
    }
    for (size_t i = 0; i < inspect_menu_fact_count; i++) {
        if (strcmp(inspect_menu_facts[i].name, name) == 0) {
            read_fact(&inspect_menu_facts[i], out, size);
            return 0;
        }
    }
    return -1;
}

const char *inspect_fact_name(size_t index)
{
    if (index < STATE_FACT_COUNT)
        return facts[index].name;
    index -= STATE_FACT_COUNT;
    return index < inspect_menu_fact_count ? inspect_menu_facts[index].name : NULL;
}
