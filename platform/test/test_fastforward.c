// deps: input.c overlay.c font.c
/*
 * test_fastforward.c: fast-forward's keys and state (fastforward.c): the
 * key names with their modifiers, which presses are which key, the hold
 * ending on its key-up and on the window losing focus, the toggle staying
 * on across frames and key-ups until pressed again, the overlay taking the
 * presses but not the release, the multiplier and the sound at each speed;
 * and the indicator's text, place and picture. The expected pictures are
 * drawn with font.h's own calls, which test_font.c checks pixel by pixel.
 */
#include "fastforward.h"
#include "overlay.h"
#include "ptest.h"

#include <SDL_events.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static SDL_Event key_event(Uint32 type, SDL_Scancode scancode, Uint16 modifiers, Uint8 repeat)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.key.keysym.scancode = scancode;
    event.key.keysym.mod = modifiers;
    event.key.repeat = repeat;
    return event;
}

static void press(SDL_Scancode scancode, Uint16 modifiers)
{
    const SDL_Event event = key_event(SDL_KEYDOWN, scancode, modifiers, 0);
    fastforward_handle_event(&event);
}

static void release(SDL_Scancode scancode, Uint16 modifiers)
{
    const SDL_Event event = key_event(SDL_KEYUP, scancode, modifiers, 0);
    fastforward_handle_event(&event);
}

static void window_event(SDL_WindowEventID id)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_WINDOWEVENT;
    event.window.event = (Uint8)id;
    fastforward_handle_event(&event);
}

/* F1, through the overlay, as the frame loop passes it. */
static void press_f1(void)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = SDL_SCANCODE_F1;
    event.key.keysym.sym = SDLK_F1;
    (void)overlay_handle_event(&event);
    fastforward_handle_event(&event);
}

static void no_text(overlay_line_sink sink, void *context)
{
    (void)sink;
    (void)context;
}

static void start_with(uint32_t speed, enum fastforward_audio audio)
{
    struct fastforward_settings settings = fastforward_defaults();
    settings.speed = speed;
    settings.audio = audio;
    fastforward_shutdown();
    PT_ASSERT_EQ_INT(fastforward_init(&settings), 0);
}

static void start(void)
{
    start_with(FASTFORWARD_DEFAULT_SPEED, FASTFORWARD_AUDIO_PLAY);
}

/* ---- the keys ---- */

PTEST(the_defaults_are_tab_held_shift_tab_toggled_twice_the_speed_and_the_sound_played)
{
    const struct fastforward_settings d = fastforward_defaults();
    PT_ASSERT_EQ_INT(d.hold.scancode, SDL_SCANCODE_TAB);
    PT_ASSERT_EQ_U32(d.hold.modifiers, 0u);
    PT_ASSERT_EQ_INT(d.toggle.scancode, SDL_SCANCODE_TAB);
    PT_ASSERT_EQ_U32(d.toggle.modifiers, FASTFORWARD_SHIFT);
    PT_ASSERT_EQ_U32(d.speed, 2u);
    PT_ASSERT_EQ_INT(d.audio, FASTFORWARD_AUDIO_PLAY);
}

static bool named(const char *name, int scancode, unsigned modifiers)
{
    struct fastforward_key key = {.scancode = -1, .modifiers = 0xFFu};
    return fastforward_key_from_name(name, &key) && key.scancode == scancode && key.modifiers == modifiers;
}

PTEST(a_key_is_sdls_name_after_shift_ctrl_and_alt_in_any_case_and_order)
{
    PT_ASSERT(named("tab", SDL_SCANCODE_TAB, 0u));
    PT_ASSERT(named("Shift+Tab", SDL_SCANCODE_TAB, FASTFORWARD_SHIFT));
    PT_ASSERT(named("shift + tab", SDL_SCANCODE_TAB, FASTFORWARD_SHIFT));
    PT_ASSERT(named("CTRL+alt+F", SDL_SCANCODE_F, FASTFORWARD_CTRL | FASTFORWARD_ALT));
    PT_ASSERT(named("alt+Ctrl+f", SDL_SCANCODE_F, FASTFORWARD_CTRL | FASTFORWARD_ALT));
    PT_ASSERT(named("Ctrl+Alt+Shift+F2", SDL_SCANCODE_F2, FASTFORWARD_CTRL | FASTFORWARD_ALT | FASTFORWARD_SHIFT));
    PT_ASSERT(named("Left Shift", SDL_SCANCODE_LSHIFT, 0u));
}

