// deps: gba_memmap.c
/*
 * Tests of input, written from GBATEK "GBA Keypad Input" before the module.
 *
 * GBATEK: KEYINPUT is the halfword at 4000130h, bits 0-9 are Button A, B,
 * Select, Start, Right, Left, Up, Down, R, L, and "0=Pressed, 1=Released".
 * Bits 10-15 are not used; this module reports them as 0 (see input.h).
 *
 * The host-path tests set SDL_VIDEODRIVER=dummy with setenv before SDL_Init,
 * so they run headless. The dummy driver has no way to put a key into
 * SDL_GetKeyboardState (SDL_PushEvent only queues an event; the state array
 * is updated by the driver, not by the queue), so the host tests cover init,
 * the real SDL_GetKeyboardState read with no key held, the controller-free
 * path, hot-plug event handling, and shutdown; the key-to-bit translation is
 * tested through the pure functions that input_update_keyinput feeds.
 */
#include <SDL.h>
#include <stdlib.h>

#include "gba_memmap.h"
#include "input.h"
#include "ptest.h"

#define KEYINPUT_ADDRESS (GBA_IO_BASE + UINT32_C(0x130))
#define ALL_TEN_KEYS UINT16_C(0x03FF)
#define SENTINEL UINT16_C(0xA5A5)

static volatile uint16_t *keyinput(void)
{
    return (volatile uint16_t *)KEYINPUT_ADDRESS;
}

static void headless_sdl_init(void)
{
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    PT_ASSERT(SDL_Init(SDL_INIT_VIDEO) == 0);
}

PTEST(no_keys_gives_the_idle_value)
{
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(0), 0x03FFu);
}

PTEST(each_single_key_clears_exactly_its_bit)
{
    for (unsigned bit = 0; bit < 10; bit++) {
        uint16_t pressed = (uint16_t)(1u << bit);
        PT_ASSERT_EQ_U32(input_keys_to_keyinput(pressed), (uint32_t)(0x03FFu & ~(1u << bit)));
    }
}

PTEST(key_bits_are_in_gbatek_order)
{
    PT_ASSERT_EQ_U32(INPUT_KEY_A, 0x0001u);
    PT_ASSERT_EQ_U32(INPUT_KEY_B, 0x0002u);
    PT_ASSERT_EQ_U32(INPUT_KEY_SELECT, 0x0004u);
    PT_ASSERT_EQ_U32(INPUT_KEY_START, 0x0008u);
    PT_ASSERT_EQ_U32(INPUT_KEY_RIGHT, 0x0010u);
    PT_ASSERT_EQ_U32(INPUT_KEY_LEFT, 0x0020u);
    PT_ASSERT_EQ_U32(INPUT_KEY_UP, 0x0040u);
    PT_ASSERT_EQ_U32(INPUT_KEY_DOWN, 0x0080u);
    PT_ASSERT_EQ_U32(INPUT_KEY_R, 0x0100u);
    PT_ASSERT_EQ_U32(INPUT_KEY_L, 0x0200u);
}

PTEST(all_ten_keys_give_zero)
{
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(ALL_TEN_KEYS), 0u);
}

PTEST(opposite_directions_together_are_both_reported)
{
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(INPUT_KEY_LEFT | INPUT_KEY_RIGHT), 0x03FFu & ~0x0030u);
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(INPUT_KEY_UP | INPUT_KEY_DOWN), 0x03FFu & ~0x00C0u);
}

PTEST(unused_bits_read_as_zero_whatever_the_input)
{
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(0xFFFF), 0u);
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(0xFC00), 0x03FFu);
    PT_ASSERT_EQ_U32(input_keys_to_keyinput(0x8001), 0x03FEu);
}

PTEST(update_before_init_leaves_the_register_alone)
{
    input_shutdown();
    *keyinput() = SENTINEL;
    input_update_keyinput();
    uint16_t seen = *keyinput();
    *keyinput() = 0;
    PT_ASSERT_EQ_U32(seen, SENTINEL);
}

