/*
 * controller.c: the controller's functions beyond the GBA buttons (controller.h).
 *
 * Written from the behaviour controller.h fixes; the control names are SDL's
 * (SDL_GameControllerGetButtonFromString, SDL_GameControllerGetStringForButton).
 */
#include "controller.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL_events.h>

#include "input.h"

#define MODULE "controller"

#define LEFT_TRIGGER_SETTING "lefttrigger"
#define RIGHT_TRIGGER_SETTING "righttrigger"

/* Room for a control's name in the settings file and its NUL: SDL's longest
 * button name is "rightshoulder". */
#define NAME_SIZE 32u

/* The held controls of a controller are bits of a uint32_t, one a control. */
_Static_assert(CONTROLLER_CONTROL_COUNT <= 32, "every control has a bit in a controller's held word");

static const struct controller_function_info functions[CONTROLLER_FUNCTION_COUNT] = {
    [CONTROLLER_FAST_FORWARD_HOLD] = {"pad_fast_forward", "Fast-forward hold", CONTROLLER_RIGHT_TRIGGER},
    [CONTROLLER_FAST_FORWARD_TOGGLE] = {"pad_fast_forward_toggle", "Fast-forward toggle",
                                        SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    [CONTROLLER_OVERLAY] = {"pad_overlay", "Help overlay", SDL_CONTROLLER_BUTTON_Y},
    [CONTROLLER_OVERLAY_CLOSE] = {"pad_overlay_close", "Overlay close", SDL_CONTROLLER_BUTTON_B},
    [CONTROLLER_QUIT] = {"pad_quit", "Overlay quit", SDL_CONTROLLER_BUTTON_X},
    [CONTROLLER_FULLSCREEN] = {"pad_fullscreen", "Fullscreen", CONTROLLER_LEFT_TRIGGER},
};

/* The names on the screen of the controls whose SDL names are cryptic; any
 * other shows SDL's. */
static const char *const labels[CONTROLLER_CONTROL_COUNT] = {
    [SDL_CONTROLLER_BUTTON_A] = "A",
    [SDL_CONTROLLER_BUTTON_B] = "B",
    [SDL_CONTROLLER_BUTTON_X] = "X",
    [SDL_CONTROLLER_BUTTON_Y] = "Y",
    [SDL_CONTROLLER_BUTTON_BACK] = "Back",
    [SDL_CONTROLLER_BUTTON_GUIDE] = "Guide",
    [SDL_CONTROLLER_BUTTON_START] = "Start",
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = "Left stick click",
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = "Right stick click",
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = "Left shoulder",
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = "Right shoulder",
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = "D-pad up",
    [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = "D-pad down",
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = "D-pad left",
    [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = "D-pad right",
    [CONTROLLER_LEFT_TRIGGER] = "Left trigger",
    [CONTROLLER_RIGHT_TRIGGER] = "Right trigger",
};

/* What one controller holds, found by SDL's instance id. */
struct slot {
    bool used;
    SDL_JoystickID id;
    uint32_t held; /* bit n: control n is down */
};

static struct {
    bool enabled;
    struct controller_bindings bindings;
    struct slot slots[INPUT_MAX_CONTROLLERS];
} controller;

const struct controller_function_info *controller_functions(void)
{
    return functions;
}

struct controller_bindings controller_default_bindings(void)
{
    struct controller_bindings bindings;
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++)
        bindings.controls[i] = functions[i].default_control;
    return bindings;
}

static bool control_is_valid(int control)
{
    return control >= 0 && control < CONTROLLER_CONTROL_COUNT;
}

int controller_control_from_name(const char *name)
{
    char lower[NAME_SIZE];
    if (name == NULL || strlen(name) >= sizeof lower)
        return CONTROLLER_NO_CONTROL;
    size_t i = 0;
    for (; name[i] != '\0'; i++)
        lower[i] = (char)tolower((unsigned char)name[i]); /* the C locale's: ASCII letters only */
    lower[i] = '\0';
    if (strcmp(lower, LEFT_TRIGGER_SETTING) == 0)
        return CONTROLLER_LEFT_TRIGGER;
    if (strcmp(lower, RIGHT_TRIGGER_SETTING) == 0)
        return CONTROLLER_RIGHT_TRIGGER;
    const SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(lower);
    return button == SDL_CONTROLLER_BUTTON_INVALID ? CONTROLLER_NO_CONTROL : (int)button;
}

const char *controller_control_setting(int control)
{
    if (!control_is_valid(control))
        return "";
    if (control == CONTROLLER_LEFT_TRIGGER)
        return LEFT_TRIGGER_SETTING;
    if (control == CONTROLLER_RIGHT_TRIGGER)
        return RIGHT_TRIGGER_SETTING;
    const char *const name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)control);
    return name != NULL ? name : "";
}