/* '+' is part of SDL's names too, and "alt" begins one. */
PTEST(a_plus_in_sdls_name_and_a_name_beginning_with_alt_are_the_key)
{
    PT_ASSERT(named("Keypad +", SDL_SCANCODE_KP_PLUS, 0u));
    PT_ASSERT(named("shift+keypad +", SDL_SCANCODE_KP_PLUS, FASTFORWARD_SHIFT));
    PT_ASSERT(named("AltErase", SDL_SCANCODE_ALTERASE, 0u));
}

PTEST(no_key_after_the_modifiers_or_one_sdl_does_not_know_is_no_key)
{
    static const char *const bad[] = {"", "shift+", "shift", "shift + ", "super+tab", "+tab", "tab+", "jump"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        struct fastforward_key key = {.scancode = 7, .modifiers = 3u};
        PT_ASSERT(!fastforward_key_from_name(bad[i], &key));
        PT_ASSERT_EQ_INT(key.scancode, 7);
        PT_ASSERT_EQ_U32(key.modifiers, 3u);
    }
}

PTEST(a_keys_name_is_its_modifiers_then_sdls_name)
{
    char name[FASTFORWARD_KEY_NAME_SIZE];
    fastforward_key_name(&(struct fastforward_key){SDL_SCANCODE_TAB, 0u}, name, sizeof name);
    PT_ASSERT_EQ_STR(name, "Tab");
    fastforward_key_name(&(struct fastforward_key){SDL_SCANCODE_TAB, FASTFORWARD_SHIFT}, name, sizeof name);
    PT_ASSERT_EQ_STR(name, "Shift+Tab");
    fastforward_key_name(
        &(struct fastforward_key){SDL_SCANCODE_F, FASTFORWARD_SHIFT | FASTFORWARD_ALT | FASTFORWARD_CTRL}, name,
        sizeof name);
    PT_ASSERT_EQ_STR(name, "Ctrl+Alt+Shift+F");
    fastforward_key_name(&(struct fastforward_key){SDL_SCANCODE_KP_PLUS, FASTFORWARD_ALT}, name, sizeof name);
    PT_ASSERT_EQ_STR(name, "Alt+Keypad +");
    fastforward_key_name(&(struct fastforward_key){SDL_SCANCODE_TAB, FASTFORWARD_SHIFT}, name, 6);
    PT_ASSERT_EQ_STR(name, "Shift");
}

PTEST(two_keys_are_equal_with_the_same_scancode_and_modifiers)
{
    const struct fastforward_key tab = {SDL_SCANCODE_TAB, 0u};
    const struct fastforward_key shift_tab = {SDL_SCANCODE_TAB, FASTFORWARD_SHIFT};
    PT_ASSERT(fastforward_keys_equal(&tab, &(struct fastforward_key){SDL_SCANCODE_TAB, 0u}));
    PT_ASSERT(!fastforward_keys_equal(&tab, &shift_tab));
    PT_ASSERT(!fastforward_keys_equal(&tab, &(struct fastforward_key){SDL_SCANCODE_F, 0u}));
}

