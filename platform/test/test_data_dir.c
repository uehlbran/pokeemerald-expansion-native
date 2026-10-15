/*
 * test_data_dir.c: where the player's files go (data_dir.c), with a host of
 * the test's own standing in for SDL's two directories: the executable's
 * and the per-user one, each a scratch directory, and counts of how often
 * each was asked for (the per-user one must not be asked for when it is not
 * used, since SDL makes it). The file system is the real one: the settings
 * file beside the "executable" and --data-dir's directory are made and
 * checked for in scratch directories.
 */
#include "data_dir.h"
#include "ptest.h"
#include "test_support.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* A directory as SDL gives it: ending in a separator. */
#define DIR_SIZE (TSUP_PATH_SIZE + 2u)

static struct {
    const char *base; /* NULL: the call fails */
    const char *pref;
    unsigned base_calls;
    unsigned pref_calls;
} fake;

static bool fake_answer(const char *dir, char *out, size_t size)
{
    if (dir == NULL) {
        (void)fprintf(stderr, "fake host: no directory\n");
        return false;
    }
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

/* Two scratch directories, the executable's and the per-user one, as the
 * fake host gives them (ending in "/"). */
struct dirs {
    char exe[TSUP_PATH_SIZE];
    char pref[TSUP_PATH_SIZE];
    char exe_slash[DIR_SIZE];
    char pref_slash[DIR_SIZE];
};

static bool make_dirs(struct dirs *dirs)
{
    if (!tsup_make_temp_directory(dirs->exe, sizeof dirs->exe, "data_dir_exe")
        || !tsup_make_temp_directory(dirs->pref, sizeof dirs->pref, "data_dir_pref"))
        return false;
    (void)snprintf(dirs->exe_slash, sizeof dirs->exe_slash, "%s/", dirs->exe);
    (void)snprintf(dirs->pref_slash, sizeof dirs->pref_slash, "%s/", dirs->pref);
    fake.base = dirs->exe_slash;
    fake.pref = dirs->pref_slash;
    fake.base_calls = 0;
    fake.pref_calls = 0;
    return true;
}

static bool remove_dirs(const struct dirs *dirs)
{
    return tsup_remove_tree(dirs->exe) && tsup_remove_tree(dirs->pref);
}

static bool make_file(const char *dir, const char *name)
{
    char path[DIR_SIZE + 32u];
    const int written = snprintf(path, sizeof path, "%s/%s", dir, name);
    if (written < 0 || (size_t)written >= sizeof path)
        return false;
    FILE *const file = fopen(path, "wb");
    return file != NULL && fclose(file) == 0;
}

/* The options of a windowed run with nothing given. */
static struct options windowed(void)
{
    return (struct options){.scale = 3};
}

static bool joined_is(const char *dir, const char *name, const char *path)
{
    char expected[DIR_SIZE + 32u];
    const int written = snprintf(expected, sizeof expected, "%s%s", dir, name);
    return written >= 0 && (size_t)written < sizeof expected && strcmp(expected, path) == 0;
}

PTEST(test_runner_mode_has_no_files_and_asks_nothing)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.test_runner = true;
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_UNUSED);
    PT_ASSERT_EQ_STR(paths.dir, "");
    PT_ASSERT_EQ_STR(paths.save, "");
    PT_ASSERT_EQ_STR(paths.rtc_offset, "");
    PT_ASSERT_EQ_STR(paths.settings, "");
    PT_ASSERT_EQ_U32(fake.base_calls + fake.pref_calls, 0u);
}

/* What every gate does: no directory is looked for, so nothing is made in the per-user one. */
PTEST(headless_with_both_paths_given_looks_for_no_directory)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.headless = true;
    o.save_path = "out/a.sav";
    o.rtc_offset_path = "out/a.rtc";
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_UNUSED);
    PT_ASSERT_EQ_STR(paths.save, "out/a.sav");
    PT_ASSERT_EQ_STR(paths.rtc_offset, "out/a.rtc");
    PT_ASSERT_EQ_STR(paths.settings, "");
    PT_ASSERT_EQ_U32(fake.base_calls + fake.pref_calls, 0u);
}

PTEST(without_a_settings_file_beside_the_executable_the_files_are_per_user)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_PER_USER);
    PT_ASSERT_EQ_STR(paths.dir, dirs.pref_slash);
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.sav", paths.save));
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.rtc", paths.rtc_offset));
    PT_ASSERT(joined_is(dirs.pref_slash, "settings.ini", paths.settings));
    PT_ASSERT_EQ_U32(fake.base_calls, 1u);
    PT_ASSERT_EQ_U32(fake.pref_calls, 1u);
}

PTEST(a_settings_file_beside_the_executable_makes_the_run_portable)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    PT_ASSERT(make_file(dirs.exe, "settings.ini"));
    struct options o = windowed();
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_PORTABLE);
    PT_ASSERT_EQ_STR(paths.dir, dirs.exe_slash);
    PT_ASSERT(joined_is(dirs.exe_slash, "pokeemerald.sav", paths.save));
    PT_ASSERT(joined_is(dirs.exe_slash, "pokeemerald.rtc", paths.rtc_offset));
    PT_ASSERT(joined_is(dirs.exe_slash, "settings.ini", paths.settings));
    PT_ASSERT_EQ_U32(fake.pref_calls, 0u); /* SDL would have made the per-user directory */
}

