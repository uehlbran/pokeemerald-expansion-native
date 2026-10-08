/*
 * Tests of the ppu's colour special effects (ppu_blend.c), GBATEK "LCD I/O
 * Color Special Effects": the per-channel arithmetic directly, and target
 * selection, semi-transparent OBJs and the window's effect bit through
 * ppu_render_line.
 *
 * The arithmetic, per 5-bit channel, with EVA, EVB, EVY of 17-31 taken as 16:
 *   alpha     I = MIN(31, (I1 * EVA + I2 * EVB) / 16)
 *   brighten  I = I1 + (31 - I1) * EVY / 16
 *   darken    I = I1 - I1 * EVY / 16
 * with the divisions truncating (the reference ports did not clamp the
 * coefficients; the clamp tests below fail without it).
 */
// deps: gba_memmap.c ppu.c ppu_bg.c ppu_obj.c ppu_window.c
#include "ppu_internal.h"
#include "ppu_scene.h"
#include "ptest.h"

#define BACKDROP rgb(2, 4, 6)
#define COLOUR_A rgb(10, 20, 30)
#define COLOUR_B rgb(20, 10, 0)
#define COLOUR_C rgb(1, 1, 1)
#define MEAN_A_B rgb(15, 15, 15)

/* ================= the arithmetic ================= */

PTEST(alpha_with_eva_and_evb_8_is_the_mean)
{
    PT_ASSERT_EQ_U32(ppu_blend_alpha(COLOUR_A, COLOUR_B, bldalpha(8, 8)), MEAN_A_B);
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(30, 0, 31), rgb(0, 30, 31), bldalpha(8, 8)), rgb(15, 15, 31));
}

PTEST(alpha_truncates_each_channel)
{
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(1, 3, 5), BLACK, bldalpha(8, 8)), rgb(0, 1, 2));
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(31, 31, 31), BLACK, bldalpha(15, 0)), rgb(29, 29, 29));
}

PTEST(alpha_saturates_at_31)
{
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(20, 20, 20), rgb(20, 20, 20), bldalpha(16, 16)), WHITE);
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(31, 0, 16), rgb(31, 0, 15), bldalpha(16, 16)), rgb(31, 0, 31));
}

PTEST(alpha_coefficients_17_to_31_count_as_16)
{
    PT_ASSERT_EQ_U32(ppu_blend_alpha(COLOUR_A, COLOUR_B, bldalpha(31, 0)), COLOUR_A);
    PT_ASSERT_EQ_U32(ppu_blend_alpha(COLOUR_A, COLOUR_B, bldalpha(17, 0)), COLOUR_A);
    PT_ASSERT_EQ_U32(ppu_blend_alpha(COLOUR_A, COLOUR_B, bldalpha(0, 17)), COLOUR_B);
    /* 10 * 16/16 + 10 * 16/16 = 20; unclamped, 31/16 each would saturate. */
    PT_ASSERT_EQ_U32(ppu_blend_alpha(rgb(10, 10, 10), rgb(10, 10, 10), bldalpha(31, 31)),
                     rgb(20, 20, 20));
}

PTEST(alpha_reads_only_bits_0_4_and_8_12)
{
    PT_ASSERT_EQ_U32(ppu_blend_alpha(COLOUR_A, COLOUR_B, (uint16_t)(0xE0E0u | bldalpha(8, 8))),
                     MEAN_A_B);
}

PTEST(brighten_moves_towards_white)
{
    PT_ASSERT_EQ_U32(ppu_blend_brighten(COLOUR_A, 0), COLOUR_A);
    PT_ASSERT_EQ_U32(ppu_blend_brighten(rgb(0, 10, 31), 8), rgb(15, 20, 31)); /* 21*8/16 = 10 */
    PT_ASSERT_EQ_U32(ppu_blend_brighten(COLOUR_A, 16), WHITE);
    PT_ASSERT_EQ_U32(ppu_blend_brighten(BLACK, 16), WHITE);
}

PTEST(brighten_evy_17_to_31_counts_as_16)
{
    PT_ASSERT_EQ_U32(ppu_blend_brighten(rgb(10, 10, 10), 31), WHITE);
    PT_ASSERT_EQ_U32(ppu_blend_brighten(rgb(10, 0, 31), 17), WHITE);
    PT_ASSERT_EQ_U32(ppu_blend_brighten(rgb(10, 0, 31), (uint16_t)(0xFFE0u | 8u)), rgb(20, 15, 31));
}

