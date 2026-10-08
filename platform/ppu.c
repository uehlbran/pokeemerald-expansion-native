/* ppu.c: stub, to be implemented (TDD red step). */
#include "ppu.h"
#include "ppu_internal.h"

int ppu_init(void) { return PPU_OK; }
void ppu_shutdown(void) {}
void ppu_latch_vblank(void) {}
int ppu_render_line(uint16_t line, uint16_t *out)
{
    (void)line;
    (void)out;
    return PPU_OK;
}
