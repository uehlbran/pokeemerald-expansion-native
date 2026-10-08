/* ppu_blend.c: stub, to be implemented (TDD red step). */
#include "ppu_internal.h"

uint16_t ppu_blend_alpha(uint16_t first, uint16_t second, uint16_t bldalpha)
{ (void)first; (void)second; (void)bldalpha; return 0; }
uint16_t ppu_blend_brighten(uint16_t colour, uint16_t bldy) { (void)colour; (void)bldy; return 0; }
uint16_t ppu_blend_darken(uint16_t colour, uint16_t bldy) { (void)colour; (void)bldy; return 0; }
uint16_t ppu_blend_pixel(const struct ppu_regs *regs, const struct ppu_pixel_targets *targets, bool effects_enabled)
{ (void)regs; (void)targets; (void)effects_enabled; return 0; }
