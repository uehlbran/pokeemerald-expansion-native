/*
 * overlay.h: the help overlay, which F1 shows over the game.
 *
 * Not hardware: the GBA had nothing like it. It shows the text main gives
 * it, what --keys prints (help_text.h: the version, the keys, the data
 * directory and the files), under the line OVERLAY_HINT, and it is where
 * the player quits: Quit is
 * the platform's, not a patch to the game's Start menu (docs/DECISIONS.md,
 * "Quit lives in the F1 overlay; the default file locations are the per-user
 * directory").
 *
 * F1 shows it and hides it. While it is shown the game runs on but reads
 * every button released (overlay_game_keys, which input.c applies to
 * KEYINPUT, from the keyboard, a controller or a script alike), so that
 * nothing pressed to read it or to quit reaches the game; a button still
 * held at the first read after it hides stays released until it is let go,
 * so that the B that closed it from a controller does not back the game out
 * of a menu next (the loop closes it and reads in one poll).
 * Q then ends the
 * game the way closing the window does (frame.c), by the normal exit path:
 * the game's own save, if one is pending, is stored as at any exit, and the
 * platform writes nothing. F1 is SDL_SCANCODE_F1 (the same key on every
 * layout, refused as a button's key by settings.h); Q is the key that types
 * q (SDLK_q), wherever the layout puts it, so that the hint names the key
 * on the keyboard. Held keys repeat; a repeat does nothing.
 *
 * A controller does the same through controller.h's functions: Y shows and
 * hides it, B hides it, X quits (overlay_handle_controller). The hint line
 * names the keys, or the controller's buttons once the last thing the player
 * pressed was a controller button or a trigger or stick pushed well over
 * (overlay_hint_for_pad), and the keyboard's again at the next key.
 *
 * main starts it only for a run with a window whose keys come from the
 * keyboard: never headless, in test-runner mode or for a replay, so it can
 * neither change a replay's keys nor show in a frame a gate compares. Even
 * shown, it is drawn by video.c over the frame in the window and never into
 * the frame buffer, which the screenshots are taken from.
 *
 * overlay_draw lays the text out and draws it into a canvas (font.h); the
 * texture behind the canvas is video.c's. Apart from reading SDL's event
 * structure, nothing here calls SDL.
 */
#ifndef PLATFORM_OVERLAY_H
#define PLATFORM_OVERLAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL_keycode.h>
#include <SDL_scancode.h>

#include "controller.h"
#include "font.h"

#define OVERLAY_TOGGLE_SCANCODE SDL_SCANCODE_F1
#define OVERLAY_QUIT_KEYCODE SDLK_q

/* The first line, above the help text, for the keyboard; the controller's
 * names its buttons (controller_function_label). */
#define OVERLAY_HINT "Q: quit  F1: close"
#define OVERLAY_PAD_HINT_SIZE 96u

/* The colours, as 32-bit ARGB (video.c's texture): a black that lets the
 * game show through faintly, and white text. */
#define OVERLAY_BACKGROUND_ARGB UINT32_C(0xE0000000)
#define OVERLAY_TEXT_ARGB UINT32_C(0xFFFFFFFF)

enum overlay_request {
    OVERLAY_CONTINUE = 0,
    OVERLAY_QUIT = 1 /* Q while shown: end the game */
};

union SDL_Event;

/* Receives one line of the overlay's text, without a newline. */
typedef void (*overlay_line_sink)(const char *line, void *context);

/* Writes the overlay's text: calls sink once a line (help_text_write, in
 * main). */
typedef void (*overlay_text_writer)(overlay_line_sink sink, void *context);

/* Makes F1 show the overlay, with the text write_text writes, which
 * overlay_draw asks for. Returns 0, or -1 (reported) for NULL or a second
 * init. */
int overlay_init(overlay_text_writer write_text);

/* Hides the overlay and makes F1 do nothing. Safe at any time and twice. */
void overlay_shutdown(void);

/* Whether the overlay is started, and whether it is shown now. */
bool overlay_enabled(void);
bool overlay_shown(void);

/* An SDL event, as the frame loop polls them: F1 pressed shows or hides
 * the overlay, Q pressed while it is shown asks to quit. Anything else, a
 * repeat, any event before init, and NULL do nothing. */
enum overlay_request overlay_handle_event(const union SDL_Event *event);

/* A controller function pressed or released (controller_handle_event's
 * answer): the overlay's Y shows or hides it, B hides it, and X while shown
 * asks to quit. Anything else, a release, and any call before init do
 * nothing. */
enum overlay_request overlay_handle_controller(const struct controller_edge *edge);

/* Whether the player's last input was the controller's, so that the hint
 * names its buttons; false until a controller button is pressed. */
bool overlay_hint_for_pad(void);

/* The buttons the game reads of keys (KEYINPUT bits, active high): all of
 * them, or none while the overlay is shown and, after it hides, none of those
 * still held then. Call it once per frame with that frame's keys. */
uint16_t overlay_game_keys(uint16_t keys);

/* The glyphs' scale for a canvas width pixels wide: a window of scale s is
 * 240s wide, and the text is drawn at s / 2, at least 1, so that a line
 * holds some 80 to 120 characters at every scale from 2 up. */
size_t overlay_glyph_scale(size_t width);

/* Fills the canvas with OVERLAY_BACKGROUND_ARGB and, once started, writes
 * OVERLAY_HINT, a blank line and the help text on it, a margin of a cell
 * and a line from the edges; a line too long for the canvas goes on in the
 * next, and the lines past the bottom are left out. */
void overlay_draw(const struct font_canvas *canvas);

#endif /* PLATFORM_OVERLAY_H */
