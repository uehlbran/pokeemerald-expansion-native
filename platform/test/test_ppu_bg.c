/*
 * Tests of the ppu's backgrounds (ppu_bg.c), through ppu_render_line on tiny
 * scenes and, for the affine reference point arithmetic, directly.
 *
 * From GBATEK "LCD I/O BG Control", "LCD I/O BG Scrolling", "LCD I/O BG
 * Rotation/Scaling", "LCD I/O Mosaic Function", "LCD VRAM Character Data",
 * "LCD VRAM BG Screen Data Format (BG Map)". Expected colours are worked out
 * from the palettes and tiles each test writes: a dot shows palette entry
 * bank * 16 + index (4bpp) or index (8bpp), and index 0 is transparent.
 */
// deps: gba_memmap.c ppu.c ppu_obj.c ppu_window.c ppu_blend.c
#include "ppu_internal.h"
#include "ppu_scene.h"
#include "ptest.h"

#define BACKDROP rgb(31, 0, 0)
#define FIXED(n) ((n) * 256) /* GBATEK: reference points and PA-PD have 8 fraction bits */

/* BG palette bank 3, colour i: red i, blue 31 - i. Distinct from BACKDROP. */
static uint16_t bank3(unsigned index)
{
    return rgb(index, 0, 31u - index);
}

static void set_bank3(void)
{
    for (unsigned i = 1; i < 16; i++)
        bg_palette_set(3, i, bank3(i));
}

/* 8bpp colours: entry i is green i / 32, blue i % 32 (distinct for 1..255). */
static uint16_t colour256(unsigned index)
{
    return rgb(0, index / 32u, index % 32u);
}

static void set_palette256(void)
{
    for (unsigned i = 1; i < 256; i++)
        palette_set(i, colour256(i));
}

/* 8bpp pattern: 1 + col + 8 * row plus an offset, distinct for all 64 dots. */
static uint8_t pattern8(unsigned col, unsigned row, unsigned offset)
{
    return (uint8_t)(1u + col + 8u * row + offset);
}

static void pattern8_tile(tile_pixels pixels, unsigned offset)
{
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            pixels[r][c] = pattern8(c, r, offset);
}

static void fill_block(uint32_t block_offset, uint16_t entry)
{
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++)
            put_map_entry(block_offset, col, row, entry);
}

/* A 4bpp tile of one colour index at character block 0. */
static void solid_tile_4bpp(unsigned tile, uint8_t index)
{
    tile_pixels pixels;
    solid_tile(pixels, index);
    put_tile_4bpp(tile * 32u, pixels);
}

/* ================= text backgrounds ================= */

PTEST(a_4bpp_tile_renders_at_its_map_position)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    tile_pixels pixels;
    pattern_tile(pixels);
    put_tile_4bpp(1 * 32u, pixels);                                   /* tile 1, character block 0 */
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 2, 1, map_entry(1, 3, 0)); /* column 2, row 1 */
    io_write16(IO_BG0CNT, bgcnt_screen_block(31));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);

    /* Worked by hand: dot (0,0) of the tile is index 1, (7,0) is 8, (0,1) is 9. */
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[16], rgb(1, 0, 30));
    PT_ASSERT_EQ_U32(out[23], rgb(8, 0, 23));
    PT_ASSERT_EQ_U32(out[15], BACKDROP);
    PT_ASSERT_EQ_U32(out[24], BACKDROP);
    PT_ASSERT_EQ_INT(ppu_render_line(9, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[16], rgb(9, 0, 22));

    for (uint16_t line = 6; line < 18; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < SCREEN_W; x++) {
            const bool inside = line >= 8 && line < 16 && x >= 16 && x < 24;
            const uint16_t expected = inside ? bank3(pattern_index(x - 16, line - 8u)) : BACKDROP;
            PT_ASSERT_EQ_U32(out[x], expected);
        }
    }
}