PTEST(darken_moves_towards_black)
{
    PT_ASSERT_EQ_U32(ppu_blend_darken(COLOUR_A, 0), COLOUR_A);
    PT_ASSERT_EQ_U32(ppu_blend_darken(rgb(31, 10, 1), 8), rgb(16, 5, 1)); /* 31 - 15, 10 - 5, 1 - 0 */
    PT_ASSERT_EQ_U32(ppu_blend_darken(COLOUR_A, 16), BLACK);
    PT_ASSERT_EQ_U32(ppu_blend_darken(WHITE, 16), BLACK);
}

PTEST(darken_evy_17_to_31_counts_as_16)
{
    PT_ASSERT_EQ_U32(ppu_blend_darken(rgb(10, 10, 10), 31), BLACK);
    PT_ASSERT_EQ_U32(ppu_blend_darken(WHITE, 20), BLACK);
}

/* ================= target selection, directly ================= */

static struct ppu_regs effect_regs(uint16_t bldcnt)
{
    struct ppu_regs regs;
    memset(&regs, 0, sizeof regs);
    regs.bldcnt = bldcnt;
    regs.bldalpha = bldalpha(8, 8);
    regs.bldy = 16;
    return regs;
}

PTEST(the_backdrop_on_top_has_nothing_to_blend_with)
{
    const struct ppu_regs regs = effect_regs(BLD_1ST_BD | BLD_ALPHA | BLD_2ND_BD);
    const struct ppu_pixel_targets px = { { COLOUR_A, PPU_LAYER_BACKDROP },
                                          { 0, PPU_LAYER_NONE }, false };
    PT_ASSERT_EQ_U32(ppu_blend_pixel(&regs, &px, true), COLOUR_A);
}

PTEST(no_effect_when_the_window_disables_effects)
{
    const struct ppu_regs regs = effect_regs(BLD_1ST_BG0 | BLD_BRIGHTEN);
    const struct ppu_pixel_targets px = { { COLOUR_A, PPU_LAYER_BG0 },
                                          { BACKDROP, PPU_LAYER_BACKDROP }, false };
    PT_ASSERT_EQ_U32(ppu_blend_pixel(&regs, &px, true), WHITE);
    PT_ASSERT_EQ_U32(ppu_blend_pixel(&regs, &px, false), COLOUR_A);
    const struct ppu_pixel_targets semi = { { COLOUR_A, PPU_LAYER_OBJ },
                                            { COLOUR_B, PPU_LAYER_BG0 }, true };
    const struct ppu_regs alpha = effect_regs(BLD_2ND_BG0);
    PT_ASSERT_EQ_U32(ppu_blend_pixel(&alpha, &semi, true), MEAN_A_B);
    PT_ASSERT_EQ_U32(ppu_blend_pixel(&alpha, &semi, false), COLOUR_A);
}

/* ================= through the renderer ================= */

/* BG bg (priority = bg) shows colour; if half, only columns 0-3 of each
 * tile are opaque. Tiles: 1 solid, 2 left half, in character block 0. */
static void effect_bg(unsigned bg, uint16_t colour, bool half)
{
    bg_palette_set(bg + 1, 1, colour);
    const unsigned block = 31u - bg;
    for (unsigned row = 0; row < 32; row++)
        for (unsigned col = 0; col < 32; col++)
            put_map_entry(block * SCREEN_BLOCK_BYTES, col, row, map_entry(half ? 2 : 1, bg + 1, 0));
    io_write16(IO_BG0CNT + 2u * bg, (uint16_t)(bgcnt_priority(bg) | bgcnt_screen_block(block)));
}

static void effect_scene(uint16_t dispcnt)
{
    scene_reset();
    palette_set(0, BACKDROP);
    tile_pixels pixels;
    solid_tile(pixels, 1);
    put_tile_4bpp(1 * 32u, pixels);
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            pixels[r][c] = c < 4 ? 1 : 0;
    put_tile_4bpp(2 * 32u, pixels);
    io_write16(IO_BLDALPHA, bldalpha(8, 8));
    io_write16(IO_DISPCNT, (uint16_t)(DISP_MODE_0 | dispcnt));
}

/* x in [0, 4) of each tile shows left, the rest right. */
static void check_halves(uint16_t left, uint16_t right)
{
    uint16_t out[SCREEN_W];
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    for (unsigned x = 0; x < SCREEN_W; x++)
        PT_ASSERT_EQ_U32(out[x], (x % 8u) < 4u ? left : right);
}

