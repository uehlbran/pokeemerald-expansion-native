// deps: console.c data_dir.c
/*
 * test_console.c: where the game's messages go when there may be no console
 * (console.c): the mode for every combination of what the two streams are and
 * whether the parent has a console, the log's place in each data directory
 * mode (with a host of the test's own for SDL's two directories, as
 * test_data_dir.c has) and its truncation, and the message box, which only
 * LOG mode shows. The streams the log is opened on are the test's own files,
 * never the process's.
 */
#include "console.h"
#include "host_file.h"
#include "ptest.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

/* --- the mode ------------------------------------------------------------------- */

static struct {
    bool succeeds;
    unsigned calls;
} attach;

static bool fake_attach(void)
{
    attach.calls++;
    return attach.succeeds;
}

static const enum host_stream_kind all_kinds[] = {HOST_STREAM_NONE, HOST_STREAM_PIPE, HOST_STREAM_FILE,
                                                  HOST_STREAM_CONSOLE};
#define KIND_COUNT (sizeof all_kinds / sizeof all_kinds[0])

PTEST(streams_the_parent_gave_are_used_as_they_are_and_nothing_is_attached)
{
    for (size_t i = 1; i < KIND_COUNT; i++) {
        for (size_t j = 1; j < KIND_COUNT; j++) {
            attach.succeeds = true;
            attach.calls = 0;
            PT_ASSERT_EQ_INT(console_choose(all_kinds[i], all_kinds[j], fake_attach), CONSOLE_INHERITED);
            PT_ASSERT_EQ_U32(attach.calls, 0u);
        }
    }
}

PTEST(nothing_given_and_a_parent_console_prints_on_it)
{
    attach.succeeds = true;
    attach.calls = 0;
    PT_ASSERT_EQ_INT(console_choose(HOST_STREAM_NONE, HOST_STREAM_NONE, fake_attach), CONSOLE_PARENT);
    PT_ASSERT_EQ_U32(attach.calls, 1u);
}

PTEST(nothing_given_and_no_parent_console_is_the_log)
{
    attach.succeeds = false;
    attach.calls = 0;
    PT_ASSERT_EQ_INT(console_choose(HOST_STREAM_NONE, HOST_STREAM_NONE, fake_attach), CONSOLE_LOG);
    PT_ASSERT_EQ_U32(attach.calls, 1u);
}

/* A prompt with only one stream redirected (`game.exe > out.txt`): the other is mended on the console. */
PTEST(one_stream_missing_is_mended_on_the_parent_console)
{
    for (size_t i = 1; i < KIND_COUNT; i++) {
        attach.succeeds = true;
        attach.calls = 0;
        PT_ASSERT_EQ_INT(console_choose(all_kinds[i], HOST_STREAM_NONE, fake_attach), CONSOLE_PARENT);
        PT_ASSERT_EQ_INT(console_choose(HOST_STREAM_NONE, all_kinds[i], fake_attach), CONSOLE_PARENT);
        PT_ASSERT_EQ_U32(attach.calls, 2u);
    }
}

/* Without a console to mend it on, the stream that was given is kept and the one that was not is the log. */
PTEST(one_stream_missing_and_no_parent_console_keeps_the_given_stream_and_logs_the_other)
{
    for (size_t i = 1; i < KIND_COUNT; i++) {
        attach.succeeds = false;
        PT_ASSERT_EQ_INT(console_choose(all_kinds[i], HOST_STREAM_NONE, fake_attach), CONSOLE_INHERITED_AND_LOG);
        PT_ASSERT_EQ_INT(console_choose(HOST_STREAM_NONE, all_kinds[i], fake_attach), CONSOLE_INHERITED_AND_LOG);
    }
}

PTEST(the_log_takes_the_streams_that_are_missing_only)
{
    struct console_streams both = console_missing_streams(HOST_STREAM_NONE, HOST_STREAM_NONE);
    struct console_streams err_only = console_missing_streams(HOST_STREAM_PIPE, HOST_STREAM_NONE);
    struct console_streams out_only = console_missing_streams(HOST_STREAM_NONE, HOST_STREAM_FILE);
    struct console_streams neither = console_missing_streams(HOST_STREAM_CONSOLE, HOST_STREAM_PIPE);
    PT_ASSERT(both.out == stdout && both.err == stderr);
    PT_ASSERT(err_only.out == NULL && err_only.err == stderr);
    PT_ASSERT(out_only.out == stdout && out_only.err == NULL);
    PT_ASSERT(neither.out == NULL && neither.err == NULL);
    PT_ASSERT(console_uses_log(CONSOLE_LOG) && console_uses_log(CONSOLE_INHERITED_AND_LOG));
    PT_ASSERT(!console_uses_log(CONSOLE_INHERITED) && !console_uses_log(CONSOLE_PARENT));
}

