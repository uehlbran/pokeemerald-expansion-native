/*
 * ppu_obj.c: the LCD controller's OBJ (sprite) unit.
 *
 * Stands in for the GBA's OBJ engine, GBATEK "LCD OBJ - Overview", "LCD OBJ -
 * OAM Attributes", "LCD OBJ - OAM Rotation/Scaling Parameters" and "LCD OBJ -
 * VRAM Character (Tile) Mapping" (https://problemkaputt.de/gbatek.htm#lcdobjoverview
 * and the sections after it). For each line the hardware walks OAM from OBJ 0
 * to OBJ 127, renders every OBJ that covers the line into a line buffer
 * (regular OBJs flipped or not, rotation/scaling OBJs through one of 32 PA-PD
 * groups in a normal or double-size area; 4bpp or 8bpp tiles in 1D or 2D
 * mapping), and runs out of time when the OBJs cost more rendering cycles
 * than the line has. The line buffer keeps, per dot, the first-drawn pixel
 * of the best (lowest) priority: so a lower OAM index wins at equal priority.
 * OBJs in OBJ window mode mark the OBJ window instead of drawing. Clean room
 * from GBATEK; the reference ports had no cycle budget and no 2D mapping.
 *
 * Here: one pass over OAM per line builds the list of OBJs on the line and
 * how many of their columns the budget lets through; then each is drawn.
 *
 * THE CYCLE BUDGET (GBATEK "Maximum Number of Sprites per Line"): 1210 cycles
 * per line, 954 with DISPCNT's H-Blank Interval Free; a regular OBJ costs its
 * width, a rotation/scaling OBJ 10 + 2 x the width of its area (doubled for
 * double size). Interpretations: (1) An OBJ costs only on lines it covers
 * vertically (GBATEK asks "Does the above also apply for VERTICALLY OFFSCREEN
 * ... sprites?" and does not answer; mGBA charges only OBJs on the line), but
 * does cost when it is horizontally off screen on that line (GBATEK's
 * caution; mGBA does not charge those, we follow GBATEK). Disabled OBJs and
 * the prohibited shape and mode cost nothing. (2) The OBJ that the budget
 * runs out in is drawn for as many columns of its area, from the left, as
 * the cycles left pay for (after the 10-cycle setup for rotation/scaling),
 * and the OBJs after it are not drawn: the hardware stops mid-OBJ (mGBA
 * draws that OBJ whole and leaves partial drawing as a TODO). Not modelled:
 * the OAM attribute fetch time GBATEK's formula leaves out.
 *
 * OTHER INTERPRETATIONS. Y: an OBJ whose area would cross line 255 starts at
 * Y - 256 (GBATEK's caution about 128-dot OBJs below Y=128 is this rule). X:
 * nine bits, 256-511 are -256..-1. OBJ mosaic: blocks in screen space as for
 * BGs; a block that starts left of or above the OBJ uses its first column or
 * row (mGBA does the same). Tile data beyond the 32 KiB OBJ area wraps
 * (ppu_obj_vram_byte). Shape 3 and mode 3, "Prohibited", draw nothing.
 * Modes 3-5, where tiles 0-511 are not displayed, are not supported (ppu.h).
 * Not reproduced: the "garbage" GBATEK warns of when a lower-numbered OBJ has
 * a lower priority than a later one that overlaps it; here the line buffer
 * simply keeps the best-priority pixel.
 */
#include "ppu_internal.h"

/* GBATEK "LCD OBJ - OAM Attributes": 128 entries of 8 bytes, attributes 0-2. */
#define OAM_OBJ_COUNT 128u
#define OAM_OBJ_BYTES 8u
#define ATTR1_OFFSET 2u
#define ATTR2_OFFSET 4u