/* The other file names in the executable's directory do not make it portable. */
PTEST(a_save_beside_the_executable_does_not_make_the_run_portable)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    PT_ASSERT(make_file(dirs.exe, "pokeemerald.sav"));
    struct options o = windowed();
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_PER_USER);
}

struct in_directory_case {
    struct options options;
    struct data_paths paths;
    enum data_dir_error status;
};

static void resolve_here(void *context)
{
    struct in_directory_case *const c = context;
    c->status = data_dir_resolve(&c->options, &fake_host, &c->paths);
}

/* The working directory is where a shortcut or a terminal happens to start the game, which is what
 * the per-user directory is for: a settings file there means nothing, one beside the executable
 * everything. */
PTEST(portable_mode_looks_beside_the_executable_not_in_the_working_directory)
{
    struct dirs dirs;
    char work[TSUP_PATH_SIZE];
    PT_ASSERT(make_dirs(&dirs));
    PT_ASSERT(tsup_make_temp_directory(work, sizeof work, "data_dir_work"));
    PT_ASSERT(make_file(work, "settings.ini"));
    struct in_directory_case from_work = {.options = windowed()};
    const bool ran = tsup_in_directory(work, resolve_here, &from_work);
    PT_ASSERT(make_file(dirs.exe, "settings.ini"));
    char empty_work[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_directory(empty_work, sizeof empty_work, "data_dir_work"));
    struct in_directory_case beside_exe = {.options = windowed()};
    const bool ran_again = tsup_in_directory(empty_work, resolve_here, &beside_exe);
    PT_ASSERT(remove_dirs(&dirs) && tsup_remove_tree(work) && tsup_remove_tree(empty_work));
    PT_ASSERT(ran && ran_again);
    PT_ASSERT_EQ_INT(from_work.status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(from_work.paths.mode, DATA_DIR_PER_USER);
    PT_ASSERT_EQ_INT(beside_exe.status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(beside_exe.paths.mode, DATA_DIR_PORTABLE);
}

PTEST(data_dir_wins_over_both_is_made_and_asks_nothing)
{
    struct dirs dirs;
    char given[DIR_SIZE + 16u];
    PT_ASSERT(make_dirs(&dirs));
    PT_ASSERT(make_file(dirs.exe, "settings.ini"));
    (void)snprintf(given, sizeof given, "%s/new", dirs.pref);
    struct options o = windowed();
    o.data_dir = given;
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    const bool made = tsup_exists(given);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_GIVEN);
    PT_ASSERT_EQ_STR(paths.dir, given);
    PT_ASSERT(joined_is(given, "/pokeemerald.sav", paths.save));
    PT_ASSERT(joined_is(given, "/pokeemerald.rtc", paths.rtc_offset));
    PT_ASSERT(joined_is(given, "/settings.ini", paths.settings));
    PT_ASSERT_EQ_U32(fake.base_calls + fake.pref_calls, 0u);
}

PTEST(an_existing_data_dir_is_used_and_a_trailing_separator_is_not_doubled)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.data_dir = dirs.pref_slash;
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.sav", paths.save));
}

struct error_case {
    struct options options;
    enum data_dir_error status;
};

static void resolve_expecting_a_report(void *context)
{
    struct error_case *const c = context;
    struct data_paths paths;
    c->status = data_dir_resolve(&c->options, &fake_host, &paths);
}

PTEST(a_data_dir_whose_parent_is_missing_is_reported)
{
    struct dirs dirs;
    char given[DIR_SIZE + 32u];
    char said[1024];
    PT_ASSERT(make_dirs(&dirs));
    (void)snprintf(given, sizeof given, "%s/missing/new", dirs.pref);
    struct error_case c = {.options = windowed()};
    c.options.data_dir = given;
    PT_ASSERT(tsup_capture_stderr(resolve_expecting_a_report, &c, said, sizeof said));
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(c.status, DATA_DIR_ERR);
    PT_ASSERT(strstr(said, "data_dir: cannot make the data directory ") != NULL);
    PT_ASSERT(strstr(said, given) != NULL && strstr(said, "(--data-dir)") != NULL);
}

PTEST(a_save_or_clock_path_given_wins_for_its_file_only)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.save_path = "mine.sav";
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    o.save_path = NULL;
    o.rtc_offset_path = "mine.rtc";
    struct data_paths clock_given;
    const enum data_dir_error clock_status = data_dir_resolve(&o, &fake_host, &clock_given);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_STR(paths.save, "mine.sav");
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.rtc", paths.rtc_offset));
    PT_ASSERT(joined_is(dirs.pref_slash, "settings.ini", paths.settings));
    PT_ASSERT_EQ_INT(clock_status, DATA_DIR_OK);
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.sav", clock_given.save));
    PT_ASSERT_EQ_STR(clock_given.rtc_offset, "mine.rtc");
}

