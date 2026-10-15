/*
 * fastforward.h: fast-forward, the game run faster than the GBA ran it,
 * while a key is held or after a key switches it on.
 *
 * Not hardware: a GBA ran at one speed. Emulators let the player hurry
 * through the slow parts (Tab held, in mGBA and VisualBoyAdvance), and so
 * does this platform, in the frame pacer alone (frame_pacer.h): the period
 * it waits is divided by the speed, so the game runs the same frames, with
 * the same logic, closer together; at FASTFORWARD_UNLIMITED it does not wait.
 * The clock (rtc.h) reads the host's time, not the frame count, so it keeps
 * the time of day; what the game counts in frames, its play time, runs with
 * the frames, as on an emulator.
 *
 * The keys come from the player's settings file (settings.h): the hold key
 * (Tab) fast-forwards while it is held, the toggle key (Shift+Tab) switches
 * fast-forward on and off. A key is a scancode and the modifiers held with
 * it, Shift, Ctrl and Alt on either side; a press is the key's when its
 * scancode is the key's and exactly the key's modifiers are held, not
 * counting Caps Lock, Num Lock, the GUI key and SDL's Mode key (KMOD_MODE),
 * nor a modifier key's own press, so that Tab and Shift+Tab are two keys and
 * Left Shift alone can be one. AltGr is not set apart: SDL reports it as
 * Right Alt (with Left Ctrl on Windows), so it counts as Alt (and Ctrl).
 * Presses that repeat do nothing.
 *
 * The hold ends when its scancode comes up, whatever the modifiers are by
 * then, and when the window loses the keyboard's focus, whose key-up would
 * go to another window: the game cannot stay fast-forwarded by a key no
 * longer held. The toggle stays on until it is pressed again (or the game
 * ends). While the help overlay is shown (overlay.h) a press is the
 * overlay's and changes nothing; a release still ends the hold, for the same
 * reason; a toggle switched on stays on, and the game runs on fast under the
 * overlay, which hides the indicator.
 *
 * A controller does the same through controller.h's two functions, hold
 * (the right trigger) and toggle (a click of the right stick), passed in as
 * the edges controller_handle_event answers (fastforward_handle_controller,
 * called after every event): a press is the overlay's while it is shown, as
 * a key's is; the hold ends with the release, with the controller going away
 * (SDL sends no release for a pulled cable) and with the window losing the
 * focus, because it lasts only as long as controller_held says some
 * controller holds the control.
 *
 * The indicator, "FF 2x", "FF 3x", "FF 4x" or "FF max", is drawn in the
 * screen's top-right corner while fast-forward is on (fastforward_draw_indicator
 * into a canvas; video.c owns the texture and draws it over the frame, never
 * into the frame buffer the screenshots come from).
 *
 * main starts it only where it starts the overlay, for a window played from
 * the keyboard (player_at_keyboard): never headless, in test-runner mode or
 * for a replay, so no gate and no replay is ever fast-forwarded. Apart from
 * reading SDL's event structure and its key names (input.h), nothing here
 * calls SDL; the state is this module's own, and only the main thread calls in.
 */
#ifndef PLATFORM_FASTFORWARD_H
#define PLATFORM_FASTFORWARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "controller.h"
#include "font.h"
#include "frame_pacer.h"

/* A key's modifiers, each either side of the keyboard. */
#define FASTFORWARD_SHIFT 0x1u
#define FASTFORWARD_CTRL 0x2u
#define FASTFORWARD_ALT 0x4u

/* The speeds, as frame_pacer multipliers: 2 to FASTFORWARD_MAX_SPEED times
 * the GBA's, or as fast as the host runs. */
#define FASTFORWARD_MIN_SPEED 2u
#define FASTFORWARD_MAX_SPEED FRAME_PACER_MAX_MULTIPLIER
#define FASTFORWARD_UNLIMITED FRAME_PACER_UNLIMITED
#define FASTFORWARD_DEFAULT_SPEED 2u

/* Room for a key's name, "Ctrl+Alt+Shift+" and SDL's longest key name, and
 * for the indicator's text, with their NULs. */
#define FASTFORWARD_KEY_NAME_SIZE 64u
#define FASTFORWARD_INDICATOR_TEXT_SIZE 8u

/* The indicator's place in the 240x160 screen: a pixel of background around
 * the text, two pixels from the top and right edges. */
