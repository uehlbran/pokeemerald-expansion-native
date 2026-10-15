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
#include "ptest.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

/* Without a console to mend it on, the stream that was given still has the messages' destination. */
PTEST(one_stream_missing_and_no_parent_console_keeps_the_given_stream)
{
    for (size_t i = 1; i < KIND_COUNT; i++) {
        attach.succeeds = false;
        PT_ASSERT_EQ_INT(console_choose(all_kinds[i], HOST_STREAM_NONE, fake_attach), CONSOLE_INHERITED);
        PT_ASSERT_EQ_INT(console_choose(HOST_STREAM_NONE, all_kinds[i], fake_attach), CONSOLE_INHERITED);
    }
}

/* --- the message box ------------------------------------------------------------- */

static struct {
    unsigned calls;
    char title[128];
    char text[2048];
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
    box.calls = 0;
    console_report_start_failure(CONSOLE_INHERITED, "", fake_box);
    console_report_start_failure(CONSOLE_PARENT, "", fake_box);
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
    char path[PATH_SIZE];
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
    PT_ASSERT(end_scene(&scene));
    PT_ASSERT_EQ_INT(status, -1);
    PT_ASSERT_EQ_STR(path, "");
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

PTEST_MAIN()