#define ATTR0_Y_MASK 0x00FFu
#define ATTR0_AFFINE 0x0100u
#define ATTR0_DOUBLE_SIZE 0x0200u /* with ATTR0_AFFINE */
#define ATTR0_DISABLE 0x0200u     /* without ATTR0_AFFINE */
#define ATTR0_MODE_SHIFT 10u
#define ATTR0_MODE_MASK 0x3u
#define ATTR0_MOSAIC 0x1000u
#define ATTR0_8BPP 0x2000u
#define ATTR0_SHAPE_SHIFT 14u
enum obj_mode { MODE_NORMAL = 0, MODE_SEMI_TRANSPARENT = 1, MODE_OBJ_WINDOW = 2, MODE_PROHIBITED = 3 };
#define SHAPE_PROHIBITED 3u

#define ATTR1_X_MASK 0x01FFu
#define ATTR1_GROUP_SHIFT 9u
#define ATTR1_GROUP_MASK 0x1Fu
#define ATTR1_HFLIP 0x1000u
#define ATTR1_VFLIP 0x2000u
#define ATTR1_SIZE_SHIFT 14u

#define ATTR2_TILE_MASK 0x03FFu
#define ATTR2_PRIORITY_SHIFT 10u
#define ATTR2_PALETTE_SHIFT 12u

/* Y is 8 bits, wrapping at 256; X is 9 bits, 256-511 being negative. */
#define Y_RANGE 256
#define X_RANGE 512
#define X_FIRST_NEGATIVE 256

/* GBATEK "LCD OBJ - OAM Rotation/Scaling Parameters": group n's PA at OAM
 * 6 + 32n, PB, PC, PD 8 bytes apart. */
#define GROUP_BYTES 32u
#define GROUP_PA_OFFSET 6u
#define GROUP_PB_OFFSET 8u  /* from PA */
#define GROUP_PC_OFFSET 16u /* from PA */
#define GROUP_PD_OFFSET 24u /* from PA */

/* GBATEK "LCD OBJ - VRAM Character (Tile) Mapping": tile numbers count 32
 * bytes; in 2D mapping a row of the 32x32 tile matrix is 32 tiles. 8bpp tiles
 * take two numbers, and in 2D mapping bit 0 of the number "is completely
 * ignored". */
#define TILE_NUMBER_BYTES 32u
#define MATRIX_TILES_ACROSS 32u /* "a matrix of 32x32 tiles" */
#define MATRIX_ROW_BYTES (MATRIX_TILES_ACROSS * TILE_NUMBER_BYTES)
#define TILE_8BPP_EVEN_MASK (~1u)

/* GBATEK "LCD OBJ - Overview", OBJ rendering cycles per line. */
#define CYCLES_PER_LINE 1210            /* 304 * 4 - 6 */
#define CYCLES_PER_LINE_HBLANK_FREE 954 /* 240 * 4 - 6 */
#define AFFINE_SETUP_CYCLES 10
#define AFFINE_CYCLES_PER_DOT 2

#define HALF_IN_FIXED_POINT 7u /* n << 7 is n / 2 in 8.8 fixed point */

/* GBATEK "LCD OBJ - OAM Attributes": [shape][size] = { width, height }. */
#define OBJ_SHAPES 3 /* shape 3 is prohibited */
#define OBJ_SIZES 4
#define DIMENSION_WIDTH 0
#define DIMENSION_COUNT 2 /* width, height */
#define DIMENSION_HEIGHT 1
static const uint8_t obj_dimensions[OBJ_SHAPES][OBJ_SIZES][DIMENSION_COUNT] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } }, /* square */
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } }, /* horizontal */
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } }, /* vertical */
};

/* One OBJ as the line needs it. */
struct obj {
    uint16_t attr0, attr1, attr2;
    int32_t x, y;                    /* top-left of its area on screen */
    int32_t width, height;           /* the image, in dots */
    int32_t area_width, area_height; /* doubled for double size */
    bool affine;
    int32_t pa, pb, pc, pd;          /* its rotation/scaling group, if affine */
};

