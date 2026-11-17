/*
 * bubble_menu.c: the bubble and its menu (bubble_menu.h).
 *
 * The rows of a tab are built afresh from the modules' state each time they
 * are drawn or clicked, so the menu keeps no copy of a speed, a key or a
 * binding that could fall out of step with the module that owns it.
 */
#include "bubble_menu.h"

#include <stdio.h>
#include <string.h>

#include <SDL_events.h>

#include "controller.h"
#include "fastforward.h"
#include "gba_timing.h"
#include "input.h"
#include "overlay.h"

#define MODULE "bubble_menu"

/* The layout, in LCD pixels. */
#define TAB_X 20u
#define TAB_Y 3u
#define TAB_PADDING_CELLS 2u
#define ROW_X 4u
#define ROW_Y 17u
#define ROW_HEIGHT 10u
#define ROW_TEXT_X 11u
#define TEXT_TOP 1u
#define FOOTER_Y 146u
#define FOOTER_X 4u
#define SAVE_LABEL "[Save]"
#define CLOSE_LABEL "[Close]"
#define BAR_X 3u
#define BAR_WIDTH 8u
#define BAR_ROWS {4u, 7u, 10u}
#define BUBBLE_CORNER 2u

#define ROW_TEXT_SIZE (BUBBLE_MENU_ROW_CHARS + 1u)
/* Room to format a row before it is cut to ROW_TEXT_SIZE. */
#define FORMAT_SIZE 128u
#define STATUS_SIZE 32u
#define NAME_COLUMN 10
#define FUNCTION_COLUMN 20
#define CHEAT_COLUMN 24

/* How long the injected R and Start are held for the game. */
#define DEBUG_MENU_PRESS_FRAMES 2u
#define ESCAPE_SCANCODE 41 /* SDL_SCANCODE_ESCAPE */
#define LEFT_BUTTON 1      /* SDL_BUTTON_LEFT */
#define RIGHT_BUTTON 3     /* SDL_BUTTON_RIGHT */

static const char *const page_names[BUBBLE_MENU_PAGE_COUNT] = {"Speed", "Cheats", "Pad", "Pad fn", "Keys"};

enum row_action {
    ROW_INFO,
    ROW_SPEED,      /* arg: FRAME_PACER_NORMAL_SPEED (fast-forward off), or a fast-forward speed */
    ROW_AUDIO,
    ROW_DEBUG_MENU,
    ROW_CHEAT,      /* arg: an enum cheat */
    ROW_PAD_BUTTON, /* arg: a button's index */
    ROW_PAD_FUNCTION,
    ROW_PAD_RESET,
    ROW_KEY,        /* arg: a button's index */
    ROW_DEBUG_KEY,
    ROW_KEYS_RESET
};

struct row {
    char text[FORMAT_SIZE];
    enum row_action action;
    uint32_t arg;
    bool marked;
};

/* What a remapping row waits for. */
enum capture { CAPTURE_NONE, CAPTURE_KEY, CAPTURE_DEBUG_KEY, CAPTURE_PAD_BUTTON, CAPTURE_PAD_FUNCTION };

struct cheat_row {
    enum cheat cheat;
    const char *label;
    bool toggle;
};

static const struct cheat_row cheat_rows[] = {
    {CHEAT_HEAL_PARTY, "Heal the party", false},
    {CHEAT_MAX_MONEY, "Max money", false},
    {CHEAT_RARE_CANDIES, "+99 Rare Candies", false},
    {CHEAT_MASTER_BALLS, "+99 Master Balls", false},
    {CHEAT_WALK_THROUGH_WALLS, "Walk through walls", true},
    {CHEAT_NO_ENCOUNTERS, "No wild encounters", true},
    {CHEAT_TRAINERS_IGNORE, "Trainers ignore you", true},
};
#define CHEAT_ROW_COUNT (sizeof cheat_rows / sizeof cheat_rows[0])

static const uint32_t speed_rows[] = {FRAME_PACER_NORMAL_SPEED, 2u, 3u, 4u, FASTFORWARD_UNLIMITED};
#define SPEED_ROW_COUNT (sizeof speed_rows / sizeof speed_rows[0])

