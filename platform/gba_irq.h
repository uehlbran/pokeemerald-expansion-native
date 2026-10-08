/*
 * gba_irq.h: the GBA's fourteen interrupt sources.
 *
 * GBATEK "GBA Interrupt Control"
 * (https://problemkaputt.de/gbatek.htm#gbainterruptcontrol): each source has
 * one bit in IE (4000200h) and IF (4000202h), and each enumerator's value is
 * that bit number: 0 LCD V-Blank, 1 LCD H-Blank, 2 LCD V-Counter Match,
 * 3-6 Timer 0-3 Overflow, 7 Serial Communication, 8-11 DMA 0-3, 12 Keypad,
 * 13 Game Pak (external IRQ source). Bits 14-15 are not used.
 */
#ifndef GBA_IRQ_H
#define GBA_IRQ_H

enum gba_irq {
    GBA_IRQ_VBLANK,
    GBA_IRQ_HBLANK,
    GBA_IRQ_VCOUNT,
    GBA_IRQ_TIMER0,
    GBA_IRQ_TIMER1,
    GBA_IRQ_TIMER2,
    GBA_IRQ_TIMER3,
    GBA_IRQ_SERIAL,
    GBA_IRQ_DMA0,
    GBA_IRQ_DMA1,
    GBA_IRQ_DMA2,
    GBA_IRQ_DMA3,
    GBA_IRQ_KEYPAD,
    GBA_IRQ_GAMEPAK,
    GBA_IRQ_COUNT /* one past the last source; not a source */
};

#endif /* GBA_IRQ_H */