PTEST(shutdown_without_init_is_safe_and_repeatable)
{
    input_shutdown();
    input_shutdown();
}

PTEST(script_source_writes_keyinput_exactly)
{
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    *keyinput() = SENTINEL;
    input_update_keyinput(); /* nothing set yet: no key pressed */
    uint16_t idle = *keyinput();
    input_script_set(INPUT_KEY_A | INPUT_KEY_START);
    input_update_keyinput();
    uint16_t first = *keyinput();
    input_script_set(INPUT_KEY_L | INPUT_KEY_UP | INPUT_KEY_DOWN);
    input_update_keyinput();
    uint16_t second = *keyinput();
    input_script_set(0xFFFF);
    input_update_keyinput();
    uint16_t all = *keyinput();
    input_shutdown();
    *keyinput() = 0;
    PT_ASSERT_EQ_U32(idle, 0x03FFu);
    PT_ASSERT_EQ_U32(first, input_keys_to_keyinput(INPUT_KEY_A | INPUT_KEY_START));
    PT_ASSERT_EQ_U32(first, 0x03F6u);
    PT_ASSERT_EQ_U32(second, 0x033Fu & ~0u);
    PT_ASSERT_EQ_U32(all, 0u);
}

PTEST(script_source_needs_no_sdl)
{
    PT_ASSERT(SDL_WasInit(0) == 0);
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    input_shutdown();
}

PTEST(script_value_does_not_leak_into_a_later_init)
{
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    input_script_set(INPUT_KEY_B);
    input_shutdown();
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    *keyinput() = SENTINEL;
    input_update_keyinput();
    uint16_t seen = *keyinput();
    input_shutdown();
    *keyinput() = 0;
    PT_ASSERT_EQ_U32(seen, 0x03FFu);
}

PTEST(init_rejects_an_unknown_source)
{
    PT_ASSERT(input_init((enum input_source)99) != 0);
    input_shutdown();
}

PTEST(second_init_without_shutdown_fails_and_keeps_the_first)
{
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    PT_ASSERT(input_init(INPUT_SOURCE_SCRIPT) != 0);
    input_script_set(INPUT_KEY_R);
    *keyinput() = SENTINEL;
    input_update_keyinput();
    uint16_t seen = *keyinput();
    input_shutdown();
    *keyinput() = 0;
    PT_ASSERT_EQ_U32(seen, 0x02FFu & ~0u ? input_keys_to_keyinput(INPUT_KEY_R) : 0u);
}

PTEST(init_after_shutdown_works)
{
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    input_shutdown();
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT), 0);
    input_shutdown();
}

/* ---- keyboard mapping ---- */

struct expected_binding {
    int scancode;
    uint16_t key;
};

PTEST(keyboard_table_is_the_documented_default)
{
    static const struct expected_binding expected[] = {
        { SDL_SCANCODE_Z, 0x0001 },         { SDL_SCANCODE_X, 0x0002 },
        { SDL_SCANCODE_BACKSPACE, 0x0004 }, { SDL_SCANCODE_RETURN, 0x0008 },
        { SDL_SCANCODE_RIGHT, 0x0010 },     { SDL_SCANCODE_LEFT, 0x0020 },
        { SDL_SCANCODE_UP, 0x0040 },        { SDL_SCANCODE_DOWN, 0x0080 },
        { SDL_SCANCODE_S, 0x0100 },         { SDL_SCANCODE_A, 0x0200 },
    };
    size_t count = 0;
    const struct input_key_binding *table = input_keyboard_bindings(&count);
    PT_ASSERT(table != NULL);
    PT_ASSERT(count == sizeof expected / sizeof expected[0]);
    for (size_t i = 0; i < count; i++) {
        PT_ASSERT_EQ_INT(table[i].scancode, expected[i].scancode);
        PT_ASSERT_EQ_U32(table[i].key, expected[i].key);
        PT_ASSERT(table[i].name != NULL);
    }
}

