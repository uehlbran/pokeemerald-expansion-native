/*
 * input.h: the GBA keypad.
 *
 * Stands in for the keypad, KEYINPUT in the I/O registers. On the GBA the
 * ten buttons are wired to one halfword the game reads whenever it likes
 * (GBATEK "GBA Keypad Input", https://problemkaputt.de/gbatek.htm#gbakeypadinput):
 * KEYINPUT, 4000130h, bits 0-9 are Button A, B, Select, Start, Right, Left,
 * Up, Down, R, L, each "0=Pressed, 1=Released". Bits 10-15 are not used by
 * the hardware; this module reads them as 0, as mGBA does and as expansion
 * assumes (src/main.c ReadKeys XORs with KEYS_MASK, 0x03FF, so it never looks
 * at them). Expansion's A_BUTTON ... L_BUTTON (include/gba/io_reg.h) have the
 * same bit layout as INPUT_KEY_* below; test_input.c checks both.
 *
 * Once per frame, after main has pumped SDL's events, input_update_keyinput()
 * writes KEYINPUT from one of two sources chosen at init:
 *   INPUT_SOURCE_HOST   the SDL keyboard and game controllers;
 *   INPUT_SOURCE_SCRIPT the value last given to input_script_set(), which is
 *                       how the headless input-replay smoke test drives the game.
 * Opposite directions pressed together (Left+Right, Up+Down) are reported
 * as pressed: the GBA's D-pad cannot do it but the hardware register can
 * hold it, and a keyboard or a scripted replay can ask for it. Nothing is
 * filtered; the game decides.
 *
 * Host mapping (SDL scancodes, so non-QWERTY layouts keep the physical
 * positions):
 *   keyboard   the keymap input_init is given (the player's settings file,
 *              settings.h), by default A=Z  B=X  Select=Backspace
 *              Start=Return  L=A  R=S  Right/Left/Up/Down = the arrow keys
 *              (VisualBoyAdvance's defaults)
 *   controller (SDL_GameController, every open controller is OR-ed together)
 *              A=A  B=B  Select=Back  Start=Start  L/R=left/right shoulder
 *              D-pad, and the left stick past INPUT_STICK_DEADZONE
 * The keyboard and the controllers are OR-ed together. While the platform's
 * help overlay is shown (overlay.h), KEYINPUT reads every button released,
 * whatever the source: the keys are the overlay's then.
 * Uses SDL's keyboard, game controller and event API; SDL_GetScancodeFromName
 * and SDL_GetScancodeName name the keys for the settings file and the help.
 * Design: docs/RESEARCH.md, "Input: write REG_KEYINPUT".
 */
#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>
#include <stdint.h>

/* GBATEK "GBA Keypad Input": KEYINPUT bits 0-9. The values are active high
 * here ("pressed"), the inverse of the register. */
#define INPUT_KEY_A UINT16_C(0x0001)
#define INPUT_KEY_B UINT16_C(0x0002)
#define INPUT_KEY_SELECT UINT16_C(0x0004)
#define INPUT_KEY_START UINT16_C(0x0008)
#define INPUT_KEY_RIGHT UINT16_C(0x0010)
#define INPUT_KEY_LEFT UINT16_C(0x0020)
#define INPUT_KEY_UP UINT16_C(0x0040)
#define INPUT_KEY_DOWN UINT16_C(0x0080)
#define INPUT_KEY_R UINT16_C(0x0100)
#define INPUT_KEY_L UINT16_C(0x0200)
/* The ten key bits; bits 10-15 of KEYINPUT are unused. */
#define INPUT_KEYS_MASK UINT16_C(0x03FF)

/* The left stick counts as a direction when its axis (SDL: -32768..32767, +y
 * down) is beyond this magnitude, about half travel. */
#define INPUT_STICK_DEADZONE 16000

/* Controllers held open at once; further ones are reported and ignored. */
#define INPUT_MAX_CONTROLLERS 4

/* The GBA's buttons, KEYINPUT bits 0-9. */
#define INPUT_BUTTON_COUNT 10u

/* What a keymap entry holds for no key: SDL_SCANCODE_UNKNOWN. */
#define INPUT_NO_SCANCODE 0

enum input_source { INPUT_SOURCE_HOST, INPUT_SOURCE_SCRIPT };

