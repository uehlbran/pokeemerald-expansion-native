/*
 * ppu_scene.h: builds tiny synthetic scenes in the GBA regions for the ppu
 * tests (test_ppu*.c), and renders lines of them.
 *
 * Register offsets and bit values are written out here from GBATEK "LCD Video
 * Controller" (https://problemkaputt.de/gbatek.htm#gbalcdvideocontroller),
 * independently of the module's own constants, so those are under test too.
 *
 * Every test starts with scene_reset(), which zeroes the regions the PPU
 * reads (I/O, palette RAM, VRAM, OAM), hides every OBJ and re-initialises the
 * module, so a test that fails half-way leaves nothing another test can see.
 *
 * Values go into the regions with memcpy in host byte order, as the game's
 * own stores put them there.
 */
#ifndef PPU_SCENE_H
#define PPU_SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gba_memmap.h"
#include "ppu.h"

#define SCREEN_W PPU_SCREEN_WIDTH
#define SCREEN_H PPU_SCREEN_HEIGHT

/* GBATEK "GBA I/O Map", LCD I/O Registers. */
enum {
    IO_DISPCNT = 0x00,
    IO_DISPSTAT = 0x04,
    IO_VCOUNT = 0x06,
    IO_BG0CNT = 0x08, /* BGnCNT at 0x08 + 2n */
    IO_BG0HOFS = 0x10, /* BGnHOFS at 0x10 + 4n, BGnVOFS 2 after */
    IO_BG0VOFS = 0x12,
    IO_BG2PA = 0x20,
    IO_BG2PB = 0x22,
    IO_BG2PC = 0x24,
    IO_BG2PD = 0x26,
    IO_BG2X = 0x28,
    IO_BG2Y = 0x2C,
    IO_BG3PA = 0x30,
    IO_BG3PB = 0x32,
    IO_BG3PC = 0x34,
    IO_BG3PD = 0x36,
    IO_BG3X = 0x38,
    IO_BG3Y = 0x3C,
    IO_WIN0H = 0x40,
    IO_WIN1H = 0x42,
    IO_WIN0V = 0x44,
    IO_WIN1V = 0x46,
    IO_WININ = 0x48,
    IO_WINOUT = 0x4A,
    IO_MOSAIC = 0x4C,
    IO_BLDCNT = 0x50,
    IO_BLDALPHA = 0x52,
    IO_BLDY = 0x54
};

/* GBATEK "LCD I/O Display Control", DISPCNT. */
enum {
    DISP_MODE_0 = 0,
    DISP_MODE_1 = 1,
    DISP_MODE_2 = 2,
    DISP_HBLANK_FREE = 0x0020,
    DISP_OBJ_1D = 0x0040,
    DISP_FORCED_BLANK = 0x0080,
    DISP_BG0 = 0x0100,
    DISP_BG1 = 0x0200,
    DISP_BG2 = 0x0400,
    DISP_BG3 = 0x0800,
    DISP_OBJ = 0x1000,
    DISP_WIN0 = 0x2000,
    DISP_WIN1 = 0x4000,
    DISP_OBJWIN = 0x8000
};

/* GBATEK "LCD I/O BG Control", BGxCNT. */
enum {
    BGCNT_MOSAIC = 0x0040,
    BGCNT_8BPP = 0x0080,
    BGCNT_WRAP = 0x2000
};
static inline uint16_t bgcnt_priority(unsigned p) { return (uint16_t)p; }
static inline uint16_t bgcnt_char_block(unsigned n) { return (uint16_t)(n << 2); }
static inline uint16_t bgcnt_screen_block(unsigned n) { return (uint16_t)(n << 8); }
static inline uint16_t bgcnt_size(unsigned n) { return (uint16_t)(n << 14); }

#define CHAR_BLOCK_BYTES 0x4000u   /* BGxCNT bits 2-3, units of 16 KiB */
#define SCREEN_BLOCK_BYTES 0x0800u /* BGxCNT bits 8-12, units of 2 KiB */
#define OBJ_VRAM 0x10000u          /* GBATEK "LCD VRAM Overview", modes 0-2 */
#define OBJ_TILE_BYTES 32u         /* OBJ tile numbers count 32-byte units */

/* GBATEK "LCD VRAM BG Screen Data Format", text map entry. */
enum {
    MAP_HFLIP = 0x0400,
    MAP_VFLIP = 0x0800
};
static inline uint16_t map_entry(unsigned tile, unsigned palette, uint16_t flips)
{
    return (uint16_t)(tile | palette << 12 | flips);
}

