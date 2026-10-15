/*
 * fullscreen.h: when the window starts fullscreen, and the key that toggles
 * it.
 *
 * Not hardware: a GBA's screen filled the case. On a Steam Deck the game
 * should fill the 1280x800 screen at once, with no desktop window to
 * maximise in Game Mode; elsewhere the window is the usual one until the
 * player asks. The window is video.c's (video_set_fullscreen toggles it as
 * desktop fullscreen, at the screen's own resolution, under the same logical
 * size, integer scale and letterbox as the window); this module holds the
 * decisions.
 *
 * SteamOS sets the environment variable SteamDeck to 1 for what it runs
 *
 * fullscreen_on_steam_deck reads exactly that, so another value, "0"
 * included, is not a Deck. The settings file's `fullscreen` (settings.h)
 * says yes or no, or leaves it to the machine (auto): fullscreen on a Deck,
 * a window elsewhere. The toggle key is F11 unless the settings move it; F1
 * stays the overlay's. Unlike fast-forward's keys, the key and the left
 * trigger work while the help overlay is shown. A press that repeats does nothing, and nothing here
 * acts before fullscreen_init, which main calls for the runs that start the
 * overlay (player.h player_at_keyboard), so that a replay is never
 * fullscreen. Apart from reading SDL's event structure and its scancodes,
 * nothing here calls SDL; state is this module's own and only the main thread
 * calls in.
 */
#ifndef PLATFORM_FULLSCREEN_H
#define PLATFORM_FULLSCREEN_H

#include <stdbool.h>

#include <SDL_scancode.h>

#define FULLSCREEN_DEFAULT_SCANCODE SDL_SCANCODE_F11

/* The environment variable SteamOS sets, and the value that means a Deck. */
#define FULLSCREEN_STEAM_DECK_VARIABLE "SteamDeck"

enum fullscreen_setting { FULLSCREEN_AUTO, FULLSCREEN_YES, FULLSCREEN_NO };

union SDL_Event;

/* Pure: whether the environment's SteamDeck (NULL: not set) is "1". */
bool fullscreen_on_steam_deck(const char *steam_deck);

/* Pure: whether the window starts fullscreen. */
bool fullscreen_at_start(enum fullscreen_setting setting, bool on_steam_deck);

/* Makes scancode (never SDL_SCANCODE_UNKNOWN) the toggle key. Returns 0, or
 * -1 (reported) for an unknown scancode or a second init. */
int fullscreen_init(int scancode);

/* Makes the key do nothing. Safe at any time and twice. */
void fullscreen_shutdown(void);

/* Whether event is the toggle key's press (not a repeat); false before init
 * and for NULL. */
bool fullscreen_toggle_requested(const union SDL_Event *event);

#endif /* PLATFORM_FULLSCREEN_H */