PTEST(a_press_is_a_key_with_exactly_its_modifiers_held_on_either_side)
{
    const struct fastforward_key tab = {SDL_SCANCODE_TAB, 0u};
    const struct fastforward_key shift_tab = {SDL_SCANCODE_TAB, FASTFORWARD_SHIFT};
    const struct fastforward_key ctrl_alt_f = {SDL_SCANCODE_F, FASTFORWARD_CTRL | FASTFORWARD_ALT};
    PT_ASSERT(fastforward_key_pressed(&tab, SDL_SCANCODE_TAB, KMOD_NONE));
    PT_ASSERT(!fastforward_key_pressed(&tab, SDL_SCANCODE_TAB, KMOD_LSHIFT));
    PT_ASSERT(!fastforward_key_pressed(&tab, SDL_SCANCODE_Q, KMOD_NONE));
    PT_ASSERT(fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_LSHIFT));
    PT_ASSERT(fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_RSHIFT));
    PT_ASSERT(fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_LSHIFT | KMOD_RSHIFT));
    PT_ASSERT(!fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_NONE));
    PT_ASSERT(!fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_LSHIFT | KMOD_LCTRL));
    PT_ASSERT(fastforward_key_pressed(&ctrl_alt_f, SDL_SCANCODE_F, KMOD_RCTRL | KMOD_LALT));
    PT_ASSERT(!fastforward_key_pressed(&ctrl_alt_f, SDL_SCANCODE_F, KMOD_RCTRL));
}

/* AltGr is not among them: SDL reports it as Right Alt (with Left Ctrl on Windows). */
PTEST(the_locks_the_gui_key_and_the_mode_key_do_not_count_but_altgr_does)
{
    const struct fastforward_key tab = {SDL_SCANCODE_TAB, 0u};
    const struct fastforward_key shift_tab = {SDL_SCANCODE_TAB, FASTFORWARD_SHIFT};
    const struct fastforward_key ctrl_alt_tab = {SDL_SCANCODE_TAB, FASTFORWARD_CTRL | FASTFORWARD_ALT};
    PT_ASSERT(fastforward_key_pressed(&tab, SDL_SCANCODE_TAB, KMOD_CAPS | KMOD_NUM | KMOD_GUI | KMOD_MODE));
    PT_ASSERT(fastforward_key_pressed(&shift_tab, SDL_SCANCODE_TAB, KMOD_LSHIFT | KMOD_CAPS));
    PT_ASSERT(!fastforward_key_pressed(&tab, SDL_SCANCODE_TAB, KMOD_RALT));
    PT_ASSERT(!fastforward_key_pressed(&tab, SDL_SCANCODE_TAB, KMOD_LCTRL | KMOD_RALT));
    PT_ASSERT(fastforward_key_pressed(&ctrl_alt_tab, SDL_SCANCODE_TAB, KMOD_LCTRL | KMOD_RALT));
}

/* SDL counts a modifier key's own press in the state it reports with it. */
PTEST(a_modifier_keys_own_press_does_not_count_but_the_other_sides_does)
{
    const struct fastforward_key left_shift = {SDL_SCANCODE_LSHIFT, 0u};
    const struct fastforward_key right_ctrl = {SDL_SCANCODE_RCTRL, 0u};
    const struct fastforward_key shift_left_alt = {SDL_SCANCODE_LALT, FASTFORWARD_SHIFT};
    PT_ASSERT(fastforward_key_pressed(&left_shift, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT));
    PT_ASSERT(!fastforward_key_pressed(&left_shift, SDL_SCANCODE_LSHIFT, KMOD_LSHIFT | KMOD_RSHIFT));
    PT_ASSERT(fastforward_key_pressed(&right_ctrl, SDL_SCANCODE_RCTRL, KMOD_RCTRL));
    PT_ASSERT(fastforward_key_pressed(&shift_left_alt, SDL_SCANCODE_LALT, KMOD_LALT | KMOD_LSHIFT));
    PT_ASSERT(!fastforward_key_pressed(&shift_left_alt, SDL_SCANCODE_LALT, KMOD_LALT));
}

/* ---- the state ---- */

PTEST(before_init_and_after_shutdown_nothing_turns_it_on)
{
    fastforward_shutdown();
    PT_ASSERT(!fastforward_enabled());
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    fastforward_handle_event(NULL);
    PT_ASSERT(!fastforward_active());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
    start();
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    fastforward_shutdown();
    PT_ASSERT(!fastforward_active());
    PT_ASSERT(!fastforward_enabled());
    fastforward_shutdown();
}