/* An OBJ on the current line and how many columns of its area to draw. */
struct line_obj {
    struct obj obj;
    int32_t columns;
};

void ppu_obj_clear_line(struct ppu_obj_line *out)
{
    for (unsigned x = 0; x < PPU_SCREEN_WIDTH; x++) {
        out->colour[x] = PPU_TRANSPARENT;
        out->priority[x] = 0;
        out->semi_transparent[x] = false;
        out->in_obj_window[x] = false;
    }
}

static enum obj_mode mode_of(uint16_t attr0)
{
    return (enum obj_mode)((attr0 >> ATTR0_MODE_SHIFT) & ATTR0_MODE_MASK);
}

/* The PA-PD group attribute 1 selects, into obj. */
static void read_affine_group(struct obj *obj)
{
    const uint32_t group = ((uint32_t)obj->attr1 >> ATTR1_GROUP_SHIFT) & ATTR1_GROUP_MASK;
    const uint32_t base = group * GROUP_BYTES + GROUP_PA_OFFSET;
    obj->pa = ppu_signed_halfword(ppu_oam_halfword(base));
    obj->pb = ppu_signed_halfword(ppu_oam_halfword(base + GROUP_PB_OFFSET));
    obj->pc = ppu_signed_halfword(ppu_oam_halfword(base + GROUP_PC_OFFSET));
    obj->pd = ppu_signed_halfword(ppu_oam_halfword(base + GROUP_PD_OFFSET));
}

/* Reads OBJ index from OAM; false if it is not displayed at all. */
static bool read_obj(uint32_t index, struct obj *obj)
{
    const uint32_t base = index * OAM_OBJ_BYTES;
    obj->attr0 = ppu_oam_halfword(base);
    obj->attr1 = ppu_oam_halfword(base + ATTR1_OFFSET);
    obj->attr2 = ppu_oam_halfword(base + ATTR2_OFFSET);
    obj->affine = (obj->attr0 & ATTR0_AFFINE) != 0;
    const unsigned shape = (unsigned)(obj->attr0 >> ATTR0_SHAPE_SHIFT);
    if ((!obj->affine && (obj->attr0 & ATTR0_DISABLE)) || shape == SHAPE_PROHIBITED
        || mode_of(obj->attr0) == MODE_PROHIBITED)
        return false;
    const unsigned size = (unsigned)(obj->attr1 >> ATTR1_SIZE_SHIFT);
    obj->width = obj_dimensions[shape][size][DIMENSION_WIDTH];
    obj->height = obj_dimensions[shape][size][DIMENSION_HEIGHT];
    const int32_t scale = obj->affine && (obj->attr0 & ATTR0_DOUBLE_SIZE) ? 2 : 1;
    obj->area_width = obj->width * scale;
    obj->area_height = obj->height * scale;
    obj->y = obj->attr0 & ATTR0_Y_MASK;
    if (obj->y + obj->area_height > Y_RANGE)
        obj->y -= Y_RANGE;
    obj->x = obj->attr1 & ATTR1_X_MASK;
    if (obj->x >= X_FIRST_NEGATIVE)
        obj->x -= X_RANGE;
    if (obj->affine)
        read_affine_group(obj);
    else
        obj->pa = obj->pb = obj->pc = obj->pd = 0; /* not used; never left indeterminate */
    return true;
}

static int32_t cycle_cost(const struct obj *obj)
{
    return obj->affine ? AFFINE_SETUP_CYCLES + AFFINE_CYCLES_PER_DOT * obj->area_width
                       : obj->area_width;
}

/* Columns of obj's area that `cycles` pay for, at most all of them. */
static int32_t affordable_columns(const struct obj *obj, int32_t cycles)
{
    const int32_t columns = obj->affine ? (cycles - AFFINE_SETUP_CYCLES) / AFFINE_CYCLES_PER_DOT : cycles;
    if (columns < 0)
        return 0;
    return columns < obj->area_width ? columns : obj->area_width;
}

