/*
 * ppu_window.c: the LCD controller's windows.
 *
 * Stands in for the GBA's window logic, GBATEK "LCD I/O Window Feature"
 * (https://problemkaputt.de/gbatek.htm#lcdiowindowfeature). Each dot of a
 * line belongs to one region: Window 0 if inside it, else Window 1, else the
 * OBJ window (dots where an OBJ in OBJ window mode has a non-zero colour),
 * else "outside". The region's WININ/WINOUT byte says which of BG0-BG3 and
 * OBJ show there and whether colour effects apply. With none of the three
 * windows enabled in DISPCNT, everything shows everywhere. Clean room from
 * GBATEK; each window reads its own registers (the reference ports read
 * WIN0's for WIN1).
 *
 * Interpretation, X1 > X2 (and Y1 > Y2). GBATEK: "Garbage values of X2>240 or
 * X1>X2 are interpreted as X2=240." The hardware instead behaves like a
 * flip-flop set when the dot counter reaches X1 and cleared when it reaches
 * X2, so a span with X1 > X2 wraps around: it covers X1 to the right edge and
 * the left edge to X2. mGBA (the project's behavioural oracle) does that.
 * The game computes spans at run time (src/battle_transition.c by HBlank DMA,
 * src/battle_anim_ghost.c), so the edge cases are reachable. X2 > 240 still
 * reaches the right edge, as GBATEK says. The rule is applied
 * per line without memory: a flip-flop's state carried across lines that
 * WINxV changes mid-frame could differ, which nothing in the game depends on.
 */
#include "ppu_internal.h"

/* GBATEK "LCD I/O Display Control", DISPCNT bits 13-15. */
#define DISPCNT_WIN0_ON 0x2000u
#define DISPCNT_WIN1_ON 0x4000u
#define DISPCNT_OBJWIN_ON 0x8000u

/* GBATEK "LCD I/O Window Feature": WINxH/WINxV bits 8-15 the first
 * coordinate, bits 0-7 one past the last. WININ bits 0-5 Window 0, 8-13
 * Window 1; WINOUT bits 0-5 outside, 8-13 the OBJ window. */
#define SPAN_FIRST_SHIFT 8u
#define SPAN_BYTE_MASK 0xFFu
#define CONTROL_HIGH_SHIFT 8u

bool ppu_window_span_contains(uint16_t span, uint16_t coordinate)
{
    const uint16_t first = (uint16_t)((span >> SPAN_FIRST_SHIFT) & SPAN_BYTE_MASK);
    const uint16_t end = (uint16_t)(span & SPAN_BYTE_MASK);
    if (first <= end)
        return coordinate >= first && coordinate < end;
    return coordinate >= first || coordinate < end; /* wraps around */
}

static uint8_t control_byte(uint16_t reg, bool high)
{
    return (uint8_t)((high ? reg >> CONTROL_HIGH_SHIFT : reg) & PPU_WINDOW_ALL_ENABLED);
}

/* Marks the dots of window (0 or 1) on line with its WININ byte, if the
 * window covers the line. */
static void apply_rectangle(const struct ppu_regs *regs, unsigned window, uint16_t line,
                            uint8_t control[PPU_SCREEN_WIDTH])
{
    if (!ppu_window_span_contains(regs->winv[window], line))
        return;
    const uint8_t inside = control_byte(regs->winin, window == 1);
    for (uint16_t x = 0; x < PPU_SCREEN_WIDTH; x++)
        if (ppu_window_span_contains(regs->winh[window], x))
            control[x] = inside;
}

void ppu_window_render_line(const struct ppu_regs *regs, uint16_t line,
                            const struct ppu_obj_line *obj, uint8_t control[PPU_SCREEN_WIDTH])
{
    const uint16_t enabled = regs->dispcnt & (DISPCNT_WIN0_ON | DISPCNT_WIN1_ON | DISPCNT_OBJWIN_ON);
    if (enabled == 0) {
        memset(control, PPU_WINDOW_ALL_ENABLED, PPU_SCREEN_WIDTH);
        return;
    }
    /* From the lowest priority region up, so a higher one overwrites. */
    memset(control, control_byte(regs->winout, false), PPU_SCREEN_WIDTH);
    if (enabled & DISPCNT_OBJWIN_ON) {
        const uint8_t obj_window = control_byte(regs->winout, true);
        for (unsigned x = 0; x < PPU_SCREEN_WIDTH; x++)
            if (obj->in_obj_window[x])
                control[x] = obj_window;
    }
    if (enabled & DISPCNT_WIN1_ON)
        apply_rectangle(regs, 1, line, control);
    if (enabled & DISPCNT_WIN0_ON)
        apply_rectangle(regs, 0, line, control);
}
