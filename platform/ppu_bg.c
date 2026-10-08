/*
 * ppu_bg.c: the LCD controller's backgrounds.
 *
 * Stands in for the GBA's background units, GBATEK "LCD I/O BG Control",
 * "LCD I/O BG Scrolling", "LCD I/O BG Rotation/Scaling", "LCD I/O Mosaic
 * Function", "LCD VRAM Character Data" and "LCD VRAM BG Screen Data Format
 * (BG Map)" (https://problemkaputt.de/gbatek.htm#lcdiobgcontrol and the
 * sections after it). A text BG is a 256 or 512 dot square or oblong map of
 * 32x32-entry screen blocks, scrolled by BGxHOFS/BGxVOFS and always wrapping;
 * each entry names a 4bpp or 8bpp tile, its flips and (4bpp) its palette. A
 * rotation/scaling BG (BG2 in mode 1) is a 128 to 1024 dot square map of
 * one-byte tile numbers, 8bpp tiles, sampled at (X + PA*x, Y + PC*x) from an
 * internal reference point (X, Y) that the hardware copies from BGxX/BGxY at
 * VBlank, steps by (PB, PD) after each line, and re-copies at once when the
 * game writes BGxX or BGxY mid-frame. Outside the map it wraps or is
 * transparent (BGxCNT bit 13). Mosaic repeats the first dot of each block of
 * the screen. Clean room from GBATEK.
 *
 * VRAM reads go through ppu_bg_vram_byte()/ppu_bg_vram_halfword(), which read
 * 0 beyond the 64 KiB BG area (ppu_internal.h). Arithmetic on the signed
 * reference point stays within int32_t (|X| < 2^27, |PA * 239| < 2^23) and
 * the 28-bit wrap is done in uint32_t; no right shift of a negative value.
 */
#include "ppu_internal.h"

/* GBATEK "LCD I/O BG Control", BGxCNT. */
#define BGCNT_CHAR_BASE_SHIFT 2u
#define BGCNT_CHAR_BASE_MASK 0x3u
#define BGCNT_MOSAIC 0x0040u
#define BGCNT_8BPP 0x0080u
#define BGCNT_SCREEN_BASE_SHIFT 8u
#define BGCNT_SCREEN_BASE_MASK 0x1Fu
#define BGCNT_OVERFLOW_WRAP 0x2000u
#define BGCNT_SIZE_SHIFT 14u
#define BGCNT_SIZE_MASK 0x3u
#define CHAR_BLOCK_BYTES 0x4000u  /* "in units of 16 KBytes" */
#define SCREEN_BLOCK_BYTES 0x0800u /* "in units of 2 KBytes" */

/* GBATEK "LCD I/O BG Scrolling": "Offset (0-511)", bits 9-15 not used. */
#define SCROLL_MASK 0x01FFu

/* GBATEK "LCD I/O BG Control", text sizes 0-3: 256x256, 512x256, 256x512,
 * 512x512, made of 256x256-dot screen blocks of 32x32 two-byte entries (SC0,
 * SC1 at +2K, SC2 at +4K, SC3 at +6K). */
#define TEXT_SIZE_WIDE 0x1u
#define TEXT_SIZE_TALL 0x2u
#define SCREEN_BLOCK_DOTS 256u
#define SCREEN_BLOCK_ENTRIES_ACROSS 32u
#define TEXT_ENTRY_BYTES 2u

/* GBATEK "LCD VRAM BG Screen Data Format (BG Map)", text entry. */
#define MAP_TILE_MASK 0x03FFu
#define MAP_HFLIP 0x0400u
#define MAP_VFLIP 0x0800u
#define MAP_PALETTE_SHIFT 12u

/* GBATEK "LCD I/O BG Control", rotation/scaling sizes 0-3: 128 << size dots. */
#define AFFINE_SMALLEST_DOTS 128u

/* GBATEK "LCD I/O BG Rotation/Scaling": 8 fraction bits; reference points
 * use bits 0-27, bit 27 the sign. */
#define FRACTION_BITS 8u
#define REFERENCE_SIGN 0x08000000u

/* GBATEK "LCD I/O Mosaic Function": bits 0-3 BG H-Size, 4-7 BG V-Size, minus 1. */
#define MOSAIC_SIZE_MASK 0xFu
#define MOSAIC_BG_V_SHIFT 4u

#define TILE_LAST_DOT (PPU_TILE_SIZE - 1u)

/* ---- the reference point ---- */

int32_t ppu_affine_sign_extend(uint32_t raw)
{
    return (int32_t)(raw & (REFERENCE_SIGN - 1u)) - (int32_t)(raw & REFERENCE_SIGN);
}

void ppu_affine_reference_latch(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs)
{
    ref->x = ppu_affine_sign_extend(regs->x);
    ref->y = ppu_affine_sign_extend(regs->y);
    ref->written_x = regs->x;
    ref->written_y = regs->y;
}

