/*
 * test_font.c: the bitmap font (font.c): the glyph table against pictures of
 * a few glyphs written out here, the drawing of a glyph into a buffer pixel
 * by pixel at scales 1 and 2, clipping at the buffer's edges, and the
 * characters of UTF-8 text.
 */
#include "font.h"
#include "ptest.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BACKGROUND UINT32_C(0x11111111)
#define INK UINT32_C(0xFFEEDDCC)

/* A picture of a glyph, top row first, '#' for a pixel drawn. */
typedef const char *picture[FONT_GLYPH_HEIGHT];

static const picture capital_t = {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..", "....."};
static const picture capital_l = {"#....", "#....", "#....", "#....", "#....", "#....", "#####", "....."};
static const picture underscore = {".....", ".....", ".....", ".....", ".....", ".....", "#####", "....."};
static const picture small_g = {".....", ".....", ".####", "#...#", "#...#", ".####", "....#", ".###."};
static const picture question = {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#..", "....."};

/* A canvas of width x height pixels, stride pixels a row, filled with BACKGROUND. */
struct test_canvas {
    struct font_canvas canvas;
    uint32_t *pixels;
};

static bool make_canvas(struct test_canvas *out, size_t width, size_t height, size_t stride)
{
    out->pixels = malloc(stride * height * sizeof(uint32_t));
    if (out->pixels == NULL)
        return false;
    for (size_t i = 0; i < stride * height; i++)
        out->pixels[i] = BACKGROUND;
    out->canvas = (struct font_canvas){.pixels = out->pixels, .width = width, .height = height, .stride = stride};
    return true;
}

static uint32_t pixel(const struct test_canvas *canvas, size_t x, size_t y)
{
    return canvas->pixels[y * canvas->canvas.stride + x];
}

static bool picture_has(const picture glyph, size_t column, size_t row)
{
    return glyph[row][column] == '#';
}

/* True if every pixel of the canvas is INK exactly where the picture, drawn at (x, y) with each of its
 * pixels a scale x scale square, has one, and BACKGROUND everywhere else. */
static bool canvas_shows(const struct test_canvas *canvas, const picture glyph, size_t x, size_t y, size_t scale)
{
    for (size_t py = 0; py < canvas->canvas.height; py++) {
        for (size_t px = 0; px < canvas->canvas.width; px++) {
            const bool inside = px >= x && py >= y && px < x + FONT_GLYPH_WIDTH * scale
                                && py < y + FONT_GLYPH_HEIGHT * scale;
            const bool ink = inside && picture_has(glyph, (px - x) / scale, (py - y) / scale);
            if (pixel(canvas, px, py) != (ink ? INK : BACKGROUND))
                return false;
        }
    }
    return true;
}

/* Whether the table sets the pixel (column, row) of c's glyph. */
static bool glyph_bit(unsigned char c, size_t row, size_t column)
{
    return (((unsigned)font_glyph_row(c, row) >> (FONT_GLYPH_WIDTH - 1u - column)) & 1u) != 0;
}

static bool rows_match_picture(unsigned char c, const picture glyph)
{
    for (size_t row = 0; row < FONT_GLYPH_HEIGHT; row++) {
        for (size_t column = 0; column < FONT_GLYPH_WIDTH; column++) {
            const bool set = glyph_bit(c, row, column);
            if (set != picture_has(glyph, column, row))
                return false;
        }
    }
    return true;
}

PTEST(the_table_holds_the_glyphs_as_drawn)
{
    PT_ASSERT(rows_match_picture('T', capital_t));
    PT_ASSERT(rows_match_picture('L', capital_l));
    PT_ASSERT(rows_match_picture('_', underscore));
    PT_ASSERT(rows_match_picture('g', small_g));
    PT_ASSERT(rows_match_picture('?', question));
    for (size_t row = 0; row < FONT_GLYPH_HEIGHT; row++)
        PT_ASSERT_EQ_U32(font_glyph_row(' ', row), 0u);
}

PTEST(every_printable_character_has_a_glyph_five_pixels_wide)
{
    for (unsigned c = FONT_FIRST_CHAR + 1u; c <= FONT_LAST_CHAR; c++) {
        uint8_t any = 0;
        for (size_t row = 0; row < FONT_GLYPH_HEIGHT; row++) {
            PT_ASSERT_EQ_U32(font_glyph_row((unsigned char)c, row) >> FONT_GLYPH_WIDTH, 0u);
            any = (uint8_t)(any | font_glyph_row((unsigned char)c, row));
        }
        PT_ASSERT(any != 0); /* only the space is blank */
    }
}

PTEST(a_byte_without_a_glyph_reads_as_the_replacement)
{
    static const unsigned char without[] = {0, 9, 31, 127, 128, 0xC3, 0xFF};
    for (size_t i = 0; i < sizeof without; i++) {
        PT_ASSERT(rows_match_picture(without[i], question));
    }
    PT_ASSERT_EQ_U32(font_glyph_row('A', FONT_GLYPH_HEIGHT), 0u);
}

PTEST(a_glyph_is_drawn_at_the_pen_pixel_for_pixel)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 12, 12, 12));
    const struct font_pen pen = {.x = 3, .y = 2, .scale = 1, .color = INK};
    font_draw_glyph(&canvas.canvas, &pen, 'T');
    const bool t_shown = canvas_shows(&canvas, capital_t, 3, 2, 1);
    /* the top row on the pen's row, nothing on the row above or below the glyph */
    const bool top = pixel(&canvas, 3, 2) == INK && pixel(&canvas, 3, 1) == BACKGROUND;
    const bool below = pixel(&canvas, 5, 8) == INK && pixel(&canvas, 5, 9) == BACKGROUND;
    free(canvas.pixels);
    PT_ASSERT(t_shown);
    PT_ASSERT(top);
    PT_ASSERT(below);
}

