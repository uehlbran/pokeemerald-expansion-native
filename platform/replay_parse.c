/*
 * replay_parse.c: the replay script's text format (replay.h has the grammar).
 *
 * Not hardware: the script stands in for the player. Plain line-by-line
 * parsing with fixed-size buffers sized by the REPLAY_MAX_* constants; the
 * first error stops the parse and names its line.
 */
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "replay.h"

struct key_name {
    const char *name;
    uint16_t key;
};

static const struct key_name key_names[] = {
    { "A", INPUT_KEY_A },       { "B", INPUT_KEY_B },       { "START", INPUT_KEY_START }, { "SELECT", INPUT_KEY_SELECT },
    { "UP", INPUT_KEY_UP },     { "DOWN", INPUT_KEY_DOWN }, { "LEFT", INPUT_KEY_LEFT },   { "RIGHT", INPUT_KEY_RIGHT },
    { "L", INPUT_KEY_L },       { "R", INPUT_KEY_R },
};

static void set_error(struct replay_parse_error *error, uint32_t line, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

static void set_error(struct replay_parse_error *error, uint32_t line, const char *format, ...)
{
    va_list arguments;
    error->line = line;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof error->message, format, arguments);
    va_end(arguments);
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Trims in place: leading blanks skipped (the returned pointer), trailing
 * blanks cut. */
static char *trim(char *text)
{
    while (is_blank(*text))
        text++;
    size_t length = strlen(text);
    while (length > 0 && is_blank(text[length - 1]))
        text[--length] = '\0';
    return text;
}

/* Splits off the first blank-separated word; *rest is what follows it,
 * trimmed (an empty string if nothing). */
static char *take_word(char *text, char **rest)
{
    size_t length = strcspn(text, " \t");
    char *word = text;
    if (text[length] == '\0') {
        *rest = text + length;
    } else {
        text[length] = '\0';
        *rest = trim(text + length + 1);
    }
    return word;
}

static bool parse_number(const char *text, uint32_t minimum, uint32_t *out)
{
    if (text[0] < '0' || text[0] > '9')
        return false;
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || value < minimum || value > REPLAY_MAX_FRAMES)
        return false;
    *out = (uint32_t)value;
    return true;
}

static bool equals_ignore_case(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return false;
    }
    return *a == *b;
}

/* "A,START" to key bits; false for an unknown or empty element. */
static bool parse_keys(char *text, uint16_t *out, const char **bad)
{
    uint16_t keys = 0;
    *bad = text;
    if (*text == '\0')
        return false;
    for (char *save = text;;) {
        char *comma = strchr(save, ',');
        if (comma != NULL)
            *comma = '\0';
        char *element = trim(save);
        bool found = false;
        for (size_t i = 0; i < sizeof key_names / sizeof key_names[0]; i++) {
            if (equals_ignore_case(element, key_names[i].name)) {
                keys |= key_names[i].key;
                found = true;
            }
        }
        if (!found) {
            *bad = element;
            return false;
        }
        if (comma == NULL)
            break;
        save = comma + 1;
    }
    *out = keys;
    return true;
}

