/*
 * input.c: the GBA keypad (see input.h for the contract and the mapping).
 *
 * Hardware: KEYINPUT, GBATEK "GBA Keypad Input". The register is the only
 * thing the game sees; this file decides each frame what it holds. The SDL
 * reads are kept apart from the pure translators (input_*_to_keys) so the
 * mapping is unit-tested without a window. Written from GBATEK and from
 * expansion's own ReadKeys (src/main.c); no code is taken from the reference
 * ports.
 *
 * State (the source, the keymap, the script value, the open controllers) is
 * this module's own and single-threaded: only the main thread calls in.
 */
#include "input.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "controller.h"
#include "gba_memmap.h"
#include "overlay.h"

/* What a controller without a name is called in the bubble menu. */
#define UNNAMED_CONTROLLER "Controller"

/* GBATEK "GBA Keypad Input": KEYINPUT is at 4000130h, a halfword. */
#define KEYINPUT_OFFSET UINT32_C(0x130)

_Static_assert(INPUT_NO_SCANCODE == SDL_SCANCODE_UNKNOWN, "no key is SDL's unknown scancode");

/* KEYINPUT bit order; the default keys are VisualBoyAdvance's. */
static const struct input_button buttons[INPUT_BUTTON_COUNT] = {
    {INPUT_KEY_A, "a", "A", SDL_SCANCODE_Z},
    {INPUT_KEY_B, "b", "B", SDL_SCANCODE_X},
    {INPUT_KEY_SELECT, "select", "Select", SDL_SCANCODE_BACKSPACE},
    {INPUT_KEY_START, "start", "Start", SDL_SCANCODE_RETURN},
    {INPUT_KEY_RIGHT, "right", "Right", SDL_SCANCODE_RIGHT},
    {INPUT_KEY_LEFT, "left", "Left", SDL_SCANCODE_LEFT},
    {INPUT_KEY_UP, "up", "Up", SDL_SCANCODE_UP},
    {INPUT_KEY_DOWN, "down", "Down", SDL_SCANCODE_DOWN},
    {INPUT_KEY_R, "r", "R", SDL_SCANCODE_S},
    {INPUT_KEY_L, "l", "L", SDL_SCANCODE_A},
};

static struct {
    bool initialised;
    enum input_source source;
    struct input_keymap keymap;
    struct input_padmap padmap;
    uint16_t script_keys;
    uint16_t inject_keys;   /* input_inject's keys, held while inject_frames lasts */
    uint32_t inject_frames;
    bool controller_subsystem;
    bool reported_no_keyboard_state; /* once, so a frame loop does not repeat it */
    SDL_GameController *controllers[INPUT_MAX_CONTROLLERS];
} state;

uint16_t input_keys_to_keyinput(uint16_t keys_active_high)
{
    return (uint16_t)(~keys_active_high & INPUT_KEYS_MASK);
}

const struct input_button *input_buttons(void)
{
    return buttons;
}

struct input_keymap input_default_keymap(void)
{
    struct input_keymap keymap;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++)
        keymap.scancodes[i] = buttons[i].default_scancode;
    keymap.debug_menu = INPUT_DEBUG_MENU_DEFAULT_SCANCODE;
    return keymap;
}

int input_scancode_from_name(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return INPUT_NO_SCANCODE; /* SDL would set its error for nothing */
    return (int)SDL_GetScancodeFromName(name);
}

const char *input_scancode_name(int scancode)
{
    if (scancode <= INPUT_NO_SCANCODE || scancode >= SDL_NUM_SCANCODES)
        return "";
    return SDL_GetScancodeName((SDL_Scancode)scancode);
}

uint16_t input_keyboard_to_keys(const struct input_keymap *keymap, const uint8_t *keyboard, int numkeys)
{
    uint16_t keys = 0;
    if (keymap == NULL || keyboard == NULL)
        return 0;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        const int scancode = keymap->scancodes[i];
        if (scancode >= 0 && scancode < numkeys && keyboard[scancode] != 0)
            keys = (uint16_t)(keys | buttons[i].key);
    }
    const int debug_menu = keymap->debug_menu;
    if (debug_menu > INPUT_NO_SCANCODE && debug_menu < numkeys && keyboard[debug_menu] != 0)
        keys = (uint16_t)(keys | INPUT_KEY_R | INPUT_KEY_START);
    return keys;
}

/* Every scancode one SDL could report; the debug menu's may be none. */
static bool keymap_is_valid(const struct input_keymap *keymap)
{
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (keymap->scancodes[i] <= INPUT_NO_SCANCODE || keymap->scancodes[i] >= SDL_NUM_SCANCODES)
            return false;
    }
    return keymap->debug_menu >= INPUT_NO_SCANCODE && keymap->debug_menu < SDL_NUM_SCANCODES;
}

_Static_assert(INPUT_DEBUG_MENU_DEFAULT_SCANCODE == SDL_SCANCODE_F4, "the debug menu's default key is F4");

