/*
 * ppu.h: the GBA LCD video controller's drawing, one scanline at a time.
 *
 * Stands in for the picture-processing half of the GBA's LCD controller,
 * GBATEK "LCD Video Controller"
 * (https://problemkaputt.de/gbatek.htm#gbalcdvideocontroller). The hardware
 * composes each of the 160 visible lines from up to four backgrounds, the 128
 * OBJs in OAM, the windows and the colour special effects, reading VRAM,
 * palette RAM, OAM and the LCD I/O registers as it goes. This module does the
 * same from the mapped GBA regions (gba_memmap.h), one line per call, so a
 * register the game changes between lines (HBlank DMA to BGxHOFS, an HBlank
 * handler writing WIN0H) takes effect on the next line as on hardware.
 *
 * Supported: BG modes 0 and 1, the only modes the game sets (src/ sets
 * DISPCNT_MODE_0 and DISPCNT_MODE_1 only). Modes 2 to 5 render nothing and
 * return PPU_ERR_UNSUPPORTED_MODE. Written from GBATEK, clean room; the
 * reference ports' renderers are derived from GPL code and were not read
 * (docs/C-RULES.md, licensing). Where GBATEK is silent or ambiguous the
 * choice made is written next to the code that makes it.
 *
 * WHO DOES WHAT. The frame loop owns time: it sets VCOUNT and the DISPSTAT
 * H-Blank, V-Blank and V-Counter flags, runs HBlank DMA and the HBlank and
 * VCount handlers between lines, and calls ppu_latch_vblank() at the start of
 * each VBlank. This module owns drawing only: ppu_render_line() reads the
 * registers and never writes any GBA region (in particular not VCOUNT or
 * DISPSTAT). Call order per frame: ppu_render_line(0) .. ppu_render_line(159),
 * each exactly once and in order, then ppu_latch_vblank(); the BG2/BG3
 * internal affine reference points advance once per rendered line, as on
 * hardware.
 *
 * Single-threaded: call from the thread that runs the game.
 */
#ifndef PPU_H
#define PPU_H

#include <stdint.h>

/* GBATEK "LCD Dimensions and Timings": 240 visible dots per line, 160 visible lines. */
#define PPU_SCREEN_WIDTH 240
#define PPU_SCREEN_HEIGHT 160

enum ppu_error {
    PPU_OK = 0,
    PPU_ERR_NOT_INITIALIZED = -1,  /* ppu_render_line() before ppu_init() or after ppu_shutdown() */
    PPU_ERR_BAD_LINE = -2,         /* line >= PPU_SCREEN_HEIGHT; nothing is written */
    PPU_ERR_NULL_OUTPUT = -3,      /* out is NULL; nothing is written */
    PPU_ERR_UNSUPPORTED_MODE = -4  /* DISPCNT BG mode 2 to 7; the line is written black */
};

/* Resets the module and latches the affine reference points from the
 * registers as a VBlank would. Returns PPU_OK. Calling it again resets again. */
int ppu_init(void);

/* Ends the module; ppu_render_line() fails until the next ppu_init(). Safe
 * to call at any time, including before init and more than once. Owns no
 * resource. */
void ppu_shutdown(void);

/* Called by the frame loop at the start of VBlank, before any line of the
 * next frame: copies the BG2X, BG2Y, BG3X and BG3Y reference points to the
 * internal registers the next frame's line 0 starts from, as the hardware
 * does (GBATEK "LCD I/O BG Rotation/Scaling", Internal Reference Point
 * Registers). A write to one of those registers after this call and before
 * line 0 (the game's VBlank handler flushing its buffered registers) is
 * picked up at line 0 as well. */
void ppu_latch_vblank(void);

/* Renders one line (0..159) into out[PPU_SCREEN_WIDTH] as BGR555 with bit 15
 * clear, reading the registers, VRAM, palette RAM and OAM as they are at the
 * moment of the call. Forced blank (DISPCNT bit 7) is not an error: the line
 * is white. Returns PPU_OK, or a ppu_error. Does not touch VCOUNT or
 * DISPSTAT: those belong to the frame loop. */
int ppu_render_line(uint16_t line, uint16_t *out);

#endif /* PPU_H */
