/*
 * ppu_internal.h: what the files of the ppu module share. Not a public
 * interface: only the ppu*.c files and their tests include it; everything
 * else uses ppu.h.
 *
 * The module is split along the hardware's own units, GBATEK "LCD Video
 * Controller" (https://problemkaputt.de/gbatek.htm#gbalcdvideocontroller):
 *   ppu.c         the register snapshot, the line, layer priority and the
 *                 affine reference point state
 *   ppu_bg.c      text and rotation/scaling backgrounds, the reference points
 *   ppu_obj.c     OBJs: the per-line OBJ list and the OBJ cycle budget
 *   ppu_window.c  WIN0, WIN1, the OBJ window and the outside region
 *   ppu_blend.c   colour special effects
 *
 * Memory. Every read of VRAM, palette RAM, OAM and the I/O registers goes
 * through the accessors below, which bound the offset by the region's size
 * (gba_memmap.h) and read with memcpy, in the host's byte order, which is how
 * the game's own stores put the values there. A GBA address is also the
 * native address of the same byte (gba_memmap.h); the integer-to-pointer
 * conversion relies on that and is flagged for the 64-bit milestone there.
 */
#ifndef PPU_INTERNAL_H
#define PPU_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "gba_memmap.h"
#include "ppu.h"

#define PPU_BG_COUNT 4         /* GBATEK "LCD I/O BG Control": BG0-BG3 */
#define PPU_AFFINE_BG_FIRST 2  /* GBATEK "LCD I/O BG Rotation/Scaling": BG2 and BG3 */
#define PPU_AFFINE_BG_COUNT 2
#define PPU_WINDOW_COUNT 2     /* GBATEK "LCD I/O Window Feature": WIN0 and WIN1 */

/* GBATEK "LCD VRAM Overview", BG Mode 0,1,2: 06000000-0600FFFF is BG map and
 * tile data, 06010000-06017FFF OBJ tiles. */
#define PPU_BG_VRAM_SIZE UINT32_C(0x10000)
#define PPU_OBJ_VRAM_BASE UINT32_C(0x10000)
#define PPU_OBJ_VRAM_SIZE UINT32_C(0x8000)

/* A 16-bit register or OAM parameter holds a two's complement value: bit 15
 * is the sign. */
#define PPU_HALFWORD_SIGN 0x8000u

/* GBATEK "LCD Color Palettes": 256 BG colours then 256 OBJ colours, two
 * bytes each, bits 0-14 used ("15 Not used"). Colour 0 of BG palette 0 is the
 * backdrop. */
#define PPU_PALETTE_COLOURS 512u
#define PPU_COLOUR_BYTES 2u
#define PPU_OBJ_PALETTE_FIRST 256u
#define PPU_PALETTE_BANK_COLOURS 16u /* 16 palettes of 16 colours in 4bpp mode */
#define PPU_BACKDROP_COLOUR_INDEX 0u
#define PPU_COLOUR_MASK 0x7FFFu

/* Not hardware: marks "no pixel" in a line buffer. Colours are masked to 15
 * bits, so bit 15 never occurs in one. */
#define PPU_TRANSPARENT 0x8000u

/* GBATEK "LCD I/O Display Control", DISPCNT. */
#define PPU_DISPCNT_MODE_MASK 0x0007u     /* bits 0-2: BG mode */
#define PPU_DISPCNT_HBLANK_FREE 0x0020u   /* H-Blank Interval Free */
#define PPU_DISPCNT_OBJ_1D 0x0040u        /* OBJ Character VRAM Mapping: 1 = one dimensional */
#define PPU_DISPCNT_FORCED_BLANK 0x0080u
#define PPU_DISPCNT_BG0_ON 0x0100u        /* BGn display at bit 8 + n */
#define PPU_DISPCNT_OBJ_ON 0x1000u
#define PPU_DISPCNT_WIN0_ON 0x2000u
#define PPU_DISPCNT_WIN1_ON 0x4000u
#define PPU_DISPCNT_OBJWIN_ON 0x8000u

/* GBATEK "LCD I/O BG Rotation/Scaling" and "LCD OBJ - OAM Rotation/Scaling
 * Parameters": reference points and PA-PD have 8 fraction bits. */
#define PPU_FRACTION_BITS 8u

/* GBATEK "LCD I/O Mosaic Function", MOSAIC: four 4-bit sizes, each minus 1. */
#define PPU_MOSAIC_BG_H_SHIFT 0u
#define PPU_MOSAIC_BG_V_SHIFT 4u
#define PPU_MOSAIC_OBJ_H_SHIFT 8u
#define PPU_MOSAIC_OBJ_V_SHIFT 12u
#define PPU_MOSAIC_SIZE_MASK 0xFu

