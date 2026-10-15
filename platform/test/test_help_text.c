// deps: input.c overlay.c font.c data_dir.c
/*
 * test_help_text.c: the lines --keys prints and the overlay shows
 * (help_text.c), line for line, for the default keys and files in each mode.
 */
#include "help_text.h"
#include "ptest.h"

#include <SDL_scancode.h>
#include <stdio.h>
#include <string.h>

#define MAX_LINES 32u

struct collected {
    size_t count;
    char lines[MAX_LINES][HELP_TEXT_LINE_SIZE];
};

static void collect(const char *line, void *context)
{
    struct collected *const c = context;
    if (c->count < MAX_LINES)
        (void)snprintf(c->lines[c->count], sizeof c->lines[0], "%s", line);
    c->count++;
}

static struct data_paths paths_in(enum data_dir_mode mode, const char *dir)
{
    struct data_paths paths = {.mode = mode};
    (void)snprintf(paths.dir, sizeof paths.dir, "%s", dir);
    (void)snprintf(paths.save, sizeof paths.save, "%spokeemerald.sav", dir);
    (void)snprintf(paths.rtc_offset, sizeof paths.rtc_offset, "%spokeemerald.rtc", dir);
    (void)snprintf(paths.settings, sizeof paths.settings, "%ssettings.ini", dir);
    return paths;
}

static struct collected collected;

PTEST(the_defaults_per_user_line_for_line)
{
    static const char *const expected[] = {
        "pokeemerald-native v1-test",
        "Data directory (per-user): /home/p/.local/share/pen/app/",
        "Save: /home/p/.local/share/pen/app/pokeemerald.sav",
        "Clock: /home/p/.local/share/pen/app/pokeemerald.rtc",
        "Settings: /home/p/.local/share/pen/app/settings.ini (not there: the defaults)",
        "Keyboard:",
        "  A       Z",
        "  B       X",
        "  Select  Backspace",
        "  Start   Return",
        "  Right   Right",
        "  Left    Left",
        "  Up      Up",
        "  Down    Down",
        "  R       S",
        "  L       A",
        "Controller: A, B, Back (Select), Start, the shoulders (L, R), the D-pad or the left stick",
    };
    const struct input_keymap keymap = input_default_keymap();
    const struct data_paths paths = paths_in(DATA_DIR_PER_USER, "/home/p/.local/share/pen/app/");
    const struct help_info info = {.version = "v1-test", .keymap = &keymap, .paths = &paths, .settings_found = false};
    memset(&collected, 0, sizeof collected);
    help_text_write(&info, collect, &collected);
    PT_ASSERT_EQ_U32(collected.count, sizeof expected / sizeof expected[0]);
    for (size_t i = 0; i < collected.count; i++)
        PT_ASSERT_EQ_STR(collected.lines[i], expected[i]);
}

PTEST(the_keys_in_force_and_a_settings_file_found_portable)
{
    struct input_keymap keymap = input_default_keymap();
    keymap.scancodes[0] = SDL_SCANCODE_SPACE;
    keymap.scancodes[9] = SDL_SCANCODE_LSHIFT;
    const struct data_paths paths = paths_in(DATA_DIR_PORTABLE, "C:\\Games\\pen\\");
    const struct help_info info = {.version = "unknown", .keymap = &keymap, .paths = &paths, .settings_found = true};
    memset(&collected, 0, sizeof collected);
    help_text_write(&info, collect, &collected);
    PT_ASSERT_EQ_STR(collected.lines[0], "pokeemerald-native unknown");
    PT_ASSERT_EQ_STR(collected.lines[6], "  A       Space");
    PT_ASSERT_EQ_STR(collected.lines[15], "  L       Left Shift");
    PT_ASSERT_EQ_STR(collected.lines[1], "Data directory (portable): C:\\Games\\pen\\");
    PT_ASSERT_EQ_STR(collected.lines[4], "Settings: C:\\Games\\pen\\settings.ini");
}

PTEST(a_given_directory_and_a_run_without_settings_or_directory)
{
    const struct input_keymap keymap = input_default_keymap();
    struct data_paths paths = paths_in(DATA_DIR_GIVEN, "out/");
    const struct help_info info = {.version = "x", .keymap = &keymap, .paths = &paths, .settings_found = true};
    memset(&collected, 0, sizeof collected);
    help_text_write(&info, collect, &collected);
    PT_ASSERT_EQ_STR(collected.lines[1], "Data directory (--data-dir): out/");
    paths = (struct data_paths){.mode = DATA_DIR_UNUSED};
    (void)snprintf(paths.save, sizeof paths.save, "a.sav");
    (void)snprintf(paths.rtc_offset, sizeof paths.rtc_offset, "a.rtc");
    memset(&collected, 0, sizeof collected);
    help_text_write(&info, collect, &collected);
    PT_ASSERT_EQ_STR(collected.lines[1], "Data directory: none");
    PT_ASSERT_EQ_STR(collected.lines[2], "Save: a.sav");
    PT_ASSERT_EQ_STR(collected.lines[3], "Clock: a.rtc");
    PT_ASSERT_EQ_STR(collected.lines[4], "Settings: none read");
}

PTEST_MAIN()
