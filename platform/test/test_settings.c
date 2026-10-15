// deps: input.c overlay.c font.c
/*
 * test_settings.c: the settings file (settings.c), from hand-written texts:
 * the defaults, every setting, names in any case, a key SDL does not know,
 * a bad scale, the help overlay's key, a duplicate (the later line wins),
 * CRLF line ends, a byte order mark, comments, blank lines, a very long
 * line, and the file read from the disk with its problems reported.
 */
#include "ptest.h"
#include "settings.h"
#include "test_support.h"

#include <SDL_scancode.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "options.h"

/* The index of each button in input_buttons(), KEYINPUT bit order. */
enum { A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L };

static void parse(const char *text, struct settings *out)
{
    settings_parse(text, strlen(text), out);
}

static bool keymap_is_default(const struct input_keymap *keymap)
{
    const struct input_keymap defaults = input_default_keymap();
    return memcmp(keymap, &defaults, sizeof defaults) == 0;
}

PTEST(no_text_gives_the_defaults)
{
    struct settings s;
    parse("", &s);
    PT_ASSERT(keymap_is_default(&s.keymap));
    PT_ASSERT_EQ_INT(s.scale, OPTIONS_DEFAULT_SCALE);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    PT_ASSERT(!s.found);
    settings_defaults(&s);
    PT_ASSERT(keymap_is_default(&s.keymap));
    PT_ASSERT_EQ_INT(s.scale, 3);
}

PTEST(every_setting_is_applied)
{
    static const char text[] = "a = space\n"
                               "b = left shift\n"
                               "select = tab\n"
                               "start = escape\n"
                               "right = d\n"
                               "left = q\n"
                               "up = w\n"
                               "down = f\n"
                               "r = e\n"
                               "l = 1\n"
                               "scale = 5\n";
    static const int expected[INPUT_BUTTON_COUNT] = {
        SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_TAB, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_D,
        SDL_SCANCODE_Q,     SDL_SCANCODE_W,      SDL_SCANCODE_F,   SDL_SCANCODE_E,      SDL_SCANCODE_1,
    };
    struct settings s;
    parse(text, &s);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++)
        PT_ASSERT_EQ_INT(s.keymap.scancodes[i], expected[i]);
    PT_ASSERT_EQ_INT(s.scale, 5);
}

PTEST(one_setting_leaves_the_others_at_their_defaults)
{
    struct settings s;
    const struct input_keymap defaults = input_default_keymap();
    parse("start = space\n", &s);
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++)
        PT_ASSERT_EQ_INT(s.keymap.scancodes[i], i == START ? SDL_SCANCODE_SPACE : defaults.scancodes[i]);
    PT_ASSERT_EQ_INT(s.scale, OPTIONS_DEFAULT_SCALE);
}

PTEST(names_and_keys_in_any_case_with_spaces_and_tabs_around_them)
{
    struct settings s;
    parse("  SELECT\t=\tBackSpace  \nStart=RETURN\n\tScale = 2\t\n", &s);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[SELECT], SDL_SCANCODE_BACKSPACE);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[START], SDL_SCANCODE_RETURN);
    PT_ASSERT_EQ_INT(s.scale, 2);
}

PTEST(a_key_sdl_does_not_know_is_a_problem_and_keeps_the_default)
{
    struct settings s;
    parse("a = x\nb = nosuchkey\n", &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_X);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[B], SDL_SCANCODE_X); /* B's default is X too */
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_U32(s.problems[0].line, 2u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_UNKNOWN_KEY);
    PT_ASSERT_EQ_STR(s.problems[0].text, "nosuchkey");
}

PTEST(a_setting_that_does_not_exist_is_a_problem)
{
    struct settings s;
    parse("jump = space\nstartt = return\n", &s);
    PT_ASSERT(keymap_is_default(&s.keymap));
    PT_ASSERT_EQ_U32(s.problem_count, 2u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_UNKNOWN_SETTING);
    PT_ASSERT_EQ_STR(s.problems[0].text, "jump");
    PT_ASSERT_EQ_U32(s.problems[1].line, 2u);
    PT_ASSERT_EQ_STR(s.problems[1].text, "startt");
}