PTEST(the_hold_is_on_while_its_key_is_down)
{
    start();
    PT_ASSERT(!fastforward_active());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(fastforward_active());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), 2u);
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(!fastforward_active());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
    fastforward_shutdown();
}

/* Shift pressed after Tab, before Tab comes up: the key-up still ends it. */
PTEST(the_hold_ends_on_its_key_up_whatever_the_modifiers)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    release(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(!fastforward_active());
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    release(SDL_SCANCODE_Q, KMOD_NONE);
    PT_ASSERT(fastforward_active());
    fastforward_shutdown();
}

/* The key-up goes to the window that has the focus by then, which is not this one. */
PTEST(the_hold_ends_when_the_window_loses_the_focus)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    window_event(SDL_WINDOWEVENT_FOCUS_LOST);
    PT_ASSERT(!fastforward_active());
    window_event(SDL_WINDOWEVENT_FOCUS_GAINED);
    PT_ASSERT(!fastforward_active());
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(!fastforward_active());
    fastforward_shutdown();
}

PTEST(other_window_events_leave_the_hold_on)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    window_event(SDL_WINDOWEVENT_MOVED);
    window_event(SDL_WINDOWEVENT_LEAVE);
    PT_ASSERT(fastforward_active());
    fastforward_shutdown();
}

PTEST(the_toggle_stays_on_across_frames_key_ups_and_focus_until_pressed_again)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(fastforward_active());
    for (unsigned frame = 0; frame < 100; frame++)
        PT_ASSERT_EQ_U32(fastforward_multiplier(), 2u);
    release(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    release(SDL_SCANCODE_LSHIFT, KMOD_NONE);
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    window_event(SDL_WINDOWEVENT_FOCUS_LOST);
    PT_ASSERT(fastforward_active());
    press(SDL_SCANCODE_TAB, KMOD_RSHIFT);
    PT_ASSERT(!fastforward_active());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
    fastforward_shutdown();
}

PTEST(a_repeating_key_changes_nothing)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    const SDL_Event repeat = key_event(SDL_KEYDOWN, SDL_SCANCODE_TAB, KMOD_LSHIFT, 1);
    fastforward_handle_event(&repeat);
    PT_ASSERT(fastforward_active());
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    const SDL_Event held = key_event(SDL_KEYDOWN, SDL_SCANCODE_TAB, KMOD_NONE, 1);
    fastforward_handle_event(&held);
    PT_ASSERT(!fastforward_active());
    fastforward_shutdown();
}

/* Shift+Tab is the toggle, not the hold with Shift: Tab's key-up after it leaves it on. */
PTEST(shift_tab_toggles_and_does_not_hold)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(!fastforward_active());
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(fastforward_active());
    fastforward_shutdown();
}

PTEST(the_hold_and_the_toggle_together_end_when_both_are_off)
{
    start();
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(fastforward_active()); /* still held */
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(!fastforward_active());
    fastforward_shutdown();
}

/* Rebound keys: the defaults no longer do anything. */
PTEST(rebound_keys_are_the_ones_that_work)
{
    struct fastforward_settings settings = fastforward_defaults();
    settings.hold = (struct fastforward_key){SDL_SCANCODE_SPACE, 0u};
    settings.toggle = (struct fastforward_key){SDL_SCANCODE_F, FASTFORWARD_CTRL};
    fastforward_shutdown();
    PT_ASSERT_EQ_INT(fastforward_init(&settings), 0);
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(!fastforward_active());
    press(SDL_SCANCODE_SPACE, KMOD_NONE);
    PT_ASSERT(fastforward_active());
    release(SDL_SCANCODE_SPACE, KMOD_NONE);
    press(SDL_SCANCODE_F, KMOD_LCTRL);
    PT_ASSERT(fastforward_active());
    press(SDL_SCANCODE_F, KMOD_LCTRL);
    PT_ASSERT(!fastforward_active());
    fastforward_shutdown();
}