/* One GBA button, as input_buttons() lists them. */
struct input_button {
    uint16_t key;         /* one INPUT_KEY_* */
    const char *setting;  /* its name in the settings file: "a", "select", "up", ... */
    const char *name;     /* its name on the screen: "A", "Select", "Up", ... */
    int default_scancode; /* an SDL_Scancode */
};

/* The keyboard key of each button: scancodes[i] presses input_buttons()[i].
 * Two buttons may share a key; a scancode is never INPUT_NO_SCANCODE. */
struct input_keymap {
    int scancodes[INPUT_BUTTON_COUNT];
};

/* A controller's state, as input_pad_to_keys() reads it. 0 = up, non-zero =
 * down; the stick axes are SDL_GameController's left stick. */
struct input_pad_state {
    uint8_t a, b, start, back, left_shoulder, right_shoulder;
    uint8_t dpad_up, dpad_down, dpad_left, dpad_right;
    int16_t left_x, left_y;
};

union SDL_Event;

/* Selects the source and, for HOST, the keymap (NULL: the defaults; copied).
 * Returns 0, or a negative value after printing the reason to stderr
 * (unknown source, a keymap with a scancode outside SDL's range, already
 * initialised, HOST without SDL's events subsystem initialised by the
 * caller, a failed SDL call). HOST opens
 * the game controllers present (up to INPUT_MAX_CONTROLLERS; a pad that
 * cannot be opened, or one too many, is reported on stderr and skipped, and
 * only SDL_NumJoysticks failing fails init); if SDL's game
 * controller subsystem cannot start it says so and carries on keyboard-only.
 * SCRIPT needs no SDL. After a failed init nothing is held. */
int input_init(enum input_source source, const struct input_keymap *keymap);

/* Closes every controller and the controller subsystem. Safe before init,
 * after a failed init, and more than once. Call it before SDL_Quit: SDL
 * frees controllers still held at quit, and this module must not close them
 * afterwards. */
void input_shutdown(void);

/* Writes KEYINPUT (4000130h) from the source. Does nothing if the module is
 * not initialised. HOST reads SDL_GetKeyboardState and the controllers, so
 * the caller pumps SDL's events first. SCRIPT writes the last value given to
 * input_script_set, or "no key pressed" before the first. Either reads as no
 * key pressed while the help overlay is shown (overlay_game_keys). */
void input_update_keyinput(void);

/* SCRIPT source: the active-high key bits for the next update, in KEYINPUT
 * bit order; bits above INPUT_KEYS_MASK are ignored. */
void input_script_set(uint16_t keys_active_high);

/* Controller hot-plug: the caller passes every SDL event it polls. Opens a
 * newly added controller, closes a removed one, ignores other events and any
 * event while not initialised with the HOST source. Returns 0, or a negative
 * value after printing the reason (NULL event, a controller that cannot be
 * opened, no free slot). Removing a controller that is not open is not an
 * error. */
int input_handle_event(const union SDL_Event *event);

/* Pure. The KEYINPUT value for the active-high keys: ~keys in bits 0-9, 0 in
 * bits 10-15. */
uint16_t input_keys_to_keyinput(uint16_t keys_active_high);

/* The ten buttons, INPUT_BUTTON_COUNT entries in KEYINPUT bit order (A, B,
 * Select, Start, Right, Left, Up, Down, R, L). */
const struct input_button *input_buttons(void);

/* Every button on its default_scancode. */
struct input_keymap input_default_keymap(void);

/* The scancode SDL names name, ignoring case ("Z", "return", "Left Shift",
 * "Keypad 8"; SDL_GetScancodeFromName), or INPUT_NO_SCANCODE for a name SDL
 * does not know. A lookup in SDL's table: no subsystem needs to be running. */
int input_scancode_from_name(const char *name);

/* SDL's name of a scancode ("Z", "Return"; SDL_GetScancodeName), or "" for
 * one it has no name for or one out of its range. */
const char *input_scancode_name(int scancode);

/* Pure translators, exposed for tests. keyboard is SDL_GetKeyboardState's
 * array of numkeys entries; a NULL keymap or keyboard, or a scancode past
 * numkeys, reads as not pressed. */
uint16_t input_keyboard_to_keys(const struct input_keymap *keymap, const uint8_t *keyboard, int numkeys);
uint16_t input_pad_to_keys(const struct input_pad_state *pad);


#endif /* INPUT_H */