PTEST(a_scale_that_is_not_one_to_sixteen_is_a_problem)
{
    static const char *const bad[] = {"0", "17", "-1", "3x", "+3", "1.5", "99999999999999999999", "three"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char text[64];
        struct settings s;
        (void)snprintf(text, sizeof text, "scale = %s\n", bad[i]);
        parse(text, &s);
        PT_ASSERT_EQ_INT(s.scale, OPTIONS_DEFAULT_SCALE);
        PT_ASSERT_EQ_U32(s.problem_count, 1u);
        PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_BAD_SCALE);
        PT_ASSERT_EQ_STR(s.problems[0].text, bad[i]);
    }
    struct settings s;
    parse("scale = 1\n", &s);
    PT_ASSERT_EQ_INT(s.scale, 1);
    parse("scale = 16\n", &s);
    PT_ASSERT_EQ_INT(s.scale, OPTIONS_MAX_SCALE);
}

PTEST(the_help_overlays_key_cannot_be_a_button)
{
    struct settings s;
    parse("a = f1\n", &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Z);
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_RESERVED_KEY);
    parse("a = f2\n", &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_F2);
}

PTEST(the_later_of_two_lines_for_a_setting_wins)
{
    struct settings s;
    parse("a = q\nscale = 2\na = w\nscale = 4\n", &s);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_W);
    PT_ASSERT_EQ_INT(s.scale, 4);
}

PTEST(a_bad_later_line_keeps_the_earlier_value)
{
    struct settings s;
    parse("a = q\na = nosuchkey\n", &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Q);
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
}

PTEST(crlf_line_ends_and_a_last_line_without_one)
{
    struct settings s;
    parse("a = q\r\nb = w\r\n\r\nscale = 2", &s);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Q);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[B], SDL_SCANCODE_W);
    PT_ASSERT_EQ_INT(s.scale, 2);
}

PTEST(a_byte_order_mark_at_the_start_is_skipped)
{
    struct settings s;
    parse("\xEF\xBB\xBF"
          "a = q\n",
          &s);
    PT_ASSERT_EQ_U32(s.problem_count, 0u);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Q);
    parse("a = q\n\xEF\xBB\xBF"
          "b = w\n",
          &s); /* anywhere else it is text: an unknown setting */
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_U32(s.problems[0].line, 2u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_UNKNOWN_SETTING);
}

PTEST(comments_and_blank_lines_are_skipped_and_counted)
{
    struct settings s;
    parse("# the keys\n\n   \n\t# indented comment\na = q\n# a = w\nbad line\n", &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Q);
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_U32(s.problems[0].line, 7u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_NOT_A_SETTING_LINE);
    PT_ASSERT_EQ_STR(s.problems[0].text, "bad line");
}

PTEST(a_line_without_a_setting_or_a_value_is_a_problem)
{
    struct settings s;
    parse("= q\na =\n=\na q\n", &s);
    PT_ASSERT(keymap_is_default(&s.keymap));
    PT_ASSERT_EQ_U32(s.problem_count, 4u);
    for (size_t i = 0; i < 4; i++) {
        PT_ASSERT_EQ_INT(s.problems[i].kind, SETTINGS_NOT_A_SETTING_LINE);
        PT_ASSERT_EQ_U32(s.problems[i].line, (uint32_t)(i + 1u));
    }
}

/* A line of 10000 characters is one problem, its text cut to the room kept, and the lines around
 * it apply. */