static bool valid_fact_name(const char *text)
{
    size_t length = strlen(text);
    if (length == 0 || length > REPLAY_MAX_FACT_LENGTH)
        return false;
    for (size_t i = 0; i < length; i++) {
        if (!((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
            return false;
    }
    return true;
}

static bool valid_file_name(const char *text)
{
    size_t length = strlen(text);
    if (length == 0 || length > REPLAY_MAX_NAME_LENGTH)
        return false;
    for (size_t i = 0; i < length; i++) {
        if (!isalnum((unsigned char)text[i]) && text[i] != '_' && text[i] != '-')
            return false;
    }
    return true;
}

/* "FACT=VALUE" into the command's fact and value. */
static bool parse_condition(char *text, struct replay_command *command, struct replay_parse_error *error)
{
    char *equals = strchr(text, '=');
    if (equals == NULL) {
        set_error(error, command->line, "expected FACT=VALUE, got '%s'", text);
        return false;
    }
    *equals = '\0';
    char *fact = trim(text);
    char *value = trim(equals + 1);
    if (!valid_fact_name(fact)) {
        set_error(error, command->line, "bad fact name '%s' (lower-case letters, digits, '_')", fact);
        return false;
    }
    if (*value == '\0' || strlen(value) > REPLAY_MAX_VALUE_LENGTH) {
        set_error(error, command->line, "the value of '%s' is empty or too long", fact);
        return false;
    }
    strcpy(command->fact, fact);
    strcpy(command->value, value);
    return true;
}

static bool parse_count_command(struct replay_command *command, const char *word, char *rest,
                                struct replay_parse_error *error)
{
    uint32_t minimum = command->kind == REPLAY_CMD_FRAME ? 1u : 0u;
    if (!parse_number(rest, minimum, &command->number)) {
        set_error(error, command->line, "'%s' needs a whole number of frames", word);
        return false;
    }
    return true;
}

/* hold KEYS [for N] */
static bool parse_hold(struct replay_command *command, char *rest, struct replay_parse_error *error)
{
    const char *bad;
    char *for_at = strstr(rest, " for ");
    command->kind = REPLAY_CMD_HOLD;
    if (for_at != NULL) {
        *for_at = '\0';
        if (!parse_number(trim(for_at + strlen(" for ")), 1u, &command->number)) {
            set_error(error, command->line, "'hold ... for' needs a whole number of frames, at least 1");
            return false;
        }
        command->kind = REPLAY_CMD_HOLD_FOR;
    }
    if (!parse_keys(trim(rest), &command->keys, &bad)) {
        set_error(error, command->line, "unknown key '%s'", bad);
        return false;
    }
    return true;
}

/* wait-until FACT=VALUE max N, and the tail of mash KEYS until FACT=VALUE max N */
static bool parse_condition_and_max(struct replay_command *command, char *rest, struct replay_parse_error *error)
{
    char *max_at = NULL;
    for (char *at = strstr(rest, " max "); at != NULL; at = strstr(at + 1, " max "))
        max_at = at;
    if (max_at == NULL) {
        set_error(error, command->line, "%s needs 'max N' after the condition", command->kind == REPLAY_CMD_MASH ? "mash" : "wait-until");
        return false;
    }
    *max_at = '\0';
    if (!parse_number(trim(max_at + strlen(" max ")), 0u, &command->number)) {
        set_error(error, command->line, "'max' needs a whole number of frames");
        return false;
    }
    return parse_condition(rest, command, error);
}

/* mash KEYS until FACT=VALUE max N */
static bool parse_mash(struct replay_command *command, char *rest, struct replay_parse_error *error)
{
    const char *bad;
    char *until_at = strstr(rest, " until ");
    if (until_at == NULL) {
        set_error(error, command->line, "mash needs 'until FACT=VALUE max N' after the keys");
        return false;
    }
    *until_at = '\0';
    if (!parse_keys(trim(rest), &command->keys, &bad)) {
        set_error(error, command->line, "unknown key '%s'", bad);
        return false;
    }
    return parse_condition_and_max(command, trim(until_at + strlen(" until ")), error);
}

static bool parse_name_command(struct replay_command *command, char *rest, struct replay_parse_error *error)
{
    if (!valid_file_name(rest)) {
        set_error(error, command->line, "bad name '%s' (letters, digits, '_', '-')", rest);
        return false;
    }
    strcpy(command->name, rest);
    return true;
}

static bool parse_command(char *text, struct replay_command *command, struct replay_parse_error *error)
{
    char *rest;
    const char *word = take_word(text, &rest);
    const char *bad;

    if (strcmp(word, "frame") == 0 || strcmp(word, "wait") == 0) {
        command->kind = word[0] == 'f' ? REPLAY_CMD_FRAME : REPLAY_CMD_WAIT;
        return parse_count_command(command, word, rest, error);
    }
    if (strcmp(word, "press") == 0 || strcmp(word, "release") == 0) {
        command->kind = word[0] == 'p' ? REPLAY_CMD_PRESS : REPLAY_CMD_RELEASE;
        if (!parse_keys(rest, &command->keys, &bad)) {
            set_error(error, command->line, "unknown key '%s'", bad);
            return false;
        }
        return true;
    }
    if (strcmp(word, "hold") == 0)
        return parse_hold(command, rest, error);
    if (strcmp(word, "wait-until") == 0) {
        command->kind = REPLAY_CMD_WAIT_UNTIL;
        return parse_condition_and_max(command, rest, error);
    }
    if (strcmp(word, "mash") == 0) {
        command->kind = REPLAY_CMD_MASH;
        return parse_mash(command, rest, error);
    }
    if (strcmp(word, "expect") == 0) {
        command->kind = REPLAY_CMD_EXPECT;
        return parse_condition(rest, command, error);
    }
    if (strcmp(word, "screenshot") == 0 || strcmp(word, "dump") == 0) {
        command->kind = word[0] == 's' ? REPLAY_CMD_SCREENSHOT : REPLAY_CMD_DUMP;
        return parse_name_command(command, rest, error);
    }
    if (strcmp(word, "restart") == 0) {
        command->kind = REPLAY_CMD_RESTART;
        if (*rest != '\0') {
            set_error(error, command->line, "restart takes no arguments, got '%s'", rest);
            return false;
        }
        return true;
    }
    set_error(error, command->line, "unknown command '%s'", word);
    return false;
}

int replay_parse(const char *text, struct replay_script *script, struct replay_parse_error *error)
{
    char line[REPLAY_MAX_LINE_LENGTH + 2u];
    uint32_t line_number = 0;
    script->count = 0;
    while (*text != '\0') {
        size_t length = strcspn(text, "\n");
        line_number++;
        if (length > REPLAY_MAX_LINE_LENGTH) {
            set_error(error, line_number, "line longer than the limit");
            return -1;
        }
        memcpy(line, text, length);
        line[length] = '\0';
        text += length + (text[length] == '\n' ? 1u : 0u);

        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';
        char *body = trim(line);
        if (*body == '\0')
            continue;
        if (script->count >= REPLAY_MAX_COMMANDS) {
            set_error(error, line_number, "more commands than the limit");
            return -1;
        }
        struct replay_command *command = &script->commands[script->count];
        *command = (struct replay_command){ .line = line_number };
        if (!parse_command(body, command, error))
            return -1;
        script->count++;
    }
    return 0;
}