PTEST(a_4bpp_tile_reads_its_character_block_and_palette_bank)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(3, 5, rgb(0, 31, 0));
    bg_palette_set(7, 5, rgb(31, 31, 0));
    solid_tile_4bpp(4, 3); /* the same tile number in block 0: must not be used */
    tile_pixels pixels;
    solid_tile(pixels, 5);
    put_tile_4bpp(2 * CHAR_BLOCK_BYTES + 4 * 32u, pixels); /* tile 4 of character block 2 */
    fill_block(8 * SCREEN_BLOCK_BYTES, map_entry(4, 7, 0));
    io_write16(IO_BG0CNT + 2, (uint16_t)(bgcnt_char_block(2) | bgcnt_screen_block(8)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(77, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(31, 31, 0)), -1);
}

PTEST(an_8bpp_tile_uses_the_256_colour_palette_and_ignores_the_map_palette)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    set_palette256();
    tile_pixels pixels;
    pattern8_tile(pixels, 0);
    put_tile_8bpp(1 * 64u, pixels); /* 8bpp tile 1 is 64 bytes in */
    fill_block(31 * SCREEN_BLOCK_BYTES, map_entry(1, 5, 0));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_8BPP | bgcnt_screen_block(31)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(3, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], colour256(25)); /* dot (0,3): 1 + 0 + 24 */
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], colour256(pattern8(x % 8u, 3, 0)));
}

PTEST(colour_0_of_an_8bpp_tile_is_transparent)
{
    /* GBATEK "LCD Color Palettes": colour 0 of all BG palettes is transparent,
     * in 256-colour mode too (a reference port drew it opaque). */
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    palette_set(200, rgb(5, 25, 5));
    bg_palette_set(1, 1, rgb(0, 31, 0));
    tile_pixels stripes;
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            stripes[r][c] = (c % 2u) ? 200 : 0;
    put_tile_8bpp(1 * 64u, stripes);
    fill_block(31 * SCREEN_BLOCK_BYTES, map_entry(1, 0, 0));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_8BPP | bgcnt_screen_block(31)));
    /* BG1, below: solid colour from character block 1. */
    tile_pixels solid;
    solid_tile(solid, 1);
    put_tile_4bpp(CHAR_BLOCK_BYTES + 1 * 32u, solid);
    fill_block(30 * SCREEN_BLOCK_BYTES, map_entry(1, 1, 0));
    io_write16(IO_BG0CNT + 2, (uint16_t)(bgcnt_priority(1) | bgcnt_char_block(1) | bgcnt_screen_block(30)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], (x % 2u) ? rgb(5, 25, 5) : rgb(0, 31, 0));
}

static void check_flipped_tile(uint16_t flips, bool hflip, bool vflip)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    tile_pixels pixels;
    pattern_tile(pixels);
    put_tile_4bpp(1 * 32u, pixels);
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 3, flips));
    io_write16(IO_BG0CNT, bgcnt_screen_block(31));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    for (uint16_t line = 0; line < 8; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 8; x++) {
            const unsigned col = hflip ? 7 - x : x;
            const unsigned row = vflip ? 7u - line : line;
            PT_ASSERT_EQ_U32(out[x], bank3(pattern_index(col, row)));
        }
        PT_ASSERT_EQ_U32(out[8], BACKDROP);
    }
}

PTEST(horizontal_flip_mirrors_the_columns)
{
    check_flipped_tile(MAP_HFLIP, true, false);
}
PTEST(vertical_flip_mirrors_the_rows)
{
    check_flipped_tile(MAP_VFLIP, false, true);
}
PTEST(both_flips_mirror_both)
{
    check_flipped_tile(MAP_HFLIP | MAP_VFLIP, true, true);
}
PTEST(no_flip_bits_draw_the_tile_as_stored)
{
    check_flipped_tile(0, false, false);
}