void ppu_affine_reference_pick_up_writes(struct ppu_affine_reference *ref,
                                         const struct ppu_affine_regs *regs)
{
    if (regs->x != ref->written_x) {
        ref->x = ppu_affine_sign_extend(regs->x);
        ref->written_x = regs->x;
    }
    if (regs->y != ref->written_y) {
        ref->y = ppu_affine_sign_extend(regs->y);
        ref->written_y = regs->y;
    }
}

/* value + step in 28-bit two's complement, as the internal register wraps
 * (the sum is formed in uint32_t, where wrapping is defined). */
static int32_t add_28_bit(int32_t value, int32_t step)
{
    return ppu_affine_sign_extend((uint32_t)value + (uint32_t)step);
}

void ppu_affine_reference_step(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs)
{
    ref->x = add_28_bit(ref->x, regs->pb);
    ref->y = add_28_bit(ref->y, regs->pd);
}

/* ---- shared ---- */

/* The mosaic block size along one axis for a BG: 1 unless BGxCNT enables it. */
static uint32_t mosaic_size(uint16_t bgcnt, uint16_t mosaic, unsigned shift)
{
    if (!(bgcnt & BGCNT_MOSAIC))
        return 1;
    return ((uint32_t)(mosaic >> shift) & MOSAIC_SIZE_MASK) + 1u;
}

static uint32_t char_base(uint16_t bgcnt)
{
    return ((uint32_t)(bgcnt >> BGCNT_CHAR_BASE_SHIFT) & BGCNT_CHAR_BASE_MASK) * CHAR_BLOCK_BYTES;
}

static uint32_t screen_base(uint16_t bgcnt)
{
    return ((uint32_t)(bgcnt >> BGCNT_SCREEN_BASE_SHIFT) & BGCNT_SCREEN_BASE_MASK) * SCREEN_BLOCK_BYTES;
}

static uint32_t size_field(uint16_t bgcnt)
{
    return (uint32_t)(bgcnt >> BGCNT_SIZE_SHIFT) & BGCNT_SIZE_MASK;
}

/* The colour of 8bpp dot (col, row) of tile, or PPU_TRANSPARENT for index 0. */
static uint16_t dot_8bpp(uint32_t chars, uint32_t tile, uint32_t col, uint32_t row)
{
    const uint8_t index =
        ppu_bg_vram_byte(chars + tile * PPU_TILE_BYTES_8BPP + row * PPU_TILE_SIZE + col);
    return index == 0 ? PPU_TRANSPARENT : ppu_palette_colour(index);
}

/* ---- text backgrounds ---- */

struct text_layout {
    uint32_t chars, screens;
    uint32_t width_mask, height_mask; /* map size in dots, minus 1 */
    uint32_t blocks_across;           /* screen blocks per row of the map: 1 or 2 */
    bool eight_bpp;
};

static struct text_layout text_layout(uint16_t bgcnt)
{
    const uint32_t size = size_field(bgcnt);
    const uint32_t width = (size & TEXT_SIZE_WIDE) ? 2 * SCREEN_BLOCK_DOTS : SCREEN_BLOCK_DOTS;
    const uint32_t height = (size & TEXT_SIZE_TALL) ? 2 * SCREEN_BLOCK_DOTS : SCREEN_BLOCK_DOTS;
    return (struct text_layout){
        .chars = char_base(bgcnt),
        .screens = screen_base(bgcnt),
        .width_mask = width - 1,
        .height_mask = height - 1,
        .blocks_across = width / SCREEN_BLOCK_DOTS,
        .eight_bpp = (bgcnt & BGCNT_8BPP) != 0,
    };
}

/* The colour at map dot (mx, my), or PPU_TRANSPARENT. */
static uint16_t text_dot(const struct text_layout *bg, uint32_t mx, uint32_t my)
{
    const uint32_t block = mx / SCREEN_BLOCK_DOTS + (my / SCREEN_BLOCK_DOTS) * bg->blocks_across;
    const uint32_t entry_index = ((my % SCREEN_BLOCK_DOTS) / PPU_TILE_SIZE) * SCREEN_BLOCK_ENTRIES_ACROSS
                                 + (mx % SCREEN_BLOCK_DOTS) / PPU_TILE_SIZE;
    const uint16_t entry = ppu_bg_vram_halfword(bg->screens + block * SCREEN_BLOCK_BYTES
                                                + entry_index * TEXT_ENTRY_BYTES);
    const uint32_t tile = entry & MAP_TILE_MASK;
    uint32_t col = mx % PPU_TILE_SIZE, row = my % PPU_TILE_SIZE;
    if (entry & MAP_HFLIP)
        col = TILE_LAST_DOT - col;
    if (entry & MAP_VFLIP)
        row = TILE_LAST_DOT - row;
    if (bg->eight_bpp) /* the entry's palette number is not used */
        return dot_8bpp(bg->chars, tile, col, row);
    const uint8_t pair = ppu_bg_vram_byte(bg->chars + tile * PPU_TILE_BYTES_4BPP
                                          + row * (PPU_TILE_SIZE / 2) + col / 2);
    const uint32_t index = (col & 1u) ? (uint32_t)pair >> PPU_NIBBLE_BITS : pair & PPU_NIBBLE_MASK;
    if (index == 0)
        return PPU_TRANSPARENT;
    return ppu_palette_colour((uint32_t)(entry >> MAP_PALETTE_SHIFT) * PPU_PALETTE_BANK_COLOURS + index);
}

