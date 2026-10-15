/*
 * console.h: where the game's messages go when there may be no console.
 *
 * Not hardware: the GBA printed nothing. The game prints (the refusals, the
 * keys, the version, the test runner's lines) and on Windows its executable is
 * a GUI program, so that Explorer opens no console window beside the game
 * (docs/DECISIONS.md, "No console window on Windows"). What that program's
 * streams are depends on who started it, and the answer, a pure function of
 * what host_console.h reports, is one of three modes:
 *   INHERITED  the parent gave the streams (a pipe: bash, the hydra, the
 *              replays' harness; a file: `> out.txt`): used as they are;
 *   PARENT     a stream is missing and the parent has a console (cmd.exe,
 *              PowerShell): the missing streams print on it;
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
 * log goes beside the executable and says so. The messages printed before the
 * log is open (a bad argument, a data directory that cannot be made) are not
 * lost: main parses and resolves again once the log is open, and the same
 * messages come out into it.
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

enum console_mode { CONSOLE_INHERITED, CONSOLE_PARENT, CONSOLE_LOG };

/* The mode for streams of these kinds. attach_parent is host_console.h's
 * (a test's own), called only when a stream is missing, and at most once. */
enum console_mode console_choose(enum host_stream_kind out, enum host_stream_kind err, bool (*attach_parent)(void));

/* Whether a start-up failure is shown in a message box: only in LOG mode,
 * where no one is looking at a stream. */
bool console_shows_message_box(enum console_mode mode);

/* The three standard streams, or a test's own. */
struct console_streams {
    FILE *in;
    FILE *out;
    FILE *err;
};

/* stdin, stdout and stderr. */
struct console_streams console_standard_streams(void);

/* LOG mode, once the options are known: finds the log's place (above), and
 * makes the output and error streams write to it, truncating it first. The
 * input stream is reopened on it too, to read: a process started with no
 * handles has no descriptor 0, 1 or 2, the C runtime gives a reopened stream
 * the lowest descriptor that is free, and the crash report (arm_division.c)
 * writes to descriptor 2, so the three are claimed in order. The path is
 * written to path (size bytes). Returns 0, or -1 if there is no place for the
 * log or it cannot be made (path is then "" and the streams are as they were). */
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