static struct {
    bool enabled;
    bool open;
    enum bubble_menu_page page;
    struct bubble_menu_target hover;
    enum capture capture;
    size_t capture_index;
    uint32_t revision;
    char status[STATUS_SIZE];
    struct bubble_menu_hooks hooks;
} menu;

/* ---- state ---- */

static void changed(void)
{
    menu.revision++;
}

static void set_status(const char *text)
{
    (void)snprintf(menu.status, sizeof menu.status, "%s", text);
    changed();
}

static void set_open(bool open)
{
    menu.open = open;
    menu.capture = CAPTURE_NONE;
    if (!open)
        menu.status[0] = '\0';
    overlay_set_menu_open(open);
    changed();
}

int bubble_menu_init(const struct bubble_menu_hooks *hooks)
{
    if (hooks == NULL || hooks->cheats_available == NULL || hooks->cheat_run == NULL || hooks->cheat_on == NULL
        || hooks->save_settings == NULL || menu.enabled) {
        (void)fprintf(stderr, MODULE ": bubble_menu_init needs every hook and is called once\n");
        return -1;
    }
    memset(&menu, 0, sizeof menu);
    menu.hooks = *hooks;
    menu.enabled = true;
    return 0;
}

void bubble_menu_shutdown(void)
{
    if (menu.open)
        overlay_set_menu_open(false);
    memset(&menu, 0, sizeof menu);
}

bool bubble_menu_enabled(void)
{
    return menu.enabled;
}

bool bubble_menu_open(void)
{
    return menu.enabled && menu.open;
}

enum bubble_menu_page bubble_menu_page(void)
{
    return menu.page;
}

uint32_t bubble_menu_revision(void)
{
    return menu.revision;
}

const char *bubble_menu_status(void)
{
    return menu.status;
}

/* ---- the rows ---- */

static void row_set(struct row *row, enum row_action action, uint32_t arg, bool marked, const char *text)
{
    row->action = action;
    row->arg = arg;
    row->marked = marked;
    size_t i = 0;
    for (; i + 1u < sizeof row->text && text[i] != '\0'; i++)
        row->text[i] = text[i];
    row->text[i] = '\0';
}

static const char *waiting(enum capture capture)
{
    return capture == CAPTURE_KEY || capture == CAPTURE_DEBUG_KEY ? "press a key (Esc)" : "press a button (Esc)";
}

static bool capturing(enum capture capture, size_t index)
{
    return menu.capture == capture && menu.capture_index == index;
}

static size_t speed_rows_build(struct row *rows)
{
    char text[FORMAT_SIZE];
    const bool on = fastforward_toggled();
    const uint32_t speed = fastforward_speed();
    for (size_t i = 0; i < SPEED_ROW_COUNT; i++) {
        const uint32_t arg = speed_rows[i];
        const bool marked = arg == FRAME_PACER_NORMAL_SPEED ? !on : on && speed == arg;
        if (arg == FRAME_PACER_NORMAL_SPEED)
            (void)snprintf(text, sizeof text, "Normal speed (1x)");
        else if (arg == FASTFORWARD_UNLIMITED)
            (void)snprintf(text, sizeof text, "Fast: as fast as it goes");
        else
            (void)snprintf(text, sizeof text, "Fast: %lux", (unsigned long)arg);
        row_set(&rows[i], ROW_SPEED, arg, marked, text);
    }
    size_t n = SPEED_ROW_COUNT;
    row_set(&rows[n++], ROW_INFO, 0, false, "");
    (void)snprintf(text, sizeof text, "%-*s %s", CHEAT_COLUMN, "Sound while fast",
                   fastforward_audio() == FASTFORWARD_AUDIO_MUTE ? "muted" : "sped up");
    row_set(&rows[n++], ROW_AUDIO, 0, false, text);
    row_set(&rows[n++], ROW_INFO, 0, false, "");
    row_set(&rows[n++], ROW_INFO, 0, false, "Tab held: fast at the speed above");
    row_set(&rows[n++], ROW_INFO, 0, false, "Shift+Tab on/off, F2/F3 slower/faster");
    return n;
}

