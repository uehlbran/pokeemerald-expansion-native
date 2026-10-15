/*
 * settings.h: the player's settings file, settings.ini in the data
 * directory (data_dir.h).
 *
 * Not hardware: the GBA's buttons were wired to KEYINPUT and could not be
 * moved. Natively each button is a keyboard key (input.h), and this file
 * lets the player choose them, the window's scale and fast-forward
 * (fastforward.h). The game reads it at start-up and never writes it; a
 * missing file means the defaults.
 *
 * The format, one setting a line:
 *     # a comment: a line whose first character other than a space or a
 *     # tab is '#'
 *     a = z
 *     start = return
 *     l = left shift
 *     scale = 4
 *     fast_forward = tab
 *     fast_forward_toggle = shift+tab
 *     fast_forward_speed = 2
 *     fast_forward_audio = play
 *     pad_overlay = x
 *     pad_fast_forward = lefttrigger
 *     fullscreen = yes
 *     fullscreen_toggle = f12
 * A setting is a button's name (input_buttons' `setting`: a, b, select,
 * start, right, left, up, down, r, l), `scale`, or one of fast-forward's:
 * `fast_forward` (the key held), `fast_forward_toggle` (the key that
 * switches it on and off), `fast_forward_speed` (2, 3, 4 or `unlimited`)
 * and `fast_forward_audio` (`play`, sped up, or `mute`); a controller
 * function's, `pad_fast_forward`, `pad_fast_forward_toggle`, `pad_overlay`,
 * `pad_overlay_close`, `pad_quit` and `pad_fullscreen` (controller.h), whose
 * value is a button as SDL names it (SDL_GameControllerGetButtonFromString:
 * x, y, leftstick, rightstick, dpup, ...) or lefttrigger or righttrigger; and
 * fullscreen's, `fullscreen` (`yes`, `no`, or `auto`, the default: fullscreen
 * on a Steam Deck only, fullscreen.h) and `fullscreen_toggle` (a key, F11).
 * The controller's GBA buttons are not settings: they are SDL's layout, and
 * Steam Input remaps them where a player wants others. A button's value is
 * a key as SDL names it (SDL_GetScancodeFromName: "Z", "Return", "Left
 * Shift", "Keypad 8", "F2", ...); a fast-forward key's is one too, after any
 * of `shift+`, `ctrl+` and `alt+` (fastforward_key_from_name); the scale is a
 * whole number from 1 to OPTIONS_MAX_SCALE. Names and values are matched
 * without regard to case, spaces and tabs around either are ignored, and the
 * lines may end in "\n" or "\r\n"; a UTF-8 byte order mark at the start is
 * skipped. The later of two lines for one setting wins. Keys are scancodes,
 * physical positions, as the defaults are. F1 is the help overlay's
 * (overlay.h) and is refused as a button's or a fast-forward key; a button
 * and fast-forward may share a key, as two buttons may. One key cannot both
 * hold and toggle: if the file makes the two one, the later of the lines
 * that set them is a problem and its setting keeps its default (checked
 * again, so that a line clashing with the other's default is one too); the
 * same goes for two controller functions on one button. F1 is refused as
 * the fullscreen key too. A
 * line that is not `setting = value`, a setting that does not exist and a
 * value that is not one are problems: each is reported with its line number
 * and the line is ignored, the rest applies.
 *
 * settings_parse is pure: text in, settings out. settings_load reads the
 * file through host_file.h and reports.
 */
#ifndef PLATFORM_SETTINGS_H
#define PLATFORM_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "controller.h"
#include "fastforward.h"
#include "fullscreen.h"
#include "input.h"

/* The largest settings file read: a dozen lines need a few hundred bytes. */
#define SETTINGS_FILE_MAX_SIZE 16384u

/* The problems kept for the report; more are counted. */
#define SETTINGS_MAX_PROBLEMS 16u

/* The start of a problem's line kept for its message, with its NUL. */
#define SETTINGS_PROBLEM_TEXT_SIZE 48u

enum settings_problem_kind {
    SETTINGS_NOT_A_SETTING_LINE,   /* no '=', or nothing on one side of it */
    SETTINGS_UNKNOWN_SETTING,      /* text: the setting's name */
    SETTINGS_UNKNOWN_KEY,          /* text: the value, which SDL names no key */
    SETTINGS_RESERVED_KEY,         /* text: the value, the help overlay's key */
    SETTINGS_BAD_SCALE,            /* text: the value, not 1 to OPTIONS_MAX_SCALE */
    SETTINGS_BAD_SPEED,            /* text: the value, not 2, 3, 4 or unlimited */
    SETTINGS_BAD_AUDIO,            /* text: the value, not play or mute */
    SETTINGS_SAME_FAST_FORWARD_KEY, /* text: the value, the other fast-forward key's too */
    SETTINGS_UNKNOWN_CONTROL,       /* text: the value, which names no controller button */
    SETTINGS_SAME_CONTROL,          /* text: the value, another function's button too */
    SETTINGS_BAD_FULLSCREEN         /* text: the value, not yes, no or auto */
};

struct settings_problem {
    uint32_t line; /* 1 is the first */
    enum settings_problem_kind kind;
    char text[SETTINGS_PROBLEM_TEXT_SIZE];
};

struct settings {
    struct input_keymap keymap;
    int scale;
    struct fastforward_settings fast_forward;
    struct controller_bindings pad;
    enum fullscreen_setting fullscreen;
    int fullscreen_key; /* an SDL_Scancode, never INPUT_NO_SCANCODE */
    bool found;           /* settings_load: the file was there */
    size_t problem_count; /* every problem, those beyond SETTINGS_MAX_PROBLEMS too */
    struct settings_problem problems[SETTINGS_MAX_PROBLEMS];
};

/* The defaults: input_default_keymap, OPTIONS_DEFAULT_SCALE,
 * fastforward_defaults, controller_default_bindings, fullscreen auto on
 * FULLSCREEN_DEFAULT_SCANCODE, no problem. */
void settings_defaults(struct settings *out);

/* The defaults with the size bytes of text applied (text need not end in a
 * NUL, and a NUL in it is an ordinary byte). */
void settings_parse(const char *text, size_t size, struct settings *out);

/* Reads the file at path and parses it, then reports its problems on stderr
 * (settings_report). A file that is not there gives the defaults with found
 * false. Returns 0, or -1 with the defaults in *out after a report on stderr
 * if the file is there but cannot be read or is larger than
 * SETTINGS_FILE_MAX_SIZE. */
int settings_load(const char *path, struct settings *out);

/* One line per problem kept, "settings: PATH:LINE: what is wrong; what
 * happens instead", and one for the problems not kept, to out. */
void settings_report(const struct settings *settings, const char *path, FILE *out);

#endif /* PLATFORM_SETTINGS_H */
