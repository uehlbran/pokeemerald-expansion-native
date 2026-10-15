/*
 * overlay.c: the help overlay (overlay.h).
 *
 * State (started, shown, the text's writer) is this module's own; only the
 * main thread calls in.
 */
#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL_events.h>

#include "gba_timing.h"

/* A window of scale s is s times the LCD's width; the glyphs are drawn at
 * s divided by this, at least 1. */
#define WINDOW_SCALES_PER_GLYPH_SCALE 2u

static struct {
    bool enabled;
    bool shown;
    bool hint_for_pad;
    bool hidden_since_read; /* shown went true to false since the last read of the keys */
    uint16_t held_through;  /* the buttons held since the overlay was shown */
    overlay_text_writer write_text;
} overlay;

/* Where the next line goes. */
struct cursor {
    const struct font_canvas *canvas;
    size_t scale;
    size_t left; /* the margin, in pixels */
    size_t top;
    size_t columns; /* characters a line holds */
    size_t rows;    /* lines the canvas holds */
    size_t row;     /* the next line's */
};

int overlay_init(overlay_text_writer write_text)
{
    if (write_text == NULL || overlay.enabled) {
        (void)fprintf(stderr, "overlay: overlay_init needs the text's writer and is called once\n");
        return -1;
    }
    overlay = (__typeof__(overlay)){.enabled = true, .write_text = write_text};
    return 0;
}

void overlay_shutdown(void)
{
    overlay = (__typeof__(overlay)){0};
}

bool overlay_enabled(void)
{
    return overlay.enabled;
}

bool overlay_shown(void)
{
    return overlay.shown;
}

/* The device of a press: a key, a controller button, or an axis pushed
 * well past its middle (a stick at rest drifts, a trigger past half is a
 * press). */
static void note_input_device(const SDL_Event *event)
{
    if (event->type == SDL_KEYDOWN)
        overlay.hint_for_pad = false;
    else if (event->type == SDL_CONTROLLERBUTTONDOWN
             || (event->type == SDL_CONTROLLERAXISMOTION && abs(event->caxis.value) > CONTROLLER_TRIGGER_PRESS))
        overlay.hint_for_pad = true;
}

static void set_shown(bool shown)
{
    if (overlay.shown && !shown)
        overlay.hidden_since_read = true;
    overlay.shown = shown;
}

enum overlay_request overlay_handle_event(const union SDL_Event *event)
{
    if (!overlay.enabled || event == NULL)
        return OVERLAY_CONTINUE;
    note_input_device(event);
    if (event->type != SDL_KEYDOWN || event->key.repeat != 0)
        return OVERLAY_CONTINUE;
    if (event->key.keysym.scancode == OVERLAY_TOGGLE_SCANCODE) {
        set_shown(!overlay.shown);
        return OVERLAY_CONTINUE;
    }
    if (overlay.shown && event->key.keysym.sym == OVERLAY_QUIT_KEYCODE)
        return OVERLAY_QUIT;
    return OVERLAY_CONTINUE;
}

enum overlay_request overlay_handle_controller(const struct controller_edge *edge)
{
    if (!overlay.enabled || edge == NULL || !edge->pressed)
        return OVERLAY_CONTINUE;
    if (edge->function == CONTROLLER_OVERLAY)
        set_shown(!overlay.shown);
    else if (edge->function == CONTROLLER_OVERLAY_CLOSE)
        set_shown(false);
    else if (edge->function == CONTROLLER_QUIT && overlay.shown)
        return OVERLAY_QUIT;
    return OVERLAY_CONTINUE;
}

bool overlay_hint_for_pad(void)
{
    return overlay.hint_for_pad;
}

uint16_t overlay_game_keys(uint16_t keys)
{
    /* The frame loop closes the overlay and reads the keys in one poll, so
     * the buttons held at the first read after it hid are the ones that
     * closed it (or were held under it): latched whether or not a read saw
     * them while it was shown, so an overlay opened and closed within one
     * poll latches too. */
    const bool just_hidden = overlay.hidden_since_read;
    overlay.hidden_since_read = false;
    if (overlay.shown) {
        overlay.held_through = keys;
        return 0;
    }
    if (just_hidden)
        overlay.held_through = keys;
    overlay.held_through &= keys;
    return (uint16_t)(keys & ~overlay.held_through);
}

size_t overlay_glyph_scale(size_t width)
{
    const size_t glyph_scale = width / GBA_LCD_VISIBLE_DOTS / WINDOW_SCALES_PER_GLYPH_SCALE;
    return glyph_scale > 0 ? glyph_scale : 1u;
}

/* Draws line, going on in the next lines while it is longer than a line. */
static void draw_line(const char *line, void *context)
{
    struct cursor *const cursor = context;
    const char *rest = line;
    do {
        if (cursor->row >= cursor->rows)
            return;
        const size_t bytes = font_prefix_bytes(rest, cursor->columns);
        struct font_pen pen = {
            .x = cursor->left,
            .y = cursor->top + cursor->row * FONT_LINE_HEIGHT * cursor->scale,
            .scale = cursor->scale,
            .color = OVERLAY_TEXT_ARGB,
        };
        font_draw_text(cursor->canvas, &pen, rest, bytes);
        rest += bytes;
        cursor->row++;
    } while (*rest != '\0');
}

static void fill(const struct font_canvas *canvas, uint32_t color)
{
    for (size_t y = 0; y < canvas->height; y++) {
        uint32_t *const row = canvas->pixels + y * canvas->stride;
        for (size_t x = 0; x < canvas->width; x++)
            row[x] = color;
    }
}

/* The space within the margins, in characters and lines. */
static size_t fit(size_t length, size_t margin, size_t step)
{
    return length > 2u * margin ? (length - 2u * margin) / step : 0;
}

void overlay_draw(const struct font_canvas *canvas)
{
    fill(canvas, OVERLAY_BACKGROUND_ARGB);
    if (!overlay.enabled)
        return;
    const size_t scale = overlay_glyph_scale(canvas->width);
    struct cursor cursor = {
        .canvas = canvas,
        .scale = scale,
        .left = FONT_CELL_WIDTH * scale,
        .top = FONT_LINE_HEIGHT * scale,
    };
    cursor.columns = fit(canvas->width, cursor.left, FONT_CELL_WIDTH * scale);
    cursor.rows = fit(canvas->height, cursor.top, FONT_LINE_HEIGHT * scale);
    if (cursor.columns == 0)
        return;
    char pad_hint[OVERLAY_PAD_HINT_SIZE];
    if (overlay.hint_for_pad)
        (void)snprintf(pad_hint, sizeof pad_hint, "%s: quit  %s: close", controller_function_label(CONTROLLER_QUIT),
                       controller_function_label(CONTROLLER_OVERLAY_CLOSE));
    draw_line(overlay.hint_for_pad ? pad_hint : OVERLAY_HINT, &cursor);
    draw_line("", &cursor);
    overlay.write_text(draw_line, &cursor);
}