static size_t cheat_rows_build(struct row *rows)
{
    char text[FORMAT_SIZE];
    size_t n = 0;
    row_set(&rows[n++], ROW_DEBUG_MENU, 0, false, "Open the debug menu (every cheat)");
    for (size_t i = 0; i < CHEAT_ROW_COUNT; i++) {
        const struct cheat_row *const c = &cheat_rows[i];
        const bool on = c->toggle && menu.hooks.cheat_on(c->cheat);
        if (c->toggle)
            (void)snprintf(text, sizeof text, "%-*s %s", CHEAT_COLUMN, c->label, on ? "ON" : "off");
        else
            (void)snprintf(text, sizeof text, "%s", c->label);
        row_set(&rows[n++], ROW_CHEAT, (uint32_t)c->cheat, on, text);
    }
    row_set(&rows[n++], ROW_INFO, 0, false, "");
    row_set(&rows[n++], ROW_INFO, 0, false,
            menu.hooks.cheats_available() ? "Saved with your game when you save."
                                          : "Cheats work in the overworld only.");
    return n;
}

static size_t pad_rows_build(struct row *rows)
{
    char text[FORMAT_SIZE];
    const char *names[INPUT_MAX_CONTROLLERS];
    const size_t count = input_controller_names(names, INPUT_MAX_CONTROLLERS);
    const struct input_padmap padmap = input_current_padmap();
    const struct input_button *const buttons = input_buttons();
    size_t n = 0;
    if (count == 0)
        (void)snprintf(text, sizeof text, "No controller: plug one in");
    else if (count == 1)
        (void)snprintf(text, sizeof text, "%s", names[0]);
    else
        (void)snprintf(text, sizeof text, "%lu controllers: %s, ...", (unsigned long)count, names[0]);
    row_set(&rows[n++], ROW_INFO, 0, false, text);
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        (void)snprintf(text, sizeof text, "%-*s %s", NAME_COLUMN, buttons[i].name,
                       capturing(CAPTURE_PAD_BUTTON, i) ? waiting(CAPTURE_PAD_BUTTON)
                                                        : controller_control_label(padmap.controls[i]));
        row_set(&rows[n++], ROW_PAD_BUTTON, (uint32_t)i, false, text);
    }
    row_set(&rows[n++], ROW_PAD_RESET, 0, false, "Reset the controller");
    return n;
}

static size_t function_rows_build(struct row *rows)
{
    char text[FORMAT_SIZE];
    const struct controller_function_info *const functions = controller_functions();
    const struct controller_bindings bindings = controller_current_bindings();
    size_t n = 0;
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        (void)snprintf(text, sizeof text, "%-*s %s", FUNCTION_COLUMN, functions[i].name,
                       capturing(CAPTURE_PAD_FUNCTION, i) ? waiting(CAPTURE_PAD_FUNCTION)
                                                          : controller_control_label(bindings.controls[i]));
        row_set(&rows[n++], ROW_PAD_FUNCTION, (uint32_t)i, false, text);
    }
    row_set(&rows[n++], ROW_INFO, 0, false, "");
    row_set(&rows[n++], ROW_PAD_RESET, 0, false, "Reset the controller");
    return n;
}

static size_t key_rows_build(struct row *rows)
{
    char text[FORMAT_SIZE];
    const struct input_keymap keymap = input_current_keymap();
    const struct input_button *const buttons = input_buttons();
    size_t n = 0;
    for (size_t i = 0; i < INPUT_BUTTON_COUNT; i++) {
        (void)snprintf(text, sizeof text, "%-*s %s", NAME_COLUMN, buttons[i].name,
                       capturing(CAPTURE_KEY, i) ? waiting(CAPTURE_KEY) : input_scancode_name(keymap.scancodes[i]));
        row_set(&rows[n++], ROW_KEY, (uint32_t)i, false, text);
    }
    const char *const debug_key = menu.capture == CAPTURE_DEBUG_KEY        ? waiting(CAPTURE_DEBUG_KEY)
                                  : keymap.debug_menu == INPUT_NO_SCANCODE ? "none (R+Start)"
                                                                           : input_scancode_name(keymap.debug_menu);
    (void)snprintf(text, sizeof text, "%-*s %s", NAME_COLUMN, "Debug menu", debug_key);
    row_set(&rows[n++], ROW_DEBUG_KEY, 0, false, text);
    row_set(&rows[n++], ROW_KEYS_RESET, 0, false, "Reset the keys");
    return n;
}

