/*
 * gba_irq.h: the GBA's fourteen interrupt sources.
 *
 * Shared by the interrupt controller and every module that raises an
 * interrupt (timers now; video, DMA, input later), so none of them has to
 * include the controller. The values are the bit numbers of REG_IE and
 * REG_IF, GBATEK "GBA Interrupt Control", register 4000200h/4000202h:
 * bit 0 LCD V-Blank, 1 LCD H-Blank, 2 LCD V-Counter Match, 3-6 Timer 0-3
 * Overflow, 7 Serial Communication, 8-11 DMA 0-3, 12 Keypad, 13 Game Pak.
 */
#ifndef GBA_IRQ_H
#define GBA_IRQ_H

enum gba_irq {
    GBA_IRQ_VBLANK = 0,
    GBA_IRQ_HBLANK = 1,
    GBA_IRQ_VCOUNT = 2,
    GBA_IRQ_TIMER0 = 3,
    GBA_IRQ_TIMER1 = 4,
    GBA_IRQ_TIMER2 = 5,
    GBA_IRQ_TIMER3 = 6,
    GBA_IRQ_SERIAL = 7,
    GBA_IRQ_DMA0 = 8,
    GBA_IRQ_DMA1 = 9,
    GBA_IRQ_DMA2 = 10,
    GBA_IRQ_DMA3 = 11,
    GBA_IRQ_KEYPAD = 12,
    GBA_IRQ_GAMEPAK = 13
};

#endif /* GBA_IRQ_H */