const char *controller_control_label(int control)
{
    if (!control_is_valid(control))
        return "";
    return labels[control] != NULL ? labels[control] : controller_control_setting(control);
}

bool controller_bindings_are_valid(const struct controller_bindings *bindings)
{
    if (bindings == NULL)
        return false;
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        if (!control_is_valid(bindings->controls[i]))
            return false;
        for (size_t j = 0; j < i; j++) {
            if (bindings->controls[j] == bindings->controls[i])
                return false;
        }
    }
    return true;
}

bool controller_trigger_pressed(bool was_pressed, int value)
{
    if (value > CONTROLLER_TRIGGER_PRESS)
        return true;
    if (value < CONTROLLER_TRIGGER_RELEASE)
        return false;
    return was_pressed;
}

int controller_init(const struct controller_bindings *bindings)
{
    if (!controller_bindings_are_valid(bindings) || controller.enabled) {
        (void)fprintf(stderr, MODULE ": controller_init needs valid bindings and is called once\n");
        return -1;
    }
    controller = (__typeof__(controller)){.enabled = true, .bindings = *bindings};
    return 0;
}

void controller_shutdown(void)
{
    controller = (__typeof__(controller)){0};
}

bool controller_enabled(void)
{
    return controller.enabled;
}

static uint32_t control_bit(int control)
{
    return UINT32_C(1) << (unsigned)control;
}

/* The slot of the controller with this id, taking a free one for a new id;
 * NULL when all are taken by others. */
static struct slot *slot_for(SDL_JoystickID id)
{
    struct slot *free_slot = NULL;
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        struct slot *const slot = &controller.slots[i];
        if (slot->used && slot->id == id)
            return slot;
        if (!slot->used && free_slot == NULL)
            free_slot = slot;
    }
    if (free_slot != NULL)
        *free_slot = (struct slot){.used = true, .id = id};
    return free_slot;
}

static void forget(SDL_JoystickID id)
{
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (controller.slots[i].used && controller.slots[i].id == id)
            controller.slots[i] = (struct slot){0};
    }
}

static void forget_all(void)
{
    memset(controller.slots, 0, sizeof controller.slots);
}

static enum controller_function function_of(int control)
{
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        if (controller.bindings.controls[i] == control)
            return (enum controller_function)i;
    }
    return CONTROLLER_NO_FUNCTION;
}

/* A control of the controller id going down or up. */
static struct controller_edge move_control(SDL_JoystickID id, int control, bool down)
{
    const struct controller_edge none = {.function = CONTROLLER_NO_FUNCTION};
    struct slot *const slot = slot_for(id);
    if (slot == NULL || !control_is_valid(control) || ((slot->held & control_bit(control)) != 0) == down)
        return none;
    slot->held ^= control_bit(control);
    return (struct controller_edge){.function = function_of(control), .pressed = down};
}

static struct controller_edge move_trigger(const SDL_ControllerAxisEvent *axis)
{
    const struct controller_edge none = {.function = CONTROLLER_NO_FUNCTION};
    int control = CONTROLLER_NO_CONTROL;
    if (axis->axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
        control = CONTROLLER_LEFT_TRIGGER;
    else if (axis->axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        control = CONTROLLER_RIGHT_TRIGGER;
    else
        return none;
    const struct slot *const slot = slot_for(axis->which);
    if (slot == NULL)
        return none;
    const bool was_pressed = (slot->held & control_bit(control)) != 0;
    return move_control(axis->which, control, controller_trigger_pressed(was_pressed, axis->value));
}

struct controller_edge controller_handle_event(const union SDL_Event *event)
{
    const struct controller_edge none = {.function = CONTROLLER_NO_FUNCTION};
    if (!controller.enabled || event == NULL)
        return none;
    switch (event->type) {
    case SDL_CONTROLLERBUTTONDOWN:
        return move_control(event->cbutton.which, event->cbutton.button, true);
    case SDL_CONTROLLERBUTTONUP:
        return move_control(event->cbutton.which, event->cbutton.button, false);
    case SDL_CONTROLLERAXISMOTION:
        return move_trigger(&event->caxis);
    case SDL_CONTROLLERDEVICEREMOVED:
        forget(event->cdevice.which);
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            forget_all();
        break;
    default:
        break;
    }
    return none;
}

bool controller_held(enum controller_function function)
{
    if (!controller.enabled || (unsigned)function >= CONTROLLER_FUNCTION_COUNT)
        return false;
    const uint32_t bit = control_bit(controller.bindings.controls[function]);
    for (size_t i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
        if (controller.slots[i].used && (controller.slots[i].held & bit) != 0)
            return true;
    }
    return false;
}

const char *controller_function_label(enum controller_function function)
{
    if ((unsigned)function >= CONTROLLER_FUNCTION_COUNT)
        return "";
    return controller_control_label(controller.enabled ? controller.bindings.controls[function]
                                                       : functions[function].default_control);
}