PTEST(flips_apply_to_8bpp_tiles)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    set_palette256();
    tile_pixels pixels;
    pattern8_tile(pixels, 0);
    put_tile_8bpp(1 * 64u, pixels);
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 0, MAP_HFLIP | MAP_VFLIP));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_8BPP | bgcnt_screen_block(31)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(2, out), PPU_OK);
    for (unsigned x = 0; x < 8; x++)
        PT_ASSERT_EQ_U32(out[x], colour256(pattern8(7 - x, 5, 0)));
}

/* Tiles 1-4: solid colour indices 1-4 of bank 3. */
static void four_solid_tiles(void)
{
    set_bank3();
    for (unsigned tile = 1; tile <= 4; tile++)
        solid_tile_4bpp(tile, (uint8_t)tile);
}

PTEST(horizontal_scroll_wraps_at_256_on_a_256_wide_map)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    four_solid_tiles();
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 31, 0, map_entry(2, 3, 0));
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 3, 0));
    io_write16(IO_BG0CNT, bgcnt_screen_block(31));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    const uint16_t offsets[] = {252, 508}; /* 508 & 255 == 252 on a 256-wide map */
    for (unsigned i = 0; i < 2; i++) {
        io_write16(IO_BG0HOFS, offsets[i]);
        PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, 4, bank3(2)), -1);  /* map x 252-255 */
        PT_ASSERT_EQ_INT(first_mismatch(out, 4, 12, bank3(1)), -1); /* map x 256 = 0 */
        PT_ASSERT_EQ_INT(first_mismatch(out, 12, 20, BACKDROP), -1);
    }
}

PTEST(vertical_scroll_wraps_at_256_on_a_256_tall_map)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    four_solid_tiles();
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 31, map_entry(2, 3, 0));
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 3, 0));
    io_write16(IO_BG0CNT, bgcnt_screen_block(31));
    io_write16(IO_BG0VOFS, 252);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    for (uint16_t line = 0; line < 20; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        const uint16_t expected = line < 4 ? bank3(2) : line < 12 ? bank3(1) : BACKDROP;
        PT_ASSERT_EQ_U32(out[0], expected);
        PT_ASSERT_EQ_U32(out[7], expected);
        PT_ASSERT_EQ_U32(out[8], BACKDROP);
    }
}

PTEST(a_512x256_map_puts_sc1_to_the_right_and_wraps_at_512)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    four_solid_tiles();
    put_map_entry(28 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 3, 0)); /* SC0 */
    put_map_entry(29 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(2, 3, 0)); /* SC1 = SC0 + 2K */
    put_map_entry(29 * SCREEN_BLOCK_BYTES, 31, 0, map_entry(3, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_screen_block(28) | bgcnt_size(1)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);

    io_write16(IO_BG0HOFS, 0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(1)), -1);
    io_write16(IO_BG0HOFS, 256);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(2)), -1);
    io_write16(IO_BG0HOFS, 504);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(3)), -1);  /* map x 504-511 */
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, bank3(1)), -1); /* map x 512 = 0 */
    /* Bits 9-15 of BGxHOFS are not used (GBATEK "LCD I/O BG Scrolling"). */
    io_write16(IO_BG0HOFS, 0xFE00u | 256u);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(2)), -1);
}

PTEST(a_256x512_map_puts_sc1_below)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    four_solid_tiles();
    put_map_entry(28 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(1, 3, 0));
    put_map_entry(29 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(2, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_screen_block(28) | bgcnt_size(2)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], bank3(1));
    PT_ASSERT_EQ_U32(out[8], BACKDROP);
    io_write16(IO_BG0VOFS, 256);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], bank3(2));
    io_write16(IO_BG0VOFS, 504);
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK); /* map y 512 = 0 */
    PT_ASSERT_EQ_U32(out[0], bank3(1));
}