/* --- the message box ------------------------------------------------------------- */

static struct {
    unsigned calls;
    char title[128];
    char text[8192];
} box;

static void fake_box(const char *title, const char *text)
{
    box.calls++;
    (void)snprintf(box.title, sizeof box.title, "%s", title);
    (void)snprintf(box.text, sizeof box.text, "%s", text);
}

PTEST(only_the_log_mode_shows_a_message_box)
{
    PT_ASSERT(console_shows_message_box(CONSOLE_LOG));
    PT_ASSERT(!console_shows_message_box(CONSOLE_INHERITED));
    PT_ASSERT(!console_shows_message_box(CONSOLE_PARENT));
    PT_ASSERT(!console_shows_message_box(CONSOLE_INHERITED_AND_LOG));
    box.calls = 0;
    console_report_start_failure(CONSOLE_INHERITED, "", fake_box);
    console_report_start_failure(CONSOLE_PARENT, "", fake_box);
    console_report_start_failure(CONSOLE_INHERITED_AND_LOG, "", fake_box); /* a stream is given: someone reads it */
    PT_ASSERT_EQ_U32(box.calls, 0u);
}

PTEST(a_start_failure_without_a_log_says_so_in_the_box)
{
    box.calls = 0;
    console_report_start_failure(CONSOLE_LOG, "", fake_box);
    PT_ASSERT_EQ_U32(box.calls, 1u);
    PT_ASSERT(strstr(box.text, "log file could not be written") != NULL);
}

/* --- the log --------------------------------------------------------------------- */

#define LONG_PATH_SIZE 2048u
#define DIR_SIZE (TSUP_PATH_SIZE + 2u)
#define PATH_SIZE (DIR_SIZE + 64u)

static struct {
    const char *base; /* NULL: the call fails */
    const char *pref;
    unsigned base_calls;
    unsigned pref_calls;
} fake;

static bool fake_answer(const char *dir, char *out, size_t size)
{
    if (dir == NULL)
        return false;
    const int written = snprintf(out, size, "%s", dir);
    return written >= 0 && (size_t)written < size;
}

static bool fake_base_dir(char *out, size_t size)
{
    fake.base_calls++;
    return fake_answer(fake.base, out, size);
}

static bool fake_pref_dir(char *out, size_t size)
{
    fake.pref_calls++;
    return fake_answer(fake.pref, out, size);
}

static const struct data_dir_host fake_host = {.base_dir = fake_base_dir, .pref_dir = fake_pref_dir};

/* The executable's directory, the per-user directory (both as SDL gives
 * them, ending in "/") and a place for the two streams' files. */
struct scene {
    char exe[TSUP_PATH_SIZE];
    char pref[TSUP_PATH_SIZE];
    char exe_slash[DIR_SIZE];
    char pref_slash[DIR_SIZE];
    char files[TSUP_PATH_SIZE];
    char out_path[PATH_SIZE];
    char err_path[PATH_SIZE];
    struct console_streams streams;
};

static bool make_scene(struct scene *scene)
{
    if (!tsup_make_temp_directory(scene->exe, sizeof scene->exe, "console_exe")
        || !tsup_make_temp_directory(scene->pref, sizeof scene->pref, "console_pref")
        || !tsup_make_temp_directory(scene->files, sizeof scene->files, "console_streams"))
        return false;
    (void)snprintf(scene->exe_slash, sizeof scene->exe_slash, "%s/", scene->exe);
    (void)snprintf(scene->pref_slash, sizeof scene->pref_slash, "%s/", scene->pref);
    (void)snprintf(scene->out_path, sizeof scene->out_path, "%s/out", scene->files);
    (void)snprintf(scene->err_path, sizeof scene->err_path, "%s/err", scene->files);
    scene->streams.out = fopen(scene->out_path, "w");
    scene->streams.err = fopen(scene->err_path, "w");
    fake.base = scene->exe_slash;
    fake.pref = scene->pref_slash;
    fake.base_calls = 0;
    fake.pref_calls = 0;
    return scene->streams.out != NULL && scene->streams.err != NULL;
}