PTEST(a_very_long_line_is_a_problem_and_the_rest_applies)
{
    static char text[10100];
    static const char start[] = "a = q\nb = ";
    memcpy(text, start, sizeof start - 1u);
    size_t length = sizeof start - 1u;
    for (size_t i = 0; i < 10000; i++)
        text[length++] = 'k';
    static const char end[] = "\nstart = w\n";
    memcpy(text + length, end, sizeof end);
    struct settings s;
    parse(text, &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Q);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[B], SDL_SCANCODE_X);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[START], SDL_SCANCODE_W);
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_U32(s.problems[0].line, 2u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_UNKNOWN_KEY);
    PT_ASSERT_EQ_U32(strlen(s.problems[0].text), SETTINGS_PROBLEM_TEXT_SIZE - 1u);
}

PTEST(a_nul_in_a_value_is_not_part_of_a_key_name)
{
    static const char text[] = "a = q\0junk\n";
    struct settings s;
    settings_parse(text, sizeof text - 1u, &s);
    PT_ASSERT_EQ_INT(s.keymap.scancodes[A], SDL_SCANCODE_Z);
    PT_ASSERT_EQ_U32(s.problem_count, 1u);
    PT_ASSERT_EQ_INT(s.problems[0].kind, SETTINGS_UNKNOWN_KEY);
}

PTEST(problems_past_the_room_are_counted)
{
    static char text[1024];
    size_t length = 0;
    for (size_t i = 0; i < SETTINGS_MAX_PROBLEMS + 3u; i++)
        length += (size_t)snprintf(text + length, sizeof text - length, "nonsense %u\n", (unsigned)i);
    struct settings s;
    settings_parse(text, length, &s);
    PT_ASSERT_EQ_U32(s.problem_count, SETTINGS_MAX_PROBLEMS + 3u);
    PT_ASSERT_EQ_U32(s.problems[SETTINGS_MAX_PROBLEMS - 1u].line, SETTINGS_MAX_PROBLEMS);
}

/* ---- the file ---- */

struct load_case {
    const char *path;
    struct settings settings;
    int status;
};

static void load(void *context)
{
    struct load_case *const c = context;
    c->status = settings_load(c->path, &c->settings);
}

/* dir/settings.ini in path (TSUP_PATH_SIZE bytes); false if it does not fit. */
static bool settings_path_in(const char *dir, char *path)
{
    const int written = snprintf(path, TSUP_PATH_SIZE, "%s/settings.ini", dir);
    return written > 0 && (size_t)written < TSUP_PATH_SIZE;
}

static bool write_file(const char *path, const char *text, size_t size)
{
    FILE *const file = fopen(path, "wb");
    if (file == NULL)
        return false;
    const bool written = fwrite(text, 1, size, file) == size;
    return fclose(file) == 0 && written;
}

PTEST(a_missing_file_gives_the_defaults_quietly)
{
    char dir[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE];
    char said[256];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "settings"));
    PT_ASSERT(settings_path_in(dir, path));
    struct load_case c = {.path = path};
    PT_ASSERT(tsup_capture_stderr(load, &c, said, sizeof said));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(c.status, 0);
    PT_ASSERT(!c.settings.found);
    PT_ASSERT(keymap_is_default(&c.settings.keymap));
    PT_ASSERT_EQ_STR(said, "");
}

PTEST(a_file_is_applied_and_each_problem_reported_with_its_line)
{
    static const char text[] = "# my keys\r\na = space\r\njump = q\r\nscale = 40\r\n";
    char dir[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE];
    char said[2048];
    char expected[2048];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "settings"));
    PT_ASSERT(settings_path_in(dir, path));
    PT_ASSERT(write_file(path, text, sizeof text - 1u));
    struct load_case c = {.path = path};
    PT_ASSERT(tsup_capture_stderr(load, &c, said, sizeof said));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(c.status, 0);
    PT_ASSERT(c.settings.found);
    PT_ASSERT_EQ_INT(c.settings.keymap.scancodes[A], SDL_SCANCODE_SPACE);
    PT_ASSERT_EQ_INT(c.settings.scale, OPTIONS_DEFAULT_SCALE);
    const int written = snprintf(
        expected, sizeof expected,
        "settings: %s:3: 'jump' is not a setting (a, b, select, start, right, left, up, down, r, l, "
        "scale); the line is ignored\n"
        "settings: %s:4: the scale '40' is not a whole number from 1 to 16; the line is ignored\n",
        path, path);
    PT_ASSERT(written > 0 && (size_t)written < sizeof expected);
    PT_ASSERT_EQ_STR(said, expected);
}