PTEST(a_512x512_map_has_four_screen_blocks)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    four_solid_tiles();
    for (unsigned sc = 0; sc < 4; sc++)
        put_map_entry((24 + sc) * SCREEN_BLOCK_BYTES, 0, 0, map_entry(sc + 1, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_screen_block(24) | bgcnt_size(3)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    const uint16_t hofs[4] = {0, 256, 0, 256};
    const uint16_t vofs[4] = {0, 0, 256, 256};
    for (unsigned sc = 0; sc < 4; sc++) {
        io_write16(IO_BG0HOFS, hofs[sc]);
        io_write16(IO_BG0VOFS, vofs[sc]);
        PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(sc + 1)), -1);
        PT_ASSERT_EQ_U32(out[8], BACKDROP);
    }
}

PTEST(bg_mosaic_repeats_the_first_dot_of_each_block)
{
    /* GBATEK "LCD I/O Mosaic Function": H-Size 1 (minus 1) makes blocks of 2. */
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    tile_pixels pixels;
    pattern_tile(pixels);
    put_tile_4bpp(1 * 32u, pixels);
    fill_block(31 * SCREEN_BLOCK_BYTES, map_entry(1, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_MOSAIC | bgcnt_screen_block(31)));
    io_write16(IO_MOSAIC, 0x0011); /* BG H-Size 2, V-Size 2 */
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    for (uint16_t line = 0; line < 4; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 16; x++)
            PT_ASSERT_EQ_U32(out[x], bank3(pattern_index((x & ~1u) % 8u, line & ~1u)));
    }
    /* Blocks are in screen space: scrolled by 1, dots 0 and 1 both show map x 1. */
    io_write16(IO_BG0HOFS, 1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], bank3(pattern_index(1, 0)));
    PT_ASSERT_EQ_U32(out[1], bank3(pattern_index(1, 0)));
    PT_ASSERT_EQ_U32(out[2], bank3(pattern_index(3, 0)));
    /* No mosaic without the BGxCNT bit, nor with sizes of 1. */
    io_write16(IO_BG0HOFS, 0);
    io_write16(IO_BG0CNT, bgcnt_screen_block(31));
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[1], bank3(pattern_index(1, 1)));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_MOSAIC | bgcnt_screen_block(31)));
    io_write16(IO_MOSAIC, 0x0000);
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[1], bank3(pattern_index(1, 1)));
}

/* BG0, 4bpp, mosaic on, showing the pattern tile everywhere. */
static void mosaic_scene(uint16_t mosaic)
{
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    tile_pixels pixels;
    pattern_tile(pixels);
    put_tile_4bpp(1 * 32u, pixels);
    fill_block(31 * SCREEN_BLOCK_BYTES, map_entry(1, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_MOSAIC | bgcnt_screen_block(31)));
    io_write16(IO_MOSAIC, mosaic);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
}

PTEST(bg_mosaic_size_12_repeats_the_first_dot_of_blocks_of_12)
{
    /* 12 is not a multiple of the 8-dot tile pattern, so the size field's
     * width (4 bits, up to 16) is told apart from 3 bits. */
    uint16_t out[SCREEN_W];
    mosaic_scene(0x00BB); /* H-Size 12 (field 11), V-Size 12 */
    for (uint16_t line = 0; line < 26; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 60; x++)
            PT_ASSERT_EQ_U32(out[x], bank3(pattern_index((x / 12u * 12u) % 8u, (line / 12u * 12u) % 8u)));
    }
}

PTEST(bg_mosaic_in_one_direction_leaves_the_other_alone)
{
    uint16_t out[SCREEN_W];
    mosaic_scene(0x0010); /* H-Size 1, V-Size 2: lines repeat, dots do not */
    for (uint16_t line = 0; line < 4; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 16; x++)
            PT_ASSERT_EQ_U32(out[x], bank3(pattern_index(x % 8u, line & ~1u)));
    }
    mosaic_scene(0x0001); /* H-Size 2, V-Size 1: dots repeat, lines do not */
    for (uint16_t line = 0; line < 4; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 16; x++)
            PT_ASSERT_EQ_U32(out[x], bank3(pattern_index((x & ~1u) % 8u, line)));
    }
}

