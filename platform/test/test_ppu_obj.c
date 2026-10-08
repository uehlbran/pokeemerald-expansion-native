/*
 * Tests of the ppu's OBJs (ppu_obj.c) through ppu_render_line on tiny scenes.
 *
 * From GBATEK "LCD OBJ - Overview" (the OBJ rendering cycles per line), "LCD
 * OBJ - OAM Attributes", "LCD OBJ - OAM Rotation/Scaling Parameters", "LCD OBJ
 * - VRAM Character (Tile) Mapping" and "LCD I/O Mosaic Function". OBJ tiles
 * are at 06010000 in modes 0-2 and OBJ tile numbers count 32-byte units.
 */
// deps: gba_memmap.c ppu.c ppu_bg.c ppu_window.c ppu_blend.c
#include "ppu.h"
#include "ppu_scene.h"
#include "ptest.h"

#define BACKDROP rgb(31, 0, 0)
#define FIXED(n) ((int16_t)((n) * 256))

/* OBJ palette bank b, colour i: red i, green b, blue 16. */
static uint16_t objc(unsigned bank, unsigned index) { return rgb(index, bank, 16); }

static void set_obj_banks(void)
{
    for (unsigned bank = 0; bank < 16; bank++)
        for (unsigned i = 1; i < 16; i++)
            obj_palette_set(bank, i, objc(bank, i));
}

static void put_obj_tile_4bpp(unsigned tile, const tile_pixels pixels)
{
    put_tile_4bpp(OBJ_VRAM + tile * OBJ_TILE_BYTES, pixels);
}

static void put_obj_tile_8bpp(unsigned tile, const tile_pixels pixels)
{
    put_tile_8bpp(OBJ_VRAM + tile * OBJ_TILE_BYTES, pixels);
}

static void solid_obj_tiles_4bpp(unsigned first, unsigned count, uint8_t index)
{
    tile_pixels pixels;
    solid_tile(pixels, index);
    for (unsigned tile = first; tile < first + count; tile++)
        put_obj_tile_4bpp(tile, pixels);
}

static void obj_scene(uint16_t dispcnt)
{
    scene_reset();
    palette_set(0, BACKDROP);
    set_obj_banks();
    io_write16(IO_DISPCNT, (uint16_t)(DISP_OBJ | dispcnt));
}

/* ---- position, palette, flips, depth ---- */

PTEST(an_8x8_4bpp_obj_draws_at_its_position_from_the_obj_palette)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    for (unsigned i = 1; i < 16; i++)
        bg_palette_set(4, i, rgb(0, 31, 31)); /* the BG half: must not be used */
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_set(0, 20, 50, a2(2, 0, 4));
    PT_ASSERT_EQ_INT(ppu_render_line(20, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[50], rgb(1, 4, 16)); /* dot (0,0): index 1 */
    PT_ASSERT_EQ_U32(out[57], rgb(8, 4, 16)); /* dot (7,0): index 8 */
    for (uint16_t line = 18; line < 30; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < SCREEN_W; x++) {
            const bool inside = line >= 20 && line < 28 && x >= 50 && x < 58;
            PT_ASSERT_EQ_U32(out[x], inside ? objc(4, pattern_index(x - 50, line - 20u)) : BACKDROP);
        }
    }
}

PTEST(objs_are_not_drawn_when_dispcnt_obj_is_off)
{
    uint16_t out[SCREEN_W];
    obj_scene(0);
    io_write16(IO_DISPCNT, DISP_MODE_0);
    solid_obj_tiles_4bpp(0, 1, 1);
    obj_set(0, 0, 0, a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
}

static void check_obj_flip(uint16_t flips, bool hflip, bool vflip)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_set(0, 30, (uint16_t)(60 | flips), a2(2, 0, 4));
    for (uint16_t line = 30; line < 38; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 60; x < 68; x++) {
            const unsigned col = hflip ? 67 - x : x - 60;
            const unsigned row = vflip ? 37u - line : line - 30u;
            PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(col, row)));
        }
    }
}

