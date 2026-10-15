// deps: data_dir.c settings.c fullscreen.c help_text.c input.c overlay.c controller.c font.c version.c fastforward.c
/*
 * test_player.c: the player's side of a run (player.c): which runs get the
 * help overlay and fast-forward, the files and the settings found for a run (with --data-dir
 * in a scratch directory, so that no system directory is asked for), the
 * window's scale, and the lines --keys prints and the overlay is given.
 */
#include "player.h"
#include "ptest.h"
#include "test_support.h"

#include <SDL_scancode.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "version.h"

/* A host whose directories are never there, and how often it was asked. */
static unsigned host_calls;

static bool no_directory(char *out, size_t size)
{
    if (size > 0)
        out[0] = '\0';
    host_calls++;
    (void)fprintf(stderr, "test host: no directory\n");
    return false;
}

static const struct data_dir_host no_host = {.base_dir = no_directory, .pref_dir = no_directory};

static struct options windowed(void)
{
    return (struct options){.scale = OPTIONS_DEFAULT_SCALE};
}

/* The overlay and fast-forward: never in a replay, a gate or the test runner. */
PTEST(a_player_is_at_the_keyboard_of_a_window_only)
{
    struct options o = windowed();
    PT_ASSERT(player_at_keyboard(&o));
    o.headless = true;
    PT_ASSERT(!player_at_keyboard(&o));
    o = windowed();
    o.test_runner = true;
    PT_ASSERT(!player_at_keyboard(&o));
    o = windowed();
    o.replay_path = "opening.replay";
    PT_ASSERT(!player_at_keyboard(&o));
    o = windowed();
    o.data_dir = "d";
    o.save_path = "a.sav";
    o.silent = true;
    o.unthrottled = true;
    PT_ASSERT(player_at_keyboard(&o)); /* the files, the sound and the pace have nothing to do with it */
}

/* A scratch data directory, its path joined with name in out. */
static bool in_dir(const char *dir, const char *name, char *out, size_t size)
{
    const int written = snprintf(out, size, "%s/%s", dir, name);
    return written > 0 && (size_t)written < size;
}

static bool write_file(const char *path, const char *text)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL)
        return false;
    const bool written = fputs(text, file) != EOF;
    return fclose(file) == 0 && written;
}

PTEST(the_files_and_the_settings_of_a_data_directory)
{
    char dir[TSUP_PATH_SIZE];
    char settings[TSUP_PATH_SIZE + 16u];
    char save[TSUP_PATH_SIZE + 16u];
    char clock[TSUP_PATH_SIZE + 16u];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    PT_ASSERT(in_dir(dir, "settings.ini", settings, sizeof settings));
    PT_ASSERT(in_dir(dir, "pokeemerald.sav", save, sizeof save));
    PT_ASSERT(in_dir(dir, "pokeemerald.rtc", clock, sizeof clock));
    PT_ASSERT(write_file(settings, "a = q\nscale = 5\nfast_forward_speed = 3\nfast_forward_toggle = ctrl+f\n"));
    struct options o = windowed();
    o.data_dir = dir;
    host_calls = 0;
    const int found = player_find(&o, &no_host);
    const int scale = player_window_scale(&o);
    o.scale_given = true;
    o.scale = 2;
    const int given_scale = player_window_scale(&o);
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(found, 0);
    PT_ASSERT_EQ_U32(host_calls, 0u);
    PT_ASSERT_EQ_STR(player_save_path(), save);
    PT_ASSERT_EQ_STR(player_rtc_offset_path(), clock);
    PT_ASSERT_EQ_INT(player_keymap()->scancodes[0], SDL_SCANCODE_Q);
    PT_ASSERT_EQ_INT(player_keymap()->scancodes[1], SDL_SCANCODE_X); /* B: the default */
    PT_ASSERT_EQ_U32(player_fast_forward()->speed, 3u);
    PT_ASSERT_EQ_INT(player_fast_forward()->toggle.scancode, SDL_SCANCODE_F);
    PT_ASSERT_EQ_INT(player_fast_forward()->hold.scancode, SDL_SCANCODE_TAB); /* the default */
    PT_ASSERT_EQ_INT(scale, 5);
    PT_ASSERT_EQ_INT(given_scale, 2);
}