PTEST(alpha_blends_a_1st_target_with_the_2nd_target_under_it)
{
    effect_scene(DISP_BG0 | DISP_BG1);
    effect_bg(0, COLOUR_A, false);
    effect_bg(1, COLOUR_B, false);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_ALPHA | BLD_2ND_BG1);
    check_halves(MEAN_A_B, MEAN_A_B);
    io_write16(IO_BLDALPHA, bldalpha(16, 0));
    check_halves(COLOUR_A, COLOUR_A);
}

PTEST(alpha_needs_the_1st_target_on_top)
{
    effect_scene(DISP_BG0 | DISP_BG1);
    effect_bg(0, COLOUR_A, false);
    effect_bg(1, COLOUR_B, false);
    io_write16(IO_BLDCNT, BLD_1ST_BG1 | BLD_ALPHA | BLD_2ND_BG0);
    check_halves(COLOUR_A, COLOUR_A);
}

PTEST(alpha_needs_the_2nd_target_directly_under_the_1st)
{
    /* GBATEK: not blended "if a non-transparent non-2nd-target pixel is moved
     * between the two targets". */
    effect_scene(DISP_BG0 | DISP_BG1 | DISP_BG2);
    effect_bg(0, COLOUR_A, false);
    effect_bg(1, COLOUR_C, true);
    effect_bg(2, COLOUR_B, false);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_ALPHA | BLD_2ND_BG2);
    check_halves(COLOUR_A, MEAN_A_B);
}

PTEST(the_backdrop_can_be_the_2nd_target)
{
    effect_scene(DISP_BG0);
    effect_bg(0, COLOUR_A, true);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_ALPHA | BLD_2ND_BD);
    check_halves(ppu_blend_alpha(COLOUR_A, BACKDROP, bldalpha(8, 8)), BACKDROP);
}

PTEST(brighten_and_darken_change_1st_targets_only)
{
    effect_scene(DISP_BG0);
    effect_bg(0, COLOUR_A, true);
    io_write16(IO_BLDY, 16);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_BRIGHTEN);
    check_halves(WHITE, BACKDROP);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_DARKEN);
    check_halves(BLACK, BACKDROP);
    io_write16(IO_BLDCNT, BLD_1ST_BD | BLD_BRIGHTEN);
    check_halves(COLOUR_A, WHITE);
    io_write16(IO_BLDY, 31); /* counts as 16 */
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_1ST_BD | BLD_DARKEN);
    check_halves(BLACK, BLACK);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_1ST_BD); /* effect "None" */
    check_halves(COLOUR_A, BACKDROP);
}

/* OBJ 0 (and 1) at x 0-7, line 0; colour O from OBJ palette bank 0 index 1. */
#define COLOUR_O rgb(30, 0, 10)
#define COLOUR_P rgb(0, 30, 0)
#define MEAN_O_B rgb(25, 5, 5)

static void effect_obj(unsigned index, uint16_t attr0_flags, unsigned priority, unsigned bank)
{
    tile_pixels pixels;
    solid_tile(pixels, 1);
    put_tile_4bpp(OBJ_VRAM, pixels);
    obj_palette_set(0, 1, COLOUR_O);
    obj_palette_set(1, 1, COLOUR_P);
    obj_set(index, attr0_flags, 0, a2(0, priority, bank));
}

PTEST(a_semi_transparent_obj_blends_with_the_2nd_target_under_it)
{
    /* GBATEK: always 1st target, always alpha, "regardless of BLDCNT". */
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_OBJ);
    effect_bg(0, COLOUR_B, false);
    effect_obj(0, A0_SEMI, 0, 0);
    io_write16(IO_BLDCNT, BLD_2ND_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, MEAN_O_B), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, SCREEN_W, COLOUR_B), -1);
    io_write16(IO_BLDCNT, 0); /* BG0 not a 2nd target: the OBJ as it is */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, COLOUR_O), -1);
    /* A normal OBJ with the same BLDCNT is not blended. */
    io_write16(IO_BLDCNT, BLD_2ND_BG0);
    effect_obj(0, 0, 0, 0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, COLOUR_O), -1);
}