PTEST(the_report_names_each_kind_of_problem_and_those_not_kept)
{
    static char text[1024];
    size_t length = (size_t)snprintf(text, sizeof text, "x\na = nokey\na = F1\n");
    for (size_t i = 0; i < SETTINGS_MAX_PROBLEMS; i++)
        length += (size_t)snprintf(text + length, sizeof text - length, "x\n");
    struct settings s;
    settings_parse(text, length, &s);
    FILE *const out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    settings_report(&s, "f.ini", out);
    static char said[4096];
    const bool rewound = fseek(out, 0, SEEK_SET) == 0;
    const size_t got = rewound ? fread(said, 1, sizeof said - 1u, out) : 0;
    said[got] = '\0';
    (void)fclose(out);
    PT_ASSERT(strstr(said, "settings: f.ini:1: 'x' is not a 'setting = value' line; the line is ignored\n") != NULL);
    PT_ASSERT(strstr(said, "settings: f.ini:2: 'nokey' is not a key name SDL knows") != NULL);
    PT_ASSERT(strstr(said, "settings: f.ini:3: 'F1' opens the help overlay and cannot be a button's key") != NULL);
    PT_ASSERT(strstr(said, "settings: f.ini: 3 more lines with problems, also ignored\n") != NULL);
}

PTEST(a_file_larger_than_the_limit_is_refused_with_the_defaults)
{
    static char text[SETTINGS_FILE_MAX_SIZE + 1u];
    static const char first_line[] = "a = q";
    memset(text, '\n', sizeof text);
    for (size_t i = 0; first_line[i] != '\0'; i++)
        text[i] = first_line[i];
    char dir[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE];
    char said[512];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "settings"));
    PT_ASSERT(settings_path_in(dir, path));
    PT_ASSERT(write_file(path, text, sizeof text));
    struct load_case c = {.path = path};
    PT_ASSERT(tsup_capture_stderr(load, &c, said, sizeof said));
    /* exactly the limit is read */
    PT_ASSERT(write_file(path, text, sizeof text - 1u));
    struct load_case at_limit = {.path = path};
    char quiet[64];
    PT_ASSERT(tsup_capture_stderr(load, &at_limit, quiet, sizeof quiet));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(c.status, -1);
    PT_ASSERT(keymap_is_default(&c.settings.keymap));
    PT_ASSERT(strstr(said, "is larger than 16384 bytes") != NULL);
    PT_ASSERT_EQ_INT(at_limit.status, 0);
    PT_ASSERT_EQ_INT(at_limit.settings.keymap.scancodes[A], SDL_SCANCODE_Q);
}

/* A directory where the file should be cannot be read: reported, and the start stops (main). */
PTEST(a_settings_path_that_cannot_be_read_is_an_error)
{
    char dir[TSUP_PATH_SIZE];
    char said[512];
    PT_ASSERT(tsup_make_temp_directory(dir, sizeof dir, "settings"));
    struct load_case c = {.path = dir};
    PT_ASSERT(tsup_capture_stderr(load, &c, said, sizeof said));
    PT_ASSERT(tsup_remove_tree(dir));
    PT_ASSERT_EQ_INT(c.status, -1);
    PT_ASSERT(keymap_is_default(&c.settings.keymap));
    PT_ASSERT(strstr(said, "settings: ") != NULL);
}

PTEST_MAIN()