#define FASTFORWARD_INDICATOR_PADDING 1u
#define FASTFORWARD_INDICATOR_MARGIN 2u

union SDL_Event;

struct fastforward_key {
    int scancode;       /* an SDL_Scancode, never INPUT_NO_SCANCODE */
    unsigned modifiers; /* FASTFORWARD_SHIFT, _CTRL, _ALT, or-ed */
};

/* What the sound does while fast-forward is on: plays sped up (frame.c says
 * how), or stops, the device paused. */
enum fastforward_audio { FASTFORWARD_AUDIO_PLAY, FASTFORWARD_AUDIO_MUTE };

struct fastforward_settings {
    struct fastforward_key hold;
    struct fastforward_key toggle; /* never the same key as hold */
    uint32_t speed;                /* FASTFORWARD_MIN_SPEED..FASTFORWARD_MAX_SPEED or FASTFORWARD_UNLIMITED */
    enum fastforward_audio audio;
};

/* An area of the screen, in LCD pixels. */
struct fastforward_rect {
    size_t x;
    size_t y;
    size_t width;
    size_t height;
};

/* Tab and Shift+Tab, FASTFORWARD_DEFAULT_SPEED, the sound played. */
struct fastforward_settings fastforward_defaults(void);

/* The key name names: an SDL key name ("Tab", "F2", "Keypad +";
 * input_scancode_from_name), after any of "shift+", "ctrl+" and "alt+", in
 * any case and order, with blanks around each '+' ("Shift + Tab", "ctrl+alt+f").
 * False, *out untouched, if SDL knows no such key or a '+' has no key after it. */
bool fastforward_key_from_name(const char *name, struct fastforward_key *out);

/* The key's name as the help shows it: "Ctrl+", "Alt+", "Shift+" in that
 * order, then SDL's name ("Shift+Tab"), cut to fit size. */
void fastforward_key_name(const struct fastforward_key *key, char *out, size_t size);

bool fastforward_keys_equal(const struct fastforward_key *a, const struct fastforward_key *b);

/* Pure: whether a press of scancode, with SDL's modifier state then
 * (SDL_Keysym.mod, KMOD_*), is key (above). */
bool fastforward_key_pressed(const struct fastforward_key *key, int scancode, uint16_t sdl_modifiers);

/* Makes the keys work, with settings (copied); off until a key is pressed.
 * Returns 0, or -1 (reported) for NULL, a second init, or settings with a
 * speed that is not one or the same key twice. */
int fastforward_init(const struct fastforward_settings *settings);

/* Turns it off and makes the keys do nothing. Safe at any time and twice. */
void fastforward_shutdown(void);

bool fastforward_enabled(void);

/* An SDL event, as the frame loop polls them (above). Anything else, any
 * event before init, and NULL do nothing. */
void fastforward_handle_event(const union SDL_Event *event);

/* A controller's edge, as controller_handle_event answered it for an
 * event; call it after every event, whatever the answer, so that a hold
 * whose control is no longer held ends. Before init it does nothing. */
void fastforward_handle_controller(const struct controller_edge *edge);

/* Whether fast-forward is on: held or toggled on. */
bool fastforward_active(void);

/* The pacer's multiplier for the frames from now on: the speed while
 * fast-forward is on, else FRAME_PACER_NORMAL_SPEED. */
uint32_t fastforward_multiplier(void);

/* Whether the sound is off now: fast-forward on with FASTFORWARD_AUDIO_MUTE. */
bool fastforward_muted(void);

/* Whether the indicator shows now: fast-forward on and the overlay hidden. */
bool fastforward_indicator_shown(void);

/* The indicator's text for a speed: "FF 2x" for 2, ..., "FF max" for
 * FASTFORWARD_UNLIMITED, into out (FASTFORWARD_INDICATOR_TEXT_SIZE bytes). */
void fastforward_indicator_text(uint32_t speed, char *out, size_t size);

/* Where the indicator for text goes: its box, the text and the padding
 * around it, in the screen's top-right corner (above). */
struct fastforward_rect fastforward_indicator_place(const char *text);

/* The started speed's indicator's box (fastforward_indicator_place). */
struct fastforward_rect fastforward_indicator_box(void);

/* The started speed's indicator, filling a canvas of its box's size: the
 * overlay's background and its text colour, the text a padding from the
 * top-left. Before init, the background only. */
void fastforward_draw_indicator(const struct font_canvas *canvas);

#endif /* PLATFORM_FASTFORWARD_H */
