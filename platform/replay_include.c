/*
 * replay_include.c: see replay_include.h.
 */
#include "replay_include.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buffers: the expansion starts at this many bytes and doubles; a reason from strerror fits
 * REASON_SIZE; a path (a name, or directory + "/" + name) fits PATH_SIZE, which is
 * REPLAY_INCLUDE_MAX_NAME plus the NUL. */
#define INITIAL_CAPACITY 4096u
#define INITIAL_MAP_LINES 1024u
#define REASON_SIZE 128u
#define PATH_SIZE (REPLAY_INCLUDE_MAX_NAME + 1u)
#define INCLUDE_WORD "include"

struct buffer {
    char *text;
    size_t length;
    size_t capacity;
};

struct expansion {
    struct buffer out;
    struct replay_source_map *map;
    bool any_include;
    char *message;
    size_t message_size;
};

/* Where a file was included from, for messages (NULL for the script itself). */
struct includer {
    const char *path;
    unsigned line;
};

void replay_source_map_init(struct replay_source_map *map)
{
    *map = (struct replay_source_map){ 0 };
}

void replay_source_map_free(struct replay_source_map *map)
{
    for (size_t i = 0; i < map->file_count; i++)
        free(map->files[i]);
    free(map->files);
    free(map->lines);
    *map = (struct replay_source_map){ 0 };
}

void replay_source_map_describe(const struct replay_source_map *map, uint32_t flat_line, char *out, size_t size)
{
    if (size == 0)
        return;
    if (flat_line >= 1u && flat_line <= map->line_count) {
        const struct replay_line_origin *origin = &map->lines[flat_line - 1u];
        snprintf(out, size, "%s:%u", map->files[origin->file], (unsigned)origin->line);
    } else {
        snprintf(out, size, "line %u", (unsigned)flat_line);
    }
}

