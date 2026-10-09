/*
 * inspect_args.c: see inspect_args.h. Compiled with the game's defines for its
 * constants (platform/Makefile).
 */
#include "inspect_args.h"

#include <string.h>

#include "inspect_decode.h"

#include "global.h"
#include "constants/flags.h"
#include "constants/global.h"
#include "constants/items.h"
#include "constants/vars.h"
#include "pokemon_storage_system.h"

#define LIMITS(a, b)          \
    {                         \
        (a), (b), false, 0, 0 \
    }
#define NO_LIMITS         \
    {                     \
        0, 0, false, 0, 0 \
    }

/* Slots of a party; moves of a Pokemon; PC boxes; items; flags (GetFlagPointer indexes
 * gSaveBlock1Ptr->flags below SPECIAL_FLAGS_START, and FLAGS_COUNT bits are stored). */
static const struct inspect_arg_spec specs[INSPECT_ARG_FACT_COUNT] = {
    [INSPECT_ARG_PARTY_SPECIES] = {"party_species", 1, {LIMITS(0, PARTY_SIZE - 1), NO_LIMITS}},
    [INSPECT_ARG_PARTY_NAME] = {"party_name", 1, {LIMITS(0, PARTY_SIZE - 1), NO_LIMITS}},
    [INSPECT_ARG_PARTY_LEVEL] = {"party_level", 1, {LIMITS(0, PARTY_SIZE - 1), NO_LIMITS}},
    [INSPECT_ARG_PARTY_EXTRAS] = {"party_extras", 1, {LIMITS(0, PARTY_SIZE - 1), NO_LIMITS}},
    [INSPECT_ARG_PARTY_MOVE] = {"party_move", 2, {LIMITS(0, PARTY_SIZE - 1), LIMITS(0, MAX_MON_MOVES - 1)}},
    [INSPECT_ARG_BAG_ITEM_COUNT] = {"bag_item_count", 1, {LIMITS(0, ITEMS_COUNT - 1), NO_LIMITS}},
    [INSPECT_ARG_PC_BOX_COUNT] = {"pc_box_count", 1, {LIMITS(0, TOTAL_BOXES_COUNT - 1), NO_LIMITS}},
    [INSPECT_ARG_PC_BOX_SPECIES] = {"pc_box_species", 1, {LIMITS(0, TOTAL_BOXES_COUNT - 1), NO_LIMITS}},
    [INSPECT_ARG_FLAG] = {"flag", 1, {LIMITS(0, FLAGS_COUNT - 1), NO_LIMITS}},
    /* VarGet: ids below VARS_START return the id, so they are not variables; between
     * VARS_END and SPECIAL_VARS_START and above SPECIAL_VARS_END there is no storage. */
    [INSPECT_ARG_VAR] = {"var", 1, {{VARS_START, VARS_END, true, SPECIAL_VARS_START, SPECIAL_VARS_END}, NO_LIMITS}},
};

const struct inspect_arg_spec *inspect_arg_find(const char *name, size_t name_length)
{
    for (size_t i = 0; i < INSPECT_ARG_FACT_COUNT; i++) {
        if (strlen(specs[i].name) == name_length && strncmp(specs[i].name, name, name_length) == 0)
            return &specs[i];
    }
    return NULL;
}

enum inspect_arg_fact inspect_arg_id(const struct inspect_arg_spec *spec)
{
    return (enum inspect_arg_fact)(spec - specs);
}

bool inspect_arg_in_range(const struct inspect_arg_spec *spec, size_t index, uint32_t value)
{
    if (index >= spec->argument_count)
        return false;
    const struct inspect_arg_limits *limits = &spec->limits[index];
    if (value >= limits->first && value <= limits->last)
        return true;
    return limits->has_second_range && value >= limits->second_first && value <= limits->second_last;
}

int inspect_arg_parse(const struct inspect_arg_spec *spec, const char *text, uint32_t values[INSPECT_MAX_ARGUMENTS])
{
    for (size_t i = 0; i < spec->argument_count; i++) {
        const char *separator = strchr(text, INSPECT_ARGUMENT_SEPARATOR);
        char number[INSPECT_ARGUMENT_MAX_DIGITS + 2u];
        size_t length = separator != NULL ? (size_t)(separator - text) : strlen(text);
        const bool last = i + 1u == spec->argument_count;
        if ((separator != NULL) == last || length >= sizeof number)
            return -1; /* too few or too many arguments, or too long to be a number */
        memcpy(number, text, length);
        number[length] = '\0';
        if (inspect_parse_argument(number, UINT32_MAX, &values[i]) != 0 || !inspect_arg_in_range(spec, i, values[i]))
            return -1;
        text = separator != NULL ? separator + 1 : text + length;
    }
    return 0;
}
