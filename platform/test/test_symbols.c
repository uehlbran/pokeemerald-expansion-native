/*
 * test_symbols.c: function names from the map the link writes next to an
 * executable (symbols.c; symbols.h has the format). The maps are fixtures
 * written to temporary files, at this build's address width; the last tests
 * read this test binary's own map, which platform/Makefile writes with the
 * recipe the game's link uses (host.mk's write_symbol_map), so the recipe
 * and the reader are tested together.
 */
#include "ptest.h"
#include "symbols.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Room for every fixture map below. */
#define MAP_TEXT_SIZE 1024u

/* Named, global and never inlined, so that they have a symbol of their own. */
__attribute__((noinline)) int symbols_test_function_alpha(int x)
{
    return x + 1;
}

__attribute__((noinline)) int symbols_test_function_beta(int x)
{
    return x * 3;
}

static uintptr_t address_of(int (*function)(int))
{
    return (uintptr_t)function;
}

/* Appends "<address> <name>\n" with `digits` hexadecimal digits to text. */
static void add_line_of_width(char *text, uintptr_t address, int digits, const char *name)
{
    const size_t used = strlen(text);
    (void)snprintf(text + used, MAP_TEXT_SIZE - used, "%0*" PRIxPTR " %s\n", digits, address, name);
}

/* Appends a line as the link writes it: this build's width. */
static void add_line(char *text, uintptr_t address, const char *name)
{
    add_line_of_width(text, address, (int)SYMBOLS_ADDRESS_DIGITS, name);
}

/* Loads the size bytes at bytes as a map, from a temporary file that is
 * removed again. 99: the fixture could not be written. */
static int load_bytes(const char *bytes, size_t size)
{
    char path[64];
    (void)snprintf(path, sizeof path, "/tmp/test_symbols_XXXXXX");
    const int fd = mkstemp(path);
    if (fd < 0)
        return 99;
    const bool written = write(fd, bytes, size) == (ssize_t)size;
    const bool closed = close(fd) == 0;
    const int result = written && closed ? symbols_load(path) : 99;
    (void)unlink(path);
    return result;
}

static int load_text(const char *text)
{
    return load_bytes(text, strlen(text));
}

/* --- Fixture maps ------------------------------------------------------------- */

/* Both ends of the address space, two functions between them, and an alias
 * (two names for one address, the first of them the name given). */
static int load_ends_and_alias(void)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0, "at_zero");
    add_line(text, 0x1000u, "first_of_two");
    add_line(text, 0x2000u, "alias_listed_first");
    add_line(text, 0x2000u, "alias_listed_second");
    add_line(text, UINTPTR_MAX, "at_the_top");
    return load_text(text);
}

PTEST(lookups_at_both_ends_of_the_map_and_between)
{
    PT_ASSERT_EQ_INT(load_ends_and_alias(), 0);
    PT_ASSERT_EQ_U32(symbols_count(), 5);
    PT_ASSERT_EQ_STR(symbols_name_at(0), "at_zero");
    PT_ASSERT_EQ_STR(symbols_name_at(UINTPTR_MAX), "at_the_top");
    PT_ASSERT_EQ_STR(symbols_name_at(0x1000u), "first_of_two");
    PT_ASSERT(symbols_name_at(1) == NULL);
    PT_ASSERT(symbols_name_at(0xFFFu) == NULL);
    PT_ASSERT(symbols_name_at(0x1001u) == NULL);
    PT_ASSERT(symbols_name_at(0x2001u) == NULL);
    PT_ASSERT(symbols_name_at(UINTPTR_MAX - 1u) == NULL);
    symbols_unload();
}

PTEST(of_two_names_for_one_address_the_first_listed_is_given)
{
    PT_ASSERT_EQ_INT(load_ends_and_alias(), 0);
    PT_ASSERT_EQ_STR(symbols_name_at(0x2000u), "alias_listed_first");
    symbols_unload();
}