PTEST(obj_horizontal_flip_mirrors_the_columns) { check_obj_flip(A1_HFLIP, true, false); }
PTEST(obj_vertical_flip_mirrors_the_rows) { check_obj_flip(A1_VFLIP, false, true); }
PTEST(obj_both_flips_mirror_both) { check_obj_flip(A1_HFLIP | A1_VFLIP, true, true); }

PTEST(horizontal_flip_of_a_wide_obj_swaps_its_tiles)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    solid_obj_tiles_4bpp(3, 1, 15);
    obj_set(0, (uint16_t)(A0_WIDE | 0), (uint16_t)(A1_HFLIP | a1_size(0) | 0), a2(2, 0, 4)); /* 16x8 */
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, objc(4, 15)), -1);
    for (unsigned x = 8; x < 16; x++)
        PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(15 - x, 1)));
}

PTEST(an_8bpp_obj_uses_the_256_colour_obj_palette_and_colour_0_is_transparent)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            pixels[r][c] = (uint8_t)(100u + c + 8u * r);
    pixels[0][3] = 0;
    for (unsigned i = 100; i < 164; i++)
        palette_set(256 + i, rgb(i % 32u, 0, i / 32u));
    put_obj_tile_8bpp(4, pixels); /* 64 bytes: tile numbers 4 and 5 */
    obj_set(0, (uint16_t)(A0_8BPP | 0), 0, a2(4, 0, 9)); /* the palette number is ignored */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], rgb(100 % 32, 0, 3));
    PT_ASSERT_EQ_U32(out[3], BACKDROP);
    PT_ASSERT_EQ_INT(ppu_render_line(6, out), PPU_OK);
    for (unsigned x = 0; x < 8; x++)
        PT_ASSERT_EQ_U32(out[x], rgb((148u + x) % 32u, 0, (148u + x) / 32u));
}

/* GBATEK "LCD OBJ - OAM Attributes": the size table, [shape][size] = {w, h}. */
static const unsigned obj_sizes[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
};

PTEST(every_shape_and_size_has_the_gbatek_dimensions)
{
    uint16_t out[SCREEN_W];
    for (unsigned shape = 0; shape < 3; shape++)
        for (unsigned size = 0; size < 4; size++) {
            const unsigned w = obj_sizes[shape][size][0], h = obj_sizes[shape][size][1];
            obj_scene(DISP_OBJ_1D);
            solid_obj_tiles_4bpp(0, w * h / 64u, 1);
            obj_set(0, (uint16_t)(shape << 14 | 10u), (uint16_t)(a1_size(size) | 10u), a2(0, 0, 1));
            PT_ASSERT_EQ_INT(ppu_render_line(10, out), PPU_OK);
            PT_ASSERT_EQ_U32(out[9], BACKDROP);
            PT_ASSERT_EQ_INT(first_mismatch(out, 10, 10 + w, objc(1, 1)), -1);
            PT_ASSERT_EQ_U32(out[10 + w], BACKDROP);
            PT_ASSERT_EQ_INT(ppu_render_line((uint16_t)(10 + h - 1), out), PPU_OK);
            PT_ASSERT_EQ_U32(out[10], objc(1, 1));
            PT_ASSERT_EQ_INT(ppu_render_line((uint16_t)(10 + h), out), PPU_OK);
            PT_ASSERT_EQ_U32(out[10], BACKDROP);
            PT_ASSERT_EQ_INT(ppu_render_line(9, out), PPU_OK);
            PT_ASSERT_EQ_U32(out[10], BACKDROP);
        }
}

PTEST(prohibited_shape_3_is_not_drawn)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 1, 1);
    obj_set(0, (uint16_t)(3u << 14 | 0u), 0, a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
}

