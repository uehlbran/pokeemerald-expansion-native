/*
 * bubble_menu.h: the bubble in the window's top-left corner, and the menu
 * it opens: fast-forward's speed, the cheats, the controller's buttons and
 * functions, and the keyboard's keys, each a click away.
 *
 * Not hardware: the GBA had nothing like it. Everything it changes is
 * already the platform's or the game's own: fast-forward (fastforward.h),
 * the keymap and the controller map (input.h), the controller's functions
 * (controller.h), expansion's debug menu (opened by pressing R and Start
 * for the game, input_inject) and the cheats (cheats.h, through the hooks
 * below, which the game's side gives). Save writes the keys, the controller
 * and the speed to the settings file through a hook (settings_save); until
 * then a change lasts until the game ends.
 *
 * The bubble, BUBBLE_MENU_BUBBLE_SIZE LCD pixels square at
 * (BUBBLE_MENU_BUBBLE_X, BUBBLE_MENU_BUBBLE_Y), is always drawn over the
 * game (video.c, over the frame, never into the frame buffer the
 * screenshots come from). A left click on it, or F5, opens the menu, which
 * covers the screen: a row of tabs (Speed, Cheats, Pad, Pad fn, Keys), up
 * to BUBBLE_MENU_ROWS rows, and a footer with Save, a status line and Close.
 * A click on a row does what it says; a remapping row waits for the next
 * key or controller button (Esc, or a right click, gives up). The bubble,
 * Close, F5, Esc and a right click close it. While it is open the game
 * runs on but reads no button (overlay_set_menu_open, as under the help
 * overlay), and the fast-forward keys do nothing.
 *
 * The coordinates are the LCD's, 240 x 160: SDL gives the mouse in the
 * renderer's logical size, which video.c sets to the LCD's. The drawing is
 * into canvases of the LCD's size times a whole scale (video.c's textures),
 * with font.h at that scale.
 *
 * main starts it for the runs that start the overlay (player_at_keyboard):
 * never headless, for a replay or in test-runner mode. State is this
 * module's own; only the main thread calls in. Apart from reading SDL's
 * event structures, nothing here calls SDL.
 */
#ifndef PLATFORM_BUBBLE_MENU_H
#define PLATFORM_BUBBLE_MENU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cheats.h"
#include "font.h"

/* The bubble, in LCD pixels. */
#define BUBBLE_MENU_BUBBLE_X 2
#define BUBBLE_MENU_BUBBLE_Y 2
#define BUBBLE_MENU_BUBBLE_SIZE 14

/* The rows a tab shows, and the longest row, in characters. */
#define BUBBLE_MENU_ROWS 12u
#define BUBBLE_MENU_ROW_CHARS 38u

/* F5 opens and closes it too. */
#define BUBBLE_MENU_TOGGLE_SCANCODE 62 /* SDL_SCANCODE_F5 */

/* The colours, 32-bit ARGB as overlay.h's. */
#define BUBBLE_MENU_BUBBLE_ARGB UINT32_C(0xA0203860)
#define BUBBLE_MENU_BUBBLE_HOT_ARGB UINT32_C(0xF03070B0)
#define BUBBLE_MENU_PANEL_ARGB UINT32_C(0xE8000000)
#define BUBBLE_MENU_TEXT_ARGB UINT32_C(0xFFFFFFFF)
#define BUBBLE_MENU_MARK_ARGB UINT32_C(0xFFFFE070)
#define BUBBLE_MENU_INFO_ARGB UINT32_C(0xFFA0A0A0)
#define BUBBLE_MENU_HOVER_ARGB UINT32_C(0xFF2C4468)
#define BUBBLE_MENU_TAB_ARGB UINT32_C(0xFF202830)
#define BUBBLE_MENU_TAB_ON_ARGB UINT32_C(0xFF3060A0)

enum bubble_menu_page {
    BUBBLE_MENU_SPEED,
    BUBBLE_MENU_CHEATS,
    BUBBLE_MENU_PAD,
    BUBBLE_MENU_PAD_FUNCTIONS,
    BUBBLE_MENU_KEYS,
    BUBBLE_MENU_PAGE_COUNT
};

/* What a point of the screen is. */
enum bubble_menu_target_kind {
    BUBBLE_MENU_NOTHING,
    BUBBLE_MENU_BUBBLE,
    BUBBLE_MENU_TAB,  /* index: the page */
    BUBBLE_MENU_ROW,  /* index: the row */
    BUBBLE_MENU_SAVE,
    BUBBLE_MENU_CLOSE
};

struct bubble_menu_target {
    enum bubble_menu_target_kind kind;
    size_t index;
};

union SDL_Event;

/* What the game's side does for the menu (main.c gives cheats.h's and
 * player.h's): none of them may be NULL. */
struct bubble_menu_hooks {
    bool (*cheats_available)(void);
    enum cheat_result (*cheat_run)(enum cheat cheat);
    bool (*cheat_on)(enum cheat cheat);
    int (*save_settings)(void); /* 0, or -1 after a report on stderr */
};

/* Starts it closed on the Speed tab, with the hooks (copied). Returns 0, or
 * -1 (reported) for NULL or a NULL hook or a second init. */
int bubble_menu_init(const struct bubble_menu_hooks *hooks);

/* Closes it (the game's keys are its own again) and makes events do
 * nothing. Safe at any time and twice. */
void bubble_menu_shutdown(void);

bool bubble_menu_enabled(void);
bool bubble_menu_open(void);
enum bubble_menu_page bubble_menu_page(void);

/* An SDL event, as the frame loop polls them. True if the event was the
 * menu's (a click on the bubble or anywhere while open, F5, any key or
 * controller button while open), which the loop then gives to nobody
 * else; false for the rest, and for every event before init or NULL. */
bool bubble_menu_handle_event(const union SDL_Event *event);

/* Pure: what the LCD point (x, y) is, with the menu open or not. */
struct bubble_menu_target bubble_menu_hit(int x, int y, bool open);

/* A count that changes whenever what the bubble or the panel shows may
 * have changed, so that video.c paints them again only then. */
uint32_t bubble_menu_revision(void);

/* The text of row `row` of the tab shown, as drawn (at most
 * BUBBLE_MENU_ROW_CHARS characters), into out; "" past its last row. */
void bubble_menu_row_text(size_t row, char *out, size_t size);

/* The status line ("" when there is none). */
const char *bubble_menu_status(void);

/* Draws the bubble into a canvas BUBBLE_MENU_BUBBLE_SIZE times a whole
 * scale square, and the panel into one of the LCD's size times a whole
 * scale (the scale is the canvas's width over the LCD's). Every pixel of
 * the canvas is written. */
void bubble_menu_draw_bubble(const struct font_canvas *canvas);
void bubble_menu_draw_panel(const struct font_canvas *canvas);

#endif /* PLATFORM_BUBBLE_MENU_H */