struct input_padmap input_default_padmap(void)
{
    static const int defaults[INPUT_BUTTON_COUNT] = {
        SDL_CONTROLLER_BUTTON_A,          SDL_CONTROLLER_BUTTON_B,         SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_START,      SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_UP,    SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    };
    struct input_padmap padmap;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++)
        padmap.controls[i] = defaults[i];
    return padmap;
}

bool input_padmap_is_valid(const struct input_padmap *padmap)
{
    if (padmap == NULL)
        return false;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (padmap->controls[i] < 0 || padmap->controls[i] >= CONTROLLER_CONTROL_COUNT)
            return false;
    }
    return true;
}

uint16_t input_pad_controls_to_keys(const struct input_padmap *padmap, const uint8_t *controls, size_t count,
                                    int16_t left_x, int16_t left_y)
{
    uint16_t keys = 0;
    if (padmap == NULL || controls == NULL)
        return 0;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        const int control = padmap->controls[i];
        if (control >= 0 && (size_t)control < count && controls[control] != 0)
            keys = (uint16_t)(keys | buttons[i].key);
    }
    if (left_y < -INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_UP;
    if (left_y > INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_DOWN;
    if (left_x < -INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_LEFT;
    if (left_x > INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_RIGHT;
    return keys;
}

uint16_t input_pad_to_keys(const struct input_pad_state *pad)
{
    if (pad == NULL)
        return 0;
    uint8_t controls[CONTROLLER_CONTROL_COUNT] = {0};
    controls[SDL_CONTROLLER_BUTTON_A] = pad->a;
    controls[SDL_CONTROLLER_BUTTON_B] = pad->b;
    controls[SDL_CONTROLLER_BUTTON_BACK] = pad->back;
    controls[SDL_CONTROLLER_BUTTON_START] = pad->start;
    controls[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = pad->left_shoulder;
    controls[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = pad->right_shoulder;
    controls[SDL_CONTROLLER_BUTTON_DPAD_UP] = pad->dpad_up;
    controls[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = pad->dpad_down;
    controls[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = pad->dpad_left;
    controls[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = pad->dpad_right;
    const struct input_padmap defaults = input_default_padmap();
    return input_pad_controls_to_keys(&defaults, controls, CONTROLLER_CONTROL_COUNT, pad->left_x, pad->left_y);
}

static void write_keyinput(uint16_t keys_active_high)
{
    volatile uint16_t *reg = gba_io_u16(KEYINPUT_OFFSET);
    *reg = input_keys_to_keyinput(keys_active_high);
}

/* The keys one controller presses through the controller map: every button
 * and both triggers (pressed past CONTROLLER_TRIGGER_PRESS), and the stick. */
static uint16_t read_controller(SDL_GameController *controller)
{
    uint8_t controls[CONTROLLER_CONTROL_COUNT] = {0};
    for (int button = 0; button < (int)SDL_CONTROLLER_BUTTON_MAX; button++)
        controls[button] = SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)button);
    controls[CONTROLLER_LEFT_TRIGGER] =
        SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > CONTROLLER_TRIGGER_PRESS;
    controls[CONTROLLER_RIGHT_TRIGGER] =
        SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > CONTROLLER_TRIGGER_PRESS;
    return input_pad_controls_to_keys(&state.padmap, controls, CONTROLLER_CONTROL_COUNT,
                                      SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX),
                                      SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
}

static uint16_t read_host_keys(void)
{
    int numkeys = 0;
    const uint8_t *keyboard = SDL_GetKeyboardState(&numkeys);
    if (keyboard == NULL && !state.reported_no_keyboard_state) {
        (void)fprintf(stderr, "input: SDL_GetKeyboardState returned NULL; keyboard reads as no keys\n");
        state.reported_no_keyboard_state = true;
    }
    uint16_t keys = input_keyboard_to_keys(&state.keymap, keyboard, numkeys);
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] != NULL)
            keys = (uint16_t)(keys | read_controller(state.controllers[i]));
    }
    return keys;
}

static bool controller_is_open(const SDL_GameController *controller)
{
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] == controller)
            return true;
    }
    return false;
}

static int open_controller(int device_index)
{
    SDL_GameController *controller = SDL_GameControllerOpen(device_index);
    if (controller == NULL) {
        (void)fprintf(stderr, "input: SDL_GameControllerOpen(%d) failed: %s\n", device_index, SDL_GetError());
        return -1;
    }
    if (controller_is_open(controller)) {
        /* SDL returns the same object, with a reference count, for a device
         * already open (it also announces present devices as "added"). */
        SDL_GameControllerClose(controller);
        return 0;
    }
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] == NULL) {
            state.controllers[i] = controller;
            return 0;
        }
    }
    (void)fprintf(stderr, "input: more than %d controllers; ignoring device %d\n", INPUT_MAX_CONTROLLERS, device_index);
    SDL_GameControllerClose(controller);
    return -1;
}

static void close_controller(SDL_JoystickID instance_id)
{
    SDL_GameController *controller = SDL_GameControllerFromInstanceID(instance_id);
    if (controller == NULL)
        return;
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] == controller) {
            SDL_GameControllerClose(controller);
            state.controllers[i] = NULL;
        }
    }
}