PTEST(semi_transparency_over_a_2nd_target_takes_priority_over_brightness)
{
    /* GBATEK: "if a semi-transparent OBJ pixel does overlap a 2nd target
     * pixel, then semi-transparency becomes priority, and the brightness
     * effect will not take place". */
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_OBJ);
    effect_bg(0, COLOUR_B, false);
    io_write16(IO_BLDY, 16);
    io_write16(IO_BLDCNT, BLD_1ST_OBJ | BLD_BRIGHTEN | BLD_2ND_BG0);
    effect_obj(0, A0_SEMI, 0, 0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, MEAN_O_B), -1);
    effect_obj(0, 0, 0, 0); /* a normal OBJ is brightened */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, WHITE), -1);
}

PTEST(a_semi_transparent_obj_without_a_2nd_target_under_it_follows_bldcnt)
{
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_OBJ);
    effect_bg(0, COLOUR_B, false);
    io_write16(IO_BLDY, 16);
    effect_obj(0, A0_SEMI, 0, 0);
    io_write16(IO_BLDCNT, BLD_1ST_OBJ | BLD_BRIGHTEN);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, WHITE), -1);
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_BRIGHTEN);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, COLOUR_O), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, SCREEN_W, WHITE), -1);
}

PTEST(objs_do_not_blend_with_each_other)
{
    /* GBATEK "The OBJ Layer": only the top-most OBJ pixel takes part, so a
     * semi-transparent OBJ over another OBJ blends with the BG under both. */
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_OBJ);
    effect_bg(0, COLOUR_B, false);
    effect_obj(0, A0_SEMI, 0, 0);
    effect_obj(1, 0, 0, 1);
    io_write16(IO_BLDCNT, BLD_2ND_OBJ | BLD_2ND_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, MEAN_O_B), -1);
}

PTEST(a_bg_can_blend_with_an_obj_under_it)
{
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_OBJ);
    effect_bg(0, COLOUR_B, true); /* priority 0 */
    effect_obj(0, 0, 1, 0);       /* priority 1: under BG0 */
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_ALPHA | BLD_2ND_OBJ);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 4, MEAN_O_B), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 4, 8, COLOUR_O), -1);
    /* And a normal OBJ selected as 1st target blends with a BG under it. */
    effect_obj(0, 0, 0, 0);
    io_write16(IO_BLDCNT, BLD_1ST_OBJ | BLD_ALPHA | BLD_2ND_BG0);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 4, MEAN_O_B), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 4, 8, COLOUR_O), -1); /* the backdrop is no 2nd target */
}

PTEST(effects_are_off_where_the_window_disables_them)
{
    uint16_t out[SCREEN_W];
    effect_scene(DISP_BG0 | DISP_BG1 | DISP_OBJ | DISP_WIN0);
    effect_bg(0, COLOUR_A, false);
    effect_bg(1, COLOUR_B, false);
    effect_obj(0, A0_SEMI, 0, 0);
    io_write16(IO_WIN0H, win_span(0, 100));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, 0x13);  /* BG0, BG1, OBJ; no effects */
    io_write16(IO_WINOUT, 0x33); /* the same, with effects */
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_ALPHA | BLD_2ND_BG0 | BLD_2ND_BG1);
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 0, 8, COLOUR_O), -1); /* semi OBJ inside: plain */
    PT_ASSERT_EQ_INT(first_mismatch(out, 8, 100, COLOUR_A), -1);
    PT_ASSERT_EQ_INT(first_mismatch(out, 100, SCREEN_W, MEAN_A_B), -1);
    obj_set(0, A0_SEMI, 120, a2(0, 0, 0)); /* outside: blends with BG0 */
    PT_ASSERT_EQ_INT(ppu_render_line(0, out), PPU_OK);
    PT_ASSERT_EQ_INT(first_mismatch(out, 120, 128, ppu_blend_alpha(COLOUR_O, COLOUR_A, bldalpha(8, 8))), -1);
}

PTEST(a_layer_the_window_hides_is_not_a_target)
{
    effect_scene(DISP_BG0 | DISP_BG1 | DISP_BG2 | DISP_WIN0);
    effect_bg(0, COLOUR_C, false);
    effect_bg(1, COLOUR_A, false);
    effect_bg(2, COLOUR_B, false);
    io_write16(IO_WIN0H, win_span(0, 240));
    io_write16(IO_WIN0V, win_span(0, 160));
    io_write16(IO_WININ, 0x26); /* BG1, BG2, effects: BG0 hidden */
    io_write16(IO_BLDCNT, BLD_1ST_BG0 | BLD_1ST_BG1 | BLD_ALPHA | BLD_2ND_BG2);
    check_halves(MEAN_A_B, MEAN_A_B);
}

PTEST_MAIN()
