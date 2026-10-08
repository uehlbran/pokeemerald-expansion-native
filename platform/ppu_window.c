/* ppu_window.c: stub, to be implemented (TDD red step). */
#include "ppu_internal.h"

bool ppu_window_span_contains(uint16_t span, uint16_t coordinate) { (void)span; (void)coordinate; return false; }
void ppu_window_render_line(const struct ppu_regs *regs, uint16_t line, const struct ppu_obj_line *obj,
                            uint8_t control[PPU_SCREEN_WIDTH])
{ (void)regs; (void)line; (void)obj; (void)control; }
