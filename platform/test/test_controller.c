/*
 * test_controller.c: the controller's functions (controller.c): the default
 * bindings, SDL's names for the controls and the two triggers, the bindings'
 * validity, the trigger's threshold with its hysteresis, and the events: a
 * press and a release as one edge each, a held button or a trigger moving
 * about its pressed range as none, the hold going when a controller is
 * removed or the window loses the focus, and several controllers at once.
 * Events are built by hand, as SDL would send them.
 */
#include "controller.h"
#include "input.h"
#include "ptest.h"

#include <SDL_events.h>
#include <stdbool.h>

#define PAD_ONE 7
#define PAD_TWO 8

static SDL_Event button_event(Uint32 type, SDL_JoystickID which, SDL_GameControllerButton button)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.cbutton.which = which;
    event.cbutton.button = (Uint8)button;
    return event;
}

static SDL_Event axis_event(SDL_JoystickID which, SDL_GameControllerAxis axis, Sint16 value)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.which = which;
    event.caxis.axis = (Uint8)axis;
    event.caxis.value = value;
    return event;
}

static SDL_Event removed_event(SDL_JoystickID which)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERDEVICEREMOVED;
    event.cdevice.which = which;
    return event;
}

static struct controller_edge send(const SDL_Event event)
{
    return controller_handle_event(&event);
}

static struct controller_edge press(SDL_JoystickID which, SDL_GameControllerButton button)
{
    return send(button_event(SDL_CONTROLLERBUTTONDOWN, which, button));
}

static struct controller_edge release(SDL_JoystickID which, SDL_GameControllerButton button)
{
    return send(button_event(SDL_CONTROLLERBUTTONUP, which, button));
}

static struct controller_edge right_trigger(SDL_JoystickID which, Sint16 value)
{
    return send(axis_event(which, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, value));
}

static void start(void)
{
    const struct controller_bindings defaults = controller_default_bindings();
    controller_shutdown();
    PT_ASSERT_EQ_INT(controller_init(&defaults), 0);
}

/* ---- the bindings ---- */

PTEST(the_default_bindings_are_the_decks_free_controls)
{
    const struct controller_bindings b = controller_default_bindings();
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_FAST_FORWARD_HOLD], CONTROLLER_RIGHT_TRIGGER);
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_FAST_FORWARD_TOGGLE], SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_OVERLAY], SDL_CONTROLLER_BUTTON_Y);
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_OVERLAY_CLOSE], SDL_CONTROLLER_BUTTON_B);
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_QUIT], SDL_CONTROLLER_BUTTON_X);
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_FULLSCREEN], CONTROLLER_LEFT_TRIGGER);
    PT_ASSERT(controller_bindings_are_valid(&b));
}

PTEST(the_defaults_leave_the_gba_buttons_alone)
{
    /* The GBA layout of input.c: A, B, Back, Start, the shoulders and the D-pad. */
    static const SDL_GameControllerButton gba[] = {
        SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_START,     SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_DPAD_UP,   SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    };
    const struct controller_bindings b = controller_default_bindings();
    for (size_t f = 0; f < CONTROLLER_FUNCTION_COUNT; f++) {
        for (size_t g = 0; g < sizeof gba / sizeof gba[0]; g++)
            PT_ASSERT(b.controls[f] != (int)gba[g]);
    }
    /* B is the one shared: it closes the overlay, which reads every button released. */
    PT_ASSERT_EQ_INT(b.controls[CONTROLLER_OVERLAY_CLOSE], SDL_CONTROLLER_BUTTON_B);
}

PTEST(each_function_has_a_setting_name_and_a_label)
{
    const struct controller_function_info *const functions = controller_functions();
    PT_ASSERT_EQ_STR(functions[CONTROLLER_FAST_FORWARD_HOLD].setting, "pad_fast_forward");
    PT_ASSERT_EQ_STR(functions[CONTROLLER_FAST_FORWARD_TOGGLE].setting, "pad_fast_forward_toggle");
    PT_ASSERT_EQ_STR(functions[CONTROLLER_OVERLAY].setting, "pad_overlay");
    PT_ASSERT_EQ_STR(functions[CONTROLLER_OVERLAY_CLOSE].setting, "pad_overlay_close");
    PT_ASSERT_EQ_STR(functions[CONTROLLER_QUIT].setting, "pad_quit");
    PT_ASSERT_EQ_STR(functions[CONTROLLER_FULLSCREEN].setting, "pad_fullscreen");
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++)
        PT_ASSERT(functions[i].name[0] != '\0');
}