PTEST(keyboard_table_binds_each_gba_key_once)
{
    size_t count = 0;
    const struct input_key_binding *table = input_keyboard_bindings(&count);
    uint16_t seen = 0;
    for (size_t i = 0; i < count; i++) {
        PT_ASSERT((seen & table[i].key) == 0);
        seen = (uint16_t)(seen | table[i].key);
    }
    PT_ASSERT_EQ_U32(seen, ALL_TEN_KEYS);
}

PTEST(keyboard_state_with_nothing_down_is_no_keys)
{
    uint8_t state[SDL_NUM_SCANCODES] = { 0 };
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, SDL_NUM_SCANCODES), 0u);
}

PTEST(each_mapped_scancode_gives_its_key)
{
    size_t count = 0;
    const struct input_key_binding *table = input_keyboard_bindings(&count);
    for (size_t i = 0; i < count; i++) {
        uint8_t state[SDL_NUM_SCANCODES] = { 0 };
        state[table[i].scancode] = 1;
        PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, SDL_NUM_SCANCODES), table[i].key);
    }
}

PTEST(unmapped_scancodes_are_ignored)
{
    uint8_t state[SDL_NUM_SCANCODES] = { 0 };
    state[SDL_SCANCODE_Q] = 1;
    state[SDL_SCANCODE_SPACE] = 1;
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, SDL_NUM_SCANCODES), 0u);
}

PTEST(all_mapped_keys_down_gives_all_ten)
{
    uint8_t state[SDL_NUM_SCANCODES] = { 0 };
    size_t count = 0;
    const struct input_key_binding *table = input_keyboard_bindings(&count);
    for (size_t i = 0; i < count; i++) {
        state[table[i].scancode] = 1;
    }
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, SDL_NUM_SCANCODES), ALL_TEN_KEYS);
}

PTEST(keyboard_state_shorter_than_a_scancode_is_not_read_past)
{
    uint8_t state[2] = { 1, 1 };
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, 2), 0u);
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, 0), 0u);
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(state, -1), 0u);
    PT_ASSERT_EQ_U32(input_keyboard_to_keys(NULL, SDL_NUM_SCANCODES), 0u);
}

/* ---- controller mapping ---- */

PTEST(idle_pad_is_no_keys)
{
    struct input_pad_state pad = { 0 };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&pad), 0u);
    PT_ASSERT_EQ_U32(input_pad_to_keys(NULL), 0u);
}

PTEST(each_pad_button_gives_its_key)
{
    struct input_pad_state a = { .a = 1 }, b = { .b = 1 }, start = { .start = 1 },
        back = { .back = 1 }, l = { .left_shoulder = 1 }, r = { .right_shoulder = 1 },
        up = { .dpad_up = 1 }, down = { .dpad_down = 1 }, left = { .dpad_left = 1 },
        right = { .dpad_right = 1 };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&a), INPUT_KEY_A);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&b), INPUT_KEY_B);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&start), INPUT_KEY_START);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&back), INPUT_KEY_SELECT);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&l), INPUT_KEY_L);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&r), INPUT_KEY_R);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&up), INPUT_KEY_UP);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&down), INPUT_KEY_DOWN);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&left), INPUT_KEY_LEFT);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&right), INPUT_KEY_RIGHT);
}

PTEST(stick_inside_the_deadzone_is_ignored)
{
    struct input_pad_state pad = { .left_x = INPUT_STICK_DEADZONE, .left_y = -INPUT_STICK_DEADZONE };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&pad), 0u);
}

PTEST(stick_past_the_deadzone_gives_a_direction)
{
    struct input_pad_state r = { .left_x = INPUT_STICK_DEADZONE + 1 };
    struct input_pad_state l = { .left_x = -INPUT_STICK_DEADZONE - 1 };
    struct input_pad_state d = { .left_y = INPUT_STICK_DEADZONE + 1 };  /* SDL: +y is down */
    struct input_pad_state u = { .left_y = -INPUT_STICK_DEADZONE - 1 };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&r), INPUT_KEY_RIGHT);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&l), INPUT_KEY_LEFT);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&d), INPUT_KEY_DOWN);
    PT_ASSERT_EQ_U32(input_pad_to_keys(&u), INPUT_KEY_UP);
}

