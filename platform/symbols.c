/*
 * symbols.c: function names from the map the link writes next to the
 * executable (symbols.h has the format).
 *
 * The map is read whole into one buffer, and each name is terminated in
 * place, so the names live as long as the load. Every byte of a line is
 * checked before it is trusted: a line that is not exactly an address of
 * this build's width, one space, a name and a newline, or an address lower
 * than the line before's, is an error, never a guess.
 */
#include "symbols.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_process.h"

/* The map's address digits are hexadecimal, 4 bits each. */
#define HEX_DIGIT_BITS 4u
#define DECIMAL_DIGIT_COUNT 10
/* ASCII DEL, the control character above the printable ones. */
#define ASCII_DELETE 0x7Fu

struct symbol {
    uintptr_t address;
    uint32_t name_offset; /* into text */
};

static struct {
    struct symbol *symbols; /* in address order, aliases in the map's order */
    uint32_t count;
    char *text; /* the map, each name NUL-terminated in place */
} loaded;

void symbols_unload(void)
{
    free(loaded.symbols);
    free(loaded.text);
    loaded.symbols = NULL;
    loaded.text = NULL;
    loaded.count = 0;
}

uint32_t symbols_count(void)
{
    return loaded.count;
}

/* The value of a hexadecimal digit, or -1. */
static int hex_value(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'a' && digit <= 'f')
        return DECIMAL_DIGIT_COUNT + (digit - 'a');
    if (digit >= 'A' && digit <= 'F')
        return DECIMAL_DIGIT_COUNT + (digit - 'A');
    return -1;
}

/* A name's characters: anything printable but the space (nm's names have
 * none), and bytes past ASCII. */
static bool is_name_character(char character)
{
    const unsigned char byte = (unsigned char)character;
    return byte > ' ' && byte != ASCII_DELETE;
}

/* Reads the line that starts at text[*at] (length bytes in all) into out,
 * terminates its name in place and moves *at past its newline. False if the
 * line is not "<address> <name>\n". */
static bool parse_line(char *text, size_t length, size_t *at, struct symbol *out)
{
    size_t i = *at;
    uintptr_t address = 0;
    for (size_t digits = 0; digits < SYMBOLS_ADDRESS_DIGITS; digits++, i++) {
        const int digit = i < length ? hex_value(text[i]) : -1;
        if (digit < 0)
            return false;
        address = address << HEX_DIGIT_BITS | (uintptr_t)digit;
    }
    if (i >= length || text[i] != ' ')
        return false;
    const size_t name = ++i;
    while (i < length && is_name_character(text[i]))
        i++;
    if (i == name || i >= length || text[i] != '\n')
        return false;
    text[i] = '\0';
    *out = (struct symbol){.address = address, .name_offset = (uint32_t)name};
    *at = i + 1u;
    return true;
}

/* "symbols: <call>(<path>): <error>" on stderr. */
static void report_failure(const char *call, const char *path, int error)
{
    (void)fprintf(stderr, "symbols: %s(%s): %s\n", call, path, strerror(error));
}

/* The size of the open file, or -1 with *failed_call naming the call. */
static long size_of_file(FILE *file, const char **failed_call)
{
    if (fseek(file, 0, SEEK_END) != 0) {
        *failed_call = "fseek";
        return -1;
    }
    const long end = ftell(file);
    if (end < 0) {
        *failed_call = "ftell";
        return -1;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        *failed_call = "fseek";
        return -1;
    }
    return end;
}

/* The size bytes of the open file in a new buffer with a NUL after them, or
 * NULL with *failed_call naming the call. A file that is shorter than it was
 * a moment ago reads as an I/O error. */
static char *read_text(FILE *file, size_t size, const char **failed_call)
{
    char *const text = malloc(size + 1u);
    if (text == NULL) {
        *failed_call = "malloc";
        return NULL;
    }
    if (fread(text, 1, size, file) != size) {
        if (!ferror(file))
            errno = EIO;
        *failed_call = "fread";
        free(text);
        return NULL;
    }
    text[size] = '\0';
    return text;
}

