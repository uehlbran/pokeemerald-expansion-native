/*
 * help_text.h: what --keys prints and the F1 overlay shows: the version, the
 * keys, the controller's buttons, the data directory and the files.
 *
 * Not hardware: the GBA's buttons were printed on its case. One function
 * writes the lines for both, so that the two never differ. The files come
 * before the keys: a small window's overlay shows the lines from the top
 * until it is full, and where the save is matters most.
 */
#ifndef PLATFORM_HELP_TEXT_H
#define PLATFORM_HELP_TEXT_H

#include <stdbool.h>

#include "data_dir.h"
#include "input.h"

/* Room for the longest line, a label and a path, with its NUL. */
#define HELP_TEXT_LINE_SIZE (DATA_DIR_PATH_SIZE + 64u)

struct help_info {
    const char *version;               /* version_string() */
    const struct input_keymap *keymap; /* the keys in force */
    const struct data_paths *paths;
    bool settings_found; /* the settings file was there */
};

/* Receives each line, without a newline; the line lives until it returns. */
typedef void (*help_text_sink)(const char *line, void *context);

/* Passes the lines to sink, in order:
 *     pokeemerald-native VERSION
 *     Data directory (MODE): DIR  (data_dir_mode_name; "Data directory: none" if unused)
 *     Save: PATH
 *     Clock: PATH
 *     Settings: PATH              (with " (not there: the defaults)" if not found,
 *                                  "Settings: none read" if the run reads none)
 *     Keyboard:
 *       A       Z                 (each button and its key, input_buttons' order)
 *     Controller: ... */
void help_text_write(const struct help_info *info, help_text_sink sink, void *context);

#endif /* PLATFORM_HELP_TEXT_H */
