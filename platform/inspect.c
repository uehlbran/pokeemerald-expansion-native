/*
 * inspect.c: the facts (inspect.h), read from expansion's variables.
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
#include "symbols.h"

#include "global.h"
#include "constants/battle.h"
#include "constants/species.h"
#include "intro.h"
#include "item_menu.h"
#include "main.h"
#include "main_menu.h"
#include "option_menu.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "save.h"
#include "script.h"
#include "battle_main.h"
#include "task.h"
#include "title_screen.h"
#include "wallclock.h"

static const char *const none_text = "none";

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
        snprintf(out, size, "%s", none_text);
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
    snprintf(out, size, "%s", none_text);
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
        snprintf(out, size, "%s", none_text);
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

static void fact_pos(char *out, size_t size)
{
    snprintf(out, size, "%d,%d", gSaveBlock1Ptr->pos.x, gSaveBlock1Ptr->pos.y);
}

static unsigned player_party_count(void)
{
    return gPartiesCount[B_TRAINER_PLAYER];
}

static void fact_party_count(char *out, size_t size)
{
    snprintf(out, size, "%u", player_party_count());
}

/* The first party Pokemon's species, 0 without one. */
static unsigned party0_species(void)
{
    if (player_party_count() == 0)
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
        snprintf(out, size, "%s", none_text);
        return;
    }
    (void)inspect_decode_text(gSpeciesInfo[species].speciesName, sizeof gSpeciesInfo[species].speciesName, out, size);
}

static void fact_in_battle(char *out, size_t size)
{
    snprintf(out, size, "%u", gMain.inBattle ? 1u : 0u);
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

struct fact {
    const char *name;
    void (*read)(char *out, size_t size);
};

static const struct fact facts[] = {
    { "frame", fact_frame },
    { "callback2", fact_callback2 },
    { "task_count", fact_task_count },
    { "task_first", fact_task_first },
    { "tasks", fact_tasks },
    { "fade_active", fact_fade_active },
    { "keys_held", fact_keys_held },
    { "keys_new", fact_keys_new },
    { "player_name", fact_player_name },
    { "player_gender", fact_player_gender },
    { "map", fact_map },
    { "pos", fact_pos },
    { "party_count", fact_party_count },
    { "party0_species", fact_party0_species },
    { "party0_name", fact_party0_name },
    { "in_battle", fact_in_battle },
    { "script_running", fact_script_running },
    { "controls_locked", fact_controls_locked },
    { "save_counter", fact_save_counter },
    { "save_status", fact_save_status },
    { "flash_dirty_writes", fact_flash_dirty_writes },
};
#define FACT_COUNT (sizeof facts / sizeof facts[0])

int inspect_init(void)
{
    return symbols_load(SYMBOLS_EXE_PATH);
}

void inspect_shutdown(void)
{
    symbols_unload();
}

int inspect_get(const char *name, char *out, size_t size)
{
    for (size_t i = 0; i < FACT_COUNT; i++) {
        if (strcmp(facts[i].name, name) == 0) {
            facts[i].read(out, size);
            return 0;
        }
    }
    return -1;
}

const char *inspect_fact_name(size_t index)
{
    return index < FACT_COUNT ? facts[index].name : NULL;
}
