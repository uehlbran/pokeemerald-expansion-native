// deps: inspect_args.c inspect_decode.c
/*
 * test_inspect_args.c: the ranges of the facts with arguments (inspect_args.c),
 * table-driven, against expansion's own constants: an argument outside the
 * storage a reader indexes must be refused before the reader runs (var:20000 and
 * var:40000 used to index past gSaveBlock1Ptr->vars and gSpecialVars).
 */
#include "ptest.h"
#include "inspect_args.h"

#include <string.h>

#include "global.h"
#include "constants/flags.h"
#include "constants/global.h"
#include "constants/items.h"
#include "constants/vars.h"
#include "pokemon_storage_system.h"

struct bound_case {
    const char *fact;
    size_t argument; /* which argument */
    uint32_t value;
    bool valid;
};

static const struct bound_case bound_cases[] = {
    { "party_species", 0, 0, true },
    { "party_species", 0, PARTY_SIZE - 1, true },
    { "party_species", 0, PARTY_SIZE, false },
    { "party_name", 0, PARTY_SIZE, false },
    { "party_level", 0, PARTY_SIZE - 1, true },
    { "party_extras", 0, PARTY_SIZE, false },
    { "party_move", 0, PARTY_SIZE - 1, true },
    { "party_move", 0, PARTY_SIZE, false },
    { "party_move", 1, 0, true },
    { "party_move", 1, MAX_MON_MOVES - 1, true },
    { "party_move", 1, MAX_MON_MOVES, false },
    { "bag_item_count", 0, 0, true },
    { "bag_item_count", 0, ITEMS_COUNT - 1, true },
    { "bag_item_count", 0, ITEMS_COUNT, false },
    { "pc_box_count", 0, TOTAL_BOXES_COUNT - 1, true },
    { "pc_box_count", 0, TOTAL_BOXES_COUNT, false },
    { "pc_box_species", 0, TOTAL_BOXES_COUNT - 1, true },
    { "pc_box_species", 0, TOTAL_BOXES_COUNT, false },
    { "flag", 0, 0, true },
    { "flag", 0, FLAGS_COUNT - 1, true },
    { "flag", 0, FLAGS_COUNT, false },
    { "flag", 0, 65535, false },
    /* variables: the save block's, then the special ones, nothing between or around */
    { "var", 0, 0, false },
    { "var", 0, VARS_START - 1, false },
    { "var", 0, VARS_START, true },
    { "var", 0, VARS_END, true },
    { "var", 0, VARS_END + 1, false },
    { "var", 0, 20000, false },
    { "var", 0, SPECIAL_VARS_START - 1, false },
    { "var", 0, SPECIAL_VARS_START, true },
    { "var", 0, SPECIAL_VARS_END, true },
    { "var", 0, SPECIAL_VARS_END + 1, false },
    { "var", 0, 40000, false },
    { "var", 0, 65535, false },
    /* an argument the fact does not have */
    { "var", 1, VARS_START, false },
    { "party_species", 1, 0, false },
};

PTEST(every_argument_fact_has_the_right_bounds)
{
    for (size_t i = 0; i < sizeof bound_cases / sizeof bound_cases[0]; i++) {
        const struct bound_case *c = &bound_cases[i];
        const struct inspect_arg_spec *spec = inspect_arg_find(c->fact, strlen(c->fact));
        PT_ASSERT(spec != NULL);
        if (inspect_arg_in_range(spec, c->argument, c->value) != c->valid)
            ptest_fail_at(__FILE__, __LINE__, "%s argument %zu value %u: expected %s", c->fact, c->argument,
                          (unsigned)c->value, c->valid ? "valid" : "invalid");
    }
}

PTEST(finds_facts_by_exact_name_only)
{
    PT_ASSERT(inspect_arg_find("var", 3) != NULL);
    PT_ASSERT(inspect_arg_find("var", 2) == NULL);
    PT_ASSERT(inspect_arg_find("varx", 3) != NULL); /* only the first 3 characters count: a name is cut by the caller */
    PT_ASSERT(inspect_arg_find("va", 2) == NULL);
    PT_ASSERT(inspect_arg_find("party", 5) == NULL);
    PT_ASSERT(inspect_arg_find("", 0) == NULL);
    PT_ASSERT(inspect_arg_find("party_move", 10) != NULL);
    PT_ASSERT(inspect_arg_id(inspect_arg_find("flag", 4)) == INSPECT_ARG_FLAG);
}

struct parse_case {
    const char *fact;
    const char *text;
    int result;
    uint32_t first;
    uint32_t second;
};

static const struct parse_case parse_cases[] = {
    { "flag", "2144", 0, 2144, 0 },
    { "flag", "2400", -1, 0, 0 },
    { "flag", "", -1, 0, 0 },
    { "flag", "1:2", -1, 0, 0 },
    { "flag", "-1", -1, 0, 0 },
    { "flag", "12a", -1, 0, 0 },
    { "var", "16384", 0, 16384, 0 },
    { "var", "20000", -1, 0, 0 },
    { "var", "40000", -1, 0, 0 },
    { "var", "32768", 0, 32768, 0 },
    { "var", "99999", -1, 0, 0 },
    { "var", "100000", -1, 0, 0 },
    { "party_move", "0:3", 0, 0, 3 },
    { "party_move", "5:0", 0, 5, 0 },
    { "party_move", "5", -1, 0, 0 },
    { "party_move", "5:", -1, 0, 0 },
    { "party_move", ":1", -1, 0, 0 },
    { "party_move", "0:4", -1, 0, 0 },
    { "party_move", "6:0", -1, 0, 0 },
    { "party_move", "0:1:2", -1, 0, 0 },
    { "party_level", "2:1", -1, 0, 0 },
};

PTEST(parses_one_or_two_arguments_and_checks_each_range)
{
    for (size_t i = 0; i < sizeof parse_cases / sizeof parse_cases[0]; i++) {
        const struct parse_case *c = &parse_cases[i];
        const struct inspect_arg_spec *spec = inspect_arg_find(c->fact, strlen(c->fact));
        uint32_t values[INSPECT_MAX_ARGUMENTS] = { 0 };
        PT_ASSERT(spec != NULL);
        const int result = inspect_arg_parse(spec, c->text, values);
        if (result != c->result)
            ptest_fail_at(__FILE__, __LINE__, "%s:%s: result %d, expected %d", c->fact, c->text, result, c->result);
        if (result == 0 && (values[0] != c->first || values[1] != c->second))
            ptest_fail_at(__FILE__, __LINE__, "%s:%s: values %u,%u, expected %u,%u", c->fact, c->text,
                          (unsigned)values[0], (unsigned)values[1], (unsigned)c->first, (unsigned)c->second);
    }
}

PTEST_MAIN()
