/* ppu_bg.c: stub, to be implemented (TDD red step). */
#include "ppu_internal.h"

int32_t ppu_affine_sign_extend(uint32_t raw) { (void)raw; return 0; }
void ppu_affine_reference_latch(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs)
{ (void)ref; (void)regs; }
void ppu_affine_reference_pick_up_writes(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs)
{ (void)ref; (void)regs; }
void ppu_affine_reference_step(struct ppu_affine_reference *ref, const struct ppu_affine_regs *regs)
{ (void)ref; (void)regs; }
void ppu_bg_render_text(const struct ppu_regs *regs, unsigned bg, uint16_t line, uint16_t out[PPU_SCREEN_WIDTH])
{ (void)regs; (void)bg; (void)line; (void)out; }
void ppu_bg_render_affine(const struct ppu_regs *regs, unsigned bg, const struct ppu_affine_reference *ref,
                          uint16_t line, uint16_t out[PPU_SCREEN_WIDTH])
{ (void)regs; (void)bg; (void)ref; (void)line; (void)out; }
