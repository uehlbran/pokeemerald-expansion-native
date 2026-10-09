/*
 * Tests of the ppu's windows (ppu_window.c): WIN0, WIN1, the OBJ window and
 * the outside region, GBATEK "LCD I/O Window Feature", through
 * ppu_render_line, plus the span rule directly.
 *
 * The scene: BG0 (priority 0) is solid FRONT, BG1 (priority 1) solid BEHIND;
 * the windows choose which of them each dot shows.
 *
 * Interpretation tested here (see ppu_window.c): GBATEK says "Garbage values
 * of X2>240 or X1>X2 are interpreted as X2=240"; the hardware (and mGBA)
 * wrap around instead when X1 > X2, so a span with first > end covers
 * [first, end of line) and [0, end). The same for Y.
 */
// deps: gba_memmap.c ppu.c ppu_bg.c ppu_obj.c ppu_blend.c
#include "ppu_internal.h"
#include "ppu_scene.h"
#include "ptest.h"

#define BACKDROP rgb(31, 0, 0)
#define FRONT rgb(0, 31, 0)
#define BEHIND rgb(0, 0, 31)

/* WININ/WINOUT bytes (GBATEK): bits 0-3 BG0-BG3, bit 4 OBJ, bit 5 effects. */
enum { SHOW_BG0 = 0x01, SHOW_BG1 = 0x02, SHOW_OBJ = 0x10, SHOW_EFFECTS = 0x20 };

static void solid_bg(unsigned bg, unsigned priority, unsigned bank, uint16_t colour)
{
    bg_palette_set(bank, 1, colour);
    const unsigned block = 31u - bg;
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++)
            put_map_entry(block * SCREEN_BLOCK_BYTES, col, row, map_entry(1, bank, 0));
    io_write16(IO_BG0CNT + 2u * bg, (uint16_t)(bgcnt_priority(priority) | bgcnt_screen_block(block)));
}

static void window_scene(uint16_t dispcnt)
{
    scene_reset();
    palette_set(0, BACKDROP);
    tile_pixels pixels;
    solid_tile(pixels, 1);
    put_tile_4bpp(32u, pixels);
    solid_bg(0, 0, 1, FRONT);
    solid_bg(1, 1, 2, BEHIND);
    io_write16(IO_DISPCNT, (uint16_t)(DISP_MODE_0 | DISP_BG0 | DISP_BG1 | dispcnt));
}

/* out[x] is FRONT for x in [from, to) and BEHIND elsewhere. */
static void check_front_span(uint16_t line, unsigned from, unsigned to)
{
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(line, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], x >= from && x < to ? FRONT : BEHIND);
}

PTEST(without_windows_every_enabled_layer_shows)
{
    window_scene(0);
    io_write16(IO_WINOUT, 0); /* ignored: no window is enabled */
    check_front_span(0, 0, SCREEN_W);
}

PTEST(win0_limits_a_bg_to_its_rectangle)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(40, 80));
    io_write16(IO_WIN0V, win_span(10, 20));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(9, 0, 0);
    check_front_span(10, 40, 80);
    check_front_span(19, 40, 80);
    check_front_span(20, 0, 0);
}

PTEST(win1_uses_its_own_registers)
{
    /* The reference ports read WIN0's registers for WIN1. WIN0's are set to
     * the whole screen here, so that bug would show FRONT everywhere. */
    window_scene(DISP_WIN1);
    io_write16(IO_WIN0H, win_span(0, 240));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WIN1H, win_span(100, 120));
    io_write16(IO_WIN1V, win_span(50, 60));
    io_write16(IO_WININ, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1));
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(49, 0, 0);
    check_front_span(50, 100, 120);
    check_front_span(59, 100, 120);
    check_front_span(60, 0, 0);
}

PTEST(win0_has_priority_over_win1_where_they_overlap)
{
    window_scene(DISP_WIN0 | DISP_WIN1);
    io_write16(IO_WIN0H, win_span(50, 100));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WIN1H, win_span(80, 150));
    io_write16(IO_WIN1V, win_span(0, 160));
    io_write16(IO_WININ, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1)); /* WIN0 hides BG0 */
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(0, 100, 150);
}

PTEST(the_outside_region_uses_winout)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(0, 10));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG0 | SHOW_BG1);
    check_front_span(0, 10, SCREEN_W);
    io_write16(IO_WINOUT, 0); /* nothing outside: the backdrop */
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 10, BEHIND), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 10, SCREEN_W, BACKDROP), -1);
}

