// deps: fastforward.c input.c controller.c overlay.c font.c gba_memmap.c
/*
 * test_bubble_menu.c: the bubble and its menu (bubble_menu.c), driven by
 * SDL events as the frame loop passes them: where the bubble, the tabs, the
 * rows and the footer are; opening and closing it by the bubble, F5, Esc,
 * Close and a right click, and the game's keys held back meanwhile; every
 * tab's rows doing what they say to fast-forward, the cheats (through fake
 * hooks), the keymap, the controller map and the controller's functions;
 * Save; and the bubble and the panel drawn.
 */
#include "bubble_menu.h"
#include "controller.h"
#include "fastforward.h"
#include "gba_memmap.h"
#include "input.h"
#include "overlay.h"
#include "ptest.h"

#include <SDL_events.h>
#include <stdlib.h>
#include <string.h>

/* The places of the header's layout, in LCD pixels: a point inside each. */
#define BUBBLE_POINT_X 8
#define BUBBLE_POINT_Y 8
#define TAB_Y 8
static const int tab_points[BUBBLE_MENU_PAGE_COUNT] = {25, 70, 115, 150, 195};
#define ROW_POINT_X 60
#define SAVE_X 10
#define CLOSE_X 205
#define FOOTER_Y 150

static int row_y(size_t row)
{
    return 17 + 10 * (int)row + 5;
}

/* ---- the fake game side ---- */

static struct {
    bool available;
    enum cheat_result result;
    int runs;
    enum cheat last;
    bool on[CHEAT_COUNT];
    int saves;
    int save_result;
} fake;

static bool fake_available(void)
{
    return fake.available;
}

static enum cheat_result fake_run(enum cheat cheat)
{
    fake.runs++;
    fake.last = cheat;
    return fake.result;
}

static bool fake_on(enum cheat cheat)
{
    return (unsigned)cheat < CHEAT_COUNT && fake.on[cheat];
}

static int fake_save(void)
{
    fake.saves++;
    return fake.save_result;
}

static const struct bubble_menu_hooks hooks = {
    .cheats_available = fake_available,
    .cheat_run = fake_run,
    .cheat_on = fake_on,
    .save_settings = fake_save,
};

static void stop(void)
{
    bubble_menu_shutdown();
    input_shutdown();
    controller_shutdown();
    fastforward_shutdown();
    overlay_shutdown();
}

static void start(void)
{
    const struct fastforward_settings fast = fastforward_defaults();
    const struct controller_bindings bindings = controller_default_bindings();
    stop();
    memset(&fake, 0, sizeof fake);
    fake.available = true;
    PT_ASSERT_EQ_INT(fastforward_init(&fast), 0);
    PT_ASSERT_EQ_INT(controller_init(&bindings), 0);
    PT_ASSERT_EQ_INT(input_init(INPUT_SOURCE_SCRIPT, NULL), 0);
    PT_ASSERT_EQ_INT(bubble_menu_init(&hooks), 0);
}

/* GBATEK "GBA Keypad Input": KEYINPUT at 4000130h. */
#define KEYINPUT_ADDRESS (GBA_IO_BASE + UINT32_C(0x130))

static uint16_t keyinput(void)
{
    return *(volatile uint16_t *)(uintptr_t)KEYINPUT_ADDRESS;
}

/* ---- events ---- */

static bool click_with(int x, int y, Uint8 button)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = button;
    event.button.x = x;
    event.button.y = y;
    return bubble_menu_handle_event(&event);
}

static bool click(int x, int y)
{
    return click_with(x, y, SDL_BUTTON_LEFT);
}

static bool move_to(int x, int y)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_MOUSEMOTION;
    event.motion.x = x;
    event.motion.y = y;
    return bubble_menu_handle_event(&event);
}

static bool key(SDL_Scancode scancode)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = scancode;
    return bubble_menu_handle_event(&event);
}