PTEST(the_disable_bit_hides_a_regular_obj)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 1, 1);
    obj_set(0, (uint16_t)(A0_DISABLE | 0), 0, a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
}

/* ---- position wrap ---- */

PTEST(y_past_255_wraps_to_the_top_of_the_screen)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_set(0, 252, 40, a2(2, 0, 4)); /* rows 4-7 on lines 0-3 */
    for (uint16_t line = 0; line < 5; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_U32(out[40], line < 4 ? objc(4, pattern_index(0, line + 4u)) : BACKDROP);
    }
}

PTEST(x_is_nine_bit_and_wraps_to_the_left_edge)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_set(0, 0, 508, a2(2, 0, 4)); /* columns 4-7 at x 0-3 */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < 4; x++)
        PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(x + 4, 0)));
    PT_ASSERT_EQ_INT(first_mismatch(out, 4, SCREEN_W, BACKDROP), -1);
}

PTEST(a_128_tall_obj_below_y_128_shows_at_the_top_only)
{
    /* GBATEK "LCD OBJ - OAM Attributes": "located at Y>128 will be treated as
     * at Y>-128 ... it is then NOT displayed at the bottom". */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 64, 1);
    obj_affine_set(0, FIXED(1) / 2, 0, 0, FIXED(1) / 2); /* 2x: fills the double-size box */
    obj_set(0, (uint16_t)(A0_AFFINE | A0_DOUBLE | 150), a1_size(3), a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 128, objc(1, 1)), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(21, out), PPU_OK); /* box row 127: 150 - 256 + 127 */
    PT_ASSERT_EQ_U32(out[0], objc(1, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(22, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], BACKDROP);
    for (uint16_t line = 150; line < SCREEN_H; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
    }
    /* A small OBJ at the same Y stays at the bottom. */
    obj_set(0, 150, 0, a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(157, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], objc(1, 1));
}

/* ---- priority ---- */

static void solid_bg0(unsigned priority, uint16_t colour)
{
    bg_palette_set(1, 1, colour);
    tile_pixels pixels;
    solid_tile(pixels, 1);
    put_tile_4bpp(32u, pixels);
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++)
            put_map_entry(31 * SCREEN_BLOCK_BYTES, col, row, map_entry(1, 1, 0));
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_priority(priority) | bgcnt_screen_block(31)));
}

PTEST(an_obj_is_above_a_bg_of_equal_or_lower_priority_and_below_a_higher_one)
{
    /* GBATEK: same priority as a BG, "the OBJ becomes higher priority". */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D | DISP_BG0);
    solid_bg0(1, rgb(0, 0, 31));
    solid_obj_tiles_4bpp(0, 1, 1);
    const uint16_t bg = rgb(0, 0, 31), obj = objc(2, 1);
    const struct { unsigned obj_priority; uint16_t expected; } cases[] = {
        { 0, obj }, { 1, obj }, { 2, bg }, { 3, bg },
    };
    for (unsigned i = 0; i < 4; i++) {
        obj_set(0, 0, 0, a2(0, cases[i].obj_priority, 2));
        PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
        PT_ASSERT_EQ_U32(out[0], cases[i].expected);
        PT_ASSERT_EQ_U32(out[8], bg);
    }
    solid_bg0(0, bg);
    obj_set(0, 0, 0, a2(0, 0, 2));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], obj);
}

PTEST(at_equal_priority_the_lower_oam_index_is_on_top)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 1, 1);
    tile_pixels left_half;
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            left_half[r][c] = c < 4 ? 2 : 0;
    put_obj_tile_4bpp(1, left_half);
    obj_set(3, 0, 0, a2(1, 1, 5)); /* front: left half only */
    obj_set(7, 0, 0, a2(0, 1, 6));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 4, objc(5, 2)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 4, 8, objc(6, 1)), -1); /* through its transparent dots */
}