/* ---- the overlay ---- */

PTEST(while_the_overlay_is_shown_presses_are_its_own)
{
    start();
    PT_ASSERT_EQ_INT(overlay_init(no_text), 0);
    press_f1();
    PT_ASSERT(overlay_shown());
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(!fastforward_active());
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    PT_ASSERT(!fastforward_active());
    press_f1();
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    const bool after = fastforward_active();
    overlay_shutdown();
    fastforward_shutdown();
    PT_ASSERT(after);
}

/* Held, then the overlay: on until the key comes up, which ends it under the overlay too. */
PTEST(under_the_overlay_the_hold_still_ends_on_its_key_up)
{
    start();
    PT_ASSERT_EQ_INT(overlay_init(no_text), 0);
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    press_f1();
    const bool on_under_it = fastforward_active();
    const bool indicator_under_it = fastforward_indicator_shown();
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    const bool after_release = fastforward_active();
    overlay_shutdown();
    fastforward_shutdown();
    PT_ASSERT(on_under_it);
    PT_ASSERT(!indicator_under_it);
    PT_ASSERT(!after_release);
}

/* A toggle switched on stays on while the overlay is shown, with the indicator hidden. */
PTEST(under_the_overlay_a_toggle_stays_on_and_the_indicator_hides)
{
    start();
    PT_ASSERT_EQ_INT(overlay_init(no_text), 0);
    press(SDL_SCANCODE_TAB, KMOD_LSHIFT);
    const bool shown_before = fastforward_indicator_shown();
    press_f1();
    const bool on_under_it = fastforward_active();
    const bool shown_under_it = fastforward_indicator_shown();
    press_f1();
    const bool shown_after = fastforward_indicator_shown();
    overlay_shutdown();
    fastforward_shutdown();
    PT_ASSERT(shown_before);
    PT_ASSERT(on_under_it);
    PT_ASSERT(!shown_under_it);
    PT_ASSERT(shown_after);
    PT_ASSERT(!fastforward_indicator_shown());
}

/* ---- the speeds and the sound ---- */

PTEST(the_multiplier_is_the_speed_while_on)
{
    static const uint32_t speeds[] = {2u, 3u, 4u, FASTFORWARD_UNLIMITED};
    for (size_t i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
        start_with(speeds[i], FASTFORWARD_AUDIO_PLAY);
        PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
        press(SDL_SCANCODE_TAB, KMOD_NONE);
        PT_ASSERT_EQ_U32(fastforward_multiplier(), speeds[i]);
        PT_ASSERT(!fastforward_muted());
        fastforward_shutdown();
    }
}