/* GBATEK "LCD VRAM Character Data": 8x8 dots per tile; 4bpp tiles are 32
 * bytes, 4 per row, two dots per byte with the left dot in the low nibble;
 * 8bpp tiles are 64 bytes, 8 per row, one dot per byte. */
#define PPU_TILE_SIZE 8u
#define PPU_TILE_BYTES_4BPP 32u
#define PPU_TILE_BYTES_8BPP 64u
#define PPU_TILE_ROW_BYTES_4BPP 4u
#define PPU_TILE_ROW_BYTES_8BPP 8u
#define PPU_DOTS_PER_BYTE_4BPP 2u
#define PPU_NIBBLE_BITS 4u
#define PPU_NIBBLE_MASK 0x0Fu

/* GBATEK "LCD I/O BG Control" and "LCD OBJ - OAM Attributes": the priority
 * field of BGxCNT (bits 0-1) and of OBJ attribute 2 (bits 10-11). */
#define PPU_PRIORITY_MASK 0x3u

/* The layers in the bit order BLDCNT (bits 0-5 and 8-13) and WININ/WINOUT
 * (bits 0-4 per region) use, GBATEK "LCD I/O Color Special Effects" and
 * "LCD I/O Window Feature". */
enum ppu_layer {
    PPU_LAYER_BG0 = 0,
    PPU_LAYER_BG1 = 1,
    PPU_LAYER_BG2 = 2,
    PPU_LAYER_BG3 = 3,
    PPU_LAYER_OBJ = 4,
    PPU_LAYER_BACKDROP = 5,
    PPU_LAYER_NONE = 6 /* not hardware: no layer (below the backdrop) */
};

/* GBATEK "LCD I/O Window Feature": in each WININ/WINOUT byte, bits 0-4
 * enable BG0-BG3 and OBJ, bit 5 enables colour special effects. */
#define PPU_WINDOW_EFFECTS_ENABLE 0x20u
#define PPU_WINDOW_ALL_ENABLED 0x3Fu

/* ---- the registers, read once per line ---- */

/* One rotation/scaling BG's parameters, GBATEK "LCD I/O BG Rotation/Scaling".
 * pa..pd are the signed 8.8 values; x and y are BGxX and BGxY as the 32 bits
 * the game wrote (only bits 0-27 count). */
struct ppu_affine_regs {
    int32_t pa, pb, pc, pd;
    uint32_t x, y;
};

/* The LCD I/O registers the drawing reads (GBATEK "GBA I/O Map", LCD I/O
 * Registers, 04000000-04000055), as they were at the start of the line. */
struct ppu_regs {
    uint16_t dispcnt;
    uint16_t bgcnt[PPU_BG_COUNT];
    uint16_t hofs[PPU_BG_COUNT];
    uint16_t vofs[PPU_BG_COUNT];
    struct ppu_affine_regs affine[PPU_AFFINE_BG_COUNT]; /* [0] is BG2, [1] is BG3 */
    uint16_t winh[PPU_WINDOW_COUNT];
    uint16_t winv[PPU_WINDOW_COUNT];
    uint16_t winin;
    uint16_t winout;
    uint16_t mosaic;
    uint16_t bldcnt;
    uint16_t bldalpha;
    uint16_t bldy;
};

/* ---- bounds-checked reads of the GBA regions ---- */

/* The native object at a GBA address: the regions are mapped at their GBA
 * addresses (gba_memmap.h), so the conversion is the identity. The only
 * integer-to-pointer conversion in the module; flagged for the 64-bit
 * milestone with gba_memmap.h's (GBA addresses fit in 32 bits). Callers
 * bound the address to a region first. */
static inline const void *ppu_gba_pointer(uint32_t address)
{
    return (const void *)(uintptr_t)address;
}

static inline uint8_t ppu_read_byte_at(uint32_t address)
{
    uint8_t value;
    memcpy(&value, ppu_gba_pointer(address), sizeof value);
    return value;
}

static inline uint16_t ppu_read_halfword_at(uint32_t address)
{
    uint16_t value;
    memcpy(&value, ppu_gba_pointer(address), sizeof value);
    return value;
}

/* A byte of BG map or tile data. GBATEK does not say what a BG fetch beyond
 * the 64 KiB BG area returns (a large tile number with character base 3 can
 * reach it); it reads as 0 here, a transparent dot or tile 0, which is what
 * mGBA does. */