PTEST(a_lower_priority_value_beats_a_lower_oam_index)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D | DISP_BG0);
    solid_bg0(1, rgb(0, 0, 31));
    solid_obj_tiles_4bpp(0, 1, 1);
    obj_set(3, 0, 0, a2(0, 2, 5)); /* behind BG0 */
    obj_set(7, 0, 0, a2(0, 0, 6)); /* in front of everything */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], objc(6, 1));
    obj_set(7, 0, 0, a2(0, 2, 6));
    obj_set(3, 0, 0, a2(0, 3, 5));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], rgb(0, 0, 31));
}

/* ---- tile mapping ---- */

PTEST(two_dimensional_mapping_puts_the_next_tile_row_32_tiles_on)
{
    /* GBATEK example: a 16x16 OBJ at tile 04h uses 04h, 05h, then 24h, 25h. */
    uint16_t out[SCREEN_W];
    obj_scene(0); /* 2D */
    solid_obj_tiles_4bpp(4, 1, 1);
    solid_obj_tiles_4bpp(5, 1, 2);
    solid_obj_tiles_4bpp(6, 1, 5); /* what 1D would use for the second row */
    solid_obj_tiles_4bpp(7, 1, 6);
    solid_obj_tiles_4bpp(0x24, 1, 3);
    solid_obj_tiles_4bpp(0x25, 1, 4);
    obj_set(0, 0, a1_size(1), a2(4, 0, 1)); /* 16x16 */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, objc(1, 1)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, objc(1, 2)), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, objc(1, 3)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, objc(1, 4)), -1);
    io_write16(IO_DISPCNT, DISP_OBJ | DISP_OBJ_1D); /* GBATEK: then 06h and 07h */
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, objc(1, 5)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, objc(1, 6)), -1);
}

static void solid_obj_tile_8bpp(unsigned tile, uint8_t index)
{
    tile_pixels pixels;
    solid_tile(pixels, index);
    put_obj_tile_8bpp(tile, pixels);
}

PTEST(an_8bpp_obj_in_2d_mapping_steps_two_tiles_and_ignores_bit_0)
{
    /* GBATEK: "(In 256 color mode: 04h and 06h, 24h and 26h.)", and in 2D
     * mapping the low bit of the tile number "is completely ignored". */
    uint16_t out[SCREEN_W];
    obj_scene(0);
    for (unsigned i = 11; i <= 14; i++)
        palette_set(256 + i, rgb(i, i, 0));
    solid_obj_tile_8bpp(4, 11);
    solid_obj_tile_8bpp(6, 12);
    solid_obj_tile_8bpp(0x24, 13);
    solid_obj_tile_8bpp(0x26, 14);
    for (unsigned tile = 4; tile <= 5; tile++) {
        obj_set(0, A0_8BPP, a1_size(1), a2(tile, 0, 0));
        PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, rgb(11, 11, 0)), -1);
        PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, rgb(12, 12, 0)), -1);
        PT_ASSERT_EQ_INT(ppu_render_line(15, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, rgb(13, 13, 0)), -1);
        PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, rgb(14, 14, 0)), -1);
    }
}

PTEST(an_8bpp_obj_in_1d_mapping_uses_consecutive_tile_pairs)
{
    /* GBATEK: "(In 256 color mode: 04h and 06h, 08h and 0Ah.)" */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    for (unsigned i = 11; i <= 14; i++)
        palette_set(256 + i, rgb(i, i, 0));
    solid_obj_tile_8bpp(4, 11);
    solid_obj_tile_8bpp(6, 12);
    solid_obj_tile_8bpp(8, 13);
    solid_obj_tile_8bpp(10, 14);
    obj_set(0, A0_8BPP, a1_size(1), a2(4, 0, 0));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, rgb(11, 11, 0)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, rgb(12, 12, 0)), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, rgb(13, 13, 0)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, rgb(14, 14, 0)), -1);
}

