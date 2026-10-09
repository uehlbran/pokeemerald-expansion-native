/*
 * replay_include.h: the replay script's `include FILE` command.
 *
 * Not hardware: it lets a script reuse another one (tests/smoke/debug_menu.replay
 * plays the first steps of the opening by including opening_start.replay).
 * The inclusion is textual and happens before parsing: a line whose command is
 * `include FILE` (a `#` comment may follow) is replaced by FILE's lines, so
 * the parser, the driver and `restart --resume-at LINE` see one flat script and
 * its line numbers. FILE is relative to the directory of the file that includes
 * it, and may include others up to REPLAY_INCLUDE_MAX_DEPTH levels deep (a file
 * that includes itself, directly or not, is refused by that limit).
 */
#ifndef PLATFORM_REPLAY_INCLUDE_H
#define PLATFORM_REPLAY_INCLUDE_H

#include <stdbool.h>
#include <stddef.h>

/* Files nested inside the first one; a script, its include, its include's include. */
#define REPLAY_INCLUDE_MAX_DEPTH 4u
/* The largest total expansion, in bytes. */
#define REPLAY_INCLUDE_MAX_BYTES (1024u * 1024u)
#define REPLAY_INCLUDE_MESSAGE_SIZE 512u

/* Reads `path` with its includes expanded into a NUL-terminated heap string
 * that the caller frees; its length is in *length_out and *expanded_out says
 * whether any include was replaced. Returns 0, or -1 with the reason in
 * `message` (message_size bytes: the file and line, and what went wrong:
 * unreadable file, nesting too deep, too large, include without a file
 * name). *text_out is NULL after a failure. */
int replay_include_expand(const char *path, char **text_out, size_t *length_out, bool *expanded_out, char *message,
                          size_t message_size);

#endif /* PLATFORM_REPLAY_INCLUDE_H */
