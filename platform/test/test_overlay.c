// deps: controller.c font.c
/*
 * test_overlay.c: the help overlay (overlay.c): F1 shows and hides it, Q
 * quits only while it is shown, the game reads no button meanwhile, nothing
 * happens before it is started; and its picture: the background, the hint
 * and the text where the layout puts them, a long line going on in the
 * next, and lines past the bottom left out. The expected pictures are drawn
 * with font.h's own calls, which test_font.c checks pixel by pixel.
 */
#include "overlay.h"
#include "ptest.h"

#include <SDL_events.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define ALL_KEYS UINT16_C(0x03FF)

static SDL_Event key_event(Uint32 type, SDL_Scancode scancode, SDL_Keycode keycode, Uint8 repeat)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = keycode;
    event.key.repeat = repeat;
    return event;
}

static SDL_Event press(SDL_Scancode scancode, SDL_Keycode keycode)
{
    return key_event(SDL_KEYDOWN, scancode, keycode, 0);
}

/* The text the tests give the overlay: lines[] up to count. */
static struct {
    const char *const *lines;
    size_t count;
} given_text;

static void write_text(overlay_line_sink sink, void *context)
{
    for (size_t i = 0; i < given_text.count; i++)
        sink(given_text.lines[i], context);
}

static void start(const char *const *lines, size_t count)
{
    given_text.lines = lines;
    given_text.count = count;
    overlay_shutdown();
    PT_ASSERT_EQ_INT(overlay_init(write_text), 0);
}

PTEST(before_it_is_started_f1_does_nothing_and_the_keys_pass)
{
    overlay_shutdown();
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    PT_ASSERT_EQ_INT(overlay_handle_event(&f1), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_enabled());
    PT_ASSERT(!overlay_shown());
    PT_ASSERT_EQ_U32(overlay_game_keys(ALL_KEYS), ALL_KEYS);
    PT_ASSERT_EQ_INT(overlay_handle_event(NULL), OVERLAY_CONTINUE);
}

PTEST(f1_shows_and_hides_it_and_the_game_reads_no_button_while_shown)
{
    start(NULL, 0);
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    PT_ASSERT(overlay_enabled() && !overlay_shown());
    PT_ASSERT_EQ_U32(overlay_game_keys(ALL_KEYS), ALL_KEYS);
    PT_ASSERT_EQ_INT(overlay_handle_event(&f1), OVERLAY_CONTINUE);
    PT_ASSERT(overlay_shown());
    PT_ASSERT_EQ_U32(overlay_game_keys(ALL_KEYS), 0u);
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0001), 0u);
    PT_ASSERT_EQ_INT(overlay_handle_event(&f1), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    /* 0x0001 was held when it hid: it stays released until let go. */
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0009), 0x0008u);
    overlay_shutdown();
}

PTEST(a_held_f1_repeating_or_released_changes_nothing)
{
    start(NULL, 0);
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    const SDL_Event repeat = key_event(SDL_KEYDOWN, SDL_SCANCODE_F1, SDLK_F1, 1);
    const SDL_Event release = key_event(SDL_KEYUP, SDL_SCANCODE_F1, SDLK_F1, 0);
    (void)overlay_handle_event(&f1);
    (void)overlay_handle_event(&repeat);
    (void)overlay_handle_event(&release);
    PT_ASSERT(overlay_shown());
    overlay_shutdown();
}

PTEST(q_quits_only_while_shown)
{
    start(NULL, 0);
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    const SDL_Event q = press(SDL_SCANCODE_Q, SDLK_q);
    const SDL_Event q_repeat = key_event(SDL_KEYDOWN, SDL_SCANCODE_Q, SDLK_q, 1);
    PT_ASSERT_EQ_INT(overlay_handle_event(&q), OVERLAY_CONTINUE); /* hidden: Q may be a button */
    (void)overlay_handle_event(&f1);
    PT_ASSERT_EQ_INT(overlay_handle_event(&q_repeat), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_event(&q), OVERLAY_QUIT);
    overlay_shutdown();
}