PTEST(a_window_does_not_show_a_layer_dispcnt_disables)
{
    window_scene(DISP_WIN0);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG1 | DISP_WIN0);
    io_write16(IO_WIN0H, win_span(0, 240));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    check_front_span(0, 0, 0);
}

PTEST(a_horizontal_span_with_x1_greater_than_x2_wraps_around)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(200, 40));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], x < 40 || x >= 200 ? FRONT : BEHIND);
}

PTEST(a_vertical_span_with_y1_greater_than_y2_wraps_around)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(0, 240));
    io_write16(IO_WIN0V, win_span(150, 10));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(0, 0, SCREEN_W);
    check_front_span(9, 0, SCREEN_W);
    check_front_span(10, 0, 0);
    check_front_span(149, 0, 0);
    check_front_span(150, 0, SCREEN_W);
    check_front_span(159, 0, SCREEN_W);
}

PTEST(x2_beyond_240_and_y2_beyond_160_reach_the_screen_edge)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(100, 250));
    io_write16(IO_WIN0V, win_span(150, 200));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(149, 0, 0);
    check_front_span(150, 100, SCREEN_W);
    check_front_span(159, 100, SCREEN_W);
}

PTEST(an_empty_span_selects_nothing)
{
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0H, win_span(70, 70));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    check_front_span(0, 0, 0);
}

PTEST(window_registers_are_read_again_for_every_line)
{
    /* The battle transitions write WIN0H by HBlank DMA (src/battle_transition.c). */
    window_scene(DISP_WIN0);
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG1);
    io_write16(IO_WIN0H, win_span(10, 20));
    check_front_span(30, 10, 20);
    io_write16(IO_WIN0H, win_span(15, 90));
    check_front_span(31, 15, 90);
}

/* ---- the OBJ window ---- */

/* OBJ 0 in OBJ window mode: an 8x8 OBJ whose columns 0-5 are opaque. */
static void obj_window_obj(uint16_t x, uint16_t y)
{
    tile_pixels pixels;
    solid_tile(pixels, 0);
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 6; c++)
            pixels[r][c] = 3; /* columns 6 and 7 transparent */
    put_tile_4bpp(OBJ_VRAM + 1 * OBJ_TILE_BYTES, pixels);
    obj_palette_set(0, 3, rgb(31, 31, 31));
    obj_set(0, (uint16_t)(A0_OBJWIN | y), x, a2(1, 0, 0));
}

PTEST(the_obj_window_is_the_non_transparent_dots_of_obj_window_objs)
{
    window_scene(DISP_OBJ | DISP_OBJWIN);
    obj_window_obj(60, 0);
    io_write16(IO_WINOUT, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1));
    check_front_span(0, 60, 66); /* the OBJ itself is not drawn */
    check_front_span(7, 60, 66);
    check_front_span(8, 0, 0);
}

PTEST(the_obj_window_needs_dispcnt_obj_and_objwin)
{
    /* GBATEK: "Both DISPCNT Bits 12 and 15 must be set". */
    window_scene(DISP_OBJWIN);
    obj_window_obj(60, 0);
    io_write16(IO_WINOUT, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1));
    check_front_span(0, 0, 0);
    io_write16(IO_DISPCNT, DISP_MODE_0 | DISP_BG0 | DISP_BG1 | DISP_OBJ);
    check_front_span(0, 0, SCREEN_W); /* no window enabled at all */
}

PTEST(a_rotation_scaling_obj_in_obj_window_mode_marks_its_transformed_dots)
{
    /* PA = PD = 2.0: the 8x8 image covers the middle 4x4 of its 8x8 area. */
    window_scene(DISP_OBJ | DISP_OBJWIN);
    tile_pixels pixels;
    solid_tile(pixels, 3);
    put_tile_4bpp(OBJ_VRAM, pixels);
    obj_palette_set(0, 3, rgb(31, 31, 31));
    obj_affine_set(0, 512, 0, 0, 512);
    obj_set(0, (uint16_t)(A0_OBJWIN | A0_AFFINE), 60, a2(0, 0, 0));
    io_write16(IO_WINOUT, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1));
    check_front_span(0, 0, 0);
    check_front_span(2, 62, 66);
    check_front_span(5, 62, 66);
    check_front_span(6, 0, 0);
}