PTEST(without_a_settings_file_the_defaults)
{
    char dir[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    struct options o = windowed();
    o.data_dir = dir;
    const int found = player_find(&o, &no_host);
    const struct input_keymap defaults = input_default_keymap();
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(found, 0);
    PT_ASSERT(memcmp(player_keymap(), &defaults, sizeof defaults) == 0);
    PT_ASSERT_EQ_INT(player_window_scale(&o), OPTIONS_DEFAULT_SCALE);
}

PTEST(test_runner_mode_has_no_files_and_the_default_keys)
{
    struct options o = windowed();
    o.test_runner = true;
    o.headless = true;
    host_calls = 0;
    PT_ASSERT_EQ_INT(player_find(&o, &no_host), 0);
    PT_ASSERT(player_save_path() == NULL);
    PT_ASSERT(player_rtc_offset_path() == NULL);
    PT_ASSERT_EQ_INT(player_keymap()->scancodes[0], SDL_SCANCODE_Z);
    PT_ASSERT_EQ_U32(host_calls, 0u);
}

struct find_case {
    struct options options;
    int status;
};

static void find(void *context)
{
    struct find_case *const c = context;
    c->status = player_find(&c->options, &no_host);
}

PTEST(no_data_directory_or_a_settings_file_that_cannot_be_read_fails)
{
    char dir[TSUP_PATH_SIZE];
    char settings[TSUP_PATH_SIZE + 16u];
    char said[1024];
    struct find_case nowhere = {.options = windowed()};
    PT_ASSERT(tsup_capture_stderr(find, &nowhere, said, sizeof said));
    PT_ASSERT_EQ_INT(nowhere.status, -1);
    PT_ASSERT(strstr(said, "--data-dir DIR") != NULL);
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    PT_ASSERT(in_dir(dir, "settings.ini", settings, sizeof settings));
    PT_ASSERT(tsup_make_directory(settings)); /* a directory where the file should be */
    struct find_case unreadable = {.options = windowed()};
    unreadable.options.data_dir = dir;
    PT_ASSERT(tsup_capture_stderr(find, &unreadable, said, sizeof said));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(unreadable.status, -1);
    PT_ASSERT(strstr(said, "settings: ") != NULL);
}

#define KEYS_TEXT_SIZE 4096u

struct collected {
    char text[KEYS_TEXT_SIZE];
    size_t length;
};

static void collect(const char *line, void *context)
{
    struct collected *const c = context;
    const int written = snprintf(c->text + c->length, sizeof c->text - c->length, "%s\n", line);
    if (written > 0 && (size_t)written < sizeof c->text - c->length)
        c->length += (size_t)written;
}

/* --keys and the overlay get the same lines: the version first, then the files, then the keys. */
PTEST(keys_prints_the_lines_the_overlay_is_given)
{
    char dir[TSUP_PATH_SIZE];
    char expected_start[TSUP_PATH_SIZE + 128u];
    static struct collected given;
    static char printed[KEYS_TEXT_SIZE];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    struct options o = windowed();
    o.data_dir = dir;
    o.keys = true;
    const int found = player_find(&o, &no_host);
    memset(&given, 0, sizeof given);
    player_write_help(collect, &given);
    FILE *const out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    const int status = player_print_keys(out);
    const bool rewound = fseek(out, 0, SEEK_SET) == 0;
    const size_t length = rewound ? fread(printed, 1, sizeof printed - 1u, out) : 0;
    printed[length] = '\0';
    (void)fclose(out);
    PT_ASSERT(tsup_remove_tree(dir));
    const int written = snprintf(expected_start, sizeof expected_start, "%s %s\nData directory (--data-dir): %s\n",
                                 VERSION_PROGRAM_NAME, version_string(), dir);
    PT_ASSERT(written > 0 && (size_t)written < sizeof expected_start);
    PT_ASSERT_EQ_INT(found, 0);
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_STR(printed, given.text);
    PT_ASSERT(strncmp(printed, expected_start, strlen(expected_start)) == 0);
    PT_ASSERT(strstr(printed, "\nKeyboard:\n  A          Z\n") != NULL);
    PT_ASSERT(strstr(printed, "\n  Fullscreen F11\nController:\n  GBA buttons: ") != NULL);
    PT_ASSERT(strstr(printed, "\n  Overlay quit         X\n") != NULL);
    PT_ASSERT(strstr(printed, "\nFast-forward:\n  Hold       Tab\n  Toggle     Shift+Tab\n  Speed      2x\n") != NULL);
}

/* player_starts_fullscreen for a run in a scratch data directory whose settings file holds
 * text (NULL: none), with SteamDeck set to deck (NULL: unset). */
static void find_and_ask(const char *text, const char *deck, struct options o, bool *fullscreen)
{
    char dir[TSUP_PATH_SIZE];
    char settings[TSUP_PATH_SIZE + 16u];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    PT_ASSERT(in_dir(dir, "settings.ini", settings, sizeof settings));
    PT_ASSERT(text == NULL || write_file(settings, text));
    o.data_dir = dir;
    PT_ASSERT(deck != NULL ? tsup_set_environment("SteamDeck", deck) : tsup_unset_environment("SteamDeck"));
    PT_ASSERT_EQ_INT(player_find(&o, &no_host), 0);
    *fullscreen = player_starts_fullscreen(&o);
    PT_ASSERT(tsup_unset_environment("SteamDeck"));
    PT_ASSERT(tsup_remove_tree(dir));
}

static bool starts_fullscreen(const char *text, const char *deck, struct options o)
{
    bool fullscreen = false;
    find_and_ask(text, deck, o, &fullscreen);
    return fullscreen;
}

PTEST(a_window_starts_fullscreen_on_a_deck_and_not_elsewhere)
{
    PT_ASSERT(starts_fullscreen(NULL, "1", windowed()));
    PT_ASSERT(!starts_fullscreen(NULL, NULL, windowed()));
    PT_ASSERT(!starts_fullscreen(NULL, "0", windowed()));
    PT_ASSERT(!starts_fullscreen(NULL, "true", windowed()));
}

PTEST(the_fullscreen_setting_overrides_the_deck)
{
    PT_ASSERT(!starts_fullscreen("fullscreen = no\n", "1", windowed()));
    PT_ASSERT(starts_fullscreen("fullscreen = yes\n", NULL, windowed()));
    PT_ASSERT(starts_fullscreen("fullscreen = auto\n", "1", windowed()));
    PT_ASSERT(starts_fullscreen("fullscreen = yes\n", "0", windowed()));
}

/* A replay, a gate and the test runner never change with the machine they run on. */
PTEST(a_deck_changes_nothing_for_a_run_without_a_player_at_the_keyboard)
{
    struct options headless = windowed();
    headless.headless = true;
    struct options replay = windowed();
    replay.replay_path = "opening.replay";
    struct options runner = windowed();
    runner.test_runner = true;
    PT_ASSERT(!starts_fullscreen("fullscreen = yes\n", "1", headless));
    PT_ASSERT(!starts_fullscreen("fullscreen = yes\n", "1", replay));
    PT_ASSERT(!starts_fullscreen(NULL, "1", headless));
    PT_ASSERT(!starts_fullscreen(NULL, "1", replay));
    PT_ASSERT(!starts_fullscreen(NULL, "1", runner));
}

PTEST(the_controller_and_the_fullscreen_key_come_from_the_settings)
{
    char dir[TSUP_PATH_SIZE];
    char settings[TSUP_PATH_SIZE + 16u];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "player"));
    PT_ASSERT(in_dir(dir, "settings.ini", settings, sizeof settings));
    PT_ASSERT(write_file(settings, "pad_overlay = leftstick\nfullscreen_toggle = f12\n"));
    struct options o = windowed();
    o.data_dir = dir;
    const int found = player_find(&o, &no_host);
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(found, 0);
    PT_ASSERT_EQ_INT(player_controller()->controls[CONTROLLER_OVERLAY], SDL_CONTROLLER_BUTTON_LEFTSTICK);
    PT_ASSERT_EQ_INT(player_controller()->controls[CONTROLLER_QUIT], SDL_CONTROLLER_BUTTON_X);
    PT_ASSERT_EQ_INT(player_fullscreen_key(), SDL_SCANCODE_F12);
}

PTEST_MAIN()