/* Closes what is open and removes the directories. */
static bool end_scene(struct scene *scene)
{
    bool closed = true;
    if (scene->streams.out != NULL)
        closed = fclose(scene->streams.out) == 0 && closed;
    if (scene->streams.err != NULL)
        closed = fclose(scene->streams.err) == 0 && closed;
    return closed && tsup_remove_tree(scene->exe) && tsup_remove_tree(scene->pref) && tsup_remove_tree(scene->files);
}

/* The file's bytes without any "\r" (a text-mode stream on Windows adds them), NUL-terminated. */
static bool read_text(const char *path, char *text, size_t size)
{
    FILE *const file = fopen(path, "rb");
    if (file == NULL)
        return false;
    size_t length = 0;
    int c;
    while ((c = fgetc(file)) != EOF && length + 1u < size) {
        if (c != '\r')
            text[length++] = (char)c;
    }
    text[length] = '\0';
    return fclose(file) == 0;
}

static bool write_file(const char *dir, const char *name, const char *text)
{
    char path[LONG_PATH_SIZE];
    (void)snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *const file = fopen(path, "wb");
    return file != NULL && fputs(text, file) >= 0 && fclose(file) == 0;
}

static struct options windowed(void)
{
    return (struct options){.scale = 3};
}

PTEST(the_log_is_in_the_per_user_directory_by_default)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    char expected[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)snprintf(expected, sizeof expected, "%spokeemerald.log", scene.pref_slash);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(path, expected);
}

PTEST(the_log_is_in_the_executables_directory_in_portable_mode)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    PT_ASSERT(write_file(scene.exe, "settings.ini", "# portable\n"));
    struct options o = windowed();
    char path[PATH_SIZE];
    char expected[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)snprintf(expected, sizeof expected, "%spokeemerald.log", scene.exe_slash);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(path, expected);
}

/* --data-dir wins over portable mode, and the per-user directory is not even asked for (SDL would make it). */
PTEST(the_log_is_in_the_data_dir_given_and_the_per_user_directory_is_not_made)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    PT_ASSERT(write_file(scene.exe, "settings.ini", "# portable\n"));
    char given[TSUP_PATH_SIZE + 16u];
    (void)snprintf(given, sizeof given, "%s/given", scene.files);
    struct options o = windowed();
    o.data_dir = given;
    char path[PATH_SIZE];
    char expected[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)snprintf(expected, sizeof expected, "%s/pokeemerald.log", given);
    const unsigned pref_calls = fake.pref_calls;
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(path, expected);
    PT_ASSERT_EQ_U32(pref_calls, 0u);
}

/* A run that needs no file (a test runner, a headless run given both paths) has a log in the same place. */
PTEST(a_run_that_needs_no_file_has_its_log_in_the_data_directory_too)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    o.headless = true;
    o.save_path = "a.sav";
    o.rtc_offset_path = "a.rtc";
    char path[PATH_SIZE];
    char expected[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)snprintf(expected, sizeof expected, "%spokeemerald.log", scene.pref_slash);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(path, expected);
}

PTEST(without_a_data_directory_the_log_is_beside_the_executable_and_says_so)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    fake.pref = NULL;
    struct options o = windowed();
    char path[PATH_SIZE];
    char expected[PATH_SIZE];
    char text[512];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)snprintf(expected, sizeof expected, "%spokeemerald.log", scene.exe_slash);
    const bool read = read_text(expected, text, sizeof text);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(path, expected);
    PT_ASSERT(read);
    PT_ASSERT(strstr(text, "this log is beside the executable") != NULL);
}

PTEST(without_any_directory_there_is_no_log)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    fake.pref = NULL;
    fake.base = NULL;
    struct options o = windowed();
    char path[PATH_SIZE] = "stale";
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, -1);
    PT_ASSERT_EQ_STR(path, "");
}

PTEST(a_log_path_that_does_not_fit_is_refused)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[8];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, -1);
    PT_ASSERT_EQ_STR(path, "");
}

PTEST(a_directory_that_cannot_hold_the_log_is_refused)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    char missing[PATH_SIZE];
    (void)snprintf(missing, sizeof missing, "%s/no/such/", scene.files);
    fake.pref = missing;
    struct options o = windowed();
    char path[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    /* What is written meanwhile goes to the null device, not to the streams' old files. */
    const bool wrote = fputs("nowhere\n", scene.streams.err) >= 0 && fflush(scene.streams.err) == 0;
    const intmax_t err_size = tsup_file_size(scene.err_path);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, -1);
    PT_ASSERT_EQ_STR(path, "");
    PT_ASSERT(wrote);
    PT_ASSERT_EQ_INT((int)err_size, 0);
}