void ppu_bg_render_text(const struct ppu_regs *regs, unsigned bg, uint16_t line,
                        uint16_t out[PPU_SCREEN_WIDTH])
{
    const uint16_t bgcnt = regs->bgcnt[bg];
    const struct text_layout layout = text_layout(bgcnt);
    const uint32_t mosaic_w = mosaic_size(bgcnt, regs->mosaic, 0);
    const uint32_t mosaic_h = mosaic_size(bgcnt, regs->mosaic, MOSAIC_BG_V_SHIFT);
    const uint32_t hofs = regs->hofs[bg] & SCROLL_MASK, vofs = regs->vofs[bg] & SCROLL_MASK;
    const uint32_t screen_y = line - line % mosaic_h;
    const uint32_t my = (screen_y + vofs) & layout.height_mask;
    for (uint32_t x = 0; x < PPU_SCREEN_WIDTH; x++) {
        const uint32_t screen_x = x - x % mosaic_w;
        out[x] = text_dot(&layout, (screen_x + hofs) & layout.width_mask, my);
    }
}

/* ---- rotation/scaling backgrounds ---- */

struct affine_layout {
    uint32_t chars, screens;
    uint32_t size; /* dots per side, a power of two */
    bool wrap;
};

/* The colour at texel coordinates (tx, ty) in 1/256 dots, or PPU_TRANSPARENT. */
static uint16_t affine_dot(const struct affine_layout *bg, int32_t tx, int32_t ty)
{
    uint32_t mx, my;
    if (bg->wrap) {
        /* Two's complement: (uint32_t)t >> 8 is floor(t / 256) modulo 2^24,
         * and size divides 2^24. */
        mx = ((uint32_t)tx >> FRACTION_BITS) & (bg->size - 1);
        my = ((uint32_t)ty >> FRACTION_BITS) & (bg->size - 1);
    } else {
        if (tx < 0 || ty < 0)
            return PPU_TRANSPARENT;
        mx = (uint32_t)tx >> FRACTION_BITS;
        my = (uint32_t)ty >> FRACTION_BITS;
        if (mx >= bg->size || my >= bg->size)
            return PPU_TRANSPARENT;
    }
    const uint32_t tiles_across = bg->size / PPU_TILE_SIZE;
    const uint8_t tile = ppu_bg_vram_byte(bg->screens + (my / PPU_TILE_SIZE) * tiles_across
                                          + mx / PPU_TILE_SIZE);
    return dot_8bpp(bg->chars, tile, mx % PPU_TILE_SIZE, my % PPU_TILE_SIZE);
}

void ppu_bg_render_affine(const struct ppu_regs *regs, unsigned bg,
                          const struct ppu_affine_reference *ref, uint16_t line,
                          uint16_t out[PPU_SCREEN_WIDTH])
{
    const uint16_t bgcnt = regs->bgcnt[bg];
    const struct ppu_affine_regs *params = &regs->affine[bg - PPU_AFFINE_BG_FIRST];
    const struct affine_layout layout = {
        .chars = char_base(bgcnt),
        .screens = screen_base(bgcnt),
        .size = AFFINE_SMALLEST_DOTS << size_field(bgcnt),
        .wrap = (bgcnt & BGCNT_OVERFLOW_WRAP) != 0,
    };
    const uint32_t mosaic_w = mosaic_size(bgcnt, regs->mosaic, 0);
    const uint32_t mosaic_h = mosaic_size(bgcnt, regs->mosaic, MOSAIC_BG_V_SHIFT);
    /* Vertical mosaic: the reference point of the block's first line, from
     * the steps taken since (interpretation; mGBA does the same). */
    const int32_t lines_back = (int32_t)(line % mosaic_h);
    const int32_t origin_x = ref->x - lines_back * params->pb;
    const int32_t origin_y = ref->y - lines_back * params->pd;
    for (uint32_t x = 0; x < PPU_SCREEN_WIDTH; x++) {
        const int32_t screen_x = (int32_t)(x - x % mosaic_w);
        out[x] = affine_dot(&layout, origin_x + params->pa * screen_x, origin_y + params->pc * screen_x);
    }
}
