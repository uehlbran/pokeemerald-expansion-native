/*
 * controller.h: what a game controller does besides the GBA's ten buttons:
 * fast-forward, the help overlay and its Close and Quit, and fullscreen.
 *
 * Not hardware: the GBA had no such functions, and its buttons are input.h's
 * (A, B, Back as Select, Start, the shoulders, the D-pad and the left stick,
 * as SDL's game-controller layout gives them; the Steam Deck appears to SDL
 * as that layout under Steam Input). Each function here is bound to one
 * control, a button of SDL_GameController or one of its two triggers (axes
 * that this module reads as buttons: pressed past CONTROLLER_TRIGGER_PRESS
 * of their travel, released below CONTROLLER_TRIGGER_RELEASE, the gap so
 * that a trigger hovering at the threshold does not press and release by
 * turns). The defaults suit a Deck's face buttons, which the GBA layout
 * leaves free (X, Y), its triggers and stick clicks:
 *     fast-forward hold    right trigger      toggle  right stick click
 *     help overlay         Y                  close   B (while shown)
 *     quit                 X (while shown)    fullscreen  left trigger
 * and the player may rebind them in the settings file (settings.h) by the
 * names SDL gives the buttons (SDL_GameControllerGetButtonFromString:
 * "x", "leftstick", "dpup", ...) and "lefttrigger" and "righttrigger". A
 * control serves one function; a button bound to a function still does its
 * GBA job too, which is the player's to avoid.
 *
 * The caller passes every SDL event it polls (frame.c) to
 * controller_handle_event, which answers with the press or release of a
 * bound function, and then to the modules that act on it: this module keeps
 * what is held, per controller, from the events alone (so it needs no open
 * controller, and a test can push events), and it is the one place that
 * knows a controller going away takes its held controls with it: SDL sends
 * no button-up for a pulled cable, and a hold must not stay on. The window
 * losing the keyboard's focus releases everything too, as fastforward.h's
 * keys do. Controls held on a controller are told apart by its SDL instance
 * id, up to INPUT_MAX_CONTROLLERS of them.
 *
 * main starts it for the runs that start the overlay (player.h
 * player_at_keyboard); not started, it answers CONTROLLER_NO_FUNCTION to
 * everything. State is this module's own; only the main thread calls in.
 * Uses SDL's game-controller event structures and name tables only.
 */
#ifndef PLATFORM_CONTROLLER_H
#define PLATFORM_CONTROLLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL_gamecontroller.h>

/* SDL's trigger axes run 0 to 32767 (SDL_JOYSTICK_AXIS_MAX): pressed past
 * about half travel, released below about a quarter. */
#define CONTROLLER_TRIGGER_PRESS 16000
#define CONTROLLER_TRIGGER_RELEASE 8000

/* A control is SDL_GameControllerButton's value, or one of the triggers
 * below, which follow the buttons. */
#define CONTROLLER_LEFT_TRIGGER ((int)SDL_CONTROLLER_BUTTON_MAX)
#define CONTROLLER_RIGHT_TRIGGER (CONTROLLER_LEFT_TRIGGER + 1)
#define CONTROLLER_CONTROL_COUNT (CONTROLLER_RIGHT_TRIGGER + 1)
#define CONTROLLER_NO_CONTROL (-1)

enum controller_function {
    CONTROLLER_FAST_FORWARD_HOLD,
    CONTROLLER_FAST_FORWARD_TOGGLE,
    CONTROLLER_OVERLAY,       /* shows and hides the help overlay */
    CONTROLLER_OVERLAY_CLOSE, /* hides it; nothing while it is hidden */
    CONTROLLER_QUIT,          /* ends the game from the shown overlay */
    CONTROLLER_FULLSCREEN,
    CONTROLLER_FUNCTION_COUNT,
    CONTROLLER_NO_FUNCTION = CONTROLLER_FUNCTION_COUNT
};

/* One function, as controller_functions() lists them. */
struct controller_function_info {
    const char *setting; /* its name in the settings file: "pad_overlay", ... */
    const char *name;    /* its name on the screen: "Help overlay", ... */
    int default_control;
};

/* The control of each function: controls[f] serves function f. Never
 * CONTROLLER_NO_CONTROL, and no two are one. */
struct controller_bindings {
    int controls[CONTROLLER_FUNCTION_COUNT];
};

/* A bound function pressed or released. */
struct controller_edge {
    enum controller_function function; /* CONTROLLER_NO_FUNCTION: nothing happened */
    bool pressed;
};

union SDL_Event;

/* The functions, CONTROLLER_FUNCTION_COUNT entries in enum order. */
const struct controller_function_info *controller_functions(void);

/* Every function on its default control. */
struct controller_bindings controller_default_bindings(void);

/* The control SDL's name names ("x", "leftstick", "lefttrigger"), ignoring
 * case, or CONTROLLER_NO_CONTROL. A lookup in SDL's table. */
int controller_control_from_name(const char *name);

/* A control's name in the settings file ("rightstick", "righttrigger"; SDL's
 * for a button), or "" for a value that is no control. */
const char *controller_control_setting(int control);

/* A control's name on the screen: "A", "Y", "Right stick click", "Right
 * trigger", or "" for a value that is no control. */
const char *controller_control_label(int control);

/* Pure: whether bindings are valid (above). */
bool controller_bindings_are_valid(const struct controller_bindings *bindings);

/* Pure: a trigger's state after an axis reading (0..32767), given whether it
 * was pressed: pressed past CONTROLLER_TRIGGER_PRESS, released below
 * CONTROLLER_TRIGGER_RELEASE, otherwise as it was. */
bool controller_trigger_pressed(bool was_pressed, int value);

/* Makes the bindings (copied) work with nothing held. Returns 0, or -1
 * (reported) for NULL, invalid bindings or a second init. */
int controller_init(const struct controller_bindings *bindings);

/* Forgets everything held and makes events do nothing. Safe at any time and
 * twice. */
void controller_shutdown(void);

bool controller_enabled(void);

/* An SDL event, as the frame loop polls them: updates what is held and
 * returns the function it pressed or released, if any. A button already
 * held, a trigger moving within its pressed range, a control no function is
 * bound to, a controller's removal (which only releases what it held), any
 * other event, an event before init and NULL are CONTROLLER_NO_FUNCTION. */
struct controller_edge controller_handle_event(const union SDL_Event *event);

/* Whether any controller holds the function's control now. */
bool controller_held(enum controller_function function);

/* The label of the control the function is bound to (the defaults before
 * init): the hint of the overlay names them. */
const char *controller_function_label(enum controller_function function);

#endif /* PLATFORM_CONTROLLER_H */