/* The rows of the tab shown; their count, at most BUBBLE_MENU_ROWS. */
static size_t build_rows(struct row rows[BUBBLE_MENU_ROWS])
{
    switch (menu.page) {
    case BUBBLE_MENU_SPEED:
        return speed_rows_build(rows);
    case BUBBLE_MENU_CHEATS:
        return cheat_rows_build(rows);
    case BUBBLE_MENU_PAD:
        return pad_rows_build(rows);
    case BUBBLE_MENU_PAD_FUNCTIONS:
        return function_rows_build(rows);
    case BUBBLE_MENU_KEYS:
    default:
        return key_rows_build(rows);
    }
}

void bubble_menu_row_text(size_t row, char *out, size_t size)
{
    struct row rows[BUBBLE_MENU_ROWS];
    if (size == 0)
        return;
    out[0] = '\0';
    if (!menu.enabled)
        return;
    const size_t count = build_rows(rows);
    if (row < count)
        (void)snprintf(out, size, "%s", rows[row].text);
}

/* ---- geometry ---- */

static size_t tab_x(size_t page)
{
    size_t x = TAB_X;
    for (size_t i = 0; i < page; i++)
        x += (strlen(page_names[i]) + TAB_PADDING_CELLS) * FONT_CELL_WIDTH;
    return x;
}

static size_t tab_width(size_t page)
{
    return (strlen(page_names[page]) + TAB_PADDING_CELLS) * FONT_CELL_WIDTH;
}

static size_t close_x(void)
{
    return GBA_LCD_VISIBLE_DOTS - FOOTER_X - (sizeof CLOSE_LABEL - 1u) * FONT_CELL_WIDTH;
}

static bool inside(int x, int y, size_t left, size_t top, size_t width, size_t height)
{
    return x >= (int)left && y >= (int)top && x < (int)(left + width) && y < (int)(top + height);
}

struct bubble_menu_target bubble_menu_hit(int x, int y, bool open)
{
    struct bubble_menu_target target = {.kind = BUBBLE_MENU_NOTHING, .index = 0};
    if (inside(x, y, BUBBLE_MENU_BUBBLE_X, BUBBLE_MENU_BUBBLE_Y, BUBBLE_MENU_BUBBLE_SIZE, BUBBLE_MENU_BUBBLE_SIZE)) {
        target.kind = BUBBLE_MENU_BUBBLE;
        return target;
    }
    if (!open)
        return target;
    for (size_t page = 0; page < BUBBLE_MENU_PAGE_COUNT; page++) {
        if (inside(x, y, tab_x(page), TAB_Y, tab_width(page), ROW_HEIGHT)) {
            target = (struct bubble_menu_target){.kind = BUBBLE_MENU_TAB, .index = page};
            return target;
        }
    }
    const size_t row_width = GBA_LCD_VISIBLE_DOTS - 2u * ROW_X;
    if (inside(x, y, ROW_X, ROW_Y, row_width, BUBBLE_MENU_ROWS * ROW_HEIGHT)) {
        target = (struct bubble_menu_target){.kind = BUBBLE_MENU_ROW, .index = (size_t)y - ROW_Y};
        target.index /= ROW_HEIGHT;
        return target;
    }
    if (inside(x, y, FOOTER_X, FOOTER_Y, (sizeof SAVE_LABEL - 1u) * FONT_CELL_WIDTH, ROW_HEIGHT))
        target.kind = BUBBLE_MENU_SAVE;
    else if (inside(x, y, close_x(), FOOTER_Y, (sizeof CLOSE_LABEL - 1u) * FONT_CELL_WIDTH, ROW_HEIGHT))
        target.kind = BUBBLE_MENU_CLOSE;
    return target;
}

