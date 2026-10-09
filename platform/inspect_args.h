/*
 * inspect_args.h: the facts that take arguments ("name:argument", inspect.h) and
 * the ranges of their arguments.
 *
 * Not hardware. Every argument is checked against the storage it indexes
 * before any reader runs: VarGet has storage only for VARS_START..VARS_END and
 * SPECIAL_VARS_START..SPECIAL_VARS_END, a party has PARTY_SIZE slots, and so
 * on. The table is built from expansion's own constants (this file is compiled
 * with the game's defines, like inspect.c) so it follows the game, and it is
 * unit tested alone (test_inspect_args.c).
 */
#ifndef PLATFORM_INSPECT_ARGS_H
#define PLATFORM_INSPECT_ARGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum inspect_arg_fact {
    INSPECT_ARG_PARTY_SPECIES,
    INSPECT_ARG_PARTY_NAME,
    INSPECT_ARG_PARTY_LEVEL,
    INSPECT_ARG_PARTY_EXTRAS,
    INSPECT_ARG_PARTY_MOVE,
    INSPECT_ARG_BAG_ITEM_COUNT,
    INSPECT_ARG_PC_BOX_COUNT,
    INSPECT_ARG_PC_BOX_SPECIES,
    INSPECT_ARG_FLAG,
    INSPECT_ARG_VAR,
    INSPECT_ARG_FACT_COUNT
};

/* The most arguments a fact takes (party_move:SLOT:MOVE). */
#define INSPECT_MAX_ARGUMENTS 2u

/* One argument's valid values: first..last, and optionally a second range
 * (var: the save block's variables and the special variables). */
struct inspect_arg_limits {
    uint32_t first;
    uint32_t last;
    bool has_second_range;
    uint32_t second_first;
    uint32_t second_last;
};

struct inspect_arg_spec {
    const char *name;
    size_t argument_count;
    struct inspect_arg_limits limits[INSPECT_MAX_ARGUMENTS];
};

/* The spec of the argument fact called `name` (name_length characters), or NULL. */
const struct inspect_arg_spec *inspect_arg_find(const char *name, size_t name_length);

/* The fact's enum value, for a spec returned by inspect_arg_find. */
enum inspect_arg_fact inspect_arg_id(const struct inspect_arg_spec *spec);

/* Whether `value` is valid for argument `index` of the fact. */
bool inspect_arg_in_range(const struct inspect_arg_spec *spec, size_t index, uint32_t value);

/* Parses "A" or "A:B" (as many numbers as the fact takes, decimal, each in range) into
 * values[]. Returns 0, or -1 for a missing, extra, malformed or out-of-range argument;
 * values is then unspecified. */
int inspect_arg_parse(const struct inspect_arg_spec *spec, const char *text, uint32_t values[INSPECT_MAX_ARGUMENTS]);

#endif /* PLATFORM_INSPECT_ARGS_H */