/* The whole file at path, NUL-terminated, in a buffer the caller frees, and
 * its length in *length (shorter than UINT32_MAX, so that a name's offset
 * fits); NULL after a report. */
static char *read_whole(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        report_failure("fopen", path, errno);
        return NULL;
    }
    const char *failed_call = NULL;
    char *text = NULL;
    const long end = size_of_file(file, &failed_call);
    if (end >= 0 && (unsigned long)end >= UINT32_MAX)
        (void)fprintf(stderr, "symbols: %s is %ld bytes; a map must be smaller than 4 GiB\n", path, end);
    else if (end >= 0)
        text = read_text(file, (size_t)end, &failed_call);
    const int error = errno; /* of the call that failed, before fclose can change it */
    (void)fclose(file);      /* read-only: nothing to lose */
    if (failed_call != NULL)
        report_failure(failed_call, path, error);
    if (text == NULL)
        return NULL;
    *length = (size_t)end;
    return text;
}

/* The lines of the text: its newlines. */
static uint32_t count_lines(const char *text, size_t length)
{
    uint32_t lines = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n')
            lines++;
    }
    return lines;
}

/* Parses every line of loaded.text into loaded.symbols. False after a report
 * naming the first bad line. */
static bool parse_map(const char *path, size_t length)
{
    const uint32_t lines = count_lines(loaded.text, length);
    loaded.symbols = malloc((lines > 0 ? lines : 1u) * sizeof *loaded.symbols);
    if (loaded.symbols == NULL) {
        (void)fprintf(stderr, "symbols: no memory for the %u functions of %s\n", lines, path);
        return false;
    }
    size_t at = 0;
    while (at < length) { /* each line parsed takes one of the newlines counted: loaded.count stays within lines */
        const uint32_t line = loaded.count + 1u;
        struct symbol symbol;
        if (!parse_line(loaded.text, length, &at, &symbol)) {
            (void)fprintf(stderr, "symbols: %s:%u: not \"<%llu hexadecimal digits> <name>\" and a newline\n", path,
                          line, (unsigned long long)SYMBOLS_ADDRESS_DIGITS);
            return false;
        }
        if (loaded.count > 0 && symbol.address < loaded.symbols[loaded.count - 1u].address) {
            (void)fprintf(stderr, "symbols: %s:%u: address lower than the line before's (the map is sorted)\n", path,
                          line);
            return false;
        }
        loaded.symbols[loaded.count++] = symbol;
    }
    if (loaded.count == 0) { /* every executable has functions: an empty map is a failed nm */
        (void)fprintf(stderr, "symbols: %s names no function\n", path);
        return false;
    }
    return true;
}

int symbols_load(const char *path)
{
    symbols_unload();
    size_t length = 0;
    loaded.text = read_whole(path, &length);
    if (loaded.text == NULL || !parse_map(path, length)) {
        symbols_unload();
        return -1;
    }
    return 0;
}

int symbols_load_own(void)
{
    char path[HOST_PROCESS_PATH_SIZE + sizeof SYMBOLS_MAP_SUFFIX];
    if (host_process_executable_path(path, HOST_PROCESS_PATH_SIZE) != 0) { /* reported */
        symbols_unload();
        return -1;
    }
    memcpy(path + strlen(path), SYMBOLS_MAP_SUFFIX, sizeof SYMBOLS_MAP_SUFFIX);
    return symbols_load(path);
}

const char *symbols_name_at(uintptr_t address)
{
    uint32_t low = 0;
    uint32_t high = loaded.count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (loaded.symbols[middle].address < address)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low < loaded.count && loaded.symbols[low].address == address)
        return loaded.text + loaded.symbols[low].name_offset;
    return NULL;
}