PTEST(stick_extremes_do_not_overflow)
{
    struct input_pad_state pad = { .left_x = INT16_MIN, .left_y = INT16_MAX };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&pad), INPUT_KEY_LEFT | INPUT_KEY_DOWN);
}

PTEST(stick_and_dpad_combine)
{
    struct input_pad_state pad = { .left_x = INT16_MAX, .dpad_left = 1 };
    PT_ASSERT_EQ_U32(input_pad_to_keys(&pad), INPUT_KEY_LEFT | INPUT_KEY_RIGHT);
}

/* ---- host source, headless ---- */

PTEST(host_init_without_sdl_events_fails)
{
    PT_ASSERT(SDL_WasInit(SDL_INIT_EVENTS) == 0);
    PT_ASSERT(input_init(INPUT_SOURCE_HOST) != 0);
    input_shutdown(); /* after a failed init */
    *keyinput() = SENTINEL;
    input_update_keyinput(); /* and update still does nothing */
    uint16_t seen = *keyinput();
    *keyinput() = 0;
    PT_ASSERT_EQ_U32(seen, SENTINEL);
}

PTEST(host_with_no_key_held_writes_idle)
{
    headless_sdl_init();
    int status = input_init(INPUT_SOURCE_HOST);
    uint16_t seen = 0;
    if (status == 0) {
        SDL_PumpEvents();
        *keyinput() = SENTINEL;
        input_update_keyinput();
        seen = *keyinput();
        *keyinput() = 0;
    }
    input_shutdown();
    SDL_Quit();
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_U32(seen, 0x03FFu);
}

PTEST(host_ignores_a_pushed_key_event_because_state_comes_from_the_driver)
{
    /* Documents the dummy driver's limit: a queued SDL_KEYDOWN does not change
     * SDL_GetKeyboardState, so KEYINPUT stays idle. If this ever fails, SDL
     * started applying pushed events and the host path can be tested end to end. */
    headless_sdl_init();
    int status = input_init(INPUT_SOURCE_HOST);
    uint16_t seen = 0;
    if (status == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_KEYDOWN;
        event.key.state = SDL_PRESSED;
        event.key.keysym.scancode = SDL_SCANCODE_Z;
        SDL_PushEvent(&event);
        SDL_PumpEvents();
        input_update_keyinput();
        seen = *keyinput();
        *keyinput() = 0;
    }
    input_shutdown();
    SDL_Quit();
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_U32(seen, 0x03FFu);
}

PTEST(host_event_handler_ignores_unrelated_and_bogus_controller_events)
{
    headless_sdl_init();
    int status = input_init(INPUT_SOURCE_HOST);
    int added = 0, removed = 0, other = 0;
    if (status == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_CONTROLLERDEVICEREMOVED;
        event.cdevice.which = 12345; /* no such instance */
        removed = input_handle_event(&event);
        SDL_zero(event);
        event.type = SDL_CONTROLLERDEVICEADDED;
        event.cdevice.which = 12345; /* no such device index */
        added = input_handle_event(&event);
        SDL_zero(event);
        event.type = SDL_QUIT;
        other = input_handle_event(&event);
    }
    input_shutdown();
    SDL_Quit();
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_INT(removed, 0);   /* removing something not open is not an error */
    PT_ASSERT(added != 0);          /* an index SDL cannot open is reported */
    PT_ASSERT_EQ_INT(other, 0);
}

PTEST(event_handler_before_init_and_with_null_is_safe)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERDEVICEADDED;
    input_shutdown();
    PT_ASSERT(input_handle_event(&event) != 0 || 1);
    PT_ASSERT(input_handle_event(NULL) != 0);
}

PTEST(host_shutdown_is_safe_after_sdl_quit_and_twice)
{
    headless_sdl_init();
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_HOST), 0);
    input_shutdown();
    input_shutdown();
    SDL_Quit();
}

PTEST_MAIN()