static int open_present_controllers(void)
{
    int count = SDL_NumJoysticks();
    if (count < 0) {
        (void)fprintf(stderr, "input: SDL_NumJoysticks failed: %s\n", SDL_GetError());
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (SDL_IsGameController(i) == SDL_TRUE) {
            /* A pad that cannot be opened, or one too many, is reported by
             * open_controller and skipped: the keyboard still works. */
            (void)open_controller(i);
        }
    }
    return 0;
}

static int init_host(void)
{
    if (SDL_WasInit(SDL_INIT_EVENTS) == 0) {
        (void)fprintf(stderr, "input: SDL's events subsystem is not initialised; main must SDL_Init first\n");
        return -1;
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        (void)fprintf(stderr, "input: SDL_InitSubSystem(GAMECONTROLLER) failed, keyboard only: %s\n", SDL_GetError());
        return 0;
    }
    state.controller_subsystem = true;
    return open_present_controllers();
}

int input_init(enum input_source source, const struct input_keymap *keymap)
{
    int status;
    if (state.initialised) {
        (void)fprintf(stderr, "input: input_init called twice without input_shutdown\n");
        return -1;
    }
    if (keymap != NULL && !keymap_is_valid(keymap)) {
        (void)fprintf(stderr, "input: input_init given a keymap with a scancode outside 1..%d\n",
                      SDL_NUM_SCANCODES - 1);
        return -1;
    }
    if (source == INPUT_SOURCE_HOST) {
        status = init_host();
    } else if (source == INPUT_SOURCE_SCRIPT) {
        status = 0;
    } else {
        (void)fprintf(stderr, "input: unknown input source %d\n", (int)source);
        status = -1;
    }
    if (status != 0) {
        input_shutdown();
        return status;
    }
    state.source = source;
    state.keymap = keymap != NULL ? *keymap : input_default_keymap();
    state.padmap = input_default_padmap();
    state.script_keys = 0;
    state.inject_keys = 0;
    state.inject_frames = 0;
    state.initialised = true;
    return 0;
}

int input_set_keymap(const struct input_keymap *keymap)
{
    if (!state.initialised || keymap == NULL || !keymap_is_valid(keymap))
        return -1;
    state.keymap = *keymap;
    return 0;
}

int input_set_padmap(const struct input_padmap *padmap)
{
    if (!state.initialised || !input_padmap_is_valid(padmap))
        return -1;
    state.padmap = *padmap;
    return 0;
}

struct input_keymap input_current_keymap(void)
{
    return state.initialised ? state.keymap : input_default_keymap();
}

struct input_padmap input_current_padmap(void)
{
    return state.initialised ? state.padmap : input_default_padmap();
}

void input_inject(uint16_t keys, uint32_t frames)
{
    if (!state.initialised)
        return;
    state.inject_keys = (uint16_t)(keys & INPUT_KEYS_MASK);
    state.inject_frames = frames < INPUT_MAX_INJECT_FRAMES ? frames : INPUT_MAX_INJECT_FRAMES;
}

size_t input_controller_names(const char **names, size_t max)
{
    size_t count = 0;
    if (!state.initialised || state.source != INPUT_SOURCE_HOST || names == NULL)
        return 0;
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS && count < max; i++) {
        if (state.controllers[i] == NULL)
            continue;
        const char *const name = SDL_GameControllerName(state.controllers[i]);
        names[count++] = name != NULL ? name : UNNAMED_CONTROLLER;
    }
    return count;
}

void input_shutdown(void)
{
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] != NULL) {
            SDL_GameControllerClose(state.controllers[i]);
            state.controllers[i] = NULL;
        }
    }
    if (state.controller_subsystem) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        state.controller_subsystem = false;
    }
    state.initialised = false;
    state.script_keys = 0;
    state.inject_keys = 0;
    state.inject_frames = 0;
    state.reported_no_keyboard_state = false;
}

void input_script_set(uint16_t keys_active_high)
{
    state.script_keys = (uint16_t)(keys_active_high & INPUT_KEYS_MASK);
}

void input_update_keyinput(void)
{
    if (!state.initialised)
        return;
    const uint16_t keys = state.source == INPUT_SOURCE_HOST ? read_host_keys() : state.script_keys;
    uint16_t game_keys = overlay_game_keys(keys);
    if (state.inject_frames > 0) {
        game_keys = (uint16_t)(game_keys | state.inject_keys);
        state.inject_frames--;
    }
    write_keyinput(game_keys);
}

int input_handle_event(const union SDL_Event *event)
{
    if (event == NULL) {
        (void)fprintf(stderr, "input: input_handle_event given a NULL event\n");
        return -1;
    }
    if (!state.initialised || state.source != INPUT_SOURCE_HOST)
        return 0;
    if (event->type == SDL_CONTROLLERDEVICEADDED)
        return open_controller(event->cdevice.which);
    if (event->type == SDL_CONTROLLERDEVICEREMOVED)
        close_controller(event->cdevice.which);
    return 0;
}
