/*
 * replay_include.c: see replay_include.h.
 */
#include "replay_include.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INCLUDE_WORD "include"
#define PATH_SIZE 1024u

struct buffer {
    char *text;
    size_t length;
    size_t capacity;
};

struct expansion {
    struct buffer out;
    bool any_include;
    char *message;
    size_t message_size;
};

static void report(struct expansion *expansion, const char *path, unsigned line, const char *reason)
{
    if (line > 0)
        snprintf(expansion->message, expansion->message_size, "%s:%u: %s", path, line, reason);
    else
        snprintf(expansion->message, expansion->message_size, "%s: %s", path, reason);
}

static int append(struct expansion *expansion, const char *path, const char *text, size_t length)
{
    struct buffer *buffer = &expansion->out;
    if (length > REPLAY_INCLUDE_MAX_BYTES - buffer->length) {
        report(expansion, path, 0, "the script with its includes is larger than the limit");
        return -1;
    }
    if (buffer->length + length + 1u > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? 4096u : buffer->capacity;
        while (capacity < buffer->length + length + 1u)
            capacity *= 2u;
        char *grown = realloc(buffer->text, capacity);
        if (grown == NULL) {
            report(expansion, path, 0, "out of memory");
            return -1;
        }
        buffer->text = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->text + buffer->length, text, length);
    buffer->length += length;
    buffer->text[buffer->length] = '\0';
    return 0;
}

/* The whole file, NUL-terminated, in a heap string; NULL after reporting. */
static char *read_file(struct expansion *expansion, const char *path, size_t *length_out)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        char reason[128];
        snprintf(reason, sizeof reason, "cannot open: %s", strerror(errno));
        report(expansion, path, 0, reason);
        return NULL;
    }
    char *text = malloc(REPLAY_INCLUDE_MAX_BYTES + 1u);
    size_t length = text != NULL ? fread(text, 1, REPLAY_INCLUDE_MAX_BYTES + 1u, file) : 0;
    bool failed = text == NULL || ferror(file) != 0 || length > REPLAY_INCLUDE_MAX_BYTES;
    fclose(file);
    if (failed) {
        report(expansion, path, 0, "cannot read it, or it is larger than the limit");
        free(text);
        return NULL;
    }
    text[length] = '\0';
    *length_out = length;
    return text;
}

/* If the line is `include FILE` (blanks around, an optional # comment), the
 * file name is copied to name (size bytes) and the result is 1; 0 if it is
 * another command; -1 if `include` has no usable file name. */
static int include_target(const char *line, size_t length, char *name, size_t size)
{
    size_t start = 0;
    size_t word = strlen(INCLUDE_WORD);
    while (start < length && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if (length - start < word || strncmp(line + start, INCLUDE_WORD, word) != 0)
        return 0;
    start += word;
    if (start < length && line[start] != ' ' && line[start] != '\t' && line[start] != '\r' && line[start] != '#')
        return 0; /* another word that starts with "include" */
    size_t end = start;
    while (end < length && line[end] != '#')
        end++;
    while (start < end && (line[start] == ' ' || line[start] == '\t'))
        start++;
    while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
        end--;
    if (end == start || end - start >= size)
        return -1;
    memcpy(name, line + start, end - start);
    name[end - start] = '\0';
    return 1;
}

/* path of `name` next to `including` (an absolute name stays as it is). */
static bool sibling_path(const char *including, const char *name, char *out, size_t size)
{
    const char *slash = strrchr(including, '/');
    int written;
    if (name[0] == '/' || slash == NULL)
        written = snprintf(out, size, "%s", name);
    else
        written = snprintf(out, size, "%.*s/%s", (int)(slash - including), including, name);
    return written >= 0 && (size_t)written < size;
}

static int expand_file(struct expansion *expansion, const char *path, unsigned depth)
{
    size_t length;
    if (depth > REPLAY_INCLUDE_MAX_DEPTH) {
        report(expansion, path, 0, "includes nested too deep (does a file include itself?)");
        return -1;
    }
    char *text = read_file(expansion, path, &length);
    if (text == NULL)
        return -1;
    int result = 0;
    unsigned line_number = 0;
    for (const char *at = text; *at != '\0' && result == 0;) {
        size_t line_length = strcspn(at, "\n");
        size_t full_length = line_length + (at[line_length] == '\n' ? 1u : 0u);
        char name[PATH_SIZE];
        char target[PATH_SIZE];
        line_number++;
        int kind = include_target(at, line_length, name, sizeof name);
        if (kind < 0) {
            report(expansion, path, line_number, "include needs a file name");
            result = -1;
        } else if (kind == 0) {
            result = append(expansion, path, at, line_length);
            if (result == 0)
                result = append(expansion, path, "\n", 1);
        } else if (!sibling_path(path, name, target, sizeof target)) {
            report(expansion, path, line_number, "the include path is too long");
            result = -1;
        } else {
            expansion->any_include = true;
            result = expand_file(expansion, target, depth + 1u); /* a failure names the included file and line */
        }
        at += full_length;
    }
    free(text);
    return result;
}

int replay_include_expand(const char *path, char **text_out, size_t *length_out, bool *expanded_out, char *message,
                          size_t message_size)
{
    struct expansion expansion = { .message = message, .message_size = message_size };
    *text_out = NULL;
    *expanded_out = false;
    if (message_size > 0)
        message[0] = '\0';
    if (expand_file(&expansion, path, 0) != 0) {
        free(expansion.out.text);
        return -1;
    }
    if (expansion.out.text == NULL && append(&expansion, path, "", 0) != 0) {
        free(expansion.out.text);
        return -1;
    }
    *text_out = expansion.out.text;
    *length_out = expansion.out.length;
    *expanded_out = expansion.any_include;
    return 0;
}