PTEST(bg_fetches_beyond_the_64k_bg_area_are_transparent)
{
    /* Character block 3 is at 0xC000: 4bpp tile 511 ends at 0xFFFF, tile 512
     * would be in OBJ VRAM. Interpretation: such fetches read 0 (see ppu_internal.h). */
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    tile_pixels solid;
    solid_tile(solid, 1);
    put_tile_4bpp(3 * CHAR_BLOCK_BYTES + 511 * 32u, solid);
    put_tile_4bpp(3 * CHAR_BLOCK_BYTES + 512 * 32u, solid); /* lands at OBJ_VRAM */
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 0, 0, map_entry(511, 3, 0));
    put_map_entry(31 * SCREEN_BLOCK_BYTES, 1, 0, map_entry(512, 3, 0));
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_char_block(3) | bgcnt_screen_block(31)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, bank3(1)), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 16, BACKDROP), -1);

    /* A 512-wide map at screen block 31 has its SC1 at 0x10000: map reads 0 there. */
    scene_reset();
    palette_set(0, BACKDROP);
    set_bank3();
    solid_tile_4bpp(1, 1);
    vram_write16(OBJ_VRAM, map_entry(1, 3, 0)); /* would be SC1's first entry */
    io_write16(IO_BG0CNT, (uint16_t)(bgcnt_screen_block(31) | bgcnt_size(1)));
    io_write16(IO_BG0HOFS, 256);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, BACKDROP), -1);
}

/* ================= rotation/scaling backgrounds ================= */

/* Mode 1, BG2: 8bpp tiles from character block 0, map at screen block 16. Tile
 * 1 is the 8bpp pattern, tile 2 the pattern + 64; the map alternates them in a
 * checkerboard, so a texel's colour depends on both its tile and its dot. */
#define AFFINE_SCREEN_BLOCK 16u

static uint16_t texel(int32_t tx, int32_t ty)
{
    const uint32_t x = (uint32_t)tx, y = (uint32_t)ty;
    const unsigned tile = ((x / 8u + y / 8u) % 2u) ? 2 : 1;
    return colour256(pattern8(x % 8u, y % 8u, tile == 2 ? 64 : 0));
}

static void affine_regs(int16_t pa, int16_t pb, int16_t pc, int16_t pd)
{
    io_write_signed16(IO_BG2PA, pa);
    io_write_signed16(IO_BG2PB, pb);
    io_write_signed16(IO_BG2PC, pc);
    io_write_signed16(IO_BG2PD, pd);
}

/* size: 0-3 for 128..1024 pixels; flags: BGCNT_WRAP and/or BGCNT_MOSAIC. */
static void affine_scene(unsigned size, uint16_t flags)
{
    scene_reset();
    palette_set(0, BACKDROP);
    set_palette256();
    tile_pixels pixels;
    pattern8_tile(pixels, 0);
    put_tile_8bpp(1 * 64u, pixels);
    pattern8_tile(pixels, 64);
    put_tile_8bpp(2 * 64u, pixels);
    const unsigned tiles = (128u << size) / 8u;
    for (unsigned row = 0; row < tiles; row++)
        for (unsigned col = 0; col < tiles; col++)
            vram_write8(AFFINE_SCREEN_BLOCK * SCREEN_BLOCK_BYTES + row * tiles + col,
                        (uint8_t)(((col + row) % 2u) ? 2 : 1));
    io_write16(IO_BG0CNT + 4, (uint16_t)(bgcnt_screen_block(AFFINE_SCREEN_BLOCK) | bgcnt_size(size) | flags));
    affine_regs(FIXED(1), 0, 0, FIXED(1));
    io_write16(IO_DISPCNT, DISP_MODE_1 | DISP_BG2);
    ppu_latch_vblank();
}