PTEST(mute_is_the_sound_off_while_fast_forward_is_on)
{
    start_with(3u, FASTFORWARD_AUDIO_MUTE);
    PT_ASSERT(!fastforward_muted());
    press(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(fastforward_muted());
    release(SDL_SCANCODE_TAB, KMOD_NONE);
    PT_ASSERT(!fastforward_muted());
    fastforward_shutdown();
}

PTEST(init_needs_settings_a_speed_two_keys_and_is_called_once)
{
    struct fastforward_settings settings = fastforward_defaults();
    fastforward_shutdown();
    PT_ASSERT_EQ_INT(fastforward_init(NULL), -1);
    settings.speed = 1u;
    PT_ASSERT_EQ_INT(fastforward_init(&settings), -1);
    settings.speed = FASTFORWARD_MAX_SPEED + 1u;
    PT_ASSERT_EQ_INT(fastforward_init(&settings), -1);
    settings = fastforward_defaults();
    settings.toggle = settings.hold;
    PT_ASSERT_EQ_INT(fastforward_init(&settings), -1);
    PT_ASSERT(!fastforward_enabled());
    settings = fastforward_defaults();
    PT_ASSERT_EQ_INT(fastforward_init(&settings), 0);
    PT_ASSERT_EQ_INT(fastforward_init(&settings), -1);
    PT_ASSERT(fastforward_enabled());
    fastforward_shutdown();
}

/* ---- the indicator ---- */

static void check_text(uint32_t speed, const char *expected)
{
    char text[FASTFORWARD_INDICATOR_TEXT_SIZE];
    fastforward_indicator_text(speed, text, sizeof text);
    PT_ASSERT_EQ_STR(text, expected);
}

PTEST(the_indicator_says_the_speed)
{
    check_text(2u, "FF 2x");
    check_text(3u, "FF 3x");
    check_text(4u, "FF 4x");
    check_text(FASTFORWARD_UNLIMITED, "FF max");
}

/* "FF 2x": 5 cells of 6 less the last cell's column of space, 29, and a pixel each side: 31
 * wide; 8 rows and a pixel above and below: 10 high; 2 from the right (240 - 2 - 31 = 207) and
 * from the top. "FF max" is a cell wider. */
PTEST(the_indicator_sits_in_the_top_right_corner)
{
    const struct fastforward_rect two = fastforward_indicator_place("FF 2x");
    PT_ASSERT_EQ_U32(two.x, 207u);
    PT_ASSERT_EQ_U32(two.y, 2u);
    PT_ASSERT_EQ_U32(two.width, 31u);
    PT_ASSERT_EQ_U32(two.height, 10u);
    const struct fastforward_rect max = fastforward_indicator_place("FF max");
    PT_ASSERT_EQ_U32(max.x, 201u);
    PT_ASSERT_EQ_U32(max.y, 2u);
    PT_ASSERT_EQ_U32(max.width, 37u);
    PT_ASSERT_EQ_U32(max.height, 10u);
}

PTEST(the_box_is_the_started_speeds)
{
    start_with(FASTFORWARD_UNLIMITED, FASTFORWARD_AUDIO_PLAY);
    const struct fastforward_rect box = fastforward_indicator_box();
    fastforward_shutdown();
    PT_ASSERT_EQ_U32(box.x, 201u);
    PT_ASSERT_EQ_U32(box.width, 37u);
}

/* Draws the indicator on a canvas of its box and compares it with the background and text
 * drawn a padding from the top-left: true if the same, pixel for pixel. */
static bool indicator_draws(size_t width, size_t height, const char *text)
{
    uint32_t *const drawn = calloc(width * height, sizeof(uint32_t));
    uint32_t *const expected = calloc(width * height, sizeof(uint32_t));
    bool same = false;
    if (drawn != NULL && expected != NULL) {
        const struct font_canvas drawn_canvas = {.pixels = drawn, .width = width, .height = height, .stride = width};
        const struct font_canvas expected_canvas = {
            .pixels = expected, .width = width, .height = height, .stride = width};
        for (size_t i = 0; i < width * height; i++)
            expected[i] = OVERLAY_BACKGROUND_ARGB;
        if (text != NULL) {
            struct font_pen pen = {.x = 1, .y = 1, .scale = 1, .color = OVERLAY_TEXT_ARGB};
            font_draw_text(&expected_canvas, &pen, text, strlen(text));
        }
        fastforward_draw_indicator(&drawn_canvas);
        same = memcmp(drawn, expected, width * height * sizeof(uint32_t)) == 0;
    }
    free(drawn);
    free(expected);
    return same;
}

PTEST(the_indicator_is_its_text_on_the_overlays_background)
{
    start_with(4u, FASTFORWARD_AUDIO_PLAY);
    const bool four = indicator_draws(31, 10, "FF 4x");
    start_with(FASTFORWARD_UNLIMITED, FASTFORWARD_AUDIO_MUTE);
    const bool max = indicator_draws(37, 10, "FF max");
    fastforward_shutdown();
    PT_ASSERT(four);
    PT_ASSERT(max);
    PT_ASSERT(indicator_draws(31, 10, NULL));
}

PTEST_MAIN()
