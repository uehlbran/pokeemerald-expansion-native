/*
 * test_inspect_decode.c: the game's text to ASCII and callback naming
 * (inspect_decode.c). Expected bytes are read off expansion's charmap.txt.
 */
#include "ptest.h"
#include "inspect_decode.h"

#include <string.h>

static void decode(const uint8_t *bytes, size_t length, char *out, size_t size, size_t *count)
{
    *count = inspect_decode_text(bytes, length, out, size);
}

PTEST(decodes_letters_digits_and_space)
{
    /* "Ab 9z" */
    const uint8_t text[] = { 0xBB, 0xD6, 0x00, 0xAA, 0xEE, 0xFF };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "Ab 9z");
    PT_ASSERT_EQ_INT(n, 5);
}

PTEST(decodes_the_edges_of_every_range)
{
    const uint8_t text[] = { 0xBB, 0xD4, 0xD5, 0xEE, 0xA1, 0xAA, 0xFF };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "AZaz09");
}

PTEST(decodes_punctuation)
{
    const uint8_t text[] = { 0xAB, 0xAC, 0xAD, 0xAE, 0xB8, 0xBA, 0xFF };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "!?.-,/");
}

PTEST(other_characters_become_a_question_mark)
{
    /* 01 is an accented capital; B0 is the ellipsis; FA..FE are text control codes */
    const uint8_t text[] = { 0x01, 0xB0, 0xA0, 0xAF, 0xEF, 0xFE, 0xFF };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "??????");
}

PTEST(stops_at_eos_and_at_the_maximum_length)
{
    const uint8_t text[] = { 0xBB, 0xFF, 0xBC, 0xBD };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "A");
    const uint8_t no_eos[] = { 0xBB, 0xBC, 0xBD, 0xBE };
    decode(no_eos, 3, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "ABC");
    decode(no_eos, 0, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "");
    PT_ASSERT_EQ_INT(n, 0);
}

PTEST(never_writes_past_the_output_and_always_terminates)
{
    const uint8_t text[] = { 0xBB, 0xBC, 0xBD, 0xBE, 0xFF };
    char out[8];
    memset(out, 'x', sizeof out);
    size_t n = inspect_decode_text(text, sizeof text, out, 3);
    PT_ASSERT_EQ_STR(out, "AB");
    PT_ASSERT_EQ_INT(n, 2);
    PT_ASSERT_EQ_INT(out[3], 'x');
    n = inspect_decode_text(text, sizeof text, out, 1);
    PT_ASSERT_EQ_STR(out, "");
    PT_ASSERT_EQ_INT(n, 0);
    out[0] = 'q';
    n = inspect_decode_text(text, sizeof text, out, 0);
    PT_ASSERT_EQ_INT(n, 0);
    PT_ASSERT_EQ_INT(out[0], 'q');
}

PTEST(keeps_trailing_spaces)
{
    const uint8_t text[] = { 0xBB, 0x00, 0x00, 0xFF };
    char out[16];
    size_t n;
    decode(text, sizeof text, out, sizeof out, &n);
    PT_ASSERT_EQ_STR(out, "A  ");
}

static void first(void) {}
static void second(void) {}

PTEST(finds_a_known_callback_by_address)
{
    const struct inspect_known_callback table[] = {
        { (uintptr_t)first, "first" },
        { (uintptr_t)second, "second" },
    };
    PT_ASSERT_EQ_STR(inspect_find_known_callback(table, 2, (uintptr_t)second), "second");
    PT_ASSERT_EQ_STR(inspect_find_known_callback(table, 2, (uintptr_t)first), "first");
    PT_ASSERT(inspect_find_known_callback(table, 2, 0) == NULL);
    PT_ASSERT(inspect_find_known_callback(table, 1, (uintptr_t)second) == NULL);
    PT_ASSERT(inspect_find_known_callback(table, 0, (uintptr_t)first) == NULL);
}

PTEST(formats_an_unknown_callback_as_hex)
{
    char out[32];
    inspect_format_unknown_callback(0x8123abcu, out, sizeof out);
    PT_ASSERT_EQ_STR(out, "unknown:0x8123abc");
    inspect_format_unknown_callback(0, out, sizeof out);
    PT_ASSERT_EQ_STR(out, "unknown:0x0");
    inspect_format_unknown_callback(0x8123abcu, out, 10);
    PT_ASSERT_EQ_STR(out, "unknown:0");
}

PTEST_MAIN()
