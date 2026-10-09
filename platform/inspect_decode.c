/*
 * inspect_decode.c: the game's text encoding and callback names (see
 * inspect_decode.h). The character values are those of expansion's
 * charmap.txt: ' ' = 00, '0'..'9' = A1..AA, '!' = AB, '?' = AC, '.' = AD,
 * '-' = AE, ',' = B8, '/' = BA, 'A'..'Z' = BB..D4, 'a'..'z' = D5..EE,
 * EOS = FF.
 */
#include "inspect_decode.h"

#include <inttypes.h>
#include <stdio.h>

static char decode_char(uint8_t byte)
{
    if (byte == INSPECT_CHAR_SPACE)
        return ' ';
    if (byte >= INSPECT_CHAR_DIGIT_0 && byte <= INSPECT_CHAR_DIGIT_9)
        return (char)('0' + (byte - INSPECT_CHAR_DIGIT_0));
    if (byte >= INSPECT_CHAR_UPPER_A && byte <= INSPECT_CHAR_UPPER_Z)
        return (char)('A' + (byte - INSPECT_CHAR_UPPER_A));
    if (byte >= INSPECT_CHAR_LOWER_A && byte <= INSPECT_CHAR_LOWER_Z)
        return (char)('a' + (byte - INSPECT_CHAR_LOWER_A));
    switch (byte) {
    case INSPECT_CHAR_EXCLAMATION:
        return '!';
    case INSPECT_CHAR_QUESTION:
        return '?';
    case INSPECT_CHAR_PERIOD:
        return '.';
    case INSPECT_CHAR_HYPHEN:
        return '-';
    case INSPECT_CHAR_COMMA:
        return ',';
    case INSPECT_CHAR_SLASH:
        return '/';
    default:
        return '?';
    }
}

size_t inspect_decode_text(const uint8_t *encoded, size_t max_length, char *out, size_t size)
{
    size_t written = 0;
    if (size == 0)
        return 0;
    for (size_t i = 0; i < max_length && encoded[i] != INSPECT_CHAR_EOS && written + 1 < size; i++)
        out[written++] = decode_char(encoded[i]);
    out[written] = '\0';
    return written;
}

const char *inspect_find_known_callback(const struct inspect_known_callback *table, size_t count, uintptr_t address)
{
    for (size_t i = 0; i < count; i++) {
        if (table[i].address == address)
            return table[i].name;
    }
    return NULL;
}

void inspect_format_unknown_callback(uintptr_t address, char *out, size_t size)
{
    snprintf(out, size, "unknown:0x%" PRIxPTR, address);
}
