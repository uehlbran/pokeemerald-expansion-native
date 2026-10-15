/*
 * font.c: the platform's bitmap font (font.h; the glyphs are font_glyphs.h).
 */
#include "font.h"

#include <stdbool.h>

#include "font_glyphs.h"

/* UTF-8: a continuation byte is 10xxxxxx; a byte from 80h up starts or
 * continues a sequence of at most four bytes. */
#define UTF8_CONTINUATION_MASK 0xC0u
#define UTF8_CONTINUATION_BITS 0x80u
#define UTF8_FIRST_MULTIBYTE 0x80u
#define UTF8_MAX_SEQUENCE 4u

static bool has_glyph(unsigned char c)
{
    return c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR;
}

uint8_t font_glyph_row(unsigned char c, size_t row)
{
    if (row >= FONT_GLYPH_HEIGHT)
        return 0;
    const unsigned char drawn = has_glyph(c) ? c : (unsigned char)FONT_REPLACEMENT;
    return font_glyphs[drawn - FONT_FIRST_CHAR][row];
}

/* The scale x scale square at (x, y), clipped to the canvas. */
static void fill_square(const struct font_canvas *canvas, size_t x, size_t y, size_t scale, uint32_t color)
{
    for (size_t dy = 0; dy < scale && y + dy < canvas->height; dy++) {
        uint32_t *const row = canvas->pixels + (y + dy) * canvas->stride;
        for (size_t dx = 0; dx < scale && x + dx < canvas->width; dx++)
            row[x + dx] = color;
    }
}

void font_draw_glyph(const struct font_canvas *canvas, const struct font_pen *pen, unsigned char c)
{
    for (size_t row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        const uint8_t bits = font_glyph_row(c, row);
        for (size_t column = 0; column < FONT_GLYPH_WIDTH; column++) {
            if ((bits >> (FONT_GLYPH_WIDTH - 1u - column)) & 1u)
                fill_square(canvas, pen->x + column * pen->scale, pen->y + row * pen->scale, pen->scale, pen->color);
        }
    }
}

static bool is_continuation(unsigned char byte)
{
    return (byte & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_BITS;
}

size_t font_char_bytes(const char *text)
{
    const unsigned char lead = (unsigned char)text[0];
    if (lead == '\0')
        return 0;
    size_t bytes = 1;
    if (lead >= UTF8_FIRST_MULTIBYTE && !is_continuation(lead)) {
        while (bytes < UTF8_MAX_SEQUENCE && is_continuation((unsigned char)text[bytes]))
            bytes++;
    }
    return bytes;
}

size_t font_prefix_bytes(const char *text, size_t cells)
{
    size_t bytes = 0;
    for (size_t cell = 0; cell < cells; cell++) {
        const size_t length = font_char_bytes(text + bytes);
        if (length == 0)
            break;
        bytes += length;
    }
    return bytes;
}

void font_draw_text(const struct font_canvas *canvas, struct font_pen *pen, const char *text, size_t bytes)
{
    size_t done = 0;
    while (done < bytes) {
        const size_t length = font_char_bytes(text + done);
        if (length == 0 || done + length > bytes)
            break;
        font_draw_glyph(canvas, pen, (unsigned char)text[done]);
        pen->x += FONT_CELL_WIDTH * pen->scale;
        done += length;
    }
}