PTEST(a_map_with_one_function_names_it_and_nothing_else)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x40001000u, "only");
    PT_ASSERT_EQ_INT(load_text(text), 0);
    PT_ASSERT_EQ_U32(symbols_count(), 1);
    PT_ASSERT_EQ_STR(symbols_name_at(0x40001000u), "only");
    PT_ASSERT(symbols_name_at(0x40000FFFu) == NULL);
    PT_ASSERT(symbols_name_at(0x40001001u) == NULL);
    symbols_unload();
}

PTEST(upper_case_digits_read_as_lower_case_ones)
{
    char text[MAP_TEXT_SIZE];
    (void)snprintf(text, sizeof text, "%0*" PRIXPTR " upper\n", (int)SYMBOLS_ADDRESS_DIGITS, (uintptr_t)0xABCDEFu);
    PT_ASSERT_EQ_INT(load_text(text), 0);
    PT_ASSERT_EQ_STR(symbols_name_at(0xABCDEFu), "upper");
    symbols_unload();
}

PTEST(an_empty_map_loads_no_functions)
{
    PT_ASSERT_EQ_INT(load_text(""), 0);
    PT_ASSERT_EQ_U32(symbols_count(), 0);
    PT_ASSERT(symbols_name_at(0) == NULL);
    symbols_unload();
}

/* --- Refused maps, each leaving nothing loaded ----------------------------------- */

/* Loads the good map first, so that a refusal is seen to unload it too. */
static int load_after_a_good_map(const char *text)
{
    if (load_ends_and_alias() != 0)
        return 98;
    return load_text(text);
}

static bool nothing_loaded(void)
{
    return symbols_count() == 0 && symbols_name_at(0x1000u) == NULL;
}

PTEST(a_missing_map_is_an_error_and_unloads)
{
    PT_ASSERT_EQ_INT(load_ends_and_alias(), 0);
    PT_ASSERT_EQ_INT(symbols_load("/nonexistent/for/test.symbols"), -1);
    PT_ASSERT(nothing_loaded());
}

PTEST(an_unsorted_map_is_refused)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x1000u, "first");
    add_line(text, 0x3000u, "third");
    add_line(text, 0x2000u, "second_out_of_order");
    PT_ASSERT_EQ_INT(load_after_a_good_map(text), -1);
    PT_ASSERT(nothing_loaded());
}

PTEST(a_map_out_of_order_only_at_its_last_line_is_refused)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x2000u, "b");
    add_line(text, 0x2000u, "b_alias");
    add_line(text, 0x1FFFu, "a");
    PT_ASSERT_EQ_INT(load_after_a_good_map(text), -1);
    PT_ASSERT(nothing_loaded());
}

PTEST(a_last_line_without_its_newline_is_refused)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x1000u, "complete");
    add_line(text, 0x2000u, "cut");
    text[strlen(text) - 1u] = '\0';
    PT_ASSERT_EQ_INT(load_after_a_good_map(text), -1);
    PT_ASSERT(nothing_loaded());
}

PTEST(an_address_of_another_width_is_refused)
{
    char shorter[MAP_TEXT_SIZE] = "";
    char longer[MAP_TEXT_SIZE] = "";
    add_line_of_width(shorter, 0x1000u, (int)SYMBOLS_ADDRESS_DIGITS - 1, "one_digit_short");
    add_line_of_width(longer, 0x1000u, (int)SYMBOLS_ADDRESS_DIGITS + 1, "one_digit_long");
    PT_ASSERT_EQ_INT(load_after_a_good_map(shorter), -1);
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_after_a_good_map(longer), -1);
    PT_ASSERT(nothing_loaded());
}

/* A good line with one byte replaced: at index (or counted from the end of
 * the line, newline included, for a negative index). */
static int load_line_with(long index, char replacement)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x1000u, "good");
    add_line(text, 0x2000u, "name");
    const size_t first_length = strlen("good") + SYMBOLS_ADDRESS_DIGITS + 2u;
    const size_t length = strlen(text);
    const size_t at = index >= 0 ? first_length + (size_t)index : length - (size_t)-index;
    text[at] = replacement;
    return load_after_a_good_map(text);
}