/* On AZERTY the key that types q sits where QWERTY has A: the hint names the letter. */
PTEST(q_is_the_key_that_types_q_wherever_the_layout_puts_it)
{
    start(NULL, 0);
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    const SDL_Event azerty_q = press(SDL_SCANCODE_A, SDLK_q);
    const SDL_Event azerty_a = press(SDL_SCANCODE_Q, SDLK_a);
    (void)overlay_handle_event(&f1);
    PT_ASSERT_EQ_INT(overlay_handle_event(&azerty_a), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_event(&azerty_q), OVERLAY_QUIT);
    overlay_shutdown();
}

PTEST(other_keys_and_events_do_nothing)
{
    start(NULL, 0);
    const SDL_Event z = press(SDL_SCANCODE_Z, SDLK_z);
    const SDL_Event escape = press(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_QUIT;
    PT_ASSERT_EQ_INT(overlay_handle_event(&z), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_event(&escape), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_event(&quit), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    overlay_shutdown();
}

PTEST(init_needs_a_writer_once_and_shutdown_hides_and_stops_it)
{
    overlay_shutdown();
    PT_ASSERT(overlay_init(NULL) != 0);
    PT_ASSERT_EQ_INT(overlay_init(write_text), 0);
    PT_ASSERT(overlay_init(write_text) != 0);
    const SDL_Event f1 = press(SDL_SCANCODE_F1, SDLK_F1);
    (void)overlay_handle_event(&f1);
    overlay_shutdown();
    overlay_shutdown();
    PT_ASSERT(!overlay_shown() && !overlay_enabled());
    PT_ASSERT_EQ_U32(overlay_game_keys(ALL_KEYS), ALL_KEYS);
}

PTEST(the_glyphs_grow_with_every_second_window_scale)
{
    PT_ASSERT_EQ_U32(overlay_glyph_scale(0), 1u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(240), 1u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(480), 1u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(720), 1u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(960), 2u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(1200), 2u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(1440), 3u);
    PT_ASSERT_EQ_U32(overlay_glyph_scale(3840), 8u);
}

/* ---- the picture ---- */

struct picture {
    struct font_canvas canvas;
    uint32_t *pixels;
};

static bool make_picture(struct picture *out, size_t width, size_t height)
{
    out->pixels = calloc(width * height, sizeof(uint32_t));
    out->canvas = (struct font_canvas){.pixels = out->pixels, .width = width, .height = height, .stride = width};
    return out->pixels != NULL;
}

/* What the overlay should show: the background, and each (x, y, text) given drawn at scale. */
struct placed {
    size_t x;
    size_t y;
    const char *text;
};

/* Draws the overlay on a width x height canvas and compares it with the background and each of
 * placed drawn at scale: true if the two are the same, pixel for pixel. */
static bool overlay_draws(size_t width, size_t height, size_t scale, const struct placed *placed, size_t count)
{
    struct picture drawn;
    struct picture expected;
    const bool made = make_picture(&drawn, width, height);
    const bool made_expected = make_picture(&expected, width, height);
    bool same = false;
    if (made && made_expected) {
        for (size_t i = 0; i < width * height; i++)
            expected.pixels[i] = OVERLAY_BACKGROUND_ARGB;
        for (size_t i = 0; i < count; i++) {
            struct font_pen pen = {.x = placed[i].x, .y = placed[i].y, .scale = scale, .color = OVERLAY_TEXT_ARGB};
            font_draw_text(&expected.canvas, &pen, placed[i].text, strlen(placed[i].text));
        }
        overlay_draw(&drawn.canvas);
        same = memcmp(drawn.pixels, expected.pixels, width * height * sizeof(uint32_t)) == 0;
    }
    free(drawn.pixels);
    free(expected.pixels);
    return same;
}

PTEST(not_started_it_draws_the_background_only)
{
    overlay_shutdown();
    PT_ASSERT(overlay_draws(240, 160, 1, NULL, 0));
}

/* Scale 3 (720x480): glyphs at 1, a margin of a cell (6) and a line (10); the hint, a blank line,
 * then the text. */
PTEST(the_hint_a_blank_line_and_the_text_from_the_top_left_margin)
{
    static const char *const lines[] = {"pokeemerald-native v1", "Save: a.sav"};
    static const struct placed placed[] = {
        {6, 10, "Q: quit  F1: close"},
        {6, 30, "pokeemerald-native v1"},
        {6, 40, "Save: a.sav"},
    };
    start(lines, 2);
    const bool drawn = overlay_draws(720, 480, 1, placed, 3);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

/* Scale 4 (960x640): glyphs at 2, a margin of 12 and 20, lines 20 apart. */
PTEST(at_scale_four_the_glyphs_and_margins_double)
{
    static const char *const lines[] = {"Keyboard:"};
    static const struct placed placed[] = {{12, 20, "Q: quit  F1: close"}, {12, 60, "Keyboard:"}};
    start(lines, 1);
    const bool drawn = overlay_draws(960, 640, 2, placed, 2);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

/* Scale 1 (240x160): (240 - 12) / 6 = 38 characters a line; a line of 45 goes on with its last 7. */
PTEST(a_line_too_long_goes_on_in_the_next)
{
    static const char *const lines[] = {"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHI", "next"};
    static const struct placed placed[] = {
        {6, 10, "Q: quit  F1: close"},
        {6, 30, "0123456789abcdefghijklmnopqrstuvwxyzAB"},
        {6, 40, "CDEFGHI"},
        {6, 50, "next"},
    };
    start(lines, 2);
    const bool drawn = overlay_draws(240, 160, 1, placed, 4);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

/* Scale 1 holds (160 - 20) / 10 = 14 lines: the hint, the blank and 12 of the text's; the bottom
 * margin stays empty. */
PTEST(lines_past_the_bottom_are_left_out)
{
    static const char *const lines[] = {"l1", "l2", "l3", "l4", "l5", "l6", "l7",
                                        "l8", "l9", "lA", "lB", "lC", "lD", "lE"};
    static struct placed placed[13];
    placed[0] = (struct placed){6, 10, "Q: quit  F1: close"};
    for (size_t i = 0; i < 12; i++)
        placed[i + 1u] = (struct placed){6, 30 + 10 * i, lines[i]};
    start(lines, 14);
    const bool drawn = overlay_draws(240, 160, 1, placed, 13);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

PTEST(a_canvas_narrower_than_its_margins_gets_the_background_only)
{
    static const char *const lines[] = {"text"};
    start(lines, 1);
    const bool drawn = overlay_draws(12, 40, 1, NULL, 0);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

/* ---- the controller ---- */

static struct controller_edge pressed_edge(enum controller_function function)
{
    return (struct controller_edge){.function = function, .pressed = true};
}

static struct controller_edge released_edge(enum controller_function function)
{
    return (struct controller_edge){.function = function, .pressed = false};
}

PTEST(the_overlay_button_shows_and_hides_it_and_the_game_reads_no_button_while_shown)
{
    start(NULL, 0);
    const struct controller_edge y = pressed_edge(CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&y), OVERLAY_CONTINUE);
    PT_ASSERT(overlay_shown());
    PT_ASSERT_EQ_U32(overlay_game_keys(ALL_KEYS), 0u); /* a controller's buttons are keys here too */
    PT_ASSERT_EQ_INT(overlay_handle_controller(&y), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    overlay_shutdown();
}

PTEST(the_close_button_hides_it_and_does_nothing_while_hidden)
{
    start(NULL, 0);
    const struct controller_edge close = pressed_edge(CONTROLLER_OVERLAY_CLOSE);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&close), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown()); /* not a toggle */
    const struct controller_edge y = pressed_edge(CONTROLLER_OVERLAY);
    (void)overlay_handle_controller(&y);
    PT_ASSERT(overlay_shown());
    PT_ASSERT_EQ_INT(overlay_handle_controller(&close), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    overlay_shutdown();
}

PTEST(the_quit_button_quits_only_while_the_overlay_is_shown)
{
    start(NULL, 0);
    const struct controller_edge x = pressed_edge(CONTROLLER_QUIT);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&x), OVERLAY_CONTINUE);
    const struct controller_edge y = pressed_edge(CONTROLLER_OVERLAY);
    (void)overlay_handle_controller(&y);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&x), OVERLAY_QUIT);
    overlay_shutdown();
}

PTEST(releases_other_functions_null_and_an_unstarted_overlay_do_nothing)
{
    start(NULL, 0);
    const struct controller_edge released_y = released_edge(CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&released_y), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    const struct controller_edge hold = pressed_edge(CONTROLLER_FAST_FORWARD_HOLD);
    const struct controller_edge none = pressed_edge(CONTROLLER_NO_FUNCTION);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&hold), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&none), OVERLAY_CONTINUE);
    PT_ASSERT_EQ_INT(overlay_handle_controller(NULL), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
    overlay_shutdown();
    const struct controller_edge y = pressed_edge(CONTROLLER_OVERLAY);
    PT_ASSERT_EQ_INT(overlay_handle_controller(&y), OVERLAY_CONTINUE);
    PT_ASSERT(!overlay_shown());
}