static bool pad_button(SDL_GameControllerButton button)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERBUTTONDOWN;
    event.cbutton.button = (Uint8)button;
    return bubble_menu_handle_event(&event);
}

static bool pad_axis(SDL_GameControllerAxis axis, Sint16 value)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.axis = (Uint8)axis;
    event.caxis.value = value;
    return bubble_menu_handle_event(&event);
}

static void open_on(enum bubble_menu_page page)
{
    if (!bubble_menu_open())
        PT_ASSERT(click(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    PT_ASSERT(click(tab_points[page], TAB_Y));
    PT_ASSERT_EQ_INT(bubble_menu_page(), page);
}

static void click_row(size_t row)
{
    PT_ASSERT(click(ROW_POINT_X, row_y(row)));
}

static bool row_is(size_t row, const char *expected)
{
    char text[64];
    bubble_menu_row_text(row, text, sizeof text);
    return strcmp(text, expected) == 0;
}

/* ---- starting ---- */

PTEST(init_needs_every_hook_and_is_called_once)
{
    struct bubble_menu_hooks missing = hooks;
    stop();
    PT_ASSERT_EQ_INT(bubble_menu_init(NULL), -1);
    missing.save_settings = NULL;
    PT_ASSERT_EQ_INT(bubble_menu_init(&missing), -1);
    missing = hooks;
    missing.cheat_on = NULL;
    PT_ASSERT_EQ_INT(bubble_menu_init(&missing), -1);
    PT_ASSERT(!bubble_menu_enabled());
    PT_ASSERT_EQ_INT(bubble_menu_init(&hooks), 0);
    PT_ASSERT_EQ_INT(bubble_menu_init(&hooks), -1);
    PT_ASSERT(bubble_menu_enabled());
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT_EQ_INT(bubble_menu_page(), BUBBLE_MENU_SPEED);
    stop();
    PT_ASSERT(!bubble_menu_enabled());
    PT_ASSERT(!bubble_menu_handle_event(NULL));
}

PTEST(not_started_it_takes_no_event)
{
    stop();
    PT_ASSERT(!click(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    PT_ASSERT(!key(SDL_SCANCODE_F5));
    PT_ASSERT(!bubble_menu_open());
}

/* ---- where things are ---- */

PTEST(the_bubble_is_its_square_and_the_rest_only_while_open)
{
    const int last = BUBBLE_MENU_BUBBLE_SIZE - 1;
    PT_ASSERT_EQ_INT(bubble_menu_hit(BUBBLE_MENU_BUBBLE_X, BUBBLE_MENU_BUBBLE_Y, false).kind, BUBBLE_MENU_BUBBLE);
    PT_ASSERT_EQ_INT(bubble_menu_hit(BUBBLE_MENU_BUBBLE_X + last, BUBBLE_MENU_BUBBLE_Y + last, false).kind,
                     BUBBLE_MENU_BUBBLE);
    PT_ASSERT_EQ_INT(bubble_menu_hit(BUBBLE_MENU_BUBBLE_X + last + 1, BUBBLE_MENU_BUBBLE_Y, false).kind,
                     BUBBLE_MENU_NOTHING);
    PT_ASSERT_EQ_INT(bubble_menu_hit(BUBBLE_MENU_BUBBLE_X - 1, BUBBLE_MENU_BUBBLE_Y, false).kind, BUBBLE_MENU_NOTHING);
    PT_ASSERT_EQ_INT(bubble_menu_hit(ROW_POINT_X, row_y(0), false).kind, BUBBLE_MENU_NOTHING); /* closed */
    PT_ASSERT_EQ_INT(bubble_menu_hit(-5, -5, true).kind, BUBBLE_MENU_NOTHING);
    for (size_t page = 0; page < BUBBLE_MENU_PAGE_COUNT; page++) {
        const struct bubble_menu_target tab = bubble_menu_hit(tab_points[page], TAB_Y, true);
        PT_ASSERT_EQ_INT(tab.kind, BUBBLE_MENU_TAB);
        PT_ASSERT_EQ_U32((uint32_t)tab.index, (uint32_t)page);
    }
    for (size_t row = 0; row < BUBBLE_MENU_ROWS; row++) {
        const struct bubble_menu_target target = bubble_menu_hit(ROW_POINT_X, row_y(row), true);
        PT_ASSERT_EQ_INT(target.kind, BUBBLE_MENU_ROW);
        PT_ASSERT_EQ_U32((uint32_t)target.index, (uint32_t)row);
    }
    PT_ASSERT_EQ_INT(bubble_menu_hit(ROW_POINT_X, 17, true).kind, BUBBLE_MENU_ROW);      /* the first row's top */
    PT_ASSERT_EQ_INT(bubble_menu_hit(ROW_POINT_X, 16, true).kind, BUBBLE_MENU_NOTHING);  /* above it */
    PT_ASSERT_EQ_INT(bubble_menu_hit(ROW_POINT_X, 136, true).kind, BUBBLE_MENU_ROW);     /* the last row's bottom */
    PT_ASSERT_EQ_INT(bubble_menu_hit(ROW_POINT_X, 137, true).kind, BUBBLE_MENU_NOTHING); /* below it */
    PT_ASSERT_EQ_INT(bubble_menu_hit(SAVE_X, FOOTER_Y, true).kind, BUBBLE_MENU_SAVE);
    PT_ASSERT_EQ_INT(bubble_menu_hit(CLOSE_X, FOOTER_Y, true).kind, BUBBLE_MENU_CLOSE);
    PT_ASSERT_EQ_INT(bubble_menu_hit(120, FOOTER_Y, true).kind, BUBBLE_MENU_NOTHING); /* the status line */
}

/* ---- opening and closing ---- */

PTEST(the_bubble_opens_and_closes_it_and_the_game_reads_no_button_meanwhile)
{
    start();
    PT_ASSERT(click(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    PT_ASSERT(bubble_menu_open());
    PT_ASSERT(overlay_takes_input());
    PT_ASSERT_EQ_U32(overlay_game_keys(INPUT_KEY_A), 0u);
    PT_ASSERT(click(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(!overlay_takes_input());
    stop();
}

PTEST(closed_it_takes_only_the_bubble_and_f5)
{
    start();
    PT_ASSERT(!click(ROW_POINT_X, row_y(2)));
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(!key(SDL_SCANCODE_Z));
    PT_ASSERT(!key(SDL_SCANCODE_ESCAPE));
    PT_ASSERT(!pad_button(SDL_CONTROLLER_BUTTON_A));
    PT_ASSERT(!move_to(100, 100));
    PT_ASSERT(click_with(BUBBLE_POINT_X, BUBBLE_POINT_Y, SDL_BUTTON_RIGHT)); /* the bubble's, but no click */
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(key(SDL_SCANCODE_F5));
    PT_ASSERT(bubble_menu_open());
    PT_ASSERT(key(SDL_SCANCODE_Z)); /* open, every key is its own */
    PT_ASSERT(pad_button(SDL_CONTROLLER_BUTTON_A));
    PT_ASSERT(key(SDL_SCANCODE_F5));
    PT_ASSERT(!bubble_menu_open());
    stop();
}

PTEST(esc_close_and_a_right_click_close_it)
{
    start();
    PT_ASSERT(key(SDL_SCANCODE_F5));
    PT_ASSERT(key(SDL_SCANCODE_ESCAPE));
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(key(SDL_SCANCODE_F5));
    PT_ASSERT(click(CLOSE_X, FOOTER_Y));
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(key(SDL_SCANCODE_F5));
    PT_ASSERT(click_with(ROW_POINT_X, row_y(3), SDL_BUTTON_RIGHT));
    PT_ASSERT(!bubble_menu_open());
    stop();
}

PTEST(a_repeating_f5_does_nothing)
{
    start();
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = SDL_SCANCODE_F5;
    event.key.repeat = 1;
    PT_ASSERT(!bubble_menu_handle_event(&event));
    PT_ASSERT(!bubble_menu_open());
    stop();
}

PTEST(the_revision_moves_with_what_is_shown)
{
    start();
    const uint32_t first = bubble_menu_revision();
    PT_ASSERT(!move_to(BUBBLE_POINT_X, BUBBLE_POINT_Y)); /* hovering the bubble lights it */
    const uint32_t hovered = bubble_menu_revision();
    PT_ASSERT(!move_to(BUBBLE_POINT_X, BUBBLE_POINT_Y)); /* no change, no repaint */
    const uint32_t same = bubble_menu_revision();
    PT_ASSERT(click(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    const uint32_t opened = bubble_menu_revision();
    PT_ASSERT(first != hovered);
    PT_ASSERT_EQ_U32(same, hovered);
    PT_ASSERT(opened != same);
    stop();
}

/* ---- Speed ---- */

PTEST(the_speed_rows_set_fast_forward_and_mark_the_one_in_force)
{
    start();
    open_on(BUBBLE_MENU_SPEED);
    PT_ASSERT(row_is(0, "Normal speed (1x)"));
    PT_ASSERT(row_is(1, "Fast: 2x"));
    PT_ASSERT(row_is(4, "Fast: as fast as it goes"));
    click_row(2);
    PT_ASSERT(fastforward_toggled());
    PT_ASSERT_EQ_U32(fastforward_speed(), 3u);
    PT_ASSERT_EQ_U32(fastforward_multiplier(), 3u);
    click_row(4);
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FASTFORWARD_UNLIMITED);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Fast: max");
    click_row(0);
    PT_ASSERT(!fastforward_toggled());
    PT_ASSERT_EQ_U32(fastforward_multiplier(), FRAME_PACER_NORMAL_SPEED);
    PT_ASSERT_EQ_U32(fastforward_speed(), FASTFORWARD_UNLIMITED); /* the speed Tab will use stays */
    PT_ASSERT(row_is(6, "Sound while fast         sped up"));
    click_row(6);
    PT_ASSERT_EQ_INT(fastforward_audio(), FASTFORWARD_AUDIO_MUTE);
    PT_ASSERT(row_is(6, "Sound while fast         muted"));
    click_row(5); /* an empty row, and a row past the last: nothing */
    click_row(11);
    PT_ASSERT_EQ_INT(fastforward_audio(), FASTFORWARD_AUDIO_MUTE);
    stop();
}

PTEST(the_speed_rows_say_so_when_fast_forward_is_not_started)
{
    start();
    fastforward_shutdown();
    open_on(BUBBLE_MENU_SPEED);
    click_row(3);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Fast-forward is off in this run");
    PT_ASSERT(!fastforward_toggled());
    stop();
}

/* ---- Cheats ---- */

PTEST(a_cheat_row_runs_its_cheat_and_says_how_it_went)
{
    start();
    open_on(BUBBLE_MENU_CHEATS);
    PT_ASSERT(row_is(1, "Heal the party"));
    click_row(1);
    PT_ASSERT_EQ_INT(fake.runs, 1);
    PT_ASSERT_EQ_INT(fake.last, CHEAT_HEAL_PARTY);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Done");
    fake.result = CHEAT_BAG_FULL;
    click_row(3);
    PT_ASSERT_EQ_INT(fake.last, CHEAT_RARE_CANDIES);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "No room in the bag");
    fake.result = CHEAT_NOT_NOW;
    click_row(2);
    PT_ASSERT_EQ_INT(fake.last, CHEAT_MAX_MONEY);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Overworld only");
    fake.result = CHEAT_UNSUPPORTED;
    click_row(7);
    PT_ASSERT_EQ_INT(fake.last, CHEAT_TRAINERS_IGNORE);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Not in this build");
    PT_ASSERT(bubble_menu_open()); /* a cheat leaves the menu open */
    stop();
}

PTEST(a_toggle_row_shows_its_state)
{
    start();
    open_on(BUBBLE_MENU_CHEATS);
    PT_ASSERT(row_is(5, "Walk through walls       off"));
    fake.on[CHEAT_WALK_THROUGH_WALLS] = true;
    PT_ASSERT(row_is(5, "Walk through walls       ON"));
    PT_ASSERT(row_is(6, "No wild encounters       off"));
    PT_ASSERT(row_is(9, "Saved with your game when you save."));
    fake.available = false;
    PT_ASSERT(row_is(9, "Cheats work in the overworld only."));
    stop();
}

PTEST(the_debug_menu_row_closes_the_menu_and_presses_r_and_start_for_the_game)
{
    start();
    open_on(BUBBLE_MENU_CHEATS);
    fake.available = false;
    click_row(0);
    PT_ASSERT(bubble_menu_open()); /* not in the overworld: it says so */
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Overworld only");
    fake.available = true;
    click_row(0);
    PT_ASSERT(!bubble_menu_open());
    PT_ASSERT(!overlay_takes_input());
    input_script_set(0);
    input_update_keyinput();
    const uint16_t pressed = keyinput();
    input_update_keyinput();
    input_update_keyinput();
    const uint16_t after = keyinput();
    stop();
    PT_ASSERT_EQ_U32(pressed, input_keys_to_keyinput(INPUT_KEY_R | INPUT_KEY_START));
    PT_ASSERT_EQ_U32(after, input_keys_to_keyinput(0));
}

/* ---- Keys ---- */

PTEST(a_key_row_takes_the_next_key)
{
    start();
    open_on(BUBBLE_MENU_KEYS);
    PT_ASSERT(row_is(0, "A          Z"));
    click_row(0);
    PT_ASSERT(row_is(0, "A          press a key (Esc)"));
    PT_ASSERT(key(SDL_SCANCODE_SPACE));
    PT_ASSERT_EQ_INT(input_current_keymap().scancodes[0], SDL_SCANCODE_SPACE);
    PT_ASSERT(row_is(0, "A          Space"));
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Key set");
    PT_ASSERT(bubble_menu_open());
    stop();
}

PTEST(esc_gives_up_a_capture_and_f1_and_f5_are_refused)
{
    start();
    open_on(BUBBLE_MENU_KEYS);
    click_row(1);
    PT_ASSERT(key(SDL_SCANCODE_F1));
    PT_ASSERT_EQ_STR(bubble_menu_status(), "That key is taken");
    PT_ASSERT(key(SDL_SCANCODE_F5)); /* still waiting: F5 is refused too, and does not close */
    PT_ASSERT(bubble_menu_open());
    PT_ASSERT(key(SDL_SCANCODE_ESCAPE));
    PT_ASSERT(bubble_menu_open()); /* Esc ended the capture, not the menu */
    PT_ASSERT(row_is(1, "B          X"));
    PT_ASSERT(key(SDL_SCANCODE_Q)); /* no capture now: nothing changes */
    PT_ASSERT_EQ_INT(input_current_keymap().scancodes[1], SDL_SCANCODE_X);
    stop();
}

PTEST(the_debug_menu_key_and_the_reset)
{
    start();
    open_on(BUBBLE_MENU_KEYS);
    PT_ASSERT(row_is(10, "Debug menu F4"));
    click_row(10);
    PT_ASSERT(key(SDL_SCANCODE_F6));
    PT_ASSERT_EQ_INT(input_current_keymap().debug_menu, SDL_SCANCODE_F6);
    click_row(2);
    PT_ASSERT(key(SDL_SCANCODE_TAB));
    PT_ASSERT_EQ_INT(input_current_keymap().scancodes[2], SDL_SCANCODE_TAB);
    click_row(11);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Keys reset");
    const struct input_keymap defaults = input_default_keymap();
    const struct input_keymap now = input_current_keymap();
    PT_ASSERT(memcmp(&now, &defaults, sizeof now) == 0);
    stop();
}

/* ---- Pad and Pad fn ---- */

PTEST(a_pad_row_takes_the_next_button_or_trigger)
{
    start();
    open_on(BUBBLE_MENU_PAD);
    PT_ASSERT(row_is(0, "No controller: plug one in"));
    PT_ASSERT(row_is(1, "A          A"));
    click_row(1);
    PT_ASSERT(row_is(1, "A          press a button (Esc)"));
    PT_ASSERT(key(SDL_SCANCODE_Z)); /* a key does not answer a button's capture */
    PT_ASSERT(pad_button(SDL_CONTROLLER_BUTTON_X));
    PT_ASSERT_EQ_INT(input_current_padmap().controls[0], SDL_CONTROLLER_BUTTON_X);
    PT_ASSERT(row_is(1, "A          X"));
    click_row(3);
    PT_ASSERT(pad_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 5000)); /* not far enough */
    PT_ASSERT_EQ_INT(input_current_padmap().controls[2], SDL_CONTROLLER_BUTTON_BACK);
    PT_ASSERT(pad_axis(SDL_CONTROLLER_AXIS_LEFTX, 30000)); /* a stick is no control */
    PT_ASSERT(pad_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000));
    PT_ASSERT_EQ_INT(input_current_padmap().controls[2], CONTROLLER_LEFT_TRIGGER);
    click_row(11);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Controller reset");
    PT_ASSERT_EQ_INT(input_current_padmap().controls[0], SDL_CONTROLLER_BUTTON_A);
    stop();
}

PTEST(a_function_takes_a_button_and_swaps_with_the_one_that_had_it)
{
    start();
    open_on(BUBBLE_MENU_PAD_FUNCTIONS);
    PT_ASSERT(row_is(0, "Fast-forward hold    Right trigger"));
    click_row(2); /* the help overlay, on Y */
    PT_ASSERT(pad_button(SDL_CONTROLLER_BUTTON_X)); /* quit's button */
    const struct controller_bindings now = controller_current_bindings();
    PT_ASSERT_EQ_INT(now.controls[CONTROLLER_OVERLAY], SDL_CONTROLLER_BUTTON_X);
    PT_ASSERT_EQ_INT(now.controls[CONTROLLER_QUIT], SDL_CONTROLLER_BUTTON_Y);
    PT_ASSERT(controller_bindings_are_valid(&now));
    click_row(0);
    PT_ASSERT(pad_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000)); /* fullscreen's trigger */
    PT_ASSERT_EQ_INT(controller_current_bindings().controls[CONTROLLER_FAST_FORWARD_HOLD], CONTROLLER_LEFT_TRIGGER);
    PT_ASSERT_EQ_INT(controller_current_bindings().controls[CONTROLLER_FULLSCREEN], CONTROLLER_RIGHT_TRIGGER);
    click_row(1);
    PT_ASSERT(click_with(ROW_POINT_X, row_y(1), SDL_BUTTON_RIGHT)); /* a right click gives up */
    PT_ASSERT(bubble_menu_open());
    PT_ASSERT(pad_button(SDL_CONTROLLER_BUTTON_A));
    PT_ASSERT_EQ_INT(controller_current_bindings().controls[CONTROLLER_FAST_FORWARD_TOGGLE],
                     SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    click_row(7);
    const struct controller_bindings defaults = controller_default_bindings();
    const struct controller_bindings reset = controller_current_bindings();
    PT_ASSERT(memcmp(&reset, &defaults, sizeof reset) == 0);
    stop();
}

PTEST(a_tab_change_ends_a_capture)
{
    start();
    open_on(BUBBLE_MENU_KEYS);
    click_row(0);
    open_on(BUBBLE_MENU_SPEED);
    open_on(BUBBLE_MENU_KEYS);
    PT_ASSERT(row_is(0, "A          Z"));
    PT_ASSERT(key(SDL_SCANCODE_SPACE));
    PT_ASSERT_EQ_INT(input_current_keymap().scancodes[0], SDL_SCANCODE_Z);
    stop();
}

/* ---- Save ---- */

PTEST(save_asks_the_game_side_and_says_how_it_went)
{
    start();
    open_on(BUBBLE_MENU_PAD);
    PT_ASSERT(click(SAVE_X, FOOTER_Y));
    PT_ASSERT_EQ_INT(fake.saves, 1);
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Saved to settings.ini");
    fake.save_result = -1;
    PT_ASSERT(click(SAVE_X, FOOTER_Y));
    PT_ASSERT_EQ_STR(bubble_menu_status(), "Save failed: see the log");
    PT_ASSERT(key(SDL_SCANCODE_ESCAPE));
    PT_ASSERT_EQ_STR(bubble_menu_status(), ""); /* closing clears it */
    stop();
}

/* ---- drawing ---- */

#define SENTINEL UINT32_C(0x12345678)

static bool every_pixel_written(void (*draw)(const struct font_canvas *), size_t width, size_t height)
{
    uint32_t *const pixels = malloc(width * height * sizeof(uint32_t));
    bool written = pixels != NULL;
    if (pixels == NULL)
        return false;
    for (size_t i = 0; i < width * height; i++)
        pixels[i] = SENTINEL;
    const struct font_canvas canvas = {.pixels = pixels, .width = width, .height = height, .stride = width};
    draw(&canvas);
    for (size_t i = 0; i < width * height; i++)
        written = written && pixels[i] != SENTINEL;
    free(pixels);
    return written;
}

PTEST(the_panel_and_the_bubble_fill_their_canvases)
{
    start();
    open_on(BUBBLE_MENU_CHEATS);
    PT_ASSERT(every_pixel_written(bubble_menu_draw_panel, 240u * 2u, 160u * 2u));
    PT_ASSERT(every_pixel_written(bubble_menu_draw_bubble, BUBBLE_MENU_BUBBLE_SIZE * 3u, BUBBLE_MENU_BUBBLE_SIZE * 3u));
    stop();
    PT_ASSERT(every_pixel_written(bubble_menu_draw_panel, 240u, 160u)); /* not started: the background */
}

PTEST(the_bubble_has_cut_corners_and_three_white_bars)
{
    enum { SCALE = 2, SIDE = BUBBLE_MENU_BUBBLE_SIZE * SCALE };
    static uint32_t pixels[SIDE * SIDE];
    const struct font_canvas canvas = {.pixels = pixels, .width = SIDE, .height = SIDE, .stride = SIDE};
    start();
    bubble_menu_draw_bubble(&canvas);
    const uint32_t corner = pixels[0];
    const uint32_t other_corner = pixels[SIDE - 1];
    const uint32_t edge = pixels[(SIDE / 2) * SIDE];
    const uint32_t bar = pixels[(4 * SCALE) * SIDE + 3 * SCALE];
    PT_ASSERT(!move_to(BUBBLE_POINT_X, BUBBLE_POINT_Y));
    bubble_menu_draw_bubble(&canvas);
    const uint32_t lit = pixels[(SIDE / 2) * SIDE];
    stop();
    PT_ASSERT_EQ_U32(corner, 0u); /* transparent */
    PT_ASSERT_EQ_U32(other_corner, 0u);
    PT_ASSERT_EQ_U32(edge, BUBBLE_MENU_BUBBLE_ARGB);
    PT_ASSERT_EQ_U32(bar, BUBBLE_MENU_TEXT_ARGB);
    PT_ASSERT_EQ_U32(lit, BUBBLE_MENU_BUBBLE_HOT_ARGB); /* hovered: lit */
}

PTEST_MAIN()