PTEST(an_affine_bg_with_the_identity_matrix_equals_a_text_bg_of_the_same_map)
{
    static uint16_t affine[SCREEN_H][SCREEN_W], text[SCREEN_H][SCREEN_W];
    affine_scene(1, 0); /* 256x256: 32x32 tiles, one byte each */
    tile_pixels pixels;
    solid_tile(pixels, 200);
    put_tile_8bpp(3 * 64u, pixels);
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++) {
            const unsigned tile = (col * 3u + row) % 4u; /* tile 0 is transparent */
            vram_write8(AFFINE_SCREEN_BLOCK * SCREEN_BLOCK_BYTES + row * 32u + col, (uint8_t)tile);
            put_map_entry(20 * SCREEN_BLOCK_BYTES, col, row, map_entry(tile, 0, 0));
        }
    for (uint16_t line = 0; line < SCREEN_H; line++)
        PT_ASSERT_EQ_INT(ppu_render_line(line, affine[line]), PPU_OK);

    io_write16(IO_BG0CNT, (uint16_t)(BGCNT_8BPP | bgcnt_screen_block(20)));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    for (uint16_t line = 0; line < SCREEN_H; line++)
        PT_ASSERT_EQ_INT(ppu_render_line(line, text[line]), PPU_OK);

    /* By hand: map (0,0) is tile 0 (transparent), (1,0) tile 3 (all 200),
     * (2,0) tile 2 (dot (0,0) is 65). */
    PT_ASSERT_EQ_U32(affine[0][0], BACKDROP);
    PT_ASSERT_EQ_U32(affine[0][8], colour256(200));
    PT_ASSERT_EQ_U32(affine[0][16], colour256(65));
    PT_ASSERT_MEM_EQ(affine, text, sizeof affine);
}

PTEST(an_affine_bg_draws_texel_x_y_at_dot_x_y_with_the_identity_matrix)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, 0);
    for (uint16_t line = 0; line < SCREEN_H; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < SCREEN_W; x++)
            PT_ASSERT_EQ_U32(out[x], texel((int32_t)x, line));
    }
}

PTEST(an_affine_bg_with_pa_2_shows_every_second_texel)
{
    uint16_t out[SCREEN_W];
    affine_scene(2, 0); /* 512x512, so 2 * 239 stays inside */
    affine_regs(FIXED(2), 0, 0, FIXED(1));
    PT_ASSERT_EQ_INT(render_frame_to(5, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[1], colour256(43)); /* texel (2,5): 1 + 2 + 40 */
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], texel(2 * (int32_t)x, 5));
}

PTEST(an_affine_bg_with_pa_one_half_shows_each_texel_twice)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, 0);
    affine_regs(FIXED(1) / 2, 0, 0, FIXED(1));
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], texel((int32_t)x / 2, 0));
}

PTEST(the_reference_point_offsets_an_affine_bg)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, BGCNT_WRAP);
    io_write32(IO_BG2X, FIXED(11));
    io_write32(IO_BG2Y, FIXED(21));
    ppu_latch_vblank();
    for (uint16_t line = 0; line < 30; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < SCREEN_W; x++)
            PT_ASSERT_EQ_U32(out[x], texel(((int32_t)x + 11) % 256, line + 21));
    }
}

PTEST(a_negative_reference_point_wraps_or_is_transparent)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, BGCNT_WRAP);
    io_write32(IO_BG2X, (uint32_t)-FIXED(2)); /* 0xFFFFFE00, as the game writes a negative s32 */
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(254, 0));
    PT_ASSERT_EQ_U32(out[1], texel(255, 0));
    PT_ASSERT_EQ_U32(out[2], texel(0, 0));

    affine_scene(1, 0);
    io_write32(IO_BG2X, (uint32_t)-FIXED(2));
    io_write32(IO_BG2Y, (uint32_t)-1); /* -1/256: dot row -1 */
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK); /* y = 255/256: texel row 0 */
    PT_ASSERT_EQ_U32(out[0], BACKDROP);
    PT_ASSERT_EQ_U32(out[1], BACKDROP);
    PT_ASSERT_EQ_U32(out[2], texel(0, 0));
}