PTEST(a_malformed_second_line_is_refused)
{
    const long space = (long)SYMBOLS_ADDRESS_DIGITS;
    PT_ASSERT_EQ_INT(load_line_with(0, 'g'), -1); /* not a hexadecimal digit */
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_line_with(space - 1, ' '), -1); /* a digit short, then two spaces */
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_line_with(space, '\t'), -1); /* a tab for the space */
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_line_with(space, '0'), -1); /* no space: the address runs into the name */
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_line_with(-3, ' '), -1); /* a space inside the name */
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_line_with(-2, '\r'), -1); /* a control character in the name */
    PT_ASSERT(nothing_loaded());
}

PTEST(an_empty_name_or_a_blank_line_is_refused)
{
    char no_name[MAP_TEXT_SIZE] = "";
    add_line(no_name, 0x1000u, "");
    char blank[MAP_TEXT_SIZE] = "";
    add_line(blank, 0x1000u, "before");
    const size_t blank_length = strlen(blank);
    blank[blank_length] = '\n'; /* an empty line after it */
    blank[blank_length + 1u] = '\0';
    PT_ASSERT_EQ_INT(load_after_a_good_map(no_name), -1);
    PT_ASSERT(nothing_loaded());
    PT_ASSERT_EQ_INT(load_after_a_good_map(blank), -1);
    PT_ASSERT(nothing_loaded());
}

PTEST(a_nul_byte_in_a_name_is_refused)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x1000u, "na_me");
    const size_t length = strlen(text);
    text[length - 4u] = '\0'; /* the underscore */
    PT_ASSERT_EQ_INT(load_ends_and_alias(), 0);
    PT_ASSERT_EQ_INT(load_bytes(text, length), -1);
    PT_ASSERT(nothing_loaded());
}

/* --- Loading and unloading ------------------------------------------------------- */

PTEST(nothing_loaded_names_nothing_and_unload_is_repeatable)
{
    symbols_unload();
    symbols_unload();
    PT_ASSERT(symbols_name_at(address_of(symbols_test_function_alpha)) == NULL);
    PT_ASSERT_EQ_U32(symbols_count(), 0);
}

PTEST(loading_again_replaces_the_map_without_leaking)
{
    char text[MAP_TEXT_SIZE] = "";
    add_line(text, 0x3000u, "from_the_second_map");
    PT_ASSERT_EQ_INT(load_ends_and_alias(), 0);
    PT_ASSERT_EQ_INT(load_text(text), 0);
    PT_ASSERT_EQ_U32(symbols_count(), 1);
    PT_ASSERT(symbols_name_at(0x1000u) == NULL);
    PT_ASSERT_EQ_STR(symbols_name_at(0x3000u), "from_the_second_map");
    symbols_unload();
}

/* --- This executable's own map ----------------------------------------------------- */

PTEST(the_executables_own_map_names_its_functions)
{
    PT_ASSERT_EQ_INT(symbols_load_own(), 0);
    PT_ASSERT_EQ_STR(symbols_name_at(address_of(symbols_test_function_alpha)), "symbols_test_function_alpha");
    PT_ASSERT_EQ_STR(symbols_name_at(address_of(symbols_test_function_beta)), "symbols_test_function_beta");
    PT_ASSERT(symbols_count() > 2);
    symbols_unload();
}

PTEST(an_address_inside_a_function_or_nowhere_has_no_name)
{
    PT_ASSERT_EQ_INT(symbols_load_own(), 0);
    PT_ASSERT(symbols_name_at(address_of(symbols_test_function_alpha) + 1) == NULL);
    PT_ASSERT(symbols_name_at(0) == NULL);
    PT_ASSERT(symbols_name_at(UINTPTR_MAX) == NULL);
    symbols_unload();
}

PTEST_MAIN()
