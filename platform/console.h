/*
 * console.h: where the game's messages go when there may be no console.
 *
 * Not hardware: the GBA printed nothing. The game prints (the refusals, the
 * keys, the version, the test runner's lines) and on Windows its executable is
 * a GUI program, so that Explorer opens no console window beside the game
 * (docs/DECISIONS.md, "No console window on Windows"). What that program's
 * streams are depends on who started it, and the answer, a pure function of
 * what host_console.h reports, is one of four modes:
 *   INHERITED  the parent gave both streams (a pipe: bash, the hydra, the
 *              replays' harness; a file: `> out.txt`): used as they are;
 *   PARENT     a stream is missing and the parent has a console (cmd.exe,
 *              PowerShell): the missing streams print on it;
 *   INHERITED_AND_LOG  one stream is given, the other missing, and there is
 *              no console: the given one is kept and only the missing one is
 *              written to the log, so that a refusal on a stream a launcher
 *              left out is not lost;
 *   LOG        nothing, and no console to attach to (Explorer, a shortcut,
 *              Steam): both streams are written to pokeemerald.log in the data
 *              directory, truncated at each start, and a start-up failure is
 *              also shown in a message box, which a player who double-clicked
 *              would otherwise never see.
 * Every OS but Windows is always INHERITED: host_console_stream_kind reports
 * the C library's streams as given.
 *
 * The log goes where the save goes (data_dir.h's rule, for any run, whether
 * or not it needs a file): --data-dir, the executable's directory in portable
 * mode, else the per-user directory. If no data directory can be found, the
 * log goes beside the executable and says so. Nothing is written to a stream
 * that has no handle: main points the streams the log will take at the null
 * device before it looks for the log's place, and finds that place from the
 * command line without a report (options_data_dir_given), so the arguments
 * are parsed, and report what is wrong with them, only once the log is open.
 * What data_dir_find reports while looking is lost to the null device; the
 * same problem is reported into the log when the player's files are found
 * (player_find). The log is not durable state (no host_file.h): a second
 * instance, refused on the save, empties the running one's log before it
 * stops, which is accepted.
 */
#ifndef PLATFORM_CONSOLE_H
#define PLATFORM_CONSOLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "data_dir.h"
#include "host_console.h"
#include "options.h"

#define CONSOLE_LOG_NAME "pokeemerald.log"

enum console_mode {
    CONSOLE_INHERITED,         /* both streams given */
    CONSOLE_PARENT,            /* the missing streams are on the parent's console */
    CONSOLE_INHERITED_AND_LOG, /* one stream given, one missing, no console: the missing one is the log */
    CONSOLE_LOG                /* both missing, no console: both are the log, and a start failure has a box */
};

/* The mode for streams of these kinds. attach_parent is host_console.h's
 * (a test's own), called only when a stream is missing, and at most once. */
enum console_mode console_choose(enum host_stream_kind out, enum host_stream_kind err, bool (*attach_parent)(void));

/* Whether a start-up failure is shown in a message box: only in LOG mode,
 * where no one is looking at a stream. */
bool console_shows_message_box(enum console_mode mode);

/* The output streams the log takes: stdout and stderr, or a test's own; a
 * NULL one is left alone. */
struct console_streams {
    FILE *out;
    FILE *err;
};

/* The streams of this process that are none, for the log to take. */
struct console_streams console_missing_streams(enum host_stream_kind out, enum host_stream_kind err);

/* Whether a mode writes a log. */
bool console_uses_log(enum console_mode mode);

/* A log mode, once the data directory is known from the command line (a
 * struct options with at least data_dir set): points the streams at the null
 * device, finds the log's place (above), and makes them write to it,
 * truncating it first. Both streams are one open file with one offset, so that
 * the C runtime's writes and a fault handler's raw write to stderr's handle
 * (crash_reboot.c, arm_division.c) cannot overwrite each other. The path is
 * written to path (size bytes). Returns 0, or -1 if there is no place for the
 * log or it cannot be made (path is then "" and the streams write to the null
 * device or are closed: nothing is written anywhere). */
int console_open_log(const struct options *options, const struct data_dir_host *host,
                     const struct console_streams *streams, char *path, size_t size);

/* What a message box is given. */
typedef void (*console_message_box)(const char *title, const char *text);

/* After a start-up failure in this mode: in LOG mode, calls show with the
 * title and a text that holds the start of the log (its first lines are the
 * error) and its path, or says that no log could be written when log_path is
 * "". In any other mode does nothing. */
void console_report_start_failure(enum console_mode mode, const char *log_path, console_message_box show);

#endif /* PLATFORM_CONSOLE_H */
