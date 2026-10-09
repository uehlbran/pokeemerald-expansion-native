/*
 * inspect_internal.h: what inspect.c (game-state facts) and inspect_menu.c
 * (the facts of the menus: debug menu, party, summary screen, bag, PC) share.
 * Not part of the module's interface (inspect.h is).
 */
#ifndef PLATFORM_INSPECT_INTERNAL_H
#define PLATFORM_INSPECT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "inspect_args.h"

/* The text of a fact that has no value ("none"). */
extern const char inspect_none_text[];

/* A fact without an argument, as `dump` lists it. */
struct inspect_fact {
    const char *name;
    void (*read)(char *out, size_t size);
    const char *value_without_save_blocks; /* what it reads before the game's boot has set up the save blocks; NULL if it does not read them */
};

/* The reader of an argument fact; the arguments are already checked against their ranges. */
struct inspect_arg_reader {
    enum inspect_arg_fact fact;
    void (*read)(const uint32_t arguments[INSPECT_MAX_ARGUMENTS], char *out, size_t size);
};

/* The menu facts, in dump order, and their argument readers (inspect_menu.c). */
extern const struct inspect_fact inspect_menu_facts[];
extern const size_t inspect_menu_fact_count;
extern const struct inspect_arg_reader inspect_menu_arg_readers[];
extern const size_t inspect_menu_arg_reader_count;

/* Game-state helpers both files use (inspect.c). */
unsigned inspect_party_count(void);

/* Whether the game's boot has set up the save blocks (until then, facts that read them must not). */
bool inspect_save_blocks_ready(void);

#endif /* PLATFORM_INSPECT_INTERNAL_H */