static inline uint8_t ppu_bg_vram_byte(uint32_t offset)
{
    if (offset >= PPU_BG_VRAM_SIZE)
        return 0;
    return ppu_read_byte_at(GBA_VRAM_BASE + offset);
}

/* A halfword of BG map data, 0 beyond the BG area as above. */
static inline uint16_t ppu_bg_vram_halfword(uint32_t offset)
{
    if (offset > PPU_BG_VRAM_SIZE - sizeof(uint16_t))
        return 0;
    return ppu_read_halfword_at(GBA_VRAM_BASE + offset);
}

/* A byte of OBJ tile data at an offset into the OBJ area. The OBJ unit
 * addresses its 32 KiB area only, so an offset past its end (a large tile
 * number, or a big 8bpp OBJ's later tiles) wraps to its start; mGBA does the
 * same, and the CPU's view of VRAM mirrors 06010000-06017FFF at
 * 06018000-0601FFFF (GBATEK "GBA Memory Map"). */
static inline uint8_t ppu_obj_vram_byte(uint32_t offset)
{
    const uint32_t wrapped = offset & (PPU_OBJ_VRAM_SIZE - 1);
    return ppu_read_byte_at(GBA_VRAM_BASE + PPU_OBJ_VRAM_BASE + wrapped);
}

/* Palette colour index (0-255 BG, 256-511 OBJ) as BGR555, bit 15 cleared.
 * Out of range reads 0 (no caller passes one). */
static inline uint16_t ppu_palette_colour(uint32_t index)
{
    if (index >= PPU_PALETTE_COLOURS)
        return 0;
    const uint16_t colour = ppu_read_halfword_at(GBA_PALETTE_RAM_BASE + index * PPU_COLOUR_BYTES);
    return (uint16_t)(colour & PPU_COLOUR_MASK);
}

/* A halfword of OAM; out of range reads 0 (no caller passes one). */
static inline uint16_t ppu_oam_halfword(uint32_t offset)
{
    if (offset > GBA_OAM_SIZE - sizeof(uint16_t))
        return 0;
    return ppu_read_halfword_at(GBA_OAM_BASE + offset);
}

/* A halfword of the I/O registers; out of range reads 0 (no caller passes one). */
static inline uint16_t ppu_io_halfword(uint32_t offset)
{
    if (offset > GBA_IO_SIZE - sizeof(uint16_t))
        return 0;
    return ppu_read_halfword_at(GBA_IO_BASE + offset);
}

/* The mosaic block size (1-16) along one axis for a BG or OBJ: the MOSAIC
 * field at shift (the register holds size minus 1) if the BG's BGxCNT bit or
 * the OBJ's attribute 0 bit enables mosaic, else 1. */
static inline uint32_t ppu_mosaic_size(bool enabled, uint16_t mosaic, unsigned shift)
{
    return enabled ? (((uint32_t)mosaic >> shift) & PPU_MOSAIC_SIZE_MASK) + 1u : 1u;
}

/* Dot dot_x (even = left) of a 4bpp tile row's byte: the left dot is the low
 * nibble. */
static inline uint32_t ppu_4bpp_index(uint8_t pair, uint32_t dot_x)
{
    return (dot_x & 1u) ? (uint32_t)pair >> PPU_NIBBLE_BITS : pair & PPU_NIBBLE_MASK;
}

/* Palette colour of 4bpp dot index in a palette bank (0-15), counted from
 * palette colour first (0 for BGs, PPU_OBJ_PALETTE_FIRST for OBJs). */
static inline uint16_t ppu_palette_colour_4bpp(uint32_t first, uint32_t bank, uint32_t index)
{
    return ppu_palette_colour(first + bank * PPU_PALETTE_BANK_COLOURS + index);
}

/* A 16-bit two's complement value as a signed number, without relying on the
 * implementation-defined conversion to int16_t. */
static inline int32_t ppu_signed_halfword(uint16_t value)
{
    return (int32_t)(value & (PPU_HALFWORD_SIGN - 1u)) - (int32_t)(value & PPU_HALFWORD_SIGN);
}

/* ---- ppu_bg.c ---- */

/* An affine BG's internal reference point (GBATEK "LCD I/O BG
 * Rotation/Scaling", Internal Reference Point Registers): x and y are signed
 * 19.8 fixed point, 28 bits. written_x and written_y are BGxX and BGxY as
 * last copied in, so a later write can be noticed. */
struct ppu_affine_reference {
    int32_t x, y;
    uint32_t written_x, written_y;
};

/* The 28-bit signed value in bits 0-27 of a BGxX/BGxY register; bits 28-31
 * are ignored. */