/* GBATEK "LCD OBJ - OAM Attributes". */
enum {
    A0_AFFINE = 0x0100,
    A0_DOUBLE = 0x0200,  /* with A0_AFFINE */
    A0_DISABLE = 0x0200, /* without A0_AFFINE */
    A0_SEMI = 0x0400,
    A0_OBJWIN = 0x0800,
    A0_MOSAIC = 0x1000,
    A0_8BPP = 0x2000,
    A0_SQUARE = 0x0000,
    A0_WIDE = 0x4000,
    A0_TALL = 0x8000,
    A1_HFLIP = 0x1000,
    A1_VFLIP = 0x2000
};
static inline uint16_t a1_size(unsigned n) { return (uint16_t)(n << 14); }
static inline uint16_t a1_affine_group(unsigned n) { return (uint16_t)(n << 9); }
static inline uint16_t a2(unsigned tile, unsigned priority, unsigned palette)
{
    return (uint16_t)(tile | priority << 10 | palette << 12);
}

/* GBATEK "LCD I/O Window Feature", WINxH/WINxV: first coordinate high, end low. */
static inline uint16_t win_span(unsigned first, unsigned end) { return (uint16_t)(first << 8 | end); }

/* GBATEK "LCD I/O Color Special Effects", BLDCNT. */
enum {
    BLD_1ST_BG0 = 0x0001,
    BLD_1ST_BG1 = 0x0002,
    BLD_1ST_BG2 = 0x0004,
    BLD_1ST_OBJ = 0x0010,
    BLD_1ST_BD = 0x0020,
    BLD_ALPHA = 0x0040,
    BLD_BRIGHTEN = 0x0080,
    BLD_DARKEN = 0x00C0,
    BLD_2ND_BG0 = 0x0100,
    BLD_2ND_BG1 = 0x0200,
    BLD_2ND_BG2 = 0x0400,
    BLD_2ND_OBJ = 0x1000,
    BLD_2ND_BD = 0x2000
};
static inline uint16_t bldalpha(unsigned eva, unsigned evb) { return (uint16_t)(eva | evb << 8); }

/* GBATEK "LCD Color Palettes": bits 0-4 red, 5-9 green, 10-14 blue. */
static inline uint16_t rgb(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(r | g << 5 | b << 10);
}
#define WHITE 0x7FFFu
#define BLACK 0x0000u

/* ---- writing the regions ---- */

static inline void *gba_pointer(uint32_t address)
{
    return (void *)(uintptr_t)address; /* the regions are mapped at their GBA addresses */
}

static inline void io_write16(uint32_t offset, uint16_t value)
{
    memcpy(gba_pointer(GBA_IO_BASE + offset), &value, sizeof value);
}

static inline uint16_t io_read16(uint32_t offset)
{
    uint16_t value;
    memcpy(&value, gba_pointer(GBA_IO_BASE + offset), sizeof value);
    return value;
}

/* A 32-bit register as the game writes it, low halfword then high. */
static inline void io_write32(uint32_t offset, uint32_t value)
{
    io_write16(offset, (uint16_t)(value & 0xFFFFu));
    io_write16(offset + 2, (uint16_t)(value >> 16));
}

static inline void io_write_signed16(uint32_t offset, int16_t value)
{
    memcpy(gba_pointer(GBA_IO_BASE + offset), &value, sizeof value);
}

static inline void palette_set(unsigned index, uint16_t colour)
{
    memcpy(gba_pointer(GBA_PALETTE_RAM_BASE + index * 2u), &colour, sizeof colour);
}

static inline void bg_palette_set(unsigned bank, unsigned index, uint16_t colour)
{
    palette_set(bank * 16u + index, colour);
}

static inline void obj_palette_set(unsigned bank, unsigned index, uint16_t colour)
{
    palette_set(256u + bank * 16u + index, colour);
}

static inline void vram_write8(uint32_t offset, uint8_t value)
{
    memcpy(gba_pointer(GBA_VRAM_BASE + offset), &value, sizeof value);
}

static inline void vram_write16(uint32_t offset, uint16_t value)
{
    memcpy(gba_pointer(GBA_VRAM_BASE + offset), &value, sizeof value);
}

static inline void oam_write16(uint32_t offset, uint16_t value)
{
    memcpy(gba_pointer(GBA_OAM_BASE + offset), &value, sizeof value);
}

