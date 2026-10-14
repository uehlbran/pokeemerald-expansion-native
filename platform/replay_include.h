/*
 * replay_include.h: the replay script's `include FILE` command.
 *
 * Not hardware: it lets a script reuse another one (tests/smoke/debug_menu.replay
 * plays the first steps of the opening by including opening_start.replay).
 * The inclusion is textual and happens before parsing: a line whose command is
 * `include FILE` (a `#` comment may follow) is replaced by FILE's lines, so
 * the parser, the driver and `restart --resume-at LINE` see one flat script and
 * its line numbers. FILE is relative to the directory of the file that includes
 * it, and may include others up to REPLAY_INCLUDE_MAX_DEPTH levels deep and
 * REPLAY_INCLUDE_MAX_FILES files in all (a file that includes itself, directly or
 * not, is refused by the depth limit, or by the file count if it fans out first).
 *
 * Messages name the file and line a problem came from, not the flat line: the
 * expansion comes with a map from each flat line to its (file, line), and
 * replay_source_map_describe turns a flat line back into "file:line".
 */
#ifndef PLATFORM_REPLAY_INCLUDE_H
#define PLATFORM_REPLAY_INCLUDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many files may be nested below the script (a self-include hits this limit). */
#define REPLAY_INCLUDE_MAX_DEPTH 4u
/* How many files one expansion may open, counting repeats: `include` lines emit nothing, so
 * the byte and depth limits alone do not bound the work of a fan-out of includes. */
#define REPLAY_INCLUDE_MAX_FILES 64u
/* The largest file read, and the largest total expansion (a newline is supplied after a
 * last line that has none), in bytes. */
#define REPLAY_INCLUDE_MAX_BYTES ((size_t)1024u * 1024u)
#define REPLAY_INCLUDE_MESSAGE_SIZE 512u
/* The longest include name, in characters (the buffer is one more, for the NUL); a path
 * made of the including file's directory and the name must fit this too. */
#define REPLAY_INCLUDE_MAX_NAME 1023u

struct replay_line_origin {
    uint32_t file; /* index into the map's files */
    uint32_t line; /* 1-based line in that file */
};

/* Where each line of an expansion came from. Zero-initialise it before the call that
 * fills it (replay_source_map_init); release it with replay_source_map_free, which is
 * safe on a zeroed map and twice. */
struct replay_source_map {
    struct replay_line_origin *lines;
    size_t line_count;
    size_t line_capacity;
    char **files;
    size_t file_count;
    size_t file_capacity;
};

void replay_source_map_init(struct replay_source_map *map);
void replay_source_map_free(struct replay_source_map *map);

/* A buffer that holds any description: a file name of up to REPLAY_INCLUDE_MAX_NAME characters,
 * ':', the 10 digits of a u32 line and the NUL. */
#define REPLAY_SOURCE_WHERE_SIZE (REPLAY_INCLUDE_MAX_NAME + 12u)

/* Writes "file:line" for flat line `flat_line` (1-based) of the expansion, or "line N" if the
 * map has no such line (out has `size` bytes, NUL-terminated, cut if too long). */
void replay_source_map_describe(const struct replay_source_map *map, uint32_t flat_line, char *out, size_t size);

/* Reads `path` with its includes expanded into a NUL-terminated heap string that the
 * caller frees; its length is in *length_out, *expanded_out says whether any include was
 * replaced, and `map` (initialised by the caller, may be NULL) says where each line came
 * from. Returns 0, or -1 with the reason in `message` (message_size bytes): the file and
 * line, what went wrong (unreadable file, a NUL byte, too large, too deep, too many files, an include
 * without a name or with a name that is too long) and, for a file that was included, where
 * from. *text_out is NULL after a failure. */
int replay_include_expand(const char *path, char **text_out, size_t *length_out, bool *expanded_out,
                          struct replay_source_map *map, char *message, size_t message_size);

#endif /* PLATFORM_REPLAY_INCLUDE_H */