PTEST(obj_tile_reads_past_the_end_of_obj_vram_wrap_to_its_start)
{
    /* An 8bpp 8x8 OBJ at tile 1023: rows 0-3 are the last 32 bytes of OBJ
     * VRAM, rows 4-7 would be past VRAM's end; they wrap to the OBJ area's start. */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    palette_set(256 + 7, rgb(7, 7, 7));
    palette_set(256 + 9, rgb(9, 9, 9));
    for (uint32_t i = 0; i < 32; i++) {
        vram_write8(OBJ_VRAM + 0x7FE0u + i, 7);
        vram_write8(OBJ_VRAM + i, 9);
    }
    obj_set(0, A0_8BPP, 0, a2(1023, 0, 0));
    for (uint16_t line = 0; line < 8; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, line < 4 ? rgb(7, 7, 7) : rgb(9, 9, 9)), -1);
    }
}

/* ---- rotation/scaling ---- */

PTEST(an_affine_obj_with_the_identity_matrix_draws_like_a_regular_one)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_affine_set(0, FIXED(1), 0, 0, FIXED(1));
    obj_set(0, (uint16_t)(A0_AFFINE | 20), 50, a2(2, 0, 4));
    for (uint16_t line = 19; line < 29; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 48; x < 60; x++) {
            const bool inside = line >= 20 && line < 28 && x >= 50 && x < 58;
            PT_ASSERT_EQ_U32(out[x], inside ? objc(4, pattern_index(x - 50, line - 20u)) : BACKDROP);
        }
    }
}

PTEST(an_affine_obj_reads_the_parameter_group_attribute_1_selects)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_affine_set(5, 0, FIXED(1), FIXED(1), 0); /* group 5: transpose */
    obj_set(0, (uint16_t)(A0_AFFINE | 0), a1_affine_group(5), a2(2, 0, 4));
    for (uint16_t line = 0; line < 8; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 8; x++) /* PB steps texel x per line, PC texel y per dot */
            PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(line, x)));
    }
}

PTEST(a_double_size_affine_obj_draws_in_a_box_twice_as_big)
{
    /* GBATEK "OBJ Double-Size Bit": a 16x16 OBJ gets a 32x32 area; unscaled,
     * its image sits in the middle of it. */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 4, 1);
    obj_affine_set(0, FIXED(1), 0, 0, FIXED(1));
    obj_set(0, (uint16_t)(A0_AFFINE | A0_DOUBLE | 30), (uint16_t)(a1_size(1) | 40), a2(0, 0, 1));
    for (uint16_t line = 30; line < 62; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        const bool image_line = line >= 38 && line < 54;
        for (unsigned x = 36; x < 76; x++) {
            const bool inside = image_line && x >= 48 && x < 64;
            PT_ASSERT_EQ_U32(out[x], inside ? objc(1, 1) : BACKDROP);
        }
    }
    /* Scaled up 2x, the image fills the box: x 40-71, lines 30-61. */
    obj_affine_set(0, FIXED(1) / 2, 0, 0, FIXED(1) / 2);
    PT_ASSERT_EQ_INT(ppu_render_line(29, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
    for (uint16_t line = 30; line < 62; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_U32(out[39], BACKDROP);
        PT_ASSERT_EQ_INT(first_mismatch(out, 40, 72, objc(1, 1)), -1);
        PT_ASSERT_EQ_U32(out[72], BACKDROP);
    }
    PT_ASSERT_EQ_INT(ppu_render_line(62, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
    /* Without double size the box is the OBJ's own 16x16, cutting the 2x image. */
    obj_set(0, (uint16_t)(A0_AFFINE | 30), (uint16_t)(a1_size(1) | 40), a2(0, 0, 1));
    PT_ASSERT_EQ_INT(ppu_render_line(30, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 40, 56, objc(1, 1)), -1);
    PT_ASSERT_EQ_U32(out[56], BACKDROP);
    PT_ASSERT_EQ_INT(ppu_render_line(46, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[40], BACKDROP);
}

PTEST(a_scaled_affine_obj_samples_texel_dot_over_scale)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    obj_affine_set(0, FIXED(1) / 2, 0, 0, FIXED(1) / 2);
    obj_set(0, (uint16_t)(A0_AFFINE | A0_DOUBLE | 0), 0, a2(2, 0, 4)); /* 8x8 in a 16x16 box */
    for (uint16_t line = 0; line < 16; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 16; x++)
            PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(x / 2, line / 2u)));
    }
}