PTEST(area_overflow_wraps_or_is_transparent)
{
    /* GBATEK "LCD I/O BG Control" bit 13, and "Area Overflow". */
    uint16_t out[SCREEN_W];
    affine_scene(0, BGCNT_WRAP); /* 128x128 */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], texel((int32_t)x % 128, 0));

    affine_scene(0, 0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < 128; x++)
        PT_ASSERT_EQ_U32(out[x], texel((int32_t)x, 0));
    PT_ASSERT_EQ_INT(first_mismatch(out, 128, SCREEN_W, BACKDROP), -1);
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(render_frame_to(127, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(0, 127));
    PT_ASSERT_EQ_INT(ppu_render_line(128, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
}

PTEST(the_reference_point_steps_by_pb_and_pd_after_each_line)
{
    uint16_t out[SCREEN_W];
    affine_scene(2, BGCNT_WRAP);
    affine_regs(FIXED(1), FIXED(1), 0, FIXED(1)); /* dmx 1: each line starts one texel right */
    for (uint16_t line = 0; line < SCREEN_H; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_U32(out[0], texel(line, line));
        PT_ASSERT_EQ_U32(out[100], texel(100 + line, line));
    }
    affine_scene(2, BGCNT_WRAP);
    affine_regs(FIXED(1), 0, 0, FIXED(2)); /* dmy 2: every second texel row */
    for (uint16_t line = 0; line < SCREEN_H; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        PT_ASSERT_EQ_U32(out[3], texel(3, 2 * line));
    }
}

PTEST(pc_moves_down_the_bg_along_a_line)
{
    uint16_t out[SCREEN_W];
    affine_scene(2, BGCNT_WRAP);
    affine_regs(0, 0, FIXED(1), FIXED(1)); /* dot x reads texel (0, line + x) */
    PT_ASSERT_EQ_INT(render_frame_to(4, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], texel(0, 4 + (int32_t)x));
}

PTEST(a_reference_write_mid_frame_relatches_at_the_current_line)
{
    /* GBATEK: a write outside VBlank "does immediately copy the new value to
     * the corresponding internal register ... the new value specifies the
     * origin of the <current> scanline". */
    uint16_t out[SCREEN_W];
    affine_scene(2, BGCNT_WRAP);
    PT_ASSERT_EQ_INT(render_frame_to(9, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(0, 9));
    io_write32(IO_BG2Y, FIXED(100));
    PT_ASSERT_EQ_INT(ppu_render_line(10, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[5], texel(5, 100));
    PT_ASSERT_EQ_INT(ppu_render_line(11, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[5], texel(5, 101)); /* and steps on from there */
    /* Only the written coordinate is re-latched; X keeps its stepped value. */
    affine_scene(2, BGCNT_WRAP);
    affine_regs(FIXED(1), FIXED(1), 0, FIXED(1));
    PT_ASSERT_EQ_INT(render_frame_to(9, out), PPU_OK);
    io_write32(IO_BG2Y, FIXED(50));
    PT_ASSERT_EQ_INT(ppu_render_line(10, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(10, 50));
    /* Without a write, line 10 continues the frame. */
    affine_scene(2, BGCNT_WRAP);
    PT_ASSERT_EQ_INT(render_frame_to(10, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[5], texel(5, 10));
}

PTEST(a_reference_write_after_the_vblank_latch_applies_from_line_0)
{
    uint16_t out[SCREEN_W];
    affine_scene(2, BGCNT_WRAP);   /* latched */
    io_write32(IO_BG2X, FIXED(7)); /* the VBlank handler flushing buffered registers */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(7, 0));
    PT_ASSERT_EQ_INT(ppu_render_line(1, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(7, 1));
}

PTEST(the_vblank_latch_restarts_the_frame_from_the_registers)
{
    uint16_t out[SCREEN_W];
    affine_scene(2, BGCNT_WRAP);
    io_write32(IO_BG2Y, FIXED(3));
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(render_frame_to(SCREEN_H - 1, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(0, 3 + SCREEN_H - 1));
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(0, 3));
}

PTEST(reference_point_bits_28_to_31_are_ignored)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, BGCNT_WRAP);
    io_write32(IO_BG2X, UINT32_C(0xF0000000) | FIXED(4)); /* bit 27 clear: +4 */
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(4, 0));
    affine_scene(1, BGCNT_WRAP);
    io_write32(IO_BG2X, UINT32_C(0x0FFFFF00)); /* bit 27 set: -1 */
    ppu_latch_vblank();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], texel(255, 0));
    PT_ASSERT_EQ_U32(out[1], texel(0, 0));
}

PTEST(affine_bg_mosaic_repeats_dots_and_lines)
{
    uint16_t out[SCREEN_W];
    affine_scene(1, BGCNT_MOSAIC);
    io_write16(IO_MOSAIC, 0x0021); /* BG H-Size 2, V-Size 3 */
    for (uint16_t line = 0; line < 6; line++) {
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
        for (unsigned x = 0; x < 16; x++)
            PT_ASSERT_EQ_U32(out[x], texel((int32_t)(x & ~1u), line - line % 3));
    }
}

/* ================= the reference point arithmetic ================= */

PTEST(sign_extend_takes_bit_27_as_the_sign)
{
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(0), 0);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0x07FFFFFF)), 134217727);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0x08000000)), -134217728);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0x0FFFFFFF)), -1);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0xF0000000)), 0);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0xFFFFFFFF)), -1);
    PT_ASSERT_EQ_INT(ppu_affine_sign_extend(UINT32_C(0x80000100)), 256);
}

