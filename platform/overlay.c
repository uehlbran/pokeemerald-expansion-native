/*
 * overlay.c: the help overlay (overlay.h).
 *
 * State (started, shown, the text's writer) is this module's own; only the
 * main thread calls in.
 */
#include "overlay.h"

#include <stdio.h>

#include <SDL_events.h>

#include "gba_timing.h"

/* A window of scale s is s times the LCD's width; the glyphs are drawn at
 * s divided by this, at least 1. */
#define WINDOW_SCALES_PER_GLYPH_SCALE 2u

static struct {
    bool enabled;
    bool shown;
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
    overlay.write_text = write_text;
    overlay.shown = false;
    overlay.enabled = true;
    return 0;
}

void overlay_shutdown(void)
{
    overlay.enabled = false;
    overlay.shown = false;
    overlay.write_text = NULL;
}

bool overlay_enabled(void)
{
    return overlay.enabled;
}

bool overlay_shown(void)
{
    return overlay.shown;
}

enum overlay_request overlay_handle_event(const union SDL_Event *event)
{
    if (!overlay.enabled || event == NULL || event->type != SDL_KEYDOWN || event->key.repeat != 0)
        return OVERLAY_CONTINUE;
    if (event->key.keysym.scancode == OVERLAY_TOGGLE_SCANCODE) {
        overlay.shown = !overlay.shown;
        return OVERLAY_CONTINUE;
    }
    if (overlay.shown && event->key.keysym.sym == OVERLAY_QUIT_KEYCODE)
        return OVERLAY_QUIT;
    return OVERLAY_CONTINUE;
}

uint16_t overlay_game_keys(uint16_t keys)
{
    return overlay.shown ? 0 : keys;
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
    draw_line(OVERLAY_HINT, &cursor);
    draw_line("", &cursor);
    overlay.write_text(draw_line, &cursor);
}