PTEST(unused_bits_6_and_7_of_each_winin_and_winout_byte_are_ignored)
{
    struct ppu_regs regs;
    memset(&regs, 0, sizeof regs);
    regs.dispcnt = DISP_WIN0 | DISP_WIN1 | DISP_OBJWIN;
    regs.winh[0] = win_span(0, 10);
    regs.winv[0] = win_span(0, 160);
    regs.winh[1] = win_span(20, 30);
    regs.winv[1] = win_span(0, 160);
    regs.winin = 0xC1C2u;  /* Window 1 0x01, Window 0 0x02, plus bits 6-7 */
    regs.winout = 0xE3E4u; /* OBJ window 0x23, outside 0x24, plus bits 6-7 */
    struct ppu_obj_line obj;
    ppu_obj_clear_line(&obj);
    obj.in_obj_window[100] = true;
    uint8_t control[SCREEN_W];
    ppu_window_render_line(&regs, 0, &obj, control);
    PT_ASSERT_EQ_U32(control[5], 0x02);
    PT_ASSERT_EQ_U32(control[25], 0x01);
    PT_ASSERT_EQ_U32(control[100], 0x23);
    PT_ASSERT_EQ_U32(control[150], 0x24);
}

PTEST(win0_and_win1_have_priority_over_the_obj_window)
{
    window_scene(DISP_OBJ | DISP_OBJWIN | DISP_WIN1);
    obj_window_obj(60, 0);
    io_write16(IO_WIN1H, win_span(62, 64));
    io_write16(IO_WIN1V, win_span(0, 160));
    io_write16(IO_WININ, (uint16_t)(SHOW_BG1 << 8));
    io_write16(IO_WINOUT, (uint16_t)((SHOW_BG0 | SHOW_BG1) << 8 | SHOW_BG1));
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], (x >= 60 && x < 62) || (x >= 64 && x < 66) ? FRONT : BEHIND);
}

PTEST(a_window_hides_objs_without_its_obj_bit)
{
    window_scene(DISP_OBJ | DISP_WIN0);
    tile_pixels pixels;
    solid_tile(pixels, 2);
    put_tile_4bpp(OBJ_VRAM, pixels);
    obj_palette_set(0, 2, rgb(31, 31, 0));
    obj_set(0, 0, 0, a2(0, 0, 0));
    obj_set(1, 0, 100, a2(0, 0, 0));
    io_write16(IO_WIN0H, win_span(0, 50));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, SHOW_BG0 | SHOW_BG1);
    io_write16(IO_WINOUT, SHOW_BG0 | SHOW_BG1 | SHOW_OBJ);
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, FRONT), -1);                 /* OBJ 0 hidden */
    PT_ASSERT_EQ_INT(first_mismatch(out, 100, 108, rgb(31, 31, 0)), -1);    /* OBJ 1 shown */
}

/* ---- the span rule ---- */

PTEST(span_contains_first_up_to_but_not_including_end)
{
    PT_ASSERT(!ppu_window_span_contains(win_span(10, 20), 9));
    PT_ASSERT(ppu_window_span_contains(win_span(10, 20), 10));
    PT_ASSERT(ppu_window_span_contains(win_span(10, 20), 19));
    PT_ASSERT(!ppu_window_span_contains(win_span(10, 20), 20));
    PT_ASSERT(!ppu_window_span_contains(win_span(5, 5), 5));
    PT_ASSERT(ppu_window_span_contains(win_span(0, 255), 239));
}

PTEST(span_with_first_after_end_wraps)
{
    PT_ASSERT(ppu_window_span_contains(win_span(200, 40), 239));
    PT_ASSERT(ppu_window_span_contains(win_span(200, 40), 0));
    PT_ASSERT(ppu_window_span_contains(win_span(200, 40), 39));
    PT_ASSERT(!ppu_window_span_contains(win_span(200, 40), 40));
    PT_ASSERT(!ppu_window_span_contains(win_span(200, 40), 199));
    PT_ASSERT(ppu_window_span_contains(win_span(200, 40), 200));
    /* X1 past the screen and X2 before it: the whole line, as with a flip-flop
     * set at X1 and cleared at X2 that is still set from the previous line. */
    PT_ASSERT(ppu_window_span_contains(win_span(250, 245), 0));
    PT_ASSERT(ppu_window_span_contains(win_span(250, 245), 239));
}

PTEST_MAIN()