/* The B that closes the overlay is still down on the next frame: the game must not take it for
 * a press (it would back out of a menu). */
PTEST(a_button_held_when_the_overlay_hides_stays_released_until_let_go)
{
    start(NULL, 0);
    const struct controller_edge y = pressed_edge(CONTROLLER_OVERLAY);
    const struct controller_edge close = pressed_edge(CONTROLLER_OVERLAY_CLOSE);
    (void)overlay_handle_controller(&y);
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0002u), 0u); /* B down while shown */
    (void)overlay_handle_controller(&close);
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0002u), 0u); /* still down: still released */
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0002u | 0x0010u), 0x0010u); /* a new button passes */
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0010u), 0x0010u);
    PT_ASSERT_EQ_U32(overlay_game_keys(0x0002u | 0x0010u), 0x0002u | 0x0010u); /* let go, pressed anew */
    overlay_shutdown();
}

static SDL_Event pad_button_down(void)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    return event;
}

static SDL_Event pad_axis(Sint16 value)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
    event.caxis.value = value;
    return event;
}

PTEST(the_hint_is_the_keyboards_until_a_controller_button_is_pressed_and_back_at_the_next_key)
{
    start(NULL, 0);
    PT_ASSERT(!overlay_hint_for_pad());
    const SDL_Event button = pad_button_down();
    (void)overlay_handle_event(&button);
    PT_ASSERT(overlay_hint_for_pad());
    const SDL_Event f2 = press(SDL_SCANCODE_F2, SDLK_F2);
    (void)overlay_handle_event(&f2);
    PT_ASSERT(!overlay_hint_for_pad());
    overlay_shutdown();
    PT_ASSERT(!overlay_hint_for_pad());
}

