/*
 * test_data_dir_linux.c: SDL's two answers behind data_dir_sdl_host, on
 * Linux, where both can be checked without touching the user's own
 * directories: SDL_GetPrefPath follows XDG_DATA_HOME, which the test points
 * at a scratch directory, and SDL_GetBasePath is the directory of
 * /proc/self/exe. The rule they feed is ../test_data_dir.c's.
 */
// deps: data_dir.c
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "data_dir.h"
#include "ptest.h"
#include "test_support.h"

PTEST(the_per_user_directory_is_made_under_xdg_data_home_with_both_names)
{
    char home[TSUP_PATH_SIZE];
    char expected[TSUP_PATH_SIZE + 64u];
    char got[DATA_DIR_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_directory(home, sizeof home, "data_dir_xdg"));
    PT_ASSERT(tsup_set_environment("XDG_DATA_HOME", home));
    const bool answered = data_dir_sdl_host()->pref_dir(got, sizeof got);
    const int written = snprintf(expected, sizeof expected, "%s/pokeemerald-expansion-native/pokeemerald-native/",
                                 home);
    const bool made = tsup_exists(expected);
    PT_ASSERT(tsup_unset_environment("XDG_DATA_HOME"));
    PT_ASSERT(tsup_remove_tree(home));
    PT_ASSERT(answered);
    PT_ASSERT(written > 0 && (size_t)written < sizeof expected);
    PT_ASSERT_EQ_STR(got, expected);
    PT_ASSERT(made);
}

PTEST(the_executables_directory_is_that_of_proc_self_exe)
{
    char exe[PATH_MAX];
    char got[DATA_DIR_PATH_SIZE];
    const ssize_t length = readlink("/proc/self/exe", exe, sizeof exe - 1u);
    PT_ASSERT(length > 0);
    exe[length] = '\0';
    char *const last_slash = strrchr(exe, '/');
    PT_ASSERT(last_slash != NULL);
    last_slash[1] = '\0';
    PT_ASSERT(data_dir_sdl_host()->base_dir(got, sizeof got));
    PT_ASSERT_EQ_STR(got, exe);
}

struct short_room {
    bool answered;
};

static void ask_base_dir_into_four_bytes(void *context)
{
    struct short_room *const c = context;
    char got[4];
    c->answered = data_dir_sdl_host()->base_dir(got, sizeof got);
}

PTEST(an_answer_longer_than_the_room_is_refused_and_reported)
{
    struct short_room c = {.answered = true};
    char said[256];
    PT_ASSERT(tsup_capture_stderr(ask_base_dir_into_four_bytes, &c, said, sizeof said));
    PT_ASSERT(!c.answered);
    PT_ASSERT(strstr(said, "data_dir: SDL_GetBasePath gave a path of ") != NULL);
}

PTEST_MAIN()