PTEST(the_reference_point_wraps_at_28_bits_when_stepped)
{
    struct ppu_affine_regs regs = {
        .pa = 256, .pb = 256, .pc = 0, .pd = -256, .x = UINT32_C(0x07FFFF00), .y = UINT32_C(0x08000000)};
    struct ppu_affine_reference ref;
    ppu_affine_reference_latch(&ref, &regs);
    PT_ASSERT_EQ_INT(ref.x, 0x07FFFF00);
    PT_ASSERT_EQ_INT(ref.y, -134217728);
    ppu_affine_reference_step(&ref, &regs);
    PT_ASSERT_EQ_INT(ref.x, -134217728);      /* 0x08000000 is the most negative */
    PT_ASSERT_EQ_INT(ref.y, 134217728 - 256); /* below the most negative wraps to the top */
}

PTEST(picking_up_writes_relatches_only_a_changed_register)
{
    struct ppu_affine_regs regs = {.pa = 256, .pb = 512, .pc = 0, .pd = 256, .x = FIXED(1), .y = FIXED(2)};
    struct ppu_affine_reference ref;
    ppu_affine_reference_latch(&ref, &regs);
    ppu_affine_reference_step(&ref, &regs);
    ppu_affine_reference_pick_up_writes(&ref, &regs);
    PT_ASSERT_EQ_INT(ref.x, FIXED(3));
    PT_ASSERT_EQ_INT(ref.y, FIXED(3));
    regs.x = FIXED(40);
    ppu_affine_reference_pick_up_writes(&ref, &regs);
    PT_ASSERT_EQ_INT(ref.x, FIXED(40));
    PT_ASSERT_EQ_INT(ref.y, FIXED(3));
    regs.y = (uint32_t)-FIXED(1);
    ppu_affine_reference_pick_up_writes(&ref, &regs);
    PT_ASSERT_EQ_INT(ref.y, -FIXED(1));
}

PTEST_MAIN()