/* ---- actions ---- */

static void choose_speed(uint32_t speed)
{
    if (speed == FRAME_PACER_NORMAL_SPEED) {
        fastforward_set_toggled(false);
        set_status("Normal speed");
        return;
    }
    if (fastforward_set_speed(speed) != 0) {
        set_status("Fast-forward is off in this run");
        return;
    }
    fastforward_set_toggled(true);
    set_status(speed == FASTFORWARD_UNLIMITED ? "Fast: max" : "Fast-forward on");
}

static void open_debug_menu(void)
{
    if (!menu.hooks.cheats_available()) {
        set_status("Overworld only");
        return;
    }
    set_open(false);
    input_inject((uint16_t)(INPUT_KEY_R | INPUT_KEY_START), DEBUG_MENU_PRESS_FRAMES);
}

static void run_cheat(enum cheat cheat)
{
    switch (menu.hooks.cheat_run(cheat)) {
    case CHEAT_DONE:
        set_status("Done");
        break;
    case CHEAT_NOT_NOW:
        set_status("Overworld only");
        break;
    case CHEAT_BAG_FULL:
        set_status("No room in the bag");
        break;
    default:
        set_status("Not in this build");
        break;
    }
}

static void reset_controller(void)
{
    const struct input_padmap padmap = input_default_padmap();
    const struct controller_bindings bindings = controller_default_bindings();
    (void)input_set_padmap(&padmap);
    (void)controller_set_bindings(&bindings);
    set_status("Controller reset");
}

static void reset_keys(void)
{
    const struct input_keymap keymap = input_default_keymap();
    (void)input_set_keymap(&keymap);
    set_status("Keys reset");
}

static void start_capture(enum capture capture, size_t index)
{
    menu.capture = capture;
    menu.capture_index = index;
    set_status(capture == CAPTURE_KEY || capture == CAPTURE_DEBUG_KEY ? "Press the new key" : "Press the new button");
}

static void activate(const struct row *row)
{
    switch (row->action) {
    case ROW_SPEED:
        choose_speed(row->arg);
        break;
    case ROW_AUDIO:
        fastforward_set_audio(fastforward_audio() == FASTFORWARD_AUDIO_MUTE ? FASTFORWARD_AUDIO_PLAY
                                                                           : FASTFORWARD_AUDIO_MUTE);
        changed();
        break;
    case ROW_DEBUG_MENU:
        open_debug_menu();
        break;
    case ROW_CHEAT:
        run_cheat((enum cheat)row->arg);
        break;
    case ROW_PAD_BUTTON:
        start_capture(CAPTURE_PAD_BUTTON, row->arg);
        break;
    case ROW_PAD_FUNCTION:
        start_capture(CAPTURE_PAD_FUNCTION, row->arg);
        break;
    case ROW_PAD_RESET:
        reset_controller();
        break;
    case ROW_KEY:
        start_capture(CAPTURE_KEY, row->arg);
        break;
    case ROW_DEBUG_KEY:
        start_capture(CAPTURE_DEBUG_KEY, 0);
        break;
    case ROW_KEYS_RESET:
        reset_keys();
        break;
    case ROW_INFO:
    default:
        break;
    }
}

static void click_row(size_t index)
{
    struct row rows[BUBBLE_MENU_ROWS];
    const size_t count = build_rows(rows);
    menu.capture = CAPTURE_NONE;
    if (index < count)
        activate(&rows[index]);
}

static void save(void)
{
    set_status(menu.hooks.save_settings() == 0 ? "Saved to settings.ini" : "Save failed: see the log");
}

