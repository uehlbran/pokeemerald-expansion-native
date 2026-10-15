/*
 * help_text.c: the lines --keys prints and the overlay shows (help_text.h).
 */
#include "help_text.h"

#include <stdio.h>

#include "version.h"

/* The button column's width: "Select" and a space ("Toggle" too). */
#define BUTTON_COLUMN_WIDTH 7

#define CONTROLLER_LINE "Controller: A, B, Back (Select), Start, the shoulders (L, R), the D-pad or the left stick"

static void write_keys(const struct input_keymap *keymap, help_text_sink sink, void *context)
{
    char line[HELP_TEXT_LINE_SIZE];
    const struct input_button *const buttons = input_buttons();
    sink("Keyboard:", context);
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        (void)snprintf(line, sizeof line, "  %-*s %s", BUTTON_COLUMN_WIDTH, buttons[i].name,
                       input_scancode_name(keymap->scancodes[i]));
        sink(line, context);
    }
    sink(CONTROLLER_LINE, context);
}

static void write_fast_forward(const struct fastforward_settings *fast_forward, help_text_sink sink, void *context)
{
    char line[HELP_TEXT_LINE_SIZE];
    char key[FASTFORWARD_KEY_NAME_SIZE];
    sink("Fast-forward:", context);
    fastforward_key_name(&fast_forward->hold, key, sizeof key);
    (void)snprintf(line, sizeof line, "  %-*s %s", BUTTON_COLUMN_WIDTH, "Hold", key);
    sink(line, context);
    fastforward_key_name(&fast_forward->toggle, key, sizeof key);
    (void)snprintf(line, sizeof line, "  %-*s %s", BUTTON_COLUMN_WIDTH, "Toggle", key);
    sink(line, context);
    if (fast_forward->speed == FASTFORWARD_UNLIMITED)
        (void)snprintf(line, sizeof line, "  %-*s unlimited", BUTTON_COLUMN_WIDTH, "Speed");
    else
        (void)snprintf(line, sizeof line, "  %-*s %lux", BUTTON_COLUMN_WIDTH, "Speed",
                       (unsigned long)fast_forward->speed);
    sink(line, context);
    (void)snprintf(line, sizeof line, "  %-*s %s", BUTTON_COLUMN_WIDTH, "Sound",
                   fast_forward->audio == FASTFORWARD_AUDIO_MUTE ? "muted" : "sped up");
    sink(line, context);
}

static void write_files(const struct help_info *info, help_text_sink sink, void *context)
{
    char line[HELP_TEXT_LINE_SIZE];
    const struct data_paths *const paths = info->paths;
    if (paths->mode == DATA_DIR_UNUSED)
        (void)snprintf(line, sizeof line, "Data directory: none");
    else
        (void)snprintf(line, sizeof line, "Data directory (%s): %s", data_dir_mode_name(paths->mode), paths->dir);
    sink(line, context);
    (void)snprintf(line, sizeof line, "Save: %s", paths->save);
    sink(line, context);
    (void)snprintf(line, sizeof line, "Clock: %s", paths->rtc_offset);
    sink(line, context);
    if (paths->settings[0] == '\0')
        (void)snprintf(line, sizeof line, "Settings: none read");
    else
        (void)snprintf(line, sizeof line, "Settings: %s%s", paths->settings,
                       info->settings_found ? "" : " (not there: the defaults)");
    sink(line, context);
}

void help_text_write(const struct help_info *info, help_text_sink sink, void *context)
{
    char line[HELP_TEXT_LINE_SIZE];
    (void)snprintf(line, sizeof line, VERSION_PROGRAM_NAME " %s", info->version);
    sink(line, context);
    write_files(info, sink, context);
    write_keys(info->keymap, sink, context);
    write_fast_forward(info->fast_forward, sink, context);
}
