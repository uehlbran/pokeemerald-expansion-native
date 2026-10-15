/*
 * font.h: the platform's own text, a bitmap font for the help overlay.
 *
 * Not hardware: the GBA drew all its text from the game's own tiles. This is
 * text the platform draws over the window (overlay.h), where the game's font
 * is not to hand and SDL2 draws no text (SDL_ttf would be a second library
 * and a font file to ship for one screen). Printable ASCII, 32 to 126, as
 * glyphs 5 pixels wide and 8 high (font_glyphs.h, drawn for this project; no
 * reference was used), each in a cell of 6 with a column of space; a line
 * is 10 rows, so two rows of space separate the descenders from the next
 * line. Any other character, a byte outside 32..126 or a whole UTF-8
 * sequence, is drawn as one FONT_REPLACEMENT.
 *
 * Pure: it draws into a buffer of 32-bit pixels the caller owns, and writes
 * no pixel outside it.
 */
#ifndef PLATFORM_FONT_H
#define PLATFORM_FONT_H

#include <stddef.h>
#include <stdint.h>

#define FONT_GLYPH_WIDTH 5u
#define FONT_GLYPH_HEIGHT 8u
#define FONT_CELL_WIDTH 6u   /* the glyph and a column of space */
#define FONT_LINE_HEIGHT 10u /* the glyph and two rows of space */

/* The characters the font has a glyph for. */
#define FONT_FIRST_CHAR 32 /* ' ' */
#define FONT_LAST_CHAR 126 /* '~' */
#define FONT_GLYPH_COUNT ((size_t)(FONT_LAST_CHAR - FONT_FIRST_CHAR + 1))

/* What any other character is drawn as. */
#define FONT_REPLACEMENT '?'

/* A buffer of width x height pixels, row after row, stride pixels apart
 * (stride >= width). The pixel format is the caller's: the font writes the
 * pen's colour as it is. */
struct font_canvas {
    uint32_t *pixels;
    size_t width;
    size_t height;
    size_t stride;
};

/* Where the next glyph goes (its cell's top-left pixel), how large (each
 * pixel of the glyph a scale x scale square, scale >= 1) and in what colour. */
struct font_pen {
    size_t x;
    size_t y;
    size_t scale;
    uint32_t color;
};

/* The row-th row of c's glyph (0 at the top, FONT_GLYPH_HEIGHT rows), bit
 * FONT_GLYPH_WIDTH - 1 the leftmost pixel; FONT_REPLACEMENT's for a byte
 * without a glyph, and 0 for a row past the glyph. */
uint8_t font_glyph_row(unsigned char c, size_t row);

/* Draws c's glyph at the pen in its colour, leaving the cell's other pixels
 * as they are, and clipped to the canvas. A scale of 0 draws nothing. */
void font_draw_glyph(const struct font_canvas *canvas, const struct font_pen *pen, unsigned char c);

/* The bytes of text's first character: 1 for a byte below 80h, the lead
 * byte and its continuation bytes (10xxxxxx) for a UTF-8 sequence, 0 at the
 * terminating NUL. Any other byte, a stray continuation byte too, is one
 * character. */
size_t font_char_bytes(const char *text);

/* The bytes of text's first `cells` characters, or of all of it if it has
 * fewer. */
size_t font_prefix_bytes(const char *text, size_t cells);

/* Draws the characters in the first `bytes` bytes of text, one cell each,
 * moving the pen right a cell (FONT_CELL_WIDTH x scale) per character. */
void font_draw_text(const struct font_canvas *canvas, struct font_pen *pen, const char *text, size_t bytes);

#endif /* PLATFORM_FONT_H */
