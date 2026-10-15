/*
 * player.h: the player's side of a run: where their files are, the settings
 * read from one of them, what --keys and the help overlay show of both, and
 * whether the run has a player at the keyboard for the overlay and
 * fast-forward at all.
 *
 * Not hardware: on the GBA the player carried the save in the cartridge and
 * pressed the buttons on the case. Natively main finds the files once,
 * before any module starts (player_find: data_dir.h's rule, then
 * settings.h's file), and hands the modules what they need of them: the
 * save's and the clock's paths, the keymap, fast-forward's settings, the
 * window's scale, the overlay's text. --keys prints that text and ends there.
 *
 * State (the paths, the settings) is this module's own, set once by
 * player_find on the main thread and read for the rest of the run.
 */
#ifndef PLATFORM_PLAYER_H
#define PLATFORM_PLAYER_H

#include <stdbool.h>
#include <stdio.h>

#include "controller.h"
#include "data_dir.h"
#include "fastforward.h"
#include "fullscreen.h"
#include "input.h"
#include "options.h"
#include "overlay.h"

/* Finds the files (data_dir_resolve, asking host for the system's
 * directories) and reads the settings file if the run has one (settings_load,
 * which reports its bad lines on stderr). Returns 0, or -1 after a report on
 * stderr (no data directory; a settings file that cannot be read). */
int player_find(const struct options *options, const struct data_dir_host *host);

/* The save's and the clock's paths, or NULL in test-runner mode, which has
 * none. */
const char *player_save_path(void);
const char *player_rtc_offset_path(void);

/* The keys the settings give the buttons (the defaults without a settings
 * file). */
const struct input_keymap *player_keymap(void);

/* The controller's functions and their buttons (the defaults without a
 * settings file). */
const struct controller_bindings *player_controller(void);

/* Whether the window starts fullscreen, from the settings file and the
 * environment's SteamDeck (fullscreen.h); false for a run without a player at
 * the keyboard. */
bool player_starts_fullscreen(const struct options *options);

/* The fullscreen key (the defaults without a settings file). */
int player_fullscreen_key(void);

/* Fast-forward's keys, speed and sound (the defaults without a settings
 * file). */
const struct fastforward_settings *player_fast_forward(void);

/* The window's scale: --scale if given, else the settings file's, else
 * OPTIONS_DEFAULT_SCALE. */
int player_window_scale(const struct options *options);

/* The overlay's text (overlay_text_writer): help_text.h's lines for this
 * run, the same --keys prints. */
void player_write_help(overlay_line_sink sink, void *context);

/* --keys: the lines to out, one a line. Returns 0, or -1 (reported on
 * stderr) if writing to out failed. */
int player_print_keys(FILE *out);

/* Whether a player is at the keyboard of a window, the runs that get the
 * help overlay and fast-forward: not headless (no window), not test-runner
 * mode (nobody plays) and not a replay (the script plays; neither may change
 * its keys, its frames or its pace). Pure. */
bool player_at_keyboard(const struct options *options);

#endif /* PLATFORM_PLAYER_H */