static void click(int x, int y)
{
    const struct bubble_menu_target target = bubble_menu_hit(x, y, menu.open);
    switch (target.kind) {
    case BUBBLE_MENU_BUBBLE:
        set_open(!menu.open);
        break;
    case BUBBLE_MENU_TAB:
        menu.page = (enum bubble_menu_page)target.index;
        menu.capture = CAPTURE_NONE;
        menu.status[0] = '\0';
        changed();
        break;
    case BUBBLE_MENU_ROW:
        click_row(target.index);
        break;
    case BUBBLE_MENU_SAVE:
        menu.capture = CAPTURE_NONE;
        save();
        break;
    case BUBBLE_MENU_CLOSE:
        set_open(false);
        break;
    case BUBBLE_MENU_NOTHING:
    default:
        break;
    }
}

/* ---- capture ---- */

static void take_key(int scancode)
{
    if (scancode == OVERLAY_TOGGLE_SCANCODE || scancode == BUBBLE_MENU_TOGGLE_SCANCODE) {
        set_status("That key is taken");
        return;
    }
    struct input_keymap keymap = input_current_keymap();
    if (menu.capture == CAPTURE_DEBUG_KEY)
        keymap.debug_menu = scancode;
    else
        keymap.scancodes[menu.capture_index] = scancode;
    menu.capture = CAPTURE_NONE;
    set_status(input_set_keymap(&keymap) == 0 ? "Key set" : "That key cannot be used");
}

/* A function's new control; one already serving another function swaps
 * with it, so that no two share one (controller.h). */
static void take_function_control(int control)
{
    struct controller_bindings bindings = controller_current_bindings();
    const int old = bindings.controls[menu.capture_index];
    for (size_t i = 0; i < CONTROLLER_FUNCTION_COUNT; i++) {
        if (bindings.controls[i] == control)
            bindings.controls[i] = old;
    }
    bindings.controls[menu.capture_index] = control;
    menu.capture = CAPTURE_NONE;
    set_status(controller_set_bindings(&bindings) == 0 ? "Button set" : "That button cannot be used");
}

static void take_control(int control)
{
    if (menu.capture == CAPTURE_PAD_FUNCTION) {
        take_function_control(control);
        return;
    }
    struct input_padmap padmap = input_current_padmap();
    padmap.controls[menu.capture_index] = control;
    menu.capture = CAPTURE_NONE;
    set_status(input_set_padmap(&padmap) == 0 ? "Button set" : "That button cannot be used");
}

static bool capturing_pad(void)
{
    return menu.capture == CAPTURE_PAD_BUTTON || menu.capture == CAPTURE_PAD_FUNCTION;
}

static bool capturing_key(void)
{
    return menu.capture == CAPTURE_KEY || menu.capture == CAPTURE_DEBUG_KEY;
}

static void cancel_capture(void)
{
    menu.capture = CAPTURE_NONE;
    set_status("");
}

/* ---- events ---- */

static bool key_down(const SDL_KeyboardEvent *key)
{
    const int scancode = (int)key->keysym.scancode;
    if (key->repeat != 0)
        return menu.open;
    if (capturing_key()) {
        if (scancode == ESCAPE_SCANCODE)
            cancel_capture();
        else
            take_key(scancode);
        return true;
    }
    if (scancode == BUBBLE_MENU_TOGGLE_SCANCODE) {
        set_open(!menu.open);
        return true;
    }
    if (!menu.open)
        return false;
    if (scancode == ESCAPE_SCANCODE) {
        if (capturing_pad())
            cancel_capture();
        else
            set_open(false);
    }
    return true;
}

static bool mouse_down(const SDL_MouseButtonEvent *button)
{
    const struct bubble_menu_target target = bubble_menu_hit(button->x, button->y, menu.open);
    if (!menu.open && target.kind != BUBBLE_MENU_BUBBLE)
        return false;
    if (button->button == RIGHT_BUTTON && menu.open) {
        if (menu.capture != CAPTURE_NONE)
            cancel_capture();
        else
            set_open(false);
    } else if (button->button == LEFT_BUTTON) {
        click(button->x, button->y);
    }
    return true;
}

static bool mouse_motion(const SDL_MouseMotionEvent *motion)
{
    const struct bubble_menu_target target = bubble_menu_hit(motion->x, motion->y, menu.open);
    if (target.kind != menu.hover.kind || target.index != menu.hover.index) {
        menu.hover = target;
        changed();
    }
    return menu.open;
}