PTEST(a_stick_or_trigger_counts_as_the_controller_only_when_pushed_well_over)
{
    start(NULL, 0);
    SDL_Event axis = pad_axis(CONTROLLER_TRIGGER_PRESS);
    (void)overlay_handle_event(&axis); /* a stick's drift and rest */
    PT_ASSERT(!overlay_hint_for_pad());
    axis = pad_axis(-CONTROLLER_TRIGGER_PRESS);
    (void)overlay_handle_event(&axis);
    PT_ASSERT(!overlay_hint_for_pad());
    axis = pad_axis(-32768);
    (void)overlay_handle_event(&axis);
    PT_ASSERT(overlay_hint_for_pad());
    overlay_shutdown();
}

PTEST(with_the_controller_last_used_the_hint_names_its_buttons)
{
    static const char *const lines[] = {"Save: a.sav"};
    static const struct placed placed[] = {
        {6, 10, "X: quit  B: close"},
        {6, 30, "Save: a.sav"},
    };
    start(lines, 1);
    const SDL_Event button = pad_button_down();
    (void)overlay_handle_event(&button);
    const bool drawn = overlay_draws(720, 480, 1, placed, 2);
    overlay_shutdown();
    PT_ASSERT(drawn);
}

PTEST(the_hint_names_rebound_buttons)
{
    static const struct placed placed[] = {{6, 10, "Right stick click: quit  Y: close"}};
    struct controller_bindings b = controller_default_bindings();
    b.controls[CONTROLLER_QUIT] = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
    b.controls[CONTROLLER_FAST_FORWARD_TOGGLE] = SDL_CONTROLLER_BUTTON_DPAD_UP;
    b.controls[CONTROLLER_OVERLAY_CLOSE] = SDL_CONTROLLER_BUTTON_Y;
    b.controls[CONTROLLER_OVERLAY] = SDL_CONTROLLER_BUTTON_LEFTSTICK;
    controller_shutdown();
    PT_ASSERT_EQ_INT(controller_init(&b), 0);
    start(NULL, 0);
    const SDL_Event button = pad_button_down();
    (void)overlay_handle_event(&button);
    const bool drawn = overlay_draws(720, 480, 1, placed, 1);
    overlay_shutdown();
    controller_shutdown();
    PT_ASSERT(drawn);
}

PTEST_MAIN()
