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
 * State (the source, the script value, the open controllers) is this
 * module's own and single-threaded: only the main thread calls in.
 */
#include "input.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "gba_memmap.h"

/* GBATEK "GBA Keypad Input": KEYINPUT is at 4000130h, a halfword. */
#define KEYINPUT_OFFSET UINT32_C(0x130)

static const struct input_key_binding keyboard_bindings[] = {
    {SDL_SCANCODE_Z, INPUT_KEY_A, "A"},
    {SDL_SCANCODE_X, INPUT_KEY_B, "B"},
    {SDL_SCANCODE_BACKSPACE, INPUT_KEY_SELECT, "Select"},
    {SDL_SCANCODE_RETURN, INPUT_KEY_START, "Start"},
    {SDL_SCANCODE_RIGHT, INPUT_KEY_RIGHT, "Right"},
    {SDL_SCANCODE_LEFT, INPUT_KEY_LEFT, "Left"},
    {SDL_SCANCODE_UP, INPUT_KEY_UP, "Up"},
    {SDL_SCANCODE_DOWN, INPUT_KEY_DOWN, "Down"},
    {SDL_SCANCODE_S, INPUT_KEY_R, "R"},
    {SDL_SCANCODE_A, INPUT_KEY_L, "L"},
};

static struct {
    bool initialised;
    enum input_source source;
    uint16_t script_keys;
    bool controller_subsystem;
    SDL_GameController *controllers[INPUT_MAX_CONTROLLERS];
} state;

uint16_t input_keys_to_keyinput(uint16_t keys_active_high)
{
    return (uint16_t)(~keys_active_high & INPUT_KEYS_MASK);
}

const struct input_key_binding *input_keyboard_bindings(size_t *count)
{
    if (count != NULL) {
        *count = sizeof keyboard_bindings / sizeof keyboard_bindings[0];
    }
    return keyboard_bindings;
}

uint16_t input_keyboard_to_keys(const uint8_t *keyboard, int numkeys)
{
    uint16_t keys = 0;
    if (keyboard == NULL) {
        return 0;
    }
    for (size_t i = 0; i < sizeof keyboard_bindings / sizeof keyboard_bindings[0]; i++) {
        int scancode = keyboard_bindings[i].scancode;
        if (scancode >= 0 && scancode < numkeys && keyboard[scancode] != 0) {
            keys = (uint16_t)(keys | keyboard_bindings[i].key);
        }
    }
    return keys;
}

uint16_t input_pad_to_keys(const struct input_pad_state *pad)
{
    uint16_t keys = 0;
    if (pad == NULL) {
        return 0;
    }
    if (pad->a != 0)
        keys |= INPUT_KEY_A;
    if (pad->b != 0)
        keys |= INPUT_KEY_B;
    if (pad->back != 0)
        keys |= INPUT_KEY_SELECT;
    if (pad->start != 0)
        keys |= INPUT_KEY_START;
    if (pad->left_shoulder != 0)
        keys |= INPUT_KEY_L;
    if (pad->right_shoulder != 0)
        keys |= INPUT_KEY_R;
    if (pad->dpad_up != 0 || pad->left_y < -INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_UP;
    if (pad->dpad_down != 0 || pad->left_y > INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_DOWN;
    if (pad->dpad_left != 0 || pad->left_x < -INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_LEFT;
    if (pad->dpad_right != 0 || pad->left_x > INPUT_STICK_DEADZONE)
        keys |= INPUT_KEY_RIGHT;
    return keys;
}

static void write_keyinput(uint16_t keys_active_high)
{
    volatile uint16_t *reg = gba_io_u16(KEYINPUT_OFFSET);
    *reg = input_keys_to_keyinput(keys_active_high);
}

static struct input_pad_state read_pad(SDL_GameController *controller)
{
    struct input_pad_state pad = {0};
    pad.a = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_A);
    pad.b = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_B);
    pad.start = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_START);
    pad.back = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_BACK);
    pad.left_shoulder = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    pad.right_shoulder = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    pad.dpad_up = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP);
    pad.dpad_down = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad.dpad_left = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    pad.dpad_right = SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    pad.left_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    pad.left_y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    return pad;
}

static uint16_t read_host_keys(void)
{
    int numkeys = 0;
    const uint8_t *keyboard = SDL_GetKeyboardState(&numkeys);
    static bool reported_no_keyboard_state;
    if (keyboard == NULL && !reported_no_keyboard_state) {
        (void)fprintf(stderr, "input: SDL_GetKeyboardState returned NULL; keyboard reads as no keys\n");
        reported_no_keyboard_state = true;
    }
    uint16_t keys = input_keyboard_to_keys(keyboard, numkeys);
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] != NULL) {
            struct input_pad_state pad = read_pad(state.controllers[i]);
            keys = (uint16_t)(keys | input_pad_to_keys(&pad));
        }
    }
    return keys;
}

static bool controller_is_open(const SDL_GameController *controller)
{
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (state.controllers[i] == controller) {
            return true;
        }
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
    if (controller == NULL) {
        return;
    }
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

int input_init(enum input_source source)
{
    int status;
    if (state.initialised) {
        (void)fprintf(stderr, "input: input_init called twice without input_shutdown\n");
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
    state.script_keys = 0;
    state.initialised = true;
    return 0;
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
}

void input_script_set(uint16_t keys_active_high)
{
    state.script_keys = (uint16_t)(keys_active_high & INPUT_KEYS_MASK);
}

void input_update_keyinput(void)
{
    if (!state.initialised) {
        return;
    }
    write_keyinput(state.source == INPUT_SOURCE_HOST ? read_host_keys() : state.script_keys);
}

int input_handle_event(const union SDL_Event *event)
{
    if (event == NULL) {
        (void)fprintf(stderr, "input: input_handle_event given a NULL event\n");
        return -1;
    }
    if (!state.initialised || state.source != INPUT_SOURCE_HOST) {
        return 0;
    }
    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        return open_controller(event->cdevice.which);
    }
    if (event->type == SDL_CONTROLLERDEVICEREMOVED) {
        close_controller(event->cdevice.which);
    }
    return 0;
}