PTEST(headless_finds_the_directory_for_its_files_but_reads_no_settings)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.headless = true;
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_PER_USER);
    PT_ASSERT(joined_is(dirs.pref_slash, "pokeemerald.sav", paths.save));
    PT_ASSERT_EQ_STR(paths.settings, "");
}

PTEST(keys_reads_the_settings_even_headless_with_both_paths)
{
    struct dirs dirs;
    PT_ASSERT(make_dirs(&dirs));
    struct options o = windowed();
    o.headless = true;
    o.keys = true;
    o.save_path = "a.sav";
    o.rtc_offset_path = "a.rtc";
    struct data_paths paths;
    const enum data_dir_error status = data_dir_resolve(&o, &fake_host, &paths);
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(paths.mode, DATA_DIR_PER_USER);
    PT_ASSERT(joined_is(dirs.pref_slash, "settings.ini", paths.settings));
    PT_ASSERT_EQ_STR(paths.save, "a.sav");
}

PTEST(no_per_user_directory_is_reported_and_names_data_dir)
{
    struct dirs dirs;
    char said[512];
    PT_ASSERT(make_dirs(&dirs));
    fake.pref = NULL;
    struct error_case c = {.options = windowed()};
    PT_ASSERT(tsup_capture_stderr(resolve_expecting_a_report, &c, said, sizeof said));
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(c.status, DATA_DIR_ERR);
    PT_ASSERT(strstr(said, "--data-dir DIR") != NULL);
}

PTEST(an_unknown_executable_directory_skips_portable_mode_and_says_so)
{
    struct dirs dirs;
    char said[512];
    PT_ASSERT(make_dirs(&dirs));
    fake.base = NULL;
    struct in_directory_case c = {.options = windowed()};
    PT_ASSERT(tsup_capture_stderr(resolve_here, &c, said, sizeof said));
    PT_ASSERT(remove_dirs(&dirs));
    PT_ASSERT_EQ_INT(c.status, DATA_DIR_OK);
    PT_ASSERT_EQ_INT(c.paths.mode, DATA_DIR_PER_USER);
    PT_ASSERT(strstr(said, "portable mode is not checked") != NULL);
}

PTEST(a_path_too_long_for_the_room_is_an_error)
{
    static char long_dir[DATA_DIR_PATH_SIZE];
    static char said[2u * DATA_DIR_PATH_SIZE];
    memset(long_dir, 'd', sizeof long_dir - 8u);
    long_dir[sizeof long_dir - 8u] = '/';
    long_dir[sizeof long_dir - 7u] = '\0';
    fake.base = NULL;
    fake.pref = long_dir; /* fits, but not with "pokeemerald.sav" after it */
    struct error_case c = {.options = windowed()};
    PT_ASSERT(tsup_capture_stderr(resolve_expecting_a_report, &c, said, sizeof said));
    PT_ASSERT_EQ_INT(c.status, DATA_DIR_ERR);
    PT_ASSERT(strstr(said, "would be a path longer than 4095 bytes") != NULL);
}

PTEST(join_adds_a_separator_only_where_there_is_none)
{
    char out[16];
    PT_ASSERT(data_dir_join("a", "b.sav", out, sizeof out));
    PT_ASSERT_EQ_STR(out, "a/b.sav");
    PT_ASSERT(data_dir_join("a/", "b.sav", out, sizeof out));
    PT_ASSERT_EQ_STR(out, "a/b.sav");
    PT_ASSERT(data_dir_join("", "b.sav", out, sizeof out));
    PT_ASSERT_EQ_STR(out, "b.sav");
    PT_ASSERT(data_dir_join("/", "b", out, sizeof out));
    PT_ASSERT_EQ_STR(out, "/b");
    PT_ASSERT(data_dir_join("0123456789", "abcd", out, sizeof out)); /* 15 and the NUL */
    PT_ASSERT(!data_dir_join("0123456789", "abcde", out, sizeof out));
    PT_ASSERT_EQ_STR(out, "");
}

PTEST(the_modes_have_their_names)
{
    PT_ASSERT_EQ_STR(data_dir_mode_name(DATA_DIR_PER_USER), "per-user");
    PT_ASSERT_EQ_STR(data_dir_mode_name(DATA_DIR_PORTABLE), "portable");
    PT_ASSERT_EQ_STR(data_dir_mode_name(DATA_DIR_GIVEN), "--data-dir");
    PT_ASSERT_EQ_STR(data_dir_mode_name(DATA_DIR_UNUSED), "none");
}

PTEST(the_names_are_the_ones_documented)
{
    PT_ASSERT_EQ_STR(DATA_DIR_ORGANISATION, "pokeemerald-expansion-native");
    PT_ASSERT_EQ_STR(DATA_DIR_APPLICATION, "pokeemerald-native");
    PT_ASSERT_EQ_STR(DATA_DIR_SAVE_NAME, "pokeemerald.sav");
    PT_ASSERT_EQ_STR(DATA_DIR_RTC_OFFSET_NAME, "pokeemerald.rtc");
    PT_ASSERT_EQ_STR(DATA_DIR_SETTINGS_NAME, "settings.ini");
}

PTEST_MAIN()