/* Both streams write the one file, in the order the lines were printed. */
PTEST(both_streams_write_the_log_in_order)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    char text[512];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT_EQ_INT(status, 0);
    (void)fprintf(scene.streams.err, "first on err\n");
    (void)fprintf(scene.streams.out, "second on out\n");
    (void)fprintf(scene.streams.err, "third on err\n");
    const bool read = read_text(path, text, sizeof text);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(read);
    PT_ASSERT_EQ_STR(text, "first on err\nsecond on out\nthird on err\n");
}

/* The two streams and a fault handler's raw write to stderr's descriptor are one open file: none
 * overwrites another. With two separate opens, the raw write lands at stderr's own offset. */
PTEST(the_streams_and_a_handlers_raw_write_continue_each_other)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    char text[512];
    static const char raw[] = "raw on stderr's descriptor\n";
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT_EQ_INT(status, 0);
    (void)fprintf(scene.streams.err, "first on err\n");
    (void)fprintf(scene.streams.out, "second on out\n");
    const host_handler_file handle = host_file_for_handler(fileno(scene.streams.err));
    const bool wrote_raw = host_file_write_from_handler(handle, raw, sizeof raw - 1u);
    (void)fprintf(scene.streams.out, "fourth on out\n");
    (void)fprintf(scene.streams.err, "fifth on err\n");
    const bool read = read_text(path, text, sizeof text);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(wrote_raw && read);
    PT_ASSERT_EQ_STR(text, "first on err\nsecond on out\nraw on stderr's descriptor\nfourth on out\nfifth on err\n");
}

#ifndef _WIN32
/* The log is made with no permission bits (umask), which the stream that made it may still write, so the
 * second open, to append, is refused: freopen has by then closed the stream, which must be left on the
 * null device and not closed. */
PTEST(a_log_that_cannot_be_reopened_leaves_the_stream_usable)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    const mode_t old_mask = umask(0777);
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    (void)umask(old_mask);
    const int written = fprintf(scene.streams.out, "after the failure\n");
    const int flushed = fflush(scene.streams.out);
    const int errored = ferror(scene.streams.out);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, -1);
    PT_ASSERT(written > 0);
    PT_ASSERT_EQ_INT(flushed, 0);
    PT_ASSERT_EQ_INT(errored, 0);
}
#endif

/* A stream given is left alone: only the missing one is the log. */
PTEST(a_stream_that_is_not_taken_keeps_its_file)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    char text[512];
    char out_text[64];
    struct console_streams only_err = {.out = NULL, .err = scene.streams.err};
    const int status = console_open_log(&o, &fake_host, &only_err, path, sizeof path);
    PT_ASSERT_EQ_INT(status, 0);
    (void)fprintf(scene.streams.err, "to the log\n");
    (void)fprintf(scene.streams.out, "to the given stream\n");
    (void)fflush(scene.streams.out);
    const bool log_read = read_text(path, text, sizeof text);
    const bool out_read = read_text(scene.out_path, out_text, sizeof out_text);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(log_read && out_read);
    PT_ASSERT_EQ_STR(text, "to the log\n");
    PT_ASSERT_EQ_STR(out_text, "to the given stream\n");
}

/* A start's log is that start's alone, however long the last one's was. */
PTEST(the_log_is_truncated_at_each_start)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    PT_ASSERT(write_file(scene.pref, "pokeemerald.log", "a line from an earlier start, longer than the next\n"));
    struct options o = windowed();
    char path[PATH_SIZE];
    char text[512];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT_EQ_INT(status, 0);
    (void)fprintf(scene.streams.out, "now\n");
    const bool read = read_text(path, text, sizeof text);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(read);
    PT_ASSERT_EQ_STR(text, "now\n");
}

