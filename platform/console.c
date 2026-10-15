/*
 * console.c: the mode, the log and the start-up message box (console.h).
 */
#include "console.h"

#include <string.h>

#define MODULE "console"
#define BOX_TITLE "pokeemerald-expansion could not start"

/* How much of the log the box shows: the errors that stop a start are a line
 * or two, and the usage text that follows a bad argument is in the log. */
/* A UTF-8 continuation byte is 10xxxxxx (RFC 3629, section 3). */
#define UTF8_CONTINUATION_MASK 0xC0u
#define UTF8_CONTINUATION 0x80u

#define BOX_LOG_BYTES 1000u
#define BOX_FRAME_BYTES 64u /* the words around the head and the path */
#define BOX_TEXT_SIZE (BOX_LOG_BYTES + DATA_DIR_PATH_SIZE + BOX_FRAME_BYTES)

enum console_mode console_choose(enum host_stream_kind out, enum host_stream_kind err, bool (*attach_parent)(void))
{
    const bool has_out = out != HOST_STREAM_NONE;
    const bool has_err = err != HOST_STREAM_NONE;
    if (has_out && has_err)
        return CONSOLE_INHERITED;
    if (attach_parent())
        return CONSOLE_PARENT;
    return has_out || has_err ? CONSOLE_INHERITED_AND_LOG : CONSOLE_LOG;
}

bool console_shows_message_box(enum console_mode mode)
{
    return mode == CONSOLE_LOG;
}

bool console_uses_log(enum console_mode mode)
{
    return mode == CONSOLE_LOG || mode == CONSOLE_INHERITED_AND_LOG;
}

struct console_streams console_missing_streams(enum host_stream_kind out, enum host_stream_kind err)
{
    return (struct console_streams){.out = out == HOST_STREAM_NONE ? stdout : NULL,
                                    .err = err == HOST_STREAM_NONE ? stderr : NULL};
}

/* The file is made and emptied first, by a stream of its own, so that a log
 * that cannot be made leaves the streams on the null device. One stream is
 * then opened on it to append, and the other made a duplicate of that one: a
 * shared open file has one offset, so the three writers (the two streams and a
 * handler's raw write to stderr's handle, which is a duplicate of the same
 * file object) continue each other. The streams are unbuffered, so that their
 * lines stay in order and a crash loses none. */
static int begin_log(const char *path, const struct console_streams *streams)
{
    FILE *const empty = fopen(path, "w");
    if (empty == NULL)
        return -1;
    if (fclose(empty) != 0)
        return -1;
    FILE *const first = streams->out != NULL ? streams->out : streams->err;
    // cppcheck-suppress leakReturnValNotUsed ; freopen gives back the stream it was given, which the caller keeps
    const bool reopened = first != NULL && freopen(path, "a", first) != NULL;
    if (!reopened) {
        /* C11 7.21.5.4: freopen closes the stream whether or not the open succeeds. */
        if (first != NULL)
            (void)host_console_discard_stream(first);
        return -1;
    }
    if (streams->out != NULL && streams->err != NULL && host_console_share_stream(streams->out, streams->err) != 0)
        return -1;
    const bool out_unbuffered = streams->out == NULL || setvbuf(streams->out, NULL, _IONBF, 0) == 0;
    const bool err_unbuffered = streams->err == NULL || setvbuf(streams->err, NULL, _IONBF, 0) == 0;
    return out_unbuffered && err_unbuffered ? 0 : -1;
}

static void discard(const struct console_streams *streams)
{
    if (streams->out != NULL)
        (void)host_console_discard_stream(streams->out);
    if (streams->err != NULL)
        (void)host_console_discard_stream(streams->err);
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
    discard(streams);
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
    if (beside_executable) {
        FILE *const note = streams->err != NULL ? streams->err : streams->out;
        (void)fprintf(note, MODULE ": no data directory could be found; this log is beside the executable\n");
    }
    return 0;
}

/* Reads the start of the log into text (size bytes, no terminator) and
 * returns its length: at most BOX_LOG_BYTES, cut so as not to end in the
 * middle of a UTF-8 character. 0 if the log cannot be read. */
static size_t read_log_head(const char *path, char *text, size_t size)
{
    FILE *const log = fopen(path, "rb");
    if (log == NULL)
        return 0;
    /* One byte more than shown, to see whether the cut falls inside a character. */
    const size_t read = fread(text, 1, size, log);
    (void)fclose(log);
    size_t length = read < BOX_LOG_BYTES ? read : BOX_LOG_BYTES;
    if (read > BOX_LOG_BYTES) {
        while (length > 0 && ((unsigned char)text[length] & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION)
            length--;
    }
    return length;
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
    const size_t length = read_log_head(log_path, head, sizeof head);
    const int written = snprintf(text, sizeof text, "%.*s\n\nThe log file is %s", (int)length, head, log_path);
    if (written < 0 || (size_t)written >= sizeof text)
        text[0] = '\0';
    show(BOX_TITLE, text);
}