PTEST(control_names_are_sdls_in_any_case_and_the_two_triggers)
{
    PT_ASSERT_EQ_INT(controller_control_from_name("x"), SDL_CONTROLLER_BUTTON_X);
    PT_ASSERT_EQ_INT(controller_control_from_name("Y"), SDL_CONTROLLER_BUTTON_Y);
    PT_ASSERT_EQ_INT(controller_control_from_name("leftstick"), SDL_CONTROLLER_BUTTON_LEFTSTICK);
    PT_ASSERT_EQ_INT(controller_control_from_name("RightStick"), SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    PT_ASSERT_EQ_INT(controller_control_from_name("dpup"), SDL_CONTROLLER_BUTTON_DPAD_UP);
    PT_ASSERT_EQ_INT(controller_control_from_name("start"), SDL_CONTROLLER_BUTTON_START);
    PT_ASSERT_EQ_INT(controller_control_from_name("lefttrigger"), CONTROLLER_LEFT_TRIGGER);
    PT_ASSERT_EQ_INT(controller_control_from_name("RIGHTTRIGGER"), CONTROLLER_RIGHT_TRIGGER);
}

PTEST(a_name_that_is_no_control_is_none)
{
    PT_ASSERT_EQ_INT(controller_control_from_name(NULL), CONTROLLER_NO_CONTROL);
    PT_ASSERT_EQ_INT(controller_control_from_name(""), CONTROLLER_NO_CONTROL);
    PT_ASSERT_EQ_INT(controller_control_from_name("banana"), CONTROLLER_NO_CONTROL);
    PT_ASSERT_EQ_INT(controller_control_from_name("left trigger"), CONTROLLER_NO_CONTROL);
    PT_ASSERT_EQ_INT(controller_control_from_name("z"), CONTROLLER_NO_CONTROL);
    /* longer than any name SDL has: refused, not truncated into one that matches */
    PT_ASSERT_EQ_INT(controller_control_from_name("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"), CONTROLLER_NO_CONTROL);
}

PTEST(setting_names_and_labels_round_trip_for_every_control)
{
    for (int control = 0; control < CONTROLLER_CONTROL_COUNT; control++) {
        PT_ASSERT(controller_control_setting(control)[0] != '\0');
        PT_ASSERT(controller_control_label(control)[0] != '\0');
        PT_ASSERT_EQ_INT(controller_control_from_name(controller_control_setting(control)), control);
    }
    PT_ASSERT_EQ_STR(controller_control_setting(CONTROLLER_RIGHT_TRIGGER), "righttrigger");
    PT_ASSERT_EQ_STR(controller_control_label(CONTROLLER_LEFT_TRIGGER), "Left trigger");
    PT_ASSERT_EQ_STR(controller_control_label(SDL_CONTROLLER_BUTTON_RIGHTSTICK), "Right stick click");
    PT_ASSERT_EQ_STR(controller_control_label(SDL_CONTROLLER_BUTTON_X), "X");
    PT_ASSERT_EQ_STR(controller_control_setting(CONTROLLER_NO_CONTROL), "");
    PT_ASSERT_EQ_STR(controller_control_label(CONTROLLER_CONTROL_COUNT), "");
}

PTEST(bindings_must_be_controls_and_all_different)
{
    struct controller_bindings b = controller_default_bindings();
    PT_ASSERT(!controller_bindings_are_valid(NULL));
    b.controls[CONTROLLER_QUIT] = b.controls[CONTROLLER_OVERLAY];
    PT_ASSERT(!controller_bindings_are_valid(&b));
    b = controller_default_bindings();
    b.controls[CONTROLLER_QUIT] = CONTROLLER_NO_CONTROL;
    PT_ASSERT(!controller_bindings_are_valid(&b));
    b.controls[CONTROLLER_QUIT] = CONTROLLER_CONTROL_COUNT;
    PT_ASSERT(!controller_bindings_are_valid(&b));
    b.controls[CONTROLLER_QUIT] = CONTROLLER_RIGHT_TRIGGER + 0; /* the hold's */
    PT_ASSERT(!controller_bindings_are_valid(&b));
}

/* ---- the trigger ---- */

PTEST(a_trigger_is_pressed_past_its_threshold_and_released_below_the_lower_one)
{
    PT_ASSERT(!controller_trigger_pressed(false, 0));
    PT_ASSERT(!controller_trigger_pressed(false, CONTROLLER_TRIGGER_PRESS)); /* not past it */
    PT_ASSERT(controller_trigger_pressed(false, CONTROLLER_TRIGGER_PRESS + 1));
    PT_ASSERT(controller_trigger_pressed(false, 32767));
    PT_ASSERT(controller_trigger_pressed(true, 32767));
    PT_ASSERT(!controller_trigger_pressed(true, 0));
    PT_ASSERT(!controller_trigger_pressed(true, CONTROLLER_TRIGGER_RELEASE - 1));
}

PTEST(between_the_thresholds_a_trigger_keeps_its_state)
{
    const int between = (CONTROLLER_TRIGGER_PRESS + CONTROLLER_TRIGGER_RELEASE) / 2;
    PT_ASSERT(!controller_trigger_pressed(false, between));
    PT_ASSERT(controller_trigger_pressed(true, between));
    PT_ASSERT(controller_trigger_pressed(true, CONTROLLER_TRIGGER_RELEASE));
}

/* ---- the events ---- */

PTEST(init_needs_valid_bindings_and_is_called_once)
{
    struct controller_bindings b = controller_default_bindings();
    controller_shutdown();
    PT_ASSERT_EQ_INT(controller_init(NULL), -1);
    b.controls[CONTROLLER_QUIT] = b.controls[CONTROLLER_OVERLAY];
    PT_ASSERT_EQ_INT(controller_init(&b), -1);
    PT_ASSERT(!controller_enabled());
    b = controller_default_bindings();
    PT_ASSERT_EQ_INT(controller_init(&b), 0);
    PT_ASSERT(controller_enabled());
    PT_ASSERT_EQ_INT(controller_init(&b), -1);
    controller_shutdown();
    controller_shutdown();
    PT_ASSERT(!controller_enabled());
}

PTEST(before_init_and_for_null_events_do_nothing)
{
    controller_shutdown();
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_A).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT(!controller_held(CONTROLLER_OVERLAY));
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    start();
    PT_ASSERT_EQ_INT(controller_handle_event(NULL).function, CONTROLLER_NO_FUNCTION);
    controller_shutdown();
}