/* ---- mosaic ---- */

PTEST(obj_mosaic_repeats_dots_and_lines)
{
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    tile_pixels pixels;
    pattern_tile(pixels);
    put_obj_tile_4bpp(2, pixels);
    io_write16(IO_MOSAIC, 0x1100); /* OBJ H-Size 2, V-Size 2 */
    obj_set(0, (uint16_t)(A0_MOSAIC | 0), 0, a2(2, 0, 4));
    for (uint16_t line = 0; line < 8; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 8; x++)
            PT_ASSERT_EQ_U32(out[x], objc(4, pattern_index(x & ~1u, line & ~1u)));
    }
    /* Blocks are in screen space; a block that starts left of (above) the
     * OBJ takes the OBJ's first column (row). */
    obj_set(0, (uint16_t)(A0_MOSAIC | 1), 1, a2(2, 0, 4));
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[1], objc(4, pattern_index(0, 0)));
    PT_ASSERT_EQ_U32(out[2], objc(4, pattern_index(1, 0)));
    PT_ASSERT_EQ_U32(out[3], objc(4, pattern_index(1, 0)));
    PT_ASSERT_EQ_U32(out[4], objc(4, pattern_index(3, 0)));
    PT_ASSERT_EQ_INT(ppu_render_line(2, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[2], objc(4, pattern_index(1, 1)));
    /* The BG mosaic size does not apply to OBJs, nor does the OBJ size without the bit. */
    io_write16(IO_MOSAIC, 0x0011);
    PT_ASSERT_EQ_INT(ppu_render_line(2, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[2], objc(4, pattern_index(1, 1)));
    io_write16(IO_MOSAIC, 0x1100);
    obj_set(0, 1, 1, a2(2, 0, 4));
    PT_ASSERT_EQ_INT(ppu_render_line(2, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[3], objc(4, pattern_index(2, 1)));
}

/* ---- the OBJ rendering cycle budget ---- */

/* GBATEK "LCD OBJ - Overview": 1210 cycles per line (954 with H-Blank
 * Interval Free); a normal OBJ costs its width, a rotation/scaling OBJ
 * 10 + 2 x its (double-size) width. A 64-wide OBJ costs 64, so 18 fit in
 * 1210 with 58 cycles left: the 19th draws its first 58 columns only. */
static void wide_obj(unsigned index, uint16_t attr0_flags, uint16_t x, unsigned y, unsigned bank)
{
    obj_set(index, (uint16_t)(A0_WIDE | attr0_flags | y), (uint16_t)(a1_size(3) | x), a2(0, 0, bank));
}

static void check_budget_cut(uint16_t line, unsigned cut_bank, unsigned cut_x, unsigned drawn)
{
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, cut_x, cut_x + drawn, objc(cut_bank, 1)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, cut_x + drawn, cut_x + 64, BACKDROP), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 180, SCREEN_W, BACKDROP), -1); /* the next OBJ: none */
}

PTEST(the_obj_cycle_budget_cuts_the_obj_that_exceeds_it_and_drops_the_rest)
{
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 32, 1);
    for (unsigned i = 0; i < 18; i++)
        wide_obj(i, 0, 0, 0, 1);
    wide_obj(18, 0, 100, 0, 2);
    wide_obj(19, 0, 180, 0, 3);
    check_budget_cut(0, 2, 100, 58);
    /* With 17 in front, the 19th fits whole and the 20th is cut instead. */
    obj_set(17, (uint16_t)(A0_DISABLE | 0), 0, 0);
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 100, 164, objc(2, 1)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 180, 180 + 58, objc(3, 1)), -1);
}