/* The OBJs on line, in OAM order, cut by the cycle budget. Returns how many. */
static unsigned collect_line_objs(uint16_t dispcnt, uint16_t line, struct line_obj list[OAM_OBJ_COUNT])
{
    int32_t cycles = (dispcnt & PPU_DISPCNT_HBLANK_FREE) ? CYCLES_PER_LINE_HBLANK_FREE : CYCLES_PER_LINE;
    unsigned count = 0;
    for (uint32_t index = 0; index < OAM_OBJ_COUNT && cycles > 0; index++) {
        struct obj obj;
        if (!read_obj(index, &obj) || line < obj.y || line >= obj.y + obj.area_height)
            continue;
        list[count].obj = obj;
        list[count].columns = affordable_columns(&obj, cycles);
        count++;
        cycles -= cycle_cost(&obj);
    }
    return count;
}

/* The image dot (tx, ty) that column `col`, row `row` of obj's area shows;
 * false where the area shows nothing (outside a transformed image). */
static bool image_dot(const struct obj *obj, int32_t col, int32_t row, int32_t *tx, int32_t *ty)
{
    if (!obj->affine) {
        *tx = (obj->attr1 & ATTR1_HFLIP) ? obj->width - 1 - col : col;
        *ty = (obj->attr1 & ATTR1_VFLIP) ? obj->height - 1 - row : row;
        return true;
    }
    /* GBATEK: the rotation centre is the middle of the OBJ (and of its area). */
    const int32_t dx = col - obj->area_width / 2, dy = row - obj->area_height / 2;
    const int32_t fx = obj->pa * dx + obj->pb * dy + (obj->width << HALF_IN_FIXED_POINT);
    const int32_t fy = obj->pc * dx + obj->pd * dy + (obj->height << HALF_IN_FIXED_POINT);
    if (fx < 0 || fy < 0)
        return false;
    *tx = fx >> PPU_FRACTION_BITS;
    *ty = fy >> PPU_FRACTION_BITS;
    return *tx < obj->width && *ty < obj->height;
}

/* How the game numbers OBJ tiles in VRAM: DISPCNT bit 6 (GBATEK "LCD OBJ - VRAM Character (Tile) Mapping"). */
enum obj_mapping { OBJ_MAPPING_2D, OBJ_MAPPING_1D };

/* The colour index (0 = transparent) of image dot (tx, ty). */
static uint32_t colour_index(const struct obj *obj, enum obj_mapping mapping, uint32_t tx, uint32_t ty)
{
    const uint32_t tile = obj->attr2 & ATTR2_TILE_MASK;
    const uint32_t tile_col = tx / PPU_TILE_SIZE, tile_row = ty / PPU_TILE_SIZE;
    const uint32_t dot_x = tx % PPU_TILE_SIZE, dot_y = ty % PPU_TILE_SIZE;
    const uint32_t tiles_across = (uint32_t)obj->width / PPU_TILE_SIZE;
    if (obj->attr0 & ATTR0_8BPP) {
        const uint32_t first = mapping == OBJ_MAPPING_1D
                                   ? tile * TILE_NUMBER_BYTES
                                         + (tile_row * tiles_across + tile_col) * PPU_TILE_BYTES_8BPP
                                   : (tile & TILE_8BPP_EVEN_MASK) * TILE_NUMBER_BYTES
                                         + tile_row * MATRIX_ROW_BYTES + tile_col * PPU_TILE_BYTES_8BPP;
        return ppu_obj_vram_byte(first + dot_y * PPU_TILE_ROW_BYTES_8BPP + dot_x);
    }
    const uint32_t row_stride = mapping == OBJ_MAPPING_1D ? tiles_across * TILE_NUMBER_BYTES : MATRIX_ROW_BYTES;
    const uint32_t first = tile * TILE_NUMBER_BYTES + tile_row * row_stride + tile_col * PPU_TILE_BYTES_4BPP;
    const uint8_t pair =
        ppu_obj_vram_byte(first + dot_y * PPU_TILE_ROW_BYTES_4BPP + dot_x / PPU_DOTS_PER_BYTE_4BPP);
    return ppu_4bpp_index(pair, dot_x);
}