PTEST(a_bound_button_press_and_release_are_one_edge_each)
{
    start();
    struct controller_edge edge = press(PAD_ONE, SDL_CONTROLLER_BUTTON_Y);
    PT_ASSERT_EQ_INT(edge.function, CONTROLLER_OVERLAY);
    PT_ASSERT(edge.pressed);
    PT_ASSERT(controller_held(CONTROLLER_OVERLAY));
    edge = release(PAD_ONE, SDL_CONTROLLER_BUTTON_Y);
    PT_ASSERT_EQ_INT(edge.function, CONTROLLER_OVERLAY);
    PT_ASSERT(!edge.pressed);
    PT_ASSERT(!controller_held(CONTROLLER_OVERLAY));
    controller_shutdown();
}

PTEST(every_default_button_gives_its_function)
{
    start();
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_RIGHTSTICK).function, CONTROLLER_FAST_FORWARD_TOGGLE);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_B).function, CONTROLLER_OVERLAY_CLOSE);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_X).function, CONTROLLER_QUIT);
    controller_shutdown();
}

PTEST(a_button_already_held_or_released_twice_is_no_edge)
{
    start();
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(release(PAD_ONE, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(release(PAD_ONE, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_NO_FUNCTION);
    controller_shutdown();
}

PTEST(a_button_no_function_is_bound_to_is_no_edge)
{
    start();
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_A).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_START).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(send(axis_event(PAD_ONE, SDL_CONTROLLER_AXIS_LEFTX, 32767)).function, CONTROLLER_NO_FUNCTION);
    controller_shutdown();
}

PTEST(the_trigger_hold_follows_the_trigger_past_the_threshold)
{
    start();
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, CONTROLLER_TRIGGER_PRESS).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    struct controller_edge edge = right_trigger(PAD_ONE, CONTROLLER_TRIGGER_PRESS + 1);
    PT_ASSERT_EQ_INT(edge.function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT(edge.pressed);
    PT_ASSERT(controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    edge = right_trigger(PAD_ONE, 0);
    PT_ASSERT_EQ_INT(edge.function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT(!edge.pressed);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    controller_shutdown();
}

/* The axis sends an event for every change of value: a trigger squeezed on gives many. */
PTEST(a_trigger_held_down_is_one_press_however_many_events_it_sends)
{
    start();
    unsigned presses = 0;
    for (int value = 17000; value <= 32767; value += 1500) {
        const struct controller_edge edge = right_trigger(PAD_ONE, (Sint16)value);
        presses += edge.function == CONTROLLER_FAST_FORWARD_HOLD && edge.pressed ? 1u : 0u;
    }
    PT_ASSERT_EQ_U32(presses, 1u);
    controller_shutdown();
}

PTEST(a_trigger_wobbling_between_the_thresholds_neither_presses_nor_releases)
{
    start();
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 30000).function, CONTROLLER_FAST_FORWARD_HOLD);
    for (int i = 0; i < 4; i++) {
        PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 12000).function, CONTROLLER_NO_FUNCTION);
        PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 15000).function, CONTROLLER_NO_FUNCTION);
    }
    PT_ASSERT(controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    controller_shutdown();
}