PTEST(hblank_interval_free_shrinks_the_obj_budget_to_954)
{
    obj_scene(DISP_OBJ_1D | DISP_HBLANK_FREE);
    solid_obj_tiles_4bpp(0, 32, 1);
    for (unsigned i = 0; i < 14; i++) /* 14 x 64 = 896, 58 left */
        wide_obj(i, 0, 0, 0, 1);
    wide_obj(14, 0, 100, 0, 2);
    wide_obj(15, 0, 180, 0, 3);
    check_budget_cut(0, 2, 100, 58);
}

PTEST(objs_not_on_the_line_and_disabled_objs_cost_nothing)
{
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 32, 1);
    for (unsigned i = 0; i < 20; i++)
        wide_obj(i, 0, 0, 100, 1); /* lines 100-131 */
    for (unsigned i = 20; i < 40; i++)
        wide_obj(i, A0_DISABLE, 0, 0, 1);
    for (unsigned i = 40; i < 58; i++)
        wide_obj(i, 0, 0, 0, 1);
    wide_obj(58, 0, 100, 0, 2);
    wide_obj(59, 0, 180, 0, 3);
    check_budget_cut(0, 2, 100, 58);
}

PTEST(objs_off_screen_horizontally_on_the_line_still_cost_cycles)
{
    /* GBATEK: "The maximum number of OBJs per line is also affected by
     * undisplayed (offscreen) OBJs which are having higher priority". */
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 32, 1);
    for (unsigned i = 0; i < 18; i++)
        wide_obj(i, 0, 300, 0, 1); /* x 300 is -212: entirely left of the screen */
    wide_obj(18, 0, 100, 0, 2);
    wide_obj(19, 0, 180, 0, 3);
    check_budget_cut(0, 2, 100, 58);
}

PTEST(an_affine_obj_costs_10_plus_2_per_dot_of_its_width)
{
    /* 8 affine 64-wide OBJs: 8 x 138 = 1104, 106 left: the 9th draws
     * (106 - 10) / 2 = 48 columns. */
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 32, 1);
    obj_affine_set(0, FIXED(1), 0, 0, FIXED(1));
    for (unsigned i = 0; i < 8; i++)
        wide_obj(i, A0_AFFINE, 0, 0, 1);
    wide_obj(8, A0_AFFINE, 100, 0, 2);
    wide_obj(9, 0, 180, 0, 3);
    check_budget_cut(0, 2, 100, 48);
}

PTEST(a_double_size_affine_obj_costs_by_its_doubled_width)
{
    /* 4 double-size 64x32 OBJs (128-wide boxes): 4 x 266 = 1064, 146 left:
     * the 5th draws box columns 0-67; its unscaled image is box columns
     * 32-95, so x 132-167 of it appear. Line 20 is inside the image. */
    uint16_t out[SCREEN_W];
    obj_scene(DISP_OBJ_1D);
    solid_obj_tiles_4bpp(0, 32, 1);
    obj_affine_set(0, FIXED(1), 0, 0, FIXED(1));
    for (unsigned i = 0; i < 4; i++)
        wide_obj(i, A0_AFFINE | A0_DOUBLE, 0, 0, 1);
    wide_obj(4, A0_AFFINE | A0_DOUBLE, 100, 0, 2);
    PT_ASSERT_EQ_INT(ppu_render_line(20, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[131], BACKDROP);
    PT_ASSERT_EQ_INT(first_mismatch(out, 132, 168, objc(2, 1)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 168, 200, BACKDROP), -1);
}

PTEST_MAIN()