/* OBJ index's three attributes (GBATEK: OBJn at 07000000 + 8n). */
static inline void obj_set(unsigned index, uint16_t attr0, uint16_t attr1, uint16_t attr2)
{
    oam_write16(index * 8u, attr0);
    oam_write16(index * 8u + 2, attr1);
    oam_write16(index * 8u + 4, attr2);
}

/* OBJ rotation/scaling group (GBATEK: PA at 07000006 + 32n, PB +8, PC +16, PD +24). */
static inline void obj_affine_set(unsigned group, int16_t pa, int16_t pb, int16_t pc, int16_t pd)
{
    const int16_t values[4] = { pa, pb, pc, pd };
    for (unsigned i = 0; i < 4; i++)
        memcpy(gba_pointer(GBA_OAM_BASE + group * 32u + 6u + i * 8u), &values[i], sizeof values[i]);
}

/* ---- tiles ---- */

/* An 8x8 tile of colour indices, [row][column]. */
typedef uint8_t tile_pixels[8][8];

/* The asymmetric pattern the tests use: every dot differs from its mirror
 * image horizontally and vertically, all indices 1-15. */
static inline uint8_t pattern_index(unsigned col, unsigned row)
{
    return (uint8_t)(1u + (col + 8u * row) % 15u);
}

static inline void pattern_tile(tile_pixels pixels)
{
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            pixels[r][c] = pattern_index(c, r);
}

static inline void solid_tile(tile_pixels pixels, uint8_t index)
{
    memset(pixels, index, sizeof(tile_pixels));
}

/* GBATEK "LCD VRAM Character Data", 4bpp: 4 bytes per row, left dot in the low nibble. */
static inline void put_tile_4bpp(uint32_t vram_offset, const tile_pixels pixels)
{
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c += 2)
            vram_write8(vram_offset + r * 4u + c / 2u,
                        (uint8_t)((pixels[r][c] & 0x0Fu) | (pixels[r][c + 1] & 0x0Fu) << 4));
}

/* GBATEK "LCD VRAM Character Data", 8bpp: 8 bytes per row, one per dot. */
static inline void put_tile_8bpp(uint32_t vram_offset, const tile_pixels pixels)
{
    for (unsigned r = 0; r < 8; r++)
        for (unsigned c = 0; c < 8; c++)
            vram_write8(vram_offset + r * 8u + c, pixels[r][c]);
}

/* Text map entry at (col, row) of the 32x32 screen block at block_offset. */
static inline void put_map_entry(uint32_t block_offset, unsigned col, unsigned row, uint16_t entry)
{
    vram_write16(block_offset + (row * 32u + col) * 2u, entry);
}

/* ---- the scene ---- */

/* Zeroes the regions and hides all 128 OBJs: an all-zero OAM entry is a
 * displayed 8x8 OBJ at (0,0) using tile 0, so every OBJ gets the disable bit,
 * as the game's own hidden OBJs do. */
static inline void scene_reset(void)
{
    memset(gba_pointer(GBA_IO_BASE), 0, GBA_IO_SIZE);
    memset(gba_pointer(GBA_PALETTE_RAM_BASE), 0, GBA_PALETTE_RAM_SIZE);
    memset(gba_pointer(GBA_VRAM_BASE), 0, GBA_VRAM_SIZE);
    memset(gba_pointer(GBA_OAM_BASE), 0, GBA_OAM_SIZE);
    for (unsigned i = 0; i < 128; i++)
        obj_set(i, A0_DISABLE, 0, 0);
    (void)ppu_init(); /* tested in test_ppu.c */
    ppu_latch_vblank();
}

/* Renders lines 0..last of a frame in order (the affine reference points step
 * per line), leaving line last in out. Returns the first error, or PPU_OK. */
static inline int render_frame_to(uint16_t last, uint16_t out[SCREEN_W])
{
    for (uint16_t line = 0; line <= last; line++) {
        const int status = ppu_render_line(line, out);
        if (status != PPU_OK)
            return status;
    }
    return PPU_OK;
}

/* The first dot of out[from, to) that is not colour, or -1. */
static inline int first_mismatch(const uint16_t out[SCREEN_W], unsigned from, unsigned to, uint16_t colour)
{
    for (unsigned x = from; x < to; x++)
        if (out[x] != colour)
            return (int)x;
    return -1;
}

#endif /* PPU_SCENE_H */