static bool controller_axis(const SDL_ControllerAxisEvent *axis)
{
    if (!capturing_pad())
        return menu.open;
    if (axis->value > CONTROLLER_TRIGGER_PRESS) {
        if (axis->axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
            take_control(CONTROLLER_LEFT_TRIGGER);
        else if (axis->axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            take_control(CONTROLLER_RIGHT_TRIGGER);
    }
    return true;
}

bool bubble_menu_handle_event(const union SDL_Event *event)
{
    if (!menu.enabled || event == NULL)
        return false;
    switch (event->type) {
    case SDL_KEYDOWN:
        return key_down(&event->key);
    case SDL_KEYUP:
        return menu.open;
    case SDL_MOUSEBUTTONDOWN:
        return mouse_down(&event->button);
    case SDL_MOUSEBUTTONUP:
        return menu.open;
    case SDL_MOUSEMOTION:
        return mouse_motion(&event->motion);
    case SDL_CONTROLLERBUTTONDOWN:
        if (capturing_pad()) {
            take_control((int)event->cbutton.button);
            return true;
        }
        return menu.open;
    case SDL_CONTROLLERBUTTONUP:
        return menu.open;
    case SDL_CONTROLLERAXISMOTION:
        return controller_axis(&event->caxis);
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERDEVICEREMOVED:
        changed();
        return false;
    default:
        return false;
    }
}

/* ---- drawing ---- */

static size_t scale_of(const struct font_canvas *canvas, size_t lcd_width)
{
    const size_t scale = canvas->width / lcd_width;
    return scale > 0 ? scale : 1u;
}

/* An LCD rectangle filled, at the canvas's scale, clipped to it. */
static void fill(const struct font_canvas *canvas, size_t scale, size_t x, size_t y, size_t width, size_t height,
                 uint32_t color)
{
    for (size_t row = y * scale; row < (y + height) * scale && row < canvas->height; row++) {
        uint32_t *const pixels = canvas->pixels + row * canvas->stride;
        for (size_t column = x * scale; column < (x + width) * scale && column < canvas->width; column++)
            pixels[column] = color;
    }
}

static void text_at(const struct font_canvas *canvas, size_t scale, size_t x, size_t y, const char *text,
                    uint32_t color)
{
    struct font_pen pen = {.x = x * scale, .y = (y + TEXT_TOP) * scale, .scale = scale, .color = color};
    font_draw_text(canvas, &pen, text, font_prefix_bytes(text, BUBBLE_MENU_ROW_CHARS));
}

static bool hovered(enum bubble_menu_target_kind kind, size_t index)
{
    return menu.hover.kind == kind && menu.hover.index == index;
}

static void draw_tabs(const struct font_canvas *canvas, size_t scale)
{
    char label[ROW_TEXT_SIZE];
    for (size_t page = 0; page < BUBBLE_MENU_PAGE_COUNT; page++) {
        const uint32_t color = page == (size_t)menu.page        ? BUBBLE_MENU_TAB_ON_ARGB
                               : hovered(BUBBLE_MENU_TAB, page) ? BUBBLE_MENU_HOVER_ARGB
                                                                : BUBBLE_MENU_TAB_ARGB;
        fill(canvas, scale, tab_x(page), TAB_Y, tab_width(page) - 1u, ROW_HEIGHT, color);
        (void)snprintf(label, sizeof label, " %s", page_names[page]);
        text_at(canvas, scale, tab_x(page), TAB_Y, label, BUBBLE_MENU_TEXT_ARGB);
    }
}

static void draw_rows(const struct font_canvas *canvas, size_t scale)
{
    struct row rows[BUBBLE_MENU_ROWS];
    const size_t count = build_rows(rows);
    const size_t row_width = GBA_LCD_VISIBLE_DOTS - 2u * ROW_X;
    for (size_t i = 0; i < count; i++) {
        const size_t y = ROW_Y + i * ROW_HEIGHT;
        const bool info = rows[i].action == ROW_INFO;
        if (!info && hovered(BUBBLE_MENU_ROW, i))
            fill(canvas, scale, ROW_X, y, row_width, ROW_HEIGHT, BUBBLE_MENU_HOVER_ARGB);
        if (rows[i].marked)
            text_at(canvas, scale, ROW_X, y, ">", BUBBLE_MENU_MARK_ARGB);
        const uint32_t color = info ? BUBBLE_MENU_INFO_ARGB : rows[i].marked ? BUBBLE_MENU_MARK_ARGB : BUBBLE_MENU_TEXT_ARGB;
        text_at(canvas, scale, ROW_TEXT_X, y, rows[i].text, color);
    }
}

static void draw_footer(const struct font_canvas *canvas, size_t scale)
{
    const size_t save_width = (sizeof SAVE_LABEL - 1u) * FONT_CELL_WIDTH;
    const size_t close_width = (sizeof CLOSE_LABEL - 1u) * FONT_CELL_WIDTH;
    if (hovered(BUBBLE_MENU_SAVE, 0))
        fill(canvas, scale, FOOTER_X, FOOTER_Y, save_width, ROW_HEIGHT, BUBBLE_MENU_HOVER_ARGB);
    if (hovered(BUBBLE_MENU_CLOSE, 0))
        fill(canvas, scale, close_x(), FOOTER_Y, close_width, ROW_HEIGHT, BUBBLE_MENU_HOVER_ARGB);
    text_at(canvas, scale, FOOTER_X, FOOTER_Y, SAVE_LABEL, BUBBLE_MENU_TEXT_ARGB);
    text_at(canvas, scale, FOOTER_X + save_width + FONT_CELL_WIDTH, FOOTER_Y, menu.status, BUBBLE_MENU_MARK_ARGB);
    text_at(canvas, scale, close_x(), FOOTER_Y, CLOSE_LABEL, BUBBLE_MENU_TEXT_ARGB);
}

void bubble_menu_draw_panel(const struct font_canvas *canvas)
{
    const size_t scale = scale_of(canvas, GBA_LCD_VISIBLE_DOTS);
    fill(canvas, 1u, 0, 0, canvas->width, canvas->height, BUBBLE_MENU_PANEL_ARGB);
    if (!menu.enabled)
        return;
    draw_tabs(canvas, scale);
    draw_rows(canvas, scale);
    draw_footer(canvas, scale);
}

/* Whether the LCD pixel (x, y) of the bubble is in one of its cut corners. */
static bool corner(size_t x, size_t y)
{
    const size_t last = (size_t)BUBBLE_MENU_BUBBLE_SIZE - 1u;
    const size_t dx = x < BUBBLE_CORNER ? BUBBLE_CORNER - x : x > last - BUBBLE_CORNER ? x - (last - BUBBLE_CORNER) : 0;
    const size_t dy = y < BUBBLE_CORNER ? BUBBLE_CORNER - y : y > last - BUBBLE_CORNER ? y - (last - BUBBLE_CORNER) : 0;
    return dx + dy > BUBBLE_CORNER;
}

void bubble_menu_draw_bubble(const struct font_canvas *canvas)
{
    static const size_t bars[] = BAR_ROWS;
    const size_t scale = scale_of(canvas, BUBBLE_MENU_BUBBLE_SIZE);
    const bool hot = menu.open || hovered(BUBBLE_MENU_BUBBLE, 0);
    fill(canvas, 1u, 0, 0, canvas->width, canvas->height, 0u);
    for (size_t y = 0; y < (size_t)BUBBLE_MENU_BUBBLE_SIZE; y++) {
        for (size_t x = 0; x < (size_t)BUBBLE_MENU_BUBBLE_SIZE; x++) {
            if (!corner(x, y))
                fill(canvas, scale, x, y, 1u, 1u, hot ? BUBBLE_MENU_BUBBLE_HOT_ARGB : BUBBLE_MENU_BUBBLE_ARGB);
        }
    }
    for (size_t i = 0; i < sizeof bars / sizeof bars[0]; i++)
        fill(canvas, scale, BAR_X, bars[i], BAR_WIDTH, 1u, BUBBLE_MENU_TEXT_ARGB);
}