PTEST(a_descender_is_drawn_on_the_eighth_row)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 8, 10, 8));
    const struct font_pen pen = {.x = 1, .y = 1, .scale = 1, .color = INK};
    font_draw_glyph(&canvas.canvas, &pen, 'g');
    const bool shown = canvas_shows(&canvas, small_g, 1, 1, 1);
    free(canvas.pixels);
    PT_ASSERT(shown);
}

PTEST(each_pixel_of_a_glyph_is_a_square_of_the_scale)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 14, 18, 16));
    const struct font_pen pen = {.x = 2, .y = 1, .scale = 2, .color = INK};
    font_draw_glyph(&canvas.canvas, &pen, 'L');
    const bool shown = canvas_shows(&canvas, capital_l, 2, 1, 2);
    bool stride_untouched = true;
    for (size_t y = 0; y < 18; y++)
        stride_untouched = stride_untouched && canvas.pixels[y * 16 + 14] == BACKGROUND
                           && canvas.pixels[y * 16 + 15] == BACKGROUND;
    free(canvas.pixels);
    PT_ASSERT(shown);
    PT_ASSERT(stride_untouched);
}

PTEST(every_glyph_is_drawn_as_its_table_says)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, FONT_CELL_WIDTH, FONT_GLYPH_HEIGHT, FONT_CELL_WIDTH));
    bool all = true;
    for (unsigned c = FONT_FIRST_CHAR; c <= FONT_LAST_CHAR && all; c++) {
        for (size_t i = 0; i < FONT_CELL_WIDTH * FONT_GLYPH_HEIGHT; i++)
            canvas.pixels[i] = BACKGROUND;
        const struct font_pen pen = {.x = 0, .y = 0, .scale = 1, .color = INK};
        font_draw_glyph(&canvas.canvas, &pen, (unsigned char)c);
        for (size_t row = 0; row < FONT_GLYPH_HEIGHT; row++) {
            for (size_t column = 0; column < FONT_CELL_WIDTH; column++) {
                const bool set = column < FONT_GLYPH_WIDTH && glyph_bit((unsigned char)c, row, column);
                all = all && pixel(&canvas, column, row) == (set ? INK : BACKGROUND);
            }
        }
    }
    free(canvas.pixels);
    PT_ASSERT(all);
}

/* The canvas is exactly its pixels, so a write past its last row is a heap overflow AddressSanitizer
 * reports, and its rows are narrower than their stride, so a write past a row's end shows. */