static uint16_t obj_colour(const struct obj *obj, uint32_t index)
{
    if (obj->attr0 & ATTR0_8BPP)
        return ppu_palette_colour(PPU_OBJ_PALETTE_FIRST + index);
    const uint32_t bank = (uint32_t)(obj->attr2 >> ATTR2_PALETTE_SHIFT);
    return ppu_palette_colour_4bpp(PPU_OBJ_PALETTE_FIRST, bank, index);
}

/* The mosaic block size along one axis for obj: 1 unless it has mosaic on. */
static int32_t mosaic_size(const struct obj *obj, uint16_t mosaic, unsigned shift)
{
    return (obj->attr0 & ATTR0_MOSAIC) ? (int32_t)ppu_mosaic_block(mosaic, shift) : 1;
}

/* Writes one dot of obj at screen x, if it wins against what is there. */
static void put_dot(const struct obj *obj, uint32_t index, int32_t x, struct ppu_obj_line *out)
{
    const enum obj_mode mode = mode_of(obj->attr0);
    if (mode == MODE_OBJ_WINDOW) {
        out->in_obj_window[x] = true;
        return;
    }
    const uint8_t priority = (uint8_t)((obj->attr2 >> ATTR2_PRIORITY_SHIFT) & PPU_PRIORITY_MASK);
    if (out->colour[x] != PPU_TRANSPARENT && out->priority[x] <= priority)
        return; /* drawn earlier (lower OAM index) at the same or a better priority */
    out->colour[x] = obj_colour(obj, index);
    out->priority[x] = priority;
    out->semi_transparent[x] = mode == MODE_SEMI_TRANSPARENT;
}

static void draw_obj(const struct ppu_regs *regs, const struct line_obj *entry, uint16_t line,
                     struct ppu_obj_line *out)
{
    const struct obj *obj = &entry->obj;
    const enum obj_mapping mapping = (regs->dispcnt & PPU_DISPCNT_OBJ_1D) ? OBJ_MAPPING_1D : OBJ_MAPPING_2D;
    const int32_t mosaic_w = mosaic_size(obj, regs->mosaic, PPU_MOSAIC_OBJ_H_SHIFT);
    const int32_t mosaic_h = mosaic_size(obj, regs->mosaic, PPU_MOSAIC_OBJ_V_SHIFT);
    int32_t sample_line = line - line % mosaic_h;
    if (sample_line < obj->y)
        sample_line = obj->y;
    const int32_t row = sample_line - obj->y;
    for (int32_t col = 0; col < entry->columns; col++) {
        const int32_t x = obj->x + col;
        if (x < 0)
            continue;
        if (x >= PPU_SCREEN_WIDTH)
            break;
        int32_t sample_x = x - x % mosaic_w;
        if (sample_x < obj->x)
            sample_x = obj->x;
        int32_t tx, ty;
        if (!image_dot(obj, sample_x - obj->x, row, &tx, &ty))
            continue;
        const uint32_t index = colour_index(obj, mapping, (uint32_t)tx, (uint32_t)ty);
        if (index != 0)
            put_dot(obj, index, x, out);
    }
}

void ppu_obj_render_line(const struct ppu_regs *regs, uint16_t line, struct ppu_obj_line *out)
{
    struct line_obj list[OAM_OBJ_COUNT];
    ppu_obj_clear_line(out);
    const unsigned count = collect_line_objs(regs->dispcnt, line, list);
    for (unsigned i = 0; i < count; i++)
        draw_obj(regs, &list[i], line, out);
}