int32_t ppu_affine_sign_extend(uint32_t raw);

/* Copies BGxX/BGxY to the internal reference point: the VBlank copy. */
void ppu_affine_reference_latch(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs);

/* Re-latches if BGxX or BGxY differs from what was last copied: GBATEK, "Writing
 * to a reference point register by software outside of the Vblank period does
 * immediately copy the new value to the corresponding internal register", so
 * the new value is the origin of the line about to be drawn. Register writes
 * are plain stores here, so a write is noticed by its value changing; a write
 * of the value the register already holds is not noticed. */
void ppu_affine_reference_pick_up_writes(struct ppu_affine_reference *ref,
                                         const struct ppu_affine_regs *regs);

/* Advances the internal reference point by PB (dmx) and PD (dmy), as the
 * hardware does after each line, wrapping at 28 bits. */
void ppu_affine_reference_step(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs);

/* Draws text BG bg (0-3) for line into out: a colour per dot, or PPU_TRANSPARENT. */
void ppu_bg_render_text(const struct ppu_regs *regs, unsigned bg, uint16_t line,
                        uint16_t out[PPU_SCREEN_WIDTH]);

/* Draws rotation/scaling BG bg (2 or 3) for line from its internal reference
 * point into out, as ppu_bg_render_text. */
void ppu_bg_render_affine(const struct ppu_regs *regs, unsigned bg,
                          const struct ppu_affine_reference *ref, uint16_t line,
                          uint16_t out[PPU_SCREEN_WIDTH]);

/* ---- ppu_obj.c ---- */

/* The OBJ layer of one line: for each dot, the top-most OBJ pixel (GBATEK
 * "LCD I/O Color Special Effects", The OBJ Layer) and the OBJ window mask. */
struct ppu_obj_line {
    uint16_t colour[PPU_SCREEN_WIDTH];  /* PPU_TRANSPARENT where no OBJ is drawn */
    uint8_t priority[PPU_SCREEN_WIDTH]; /* the drawn OBJ's priority relative to BG, 0-3 */
    bool semi_transparent[PPU_SCREEN_WIDTH];
    bool in_obj_window[PPU_SCREEN_WIDTH]; /* an OBJ window OBJ has a non-zero dot here */
};

/* Empties the line: no OBJ pixel, no OBJ window. */
void ppu_obj_clear_line(struct ppu_obj_line *out);

/* Draws the OBJs that cover line into out, within the line's OBJ rendering
 * cycle budget. Does not look at DISPCNT's OBJ enable bit: the caller does. */
void ppu_obj_render_line(const struct ppu_regs *regs, uint16_t line, struct ppu_obj_line *out);

/* ---- ppu_window.c ---- */

/* True if coordinate lies in the span a WINxH or WINxV register gives (high
 * byte the first coordinate, low byte one past the last). */
bool ppu_window_span_contains(uint16_t span, uint16_t coordinate);

/* For each dot of line, the WININ/WINOUT control byte of the region the dot is
 * in (bits 0-4 layers, bit 5 effects), or PPU_WINDOW_ALL_ENABLED when no
 * window is enabled in DISPCNT. */
void ppu_window_render_line(const struct ppu_regs *regs, uint16_t line,
                            const struct ppu_obj_line *obj, uint8_t control[PPU_SCREEN_WIDTH]);

/* ---- ppu_blend.c ---- */

struct ppu_target {
    uint16_t colour;
    enum ppu_layer layer;
};

/* The top-most visible pixel of a dot, the one under it (the backdrop if
 * nothing else; PPU_LAYER_NONE under the backdrop), and whether the top one
 * is a semi-transparent OBJ's. */
struct ppu_pixel_targets {
    struct ppu_target top;
    struct ppu_target below;
    bool top_is_semi_transparent;
};

/* GBATEK "LCD I/O Color Special Effects", the per-channel arithmetic, with
 * EVA, EVB and EVY values 17-31 taken as 16. */
uint16_t ppu_blend_alpha(uint16_t first, uint16_t second, uint16_t bldalpha);
uint16_t ppu_blend_brighten(uint16_t colour, uint16_t bldy);
uint16_t ppu_blend_darken(uint16_t colour, uint16_t bldy);

/* The colour a dot shows: the top pixel with BLDCNT's effect and the
 * semi-transparent OBJ rule applied, if the dot's window enables effects. */
uint16_t ppu_blend_pixel(const struct ppu_regs *regs, const struct ppu_pixel_targets *targets,
                         bool effects_enabled);

#endif /* PPU_INTERNAL_H */