/* The box: the start of the log, where the error is, and where the log is. */
PTEST(a_start_failure_in_log_mode_shows_the_error_and_the_log_path)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    struct options o = windowed();
    char path[PATH_SIZE];
    const int status = console_open_log(&o, &fake_host, &scene.streams, path, sizeof path);
    PT_ASSERT_EQ_INT(status, 0);
    (void)fprintf(scene.streams.err, "save_lock: pokeemerald.sav is in use by another instance\n");
    box.calls = 0;
    console_report_start_failure(CONSOLE_LOG, path, fake_box);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_U32(box.calls, 1u);
    PT_ASSERT(strstr(box.title, "could not start") != NULL);
    PT_ASSERT(strstr(box.text, "save_lock: pokeemerald.sav is in use by another instance") != NULL);
    PT_ASSERT(strstr(box.text, path) != NULL);
}

/* A log whose first bytes are head followed by filler, in the per-user directory of a scene
 * with a path of the given depth, shown in the box. */
static bool box_for_log_text(const char *log_text, const char *extra_directory, const struct scene *scene)
{
    char dir[LONG_PATH_SIZE];
    char path[LONG_PATH_SIZE];
    if (snprintf(dir, sizeof dir, "%s%s", scene->pref, extra_directory) >= (int)sizeof dir)
        return false;
    if (!write_file(dir, "pokeemerald.log", log_text))
        return false;
    if (snprintf(path, sizeof path, "%s/pokeemerald.log", dir) >= (int)sizeof path)
        return false;
    box.calls = 0;
    console_report_start_failure(CONSOLE_LOG, path, fake_box);
    return box.calls == 1u;
}

/* The box shows the first 1000 bytes of the log, then the path. */
PTEST(the_box_shows_no_more_than_a_thousand_bytes_of_the_log)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    static char big[3001];
    memset(big, 'x', sizeof big - 1u);
    const bool shown = box_for_log_text(big, "", &scene);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(shown);
    PT_ASSERT_EQ_U32((unsigned)strspn(box.text, "x"), 1000u);
    PT_ASSERT(strncmp(box.text + 1000, "\n\nThe log file is ", 18) == 0);
}

/* A log shorter than the cap is shown whole, with no stale bytes after it. */
PTEST(the_box_shows_a_short_log_whole)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    memset(box.text, 1, sizeof box.text - 1u); /* what a box left before it */
    const bool shown = box_for_log_text("short\n", "", &scene);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(shown);
    PT_ASSERT(strncmp(box.text, "short\n\n\nThe log file is ", 24) == 0);
    PT_ASSERT(strchr(box.text, 1) == NULL);
}

/* The cut at 1000 bytes does not leave half of a two-byte character: 999 letters, then "e acute". */
PTEST(the_box_does_not_cut_a_utf8_character_in_two)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    static char text[2001];
    memset(text, 'x', 999u);
    memcpy(text + 999, "\xc3\xa9", 2u);
    memset(text + 1001, 'y', 1000u);
    text[2001 - 1] = '\0';
    const bool shown = box_for_log_text(text, "", &scene);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(shown);
    PT_ASSERT_EQ_U32((unsigned)strspn(box.text, "x"), 999u);
    PT_ASSERT(strncmp(box.text + 999, "\n\nThe log file is ", 18) == 0);
}

/* The path is not limited by the room the log's start leaves in the box. */
PTEST(the_box_names_a_log_path_of_over_four_hundred_bytes)
{
    struct scene scene;
    PT_ASSERT(make_scene(&scene));
    /* Directories 60 bytes a level down to 430 bytes of path; Windows stops at MAX_PATH (260), where the
     * path is as long as it can be there. */
    char extra[512] = "";
    char dir[LONG_PATH_SIZE];
    while (strlen(scene.pref) + strlen(extra) < 430u) {
        const size_t used = strlen(extra);
        (void)snprintf(extra + used, sizeof extra - used, "/%.60s",
                       "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
        (void)snprintf(dir, sizeof dir, "%s%s", scene.pref, extra);
        if (!tsup_make_directory(dir)) {
            extra[used] = '\0';
            break;
        }
    }
    static char log_text[1201];
    memset(log_text, 'e', sizeof log_text - 1u); /* a full head, so that head and path together outgrow 1400 bytes */
    const bool shown = box_for_log_text(log_text, extra, &scene);
    (void)snprintf(dir, sizeof dir, "%s%s/pokeemerald.log", scene.pref, extra);
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT(shown);
    PT_ASSERT_EQ_U32((unsigned)strspn(box.text, "e"), 1000u);
    PT_ASSERT(strstr(box.text, dir) != NULL);
    PT_ASSERT(strlen(box.text) > 1000u + strlen(dir));
}

PTEST_MAIN()
