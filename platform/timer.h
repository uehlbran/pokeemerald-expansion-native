/*
 * timer.h: Timers 0 to 3 (see timer.c for the full model).
 *
 * Stands in for the four 16-bit hardware timers at 4000100h-400010Eh,
 * GBATEK "GBA Timers". Call timer_advance() once per frame (and, if a test
 * wants it, with any other cycle count); the module reads TMxCNT_H and
 * TMxCNT_L from the I/O page, counts, raises overflow interrupts and writes
 * the counters back.
 *
 * The plain-memory problem. On hardware, TMxCNT_L is two registers at one
 * address: a write sets the reload value, a read returns the live counter.
 * In the mapped I/O page it is one cell, so the module keeps its own counter
 * and reload per timer and remembers the last value it stored in each cell.
 * At the start of every advance, a cell that differs from that remembered
 * value was written by the game, so it is a new reload; the counter is not
 * touched (as on hardware). After advancing, the module stores the counter
 * in the cell and remembers it, which is what a game's read sees. Limit: a
 * game write of exactly the value the cell already holds cannot be seen;
 * that changes nothing when it is the counter value, and it matters only
 * if the game wants that value as the reload: writing CNT_L equal to the
 * cell and then enabling loads the old reload, not the written value.
 * Second limit: the enable bit is compared between advances, so a stop and
 * restart with no advance in between (enabled at both samples) is invisible
 * and does not reload the counter. Init seeds counter and reload from the
 * cell, so a value in the cell at init is the first reload.
 *
 * Enabling (CNT_H enable bit 0 to 1 since the last advance) loads the
 * counter from the reload, which is the cell the game wrote just before,
 * and clears the prescaler residue. A disabled timer keeps its counter and
 * its cell. A count-up (cascade) timer counts overflows of the timer before
 * it and ignores its prescaler; Timer 0 has no cascade source and ignores
 * the bit. Prescaler residue carries over between advances.
 *
 * A timer that overflows several times within one advance (a long advance
 * or a short period) raises its interrupt once: the controller has one
 * flag per source, so the repeats would be indistinguishable anyway.
 */
#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#include "gba_irq.h"

/* One frame in CPU cycles: GBATEK "LCD Dimensions and Timings", 308 dots *
 * 4 cycles * 228 lines = 280896. The frame loop passes this to
 * timer_advance(). */
#define TIMER_CYCLES_PER_FRAME UINT32_C(280896)

enum timer_error {
    TIMER_OK = 0,
    TIMER_ERR_NO_RAISE = -1 /* timer_init() was given a NULL raise function */
};

/* Starts the timers from zero state and keeps `raise`, which timer_advance()
 * calls with GBA_IRQ_TIMER0..3 on an overflow of a timer whose IRQ enable
 * bit is set (main passes interrupt_raise). Returns TIMER_OK, or
 * TIMER_ERR_NO_RAISE for NULL, leaving the module uninitialised. */
int timer_init(void (*raise)(enum gba_irq));

/* Forgets the raise function. Safe at any time, including before init. */
void timer_shutdown(void);

/* Advances all four timers by `cycles` CPU cycles, in order 0..3 so that
 * cascades see their source's overflows; writes the current counters into
 * TMxCNT_L; raises the overflow interrupt of each timer that overflowed and
 * has its IRQ bit set. Does nothing before timer_init(). */
void timer_advance(uint32_t cycles);

#endif /* TIMER_H */
