/*
 * console.c: the mode, the log and the start-up message box (console.h).
 */
#include "console.h"

#include <string.h>

#define MODULE "console"
#define BOX_TITLE "pokeemerald-expansion could not start"

/* How much of the log the box shows: the errors that stop a start are a line
 * or two, and the usage text that follows a bad argument is in the log. */
#define BOX_LOG_BYTES 1000u
#define BOX_TEXT_SIZE (BOX_LOG_BYTES + 400u)

enum console_mode console_choose(enum host_stream_kind out, enum host_stream_kind err, bool (*attach_parent)(void))
{
    const bool has_out = out != HOST_STREAM_NONE;
    const bool has_err = err != HOST_STREAM_NONE;
    if (has_out && has_err)
        return CONSOLE_INHERITED;
    if (attach_parent())
        return CONSOLE_PARENT;
    return has_out || has_err ? CONSOLE_INHERITED : CONSOLE_LOG;
}

bool console_shows_message_box(enum console_mode mode)
{
    return mode == CONSOLE_LOG;
}

struct console_streams console_standard_streams(void)
{
    return (struct console_streams){.in = stdin, .out = stdout, .err = stderr};
}

/* The file is made and emptied first, by a stream of its own, so that a log
 * that cannot be made leaves the process's streams as they were; the output
 * streams then append, so that they share the file without one overwriting the
 * other at its own offset, and are unbuffered, so that their lines stay in
 * order and a crash loses none. The input stream comes first, to claim
 * descriptor 0 (console.h). */
static int begin_log(const char *path, const struct console_streams *streams)
{
    FILE *const empty = fopen(path, "w");
    if (empty == NULL)
        return -1;
    if (fclose(empty) != 0)
        return -1;
    // cppcheck-suppress leakReturnValNotUsed ; freopen gives back the stream it was given, which the caller keeps
    const bool in_reopened = freopen(path, "r", streams->in) != NULL;
    // cppcheck-suppress leakReturnValNotUsed ; as above
    const bool out_reopened = in_reopened && freopen(path, "a", streams->out) != NULL;
    // cppcheck-suppress leakReturnValNotUsed ; as above
    const bool err_reopened = out_reopened && freopen(path, "a", streams->err) != NULL;
    if (!err_reopened)
        return -1;
    return setvbuf(streams->err, NULL, _IONBF, 0) == 0 && setvbuf(streams->out, NULL, _IONBF, 0) == 0 ? 0 : -1;
}

int console_open_log(const struct options *options, const struct data_dir_host *host,
                     const struct console_streams *streams, char *path, size_t size)
{
    struct data_paths where;
    char base[DATA_DIR_PATH_SIZE];
    const char *dir = NULL;
    bool beside_executable = false;
    memset(&where, 0, sizeof where);
    path[0] = '\0';
    if (data_dir_find(options, host, &where) == DATA_DIR_OK) {
        dir = where.dir;
    } else if (host->base_dir(base, sizeof base)) {
        dir = base;
        beside_executable = true;
    }
    if (dir == NULL || !data_dir_join(dir, CONSOLE_LOG_NAME, path, size) || begin_log(path, streams) != 0) {
        path[0] = '\0';
        return -1;
    }
    if (beside_executable)
        (void)fprintf(streams->err, MODULE ": no data directory could be found; this log is beside the executable\n");
    return 0;
}

/* The start of the log, NUL-terminated, or "" if it cannot be read. */
static void read_log_head(const char *path, char *text, size_t size)
{
    text[0] = '\0';
    FILE *const log = fopen(path, "rb");
    if (log == NULL)
        return;
    const size_t length = fread(text, 1, size - 1u, log);
    text[length] = '\0';
    (void)fclose(log);
}

void console_report_start_failure(enum console_mode mode, const char *log_path, console_message_box show)
{
    if (!console_shows_message_box(mode))
        return;
    char head[BOX_LOG_BYTES + 1u];
    char text[BOX_TEXT_SIZE];
    if (log_path[0] == '\0') {
        show(BOX_TITLE, "The game could not start, and its log file could not be written, so there is no more to "
                        "say. Starting it from a command prompt shows the reason.");
        return;
    }
    (void)fflush(NULL); /* the streams are the log: what they hold is not in the file yet */
    read_log_head(log_path, head, sizeof head);
    const int written = snprintf(text, sizeof text, "%s\n\nThe log file is %s", head, log_path);
    if (written < 0 || (size_t)written >= sizeof text)
        text[0] = '\0';
    show(BOX_TITLE, text);
}
