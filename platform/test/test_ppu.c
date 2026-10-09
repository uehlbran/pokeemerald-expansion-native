/*
 * Tests of ppu's public contract (ppu.h): init and shutdown, argument errors,
 * forced blank, unsupported modes, the backdrop, priority between
 * backgrounds, and that drawing a line writes nothing into the GBA regions
 * (VCOUNT and DISPSTAT belong to the frame loop). Expected values come from
 * GBATEK "LCD Video Controller"; the scenes are built by ppu_scene.h.
 * Backgrounds, OBJs, windows and effects have their own test files.
 */
// deps: gba_memmap.c ppu_bg.c ppu_obj.c ppu_window.c ppu_blend.c
#include "ppu.h"
#include "ppu_scene.h"
#include "ptest.h"

#define BACKDROP rgb(31, 0, 0)

/* BG bg shows tile `tile` (every dot colour index `index`, 4bpp, palette bank
 * `bank`) everywhere: character block 0, screen block 31 - bg. */
static void solid_text_bg(unsigned bg, unsigned priority, unsigned tile, uint8_t index, unsigned bank)
{
    tile_pixels pixels;
    solid_tile(pixels, index);
    put_tile_4bpp(tile * 32u, pixels);
    const unsigned block = 31u - bg;
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++)
            put_map_entry(block * SCREEN_BLOCK_BYTES, col, row, map_entry(tile, bank, 0));
    io_write16(IO_BG0CNT + 2u * bg, (uint16_t)(bgcnt_priority(priority) | bgcnt_screen_block(block)));
}

/* ---- init, shutdown, arguments ---- */

PTEST(init_returns_ok_and_is_repeatable)
{
    scene_reset();
    PT_ASSERT_EQ_INT(ppu_init(), PPU_OK);
    PT_ASSERT_EQ_INT(ppu_init(), PPU_OK);
}

PTEST(render_fails_before_init_and_after_shutdown)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    ppu_shutdown();
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_ERR_NOT_INITIALIZED);
    ppu_shutdown(); /* twice is safe */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_ERR_NOT_INITIALIZED);
    PT_ASSERT_EQ_INT(ppu_init(), PPU_OK);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
}

PTEST(shutdown_before_any_init_is_safe)
{
    ppu_shutdown();
    ppu_shutdown();
    scene_reset(); /* leave the module initialised for the other tests */
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
}

PTEST(line_160_and_above_is_rejected_and_out_untouched)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    for (unsigned x = 0; x < SCREEN_W; x++)
        out[x] = 0xABCD;
    PT_ASSERT_EQ_INT(ppu_render_line(SCREEN_H, out), PPU_ERR_BAD_LINE);
    PT_ASSERT_EQ_INT(ppu_render_line(UINT16_MAX, out), PPU_ERR_BAD_LINE);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, 0xABCD), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(SCREEN_H - 1, out), PPU_OK);
}

PTEST(null_output_is_rejected)
{
    scene_reset();
    PT_ASSERT_EQ_INT(ppu_render_line(0, NULL), PPU_ERR_NULL_OUTPUT);
}

/* ---- backdrop, forced blank, modes ---- */

PTEST(backdrop_is_bg_palette_colour_0)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, rgb(3, 7, 11));
    palette_set(256, rgb(31, 31, 0)); /* OBJ colour 0 is not the backdrop */
    io_write16(IO_DISPCNT, DISP_MODE_0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(3, 7, 11)), -1);
    PT_ASSERT_EQ_INT(ppu_render_line(SCREEN_H - 1, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(3, 7, 11)), -1);
}

PTEST(output_has_bit_15_clear)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, (uint16_t)(0x8000u | rgb(1, 2, 3))); /* GBATEK: bit 15 "Not used" */
    io_write16(IO_DISPCNT, DISP_MODE_0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(1, 2, 3)), -1);
}

PTEST(forced_blank_is_white)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(0, 1, rgb(0, 31, 0));
    solid_text_bg(0, 0, 1, 1, 0);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0 | DISP_FORCED_BLANK);
    PT_ASSERT_EQ_INT(ppu_render_line(40, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, WHITE), -1);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(41, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(0, 31, 0)), -1);
}

PTEST(forced_blank_is_white_even_in_an_unsupported_mode)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    io_write16(IO_DISPCNT, DISP_MODE_2 | DISP_FORCED_BLANK);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, WHITE), -1);
}

PTEST(modes_2_to_7_are_unsupported_and_draw_black)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    for (uint16_t mode = 2; mode <= 7; mode++) {
        for (unsigned x = 0; x < SCREEN_W; x++)
            out[x] = 0x1234;
        io_write16(IO_DISPCNT, mode);
        PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_ERR_UNSUPPORTED_MODE);
        PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BLACK), -1);
    }
    io_write16(IO_DISPCNT, DISP_MODE_1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
}

/* ---- the frame loop's registers ---- */