PTEST(the_left_trigger_is_the_fullscreen_button)
{
    start();
    const struct controller_edge edge = send(axis_event(PAD_ONE, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767));
    PT_ASSERT_EQ_INT(edge.function, CONTROLLER_FULLSCREEN);
    PT_ASSERT(edge.pressed);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    controller_shutdown();
}

/* SDL sends no button-up for a pulled cable: the removal releases what it held. */
PTEST(removing_a_controller_releases_what_it_held)
{
    start();
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 32767).function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT(controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    PT_ASSERT_EQ_INT(send(removed_event(PAD_ONE)).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    /* and the same control pressed again is a new press */
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 32767).function, CONTROLLER_FAST_FORWARD_HOLD);
    controller_shutdown();
}

PTEST(the_window_losing_the_focus_releases_everything)
{
    start();
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 32767).function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT_EQ_INT(press(PAD_TWO, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_OVERLAY);
    SDL_Event lost;
    SDL_zero(lost);
    lost.type = SDL_WINDOWEVENT;
    lost.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    PT_ASSERT_EQ_INT(send(lost).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    PT_ASSERT(!controller_held(CONTROLLER_OVERLAY));
    controller_shutdown();
}

PTEST(a_control_held_on_two_controllers_is_held_until_both_let_go_or_leave)
{
    start();
    PT_ASSERT_EQ_INT(right_trigger(PAD_ONE, 32767).function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT_EQ_INT(right_trigger(PAD_TWO, 32767).function, CONTROLLER_FAST_FORWARD_HOLD);
    (void)send(removed_event(PAD_ONE));
    PT_ASSERT(controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    (void)right_trigger(PAD_TWO, 0);
    PT_ASSERT(!controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    controller_shutdown();
}

PTEST(more_controllers_than_the_input_holds_are_ignored)
{
    start();
    for (SDL_JoystickID id = 0; id < INPUT_MAX_CONTROLLERS; id++)
        PT_ASSERT_EQ_INT(press(id, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(press(INPUT_MAX_CONTROLLERS, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_NO_FUNCTION);
    (void)send(removed_event(0)); /* a slot is free again */
    PT_ASSERT_EQ_INT(press(INPUT_MAX_CONTROLLERS, SDL_CONTROLLER_BUTTON_Y).function, CONTROLLER_OVERLAY);
    controller_shutdown();
}

PTEST(rebound_functions_follow_their_new_controls)
{
    struct controller_bindings b = controller_default_bindings();
    b.controls[CONTROLLER_QUIT] = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    b.controls[CONTROLLER_FAST_FORWARD_HOLD] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    controller_shutdown();
    PT_ASSERT_EQ_INT(controller_init(&b), 0);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_X).function, CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_LEFTSTICK).function, CONTROLLER_QUIT);
    PT_ASSERT_EQ_INT(press(PAD_ONE, SDL_CONTROLLER_BUTTON_LEFTSHOULDER).function, CONTROLLER_FAST_FORWARD_HOLD);
    PT_ASSERT(controller_held(CONTROLLER_FAST_FORWARD_HOLD));
    controller_shutdown();
}

PTEST(the_label_of_a_function_is_its_controls_defaults_before_init)
{
    controller_shutdown();
    PT_ASSERT_EQ_STR(controller_function_label(CONTROLLER_QUIT), "X");
    PT_ASSERT_EQ_STR(controller_function_label(CONTROLLER_OVERLAY_CLOSE), "B");
    struct controller_bindings b = controller_default_bindings();
    b.controls[CONTROLLER_QUIT] = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    b.controls[CONTROLLER_FAST_FORWARD_TOGGLE] = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    PT_ASSERT_EQ_INT(controller_init(&b), 0);
    PT_ASSERT_EQ_STR(controller_function_label(CONTROLLER_QUIT), "Right stick click");
    PT_ASSERT_EQ_STR(controller_function_label(CONTROLLER_NO_FUNCTION), "");
    controller_shutdown();
}

PTEST_MAIN()