/* The index of `path` among the map's files, adding it; -1 if out of memory. */
static int64_t map_file_index(struct replay_source_map *map, const char *path)
{
    for (size_t i = 0; i < map->file_count; i++) {
        if (strcmp(map->files[i], path) == 0)
            return (int64_t)i;
    }
    if (map->file_count == map->file_capacity) {
        size_t capacity = map->file_capacity == 0 ? 4u : map->file_capacity * 2u;
        char **grown = realloc(map->files, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        map->files = grown;
        map->file_capacity = capacity;
    }
    char *copy = strdup(path);
    if (copy == NULL)
        return -1;
    map->files[map->file_count] = copy;
    return (int64_t)map->file_count++;
}

static int map_add_line(struct replay_source_map *map, uint32_t file, unsigned line)
{
    if (map->line_count == map->line_capacity) {
        size_t capacity = map->line_capacity == 0 ? INITIAL_MAP_LINES : map->line_capacity * 2u;
        struct replay_line_origin *grown = realloc(map->lines, capacity * sizeof *grown);
        if (grown == NULL)
            return -1;
        map->lines = grown;
        map->line_capacity = capacity;
    }
    map->lines[map->line_count++] = (struct replay_line_origin){ file, line };
    return 0;
}

/* "path:line: reason", or "path: reason" without a line, then " (included at X:L)". */
static void report(struct expansion *expansion, const char *path, unsigned line, const char *reason,
                   const struct includer *includer)
{
    char where[PATH_SIZE + 32u];
    if (line > 0)
        snprintf(where, sizeof where, "%s:%u", path, line);
    else
        snprintf(where, sizeof where, "%s", path);
    if (includer != NULL)
        snprintf(expansion->message, expansion->message_size, "%s: %s (included at %s:%u)", where, reason,
                 includer->path, includer->line);
    else
        snprintf(expansion->message, expansion->message_size, "%s: %s", where, reason);
}

static int append(struct expansion *expansion, const char *path, const char *text, size_t length,
                  const struct includer *includer)
{
    struct buffer *buffer = &expansion->out;
    if (length > REPLAY_INCLUDE_MAX_BYTES - buffer->length) {
        report(expansion, path, 0, "the script with its includes is larger than the limit", includer);
        return -1;
    }
    if (buffer->length + length + 1u > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? INITIAL_CAPACITY : buffer->capacity;
        while (capacity < buffer->length + length + 1u)
            capacity *= 2u;
        char *grown = realloc(buffer->text, capacity);
        if (grown == NULL) {
            report(expansion, path, 0, "out of memory", includer);
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
static char *read_file(struct expansion *expansion, const char *path, size_t *length_out,
                       const struct includer *includer)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        char reason[REASON_SIZE];
        snprintf(reason, sizeof reason, "cannot open: %s", strerror(errno));
        report(expansion, path, 0, reason, includer);
        return NULL;
    }
    char *text = malloc(REPLAY_INCLUDE_MAX_BYTES + 1u);
    size_t length = text != NULL ? fread(text, 1, REPLAY_INCLUDE_MAX_BYTES + 1u, file) : 0;
    bool failed = text == NULL || ferror(file) != 0 || length > REPLAY_INCLUDE_MAX_BYTES;
    fclose(file);
    if (failed) {
        report(expansion, path, 0, "cannot read it, or it is larger than the limit", includer);
        free(text);
        return NULL;
    }
    text[length] = '\0';
    /* a NUL would end the text for every string function below and silently cut the script */
    const char *nul = memchr(text, '\0', length);
    if (nul != NULL) {
        char reason[REASON_SIZE];
        snprintf(reason, sizeof reason, "contains a NUL byte at offset %zu", (size_t)(nul - text));
        report(expansion, path, 0, reason, includer);
        free(text);
        return NULL;
    }
    *length_out = length;
    return text;
}

enum include_kind {
    INCLUDE_NOT,       /* another command */
    INCLUDE_NAMED,     /* `include FILE`; the name is in `name` */
    INCLUDE_NO_NAME,   /* `include` and nothing (but a comment) after it */
    INCLUDE_NAME_LONG  /* a name longer than REPLAY_INCLUDE_MAX_NAME */
};

/* Classifies the line (blanks around, an optional # comment) and, for INCLUDE_NAMED, copies the
 * file name to name (PATH_SIZE bytes). */
static enum include_kind include_target(const char *line, size_t length, char *name)
{
    size_t start = 0;
    const size_t word = sizeof INCLUDE_WORD - 1u;
    while (start < length && (line[start] == ' ' || line[start] == '\t'))
        start++;
    if (strncmp(line + start, INCLUDE_WORD, word) != 0)
        return INCLUDE_NOT;
    start += word;
    if (start < length && line[start] != ' ' && line[start] != '\t' && line[start] != '\r' && line[start] != '#')
        return INCLUDE_NOT; /* another word that starts with "include" */
    size_t end = start;
    while (end < length && line[end] != '#')
        end++;
    while (start < end && (line[start] == ' ' || line[start] == '\t'))
        start++;
    while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r'))
        end--;
    if (end == start)
        return INCLUDE_NO_NAME;
    if (end - start > REPLAY_INCLUDE_MAX_NAME)
        return INCLUDE_NAME_LONG;
    memcpy(name, line + start, end - start);
    name[end - start] = '\0';
    return INCLUDE_NAMED;
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

static int expand_file(struct expansion *expansion, const char *path, unsigned depth, const struct includer *includer);

/* Appends one line of the script (without its newline) and records where it came from. */
static int emit_line(struct expansion *expansion, const char *path, uint32_t file, unsigned line_number,
                     const char *text, size_t length, const struct includer *includer)
{
    if (append(expansion, path, text, length, includer) != 0 || append(expansion, path, "\n", 1, includer) != 0)
        return -1;
    if (expansion->map != NULL && map_add_line(expansion->map, file, line_number) != 0) {
        report(expansion, path, 0, "out of memory", includer);
        return -1;
    }
    return 0;
}

static int expand_text(struct expansion *expansion, const char *path, const char *text, unsigned depth,
                       const struct includer *includer)
{
    uint32_t file = 0;
    unsigned line_number = 0;
    if (expansion->map != NULL) {
        int64_t index = map_file_index(expansion->map, path);
        if (index < 0) {
            report(expansion, path, 0, "out of memory", includer);
            return -1;
        }
        file = (uint32_t)index;
    }
    for (const char *at = text; *at != '\0';) {
        size_t line_length = strcspn(at, "\n");
        size_t full_length = line_length + (at[line_length] == '\n' ? 1u : 0u);
        char name[PATH_SIZE];
        line_number++;
        switch (include_target(at, line_length, name)) {
        case INCLUDE_NOT:
            if (emit_line(expansion, path, file, line_number, at, line_length, includer) != 0)
                return -1;
            break;
        case INCLUDE_NO_NAME:
            report(expansion, path, line_number, "include needs a file name", includer);
            return -1;
        case INCLUDE_NAME_LONG:
            report(expansion, path, line_number, "the include name is longer than the limit", includer);
            return -1;
        case INCLUDE_NAMED: {
            struct includer here = { path, line_number };
            char target[PATH_SIZE];
            if (!sibling_path(path, name, target, sizeof target)) {
                report(expansion, path, line_number, "the include path is too long", includer);
                return -1;
            }
            expansion->any_include = true;
            if (expand_file(expansion, target, depth + 1u, &here) != 0)
                return -1;
            break;
        }
        }
        at += full_length;
    }
    return 0;
}

static int expand_file(struct expansion *expansion, const char *path, unsigned depth, const struct includer *includer)
{
    size_t length;
    if (depth > REPLAY_INCLUDE_MAX_DEPTH) {
        report(expansion, path, 0, "includes nested too deep (does a file include itself?)", includer);
        return -1;
    }
    char *text = read_file(expansion, path, &length, includer);
    if (text == NULL)
        return -1;
    int result = expand_text(expansion, path, text, depth, includer);
    free(text);
    return result;
}

int replay_include_expand(const char *path, char **text_out, size_t *length_out, bool *expanded_out,
                          struct replay_source_map *map, char *message, size_t message_size)
{
    struct expansion expansion = { .map = map, .message = message, .message_size = message_size };
    *text_out = NULL;
    *expanded_out = false;
    if (message_size > 0)
        message[0] = '\0';
    if (expand_file(&expansion, path, 0, NULL) != 0) {
        free(expansion.out.text);
        return -1;
    }
    if (expansion.out.text == NULL && append(&expansion, path, "", 0, NULL) != 0) {
        free(expansion.out.text);
        return -1;
    }
    *text_out = expansion.out.text;
    *length_out = expansion.out.length;
    *expanded_out = expansion.any_include;
    return 0;
}