PTEST(render_writes_no_gba_region_so_vcount_and_dispstat_are_untouched)
{
    static uint8_t io_before[GBA_IO_SIZE], pal_before[GBA_PALETTE_RAM_SIZE];
    static uint8_t vram_before[GBA_VRAM_SIZE], oam_before[GBA_OAM_SIZE];
    uint16_t out[SCREEN_W];
    scene_reset();
    io_write16(IO_VCOUNT, 0x00A5);
    io_write16(IO_DISPSTAT, 0x5A3C); /* LYC 0x5A, every IRQ enable, flag bits set */
    solid_text_bg(0, 0, 1, 1, 0);
    io_write16(IO_DISPCNT, DISP_MODE_1 | DISP_BG0 | DISP_BG2 | DISP_OBJ);
    memcpy(io_before, gba_pointer(GBA_IO_BASE), sizeof io_before);
    memcpy(pal_before, gba_pointer(GBA_PALETTE_RAM_BASE), sizeof pal_before);
    memcpy(vram_before, gba_pointer(GBA_VRAM_BASE), sizeof vram_before);
    memcpy(oam_before, gba_pointer(GBA_OAM_BASE), sizeof oam_before);
    for (uint16_t line = 0; line < SCREEN_H; line++)
        PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
    ppu_latch_vblank();
    PT_ASSERT_EQ_U32(io_read16(IO_VCOUNT), 0x00A5);
    PT_ASSERT_EQ_U32(io_read16(IO_DISPSTAT), 0x5A3C);
    PT_ASSERT_MEM_EQ(gba_pointer(GBA_IO_BASE), io_before, sizeof io_before);
    PT_ASSERT_MEM_EQ(gba_pointer(GBA_PALETTE_RAM_BASE), pal_before, sizeof pal_before);
    PT_ASSERT_MEM_EQ(gba_pointer(GBA_VRAM_BASE), vram_before, sizeof vram_before);
    PT_ASSERT_MEM_EQ(gba_pointer(GBA_OAM_BASE), oam_before, sizeof oam_before);
}

/* ---- backgrounds against each other ---- */

PTEST(a_bg_is_drawn_only_when_enabled_in_dispcnt)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(2, 1, rgb(0, 0, 31));
    solid_text_bg(1, 0, 1, 1, 2);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0); /* BG1 configured, not enabled */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(0, 0, 31)), -1);
}

PTEST(lower_priority_value_bg_is_on_top)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    bg_palette_set(1, 1, rgb(31, 31, 0));
    bg_palette_set(2, 1, rgb(0, 31, 31));
    solid_text_bg(0, 1, 1, 1, 1);
    solid_text_bg(1, 0, 1, 1, 2);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(5, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(0, 31, 31)), -1);
}

PTEST(at_equal_priority_the_lower_numbered_bg_is_on_top)
{
    /* GBATEK "LCD I/O BG Control": "BG0 is having the highest, and BG3 the lowest priority". */
    uint16_t out[SCREEN_W];
    scene_reset();
    bg_palette_set(1, 1, rgb(1, 1, 1));
    bg_palette_set(2, 1, rgb(2, 2, 2));
    bg_palette_set(3, 1, rgb(3, 3, 3));
    bg_palette_set(4, 1, rgb(4, 4, 4));
    for (unsigned bg = 0; bg < 4; bg++)
        solid_text_bg(bg, 2, 1, 1, bg + 1);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG3 | DISP_BG2 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(2, 2, 2)), -1);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG3 | DISP_BG2 | DISP_BG1 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(1, 1, 1)), -1);
}

PTEST(a_transparent_dot_shows_the_layer_below)
{
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(1, 1, rgb(10, 0, 0));
    bg_palette_set(2, 1, rgb(0, 10, 0));
    /* BG0: tile 2, left half colour 1, right half transparent. */
    tile_pixels half;
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            half[r][c] = c < 4 ? 1 : 0;
    solid_text_bg(0, 0, 2, 0, 1);
    put_tile_4bpp(2 * 32u, half);
    solid_text_bg(1, 1, 1, 1, 2);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0 | DISP_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(3, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], (x % 8u) < 4u ? rgb(10, 0, 0) : rgb(0, 10, 0));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(3, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], (x % 8u) < 4u ? rgb(10, 0, 0) : BACKDROP);
}

PTEST(mode_1_does_not_display_bg3)
{
    /* GBATEK "LCD I/O Display Control": mode 1 layers are "012-". Character
     * block 0 and BG3's map are all 0x11 bytes, so BG3 would show something
     * whether drawn as a text BG (4bpp index 1) or as a rotation/scaling BG
     * (tile 0x11, 8bpp index 0x11). */
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(0, 1, rgb(9, 9, 9));
    bg_palette_set(1, 1, rgb(9, 9, 9)); /* colour 0x11 */
    for (uint32_t i = 0; i < CHAR_BLOCK_BYTES; i++)
        vram_write8(i, 0x11);
    for (uint32_t i = 0; i < SCREEN_BLOCK_BYTES; i++)
        vram_write8(28 * SCREEN_BLOCK_BYTES + i, 0x11);
    io_write16(IO_BG0CNT + 6, bgcnt_screen_block(28));
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG3);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, rgb(9, 9, 9)), -1);
    io_write16(IO_DISPCNT, DISP_MODE_1 | DISP_BG3);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, SCREEN_W, BACKDROP), -1);
}

PTEST(registers_are_read_again_for_every_line)
{
    /* What HBlank DMA to BGxHOFS relies on (src/scanline_effect.c): a value
     * written between two lines applies to the second. */
    uint16_t out[SCREEN_W];
    scene_reset();
    palette_set(0, BACKDROP);
    bg_palette_set(1, 1, rgb(0, 20, 0));
    tile_pixels one_dot;
    solid_tile(one_dot, 0);
    one_dot[0][0] = 1;
    solid_text_bg(0, 0, 1, 0, 1);
    put_tile_4bpp(32u, one_dot);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], rgb(0, 20, 0));
    PT_ASSERT_EQ_U32(out[1], BACKDROP);
    io_write16(IO_BG0HOFS, 7); /* dot 0 of each tile now at x = 1, 9, ... */
    PT_ASSERT_EQ_INT(ppu_render_line(8, out), PPU_OK);
    PT_ASSERT_EQ_U32(out[0], BACKDROP);
    PT_ASSERT_EQ_U32(out[1], rgb(0, 20, 0));
}

PTEST_MAIN()
