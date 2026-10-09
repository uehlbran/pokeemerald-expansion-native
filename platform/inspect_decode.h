/*
 * inspect_decode.h: the pure helpers behind the inspection facts
 * (inspect.h): the game's text encoding to ASCII, and callback addresses to
 * names. No access to the game's variables, so they are unit tested alone
 * (test_inspect_decode.c).
 */
#ifndef PLATFORM_INSPECT_DECODE_H
#define PLATFORM_INSPECT_DECODE_H

#include <stddef.h>
#include <stdint.h>

/* Bytes of the game's character map (expansion's charmap.txt) that decode to
 * something other than '?'. */
#define INSPECT_CHAR_SPACE UINT8_C(0x00)
#define INSPECT_CHAR_DIGIT_0 UINT8_C(0xA1)
#define INSPECT_CHAR_DIGIT_9 UINT8_C(0xAA)
#define INSPECT_CHAR_EXCLAMATION UINT8_C(0xAB)
#define INSPECT_CHAR_QUESTION UINT8_C(0xAC)
#define INSPECT_CHAR_PERIOD UINT8_C(0xAD)
#define INSPECT_CHAR_HYPHEN UINT8_C(0xAE)
#define INSPECT_CHAR_COMMA UINT8_C(0xB8)
#define INSPECT_CHAR_SLASH UINT8_C(0xBA)
#define INSPECT_CHAR_UPPER_A UINT8_C(0xBB)
#define INSPECT_CHAR_UPPER_Z UINT8_C(0xD4)
#define INSPECT_CHAR_LOWER_A UINT8_C(0xD5)
#define INSPECT_CHAR_LOWER_Z UINT8_C(0xEE)
#define INSPECT_CHAR_EOS UINT8_C(0xFF)

/* Decodes the game string at `encoded` (at most `max_length` bytes; it ends
 * at EOS) to ASCII in out (size bytes, always NUL-terminated, cut if too
 * long). Letters, digits, space and ! ? . - , / decode; any other character
 * is '?'. Returns the number of characters written. Trailing spaces are
 * kept. */
size_t inspect_decode_text(const uint8_t *encoded, size_t max_length, char *out, size_t size);

/* An entry of a table of known function addresses. */
struct inspect_known_callback {
    uintptr_t address;
    const char *name;
};

/* The name of the entry whose address matches, or NULL. */
const char *inspect_find_known_callback(const struct inspect_known_callback *table, size_t count, uintptr_t address);

/* Writes "unknown:0x<hex address>" to out (size bytes). */
void inspect_format_unknown_callback(uintptr_t address, char *out, size_t size);

#endif /* PLATFORM_INSPECT_DECODE_H */