PTEST(a_glyph_at_the_edges_is_clipped_to_the_canvas)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 7, 6, 9));
    const struct font_pen corner = {.x = 4, .y = 3, .scale = 2, .color = INK};
    font_draw_glyph(&canvas.canvas, &corner, 'W');
    const struct font_pen outside = {.x = 7, .y = 6, .scale = 1, .color = INK};
    font_draw_glyph(&canvas.canvas, &outside, 'W');
    bool margins_untouched = true;
    for (size_t y = 0; y < 6; y++)
        margins_untouched = margins_untouched && pixel(&canvas, 7, y) == BACKGROUND
                            && pixel(&canvas, 8, y) == BACKGROUND;
    const bool drew_inside = pixel(&canvas, 4, 3) == INK; /* W's top-left pixel */
    free(canvas.pixels);
    PT_ASSERT(margins_untouched);
    PT_ASSERT(drew_inside);
}

PTEST(scale_zero_draws_nothing)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 8, 8, 8));
    const struct font_pen pen = {.x = 0, .y = 0, .scale = 0, .color = INK};
    font_draw_glyph(&canvas.canvas, &pen, '#');
    bool blank = true;
    for (size_t i = 0; i < 64; i++)
        blank = blank && canvas.pixels[i] == BACKGROUND;
    free(canvas.pixels);
    PT_ASSERT(blank);
}

PTEST(a_character_is_one_byte_or_a_whole_utf8_sequence)
{
    PT_ASSERT_EQ_U32(font_char_bytes(""), 0u);
    PT_ASSERT_EQ_U32(font_char_bytes("A"), 1u);
    PT_ASSERT_EQ_U32(font_char_bytes("\xC3\xA9x"), 2u);            /* e acute */
    PT_ASSERT_EQ_U32(font_char_bytes("\xE6\x97\xA5"), 3u);         /* a kanji */
    PT_ASSERT_EQ_U32(font_char_bytes("\xF0\x9F\x8E\xAE!"), 4u);    /* an emoji */
    PT_ASSERT_EQ_U32(font_char_bytes("\xC3"), 1u);                 /* a lead byte at the end */
    PT_ASSERT_EQ_U32(font_char_bytes("\x80\x80"), 1u);             /* stray continuation bytes, one each */
    PT_ASSERT_EQ_U32(font_char_bytes("\xF0\x80\x80\x80\x80"), 4u); /* never more than four */
}

PTEST(a_prefix_counts_characters_not_bytes)
{
    PT_ASSERT_EQ_U32(font_prefix_bytes("abc", 2), 2u);
    PT_ASSERT_EQ_U32(font_prefix_bytes("a\xC3\xA9 b", 2), 3u);
    PT_ASSERT_EQ_U32(font_prefix_bytes("abc", 10), 3u);
    PT_ASSERT_EQ_U32(font_prefix_bytes("abc", 0), 0u);
    PT_ASSERT_EQ_U32(font_prefix_bytes("", 3), 0u);
}

PTEST(text_moves_a_cell_a_character_and_stops_at_its_bytes)
{
    struct test_canvas text;
    struct test_canvas one;
    PT_ASSERT(make_canvas(&text, 30, 8, 30));
    PT_ASSERT(make_canvas(&one, 30, 8, 30));
    struct font_pen pen = {.x = 0, .y = 0, .scale = 1, .color = INK};
    font_draw_text(&text.canvas, &pen, "T\xC3\xA9L_", 4); /* T, e acute as one '?', L; not the '_' */
    const size_t end = pen.x;
    struct font_pen each = {.x = 0, .y = 0, .scale = 1, .color = INK};
    font_draw_glyph(&one.canvas, &each, 'T');
    each.x = FONT_CELL_WIDTH;
    font_draw_glyph(&one.canvas, &each, '?');
    each.x = 2u * FONT_CELL_WIDTH;
    font_draw_glyph(&one.canvas, &each, 'L');
    const bool same = memcmp(text.pixels, one.pixels, 30u * 8u * sizeof(uint32_t)) == 0;
    free(text.pixels);
    free(one.pixels);
    PT_ASSERT_EQ_U32(end, 3u * FONT_CELL_WIDTH);
    PT_ASSERT(same);
}

PTEST(text_cut_inside_a_sequence_draws_the_whole_characters_only)
{
    struct test_canvas canvas;
    PT_ASSERT(make_canvas(&canvas, 20, 8, 20));
    struct font_pen pen = {.x = 0, .y = 0, .scale = 1, .color = INK};
    font_draw_text(&canvas.canvas, &pen, "T\xE6\x97\xA5", 3); /* the kanji's three bytes are not all given */
    free(canvas.pixels);
    PT_ASSERT_EQ_U32(pen.x, FONT_CELL_WIDTH);
}

PTEST_MAIN()
