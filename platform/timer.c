/*
 * timer.c: Timers 0 to 3.
 *
 * Stands in for the four 16-bit up-counters at 4000100h-400010Eh. On
 * hardware each counts CPU cycles divided by a prescaler (or the overflows
 * of the timer before it), reloads on overflow and can raise an interrupt;
 * GBATEK "GBA Timers". Clean room from GBATEK; the frame-granular design and
 * the plain-memory policy for TMxCNT_L are in docs/RESEARCH.md ("Timers and
 * RTC") and timer.h. Single-threaded: the frame loop calls timer_advance().
 */
#include <stdbool.h>
#include <stddef.h>

#include "gba_memmap.h"
#include "timer.h"

/* GBATEK "GBA Timers": four timers, TM0CNT to TM3CNT. */
#define TIMER_COUNT 4u

/* GBATEK "GBA Timers": TM0CNT_L at 4000100h, TM0CNT_H at 4000102h, then the
 * next timer four bytes on (TM3CNT_H is 400010Eh). */
#define TIMER_REGISTERS_OFFSET UINT32_C(0x100)
#define TIMER_REGISTER_STRIDE UINT32_C(4)
#define TIMER_CNT_H_OFFSET UINT32_C(2)

/* GBATEK "GBA Timers", 4000102h TMxCNT_H. Bits 0-1: prescaler selection
 * (0 = F/1, 1 = F/64, 2 = F/256, 3 = F/1024). Bit 2: count-up timing. Bit 6:
 * timer IRQ enable. Bit 7: timer start/stop. */
#define TIMER_CNT_H_PRESCALER_MASK UINT16_C(0x0003)
#define TIMER_CNT_H_COUNT_UP UINT16_C(0x0004)
#define TIMER_CNT_H_IRQ_ENABLE UINT16_C(0x0040)
#define TIMER_CNT_H_ENABLE UINT16_C(0x0080)

/* GBATEK "GBA Timers": the counter is 16 bits, so it overflows from FFFFh
 * to the reload after 10000h - reload ticks. */
#define TIMER_COUNTER_RANGE UINT32_C(0x10000)

/* The prescaler dividers selected by CNT_H bits 0-1, in cycles per tick
 * (GBATEK "GBA Timers": F/1, F/64, F/256, F/1024). */
static const uint32_t prescaler_cycles[] = { 1, 64, 256, 1024 };

struct timer {
    uint16_t counter;
    uint16_t reload;
    uint16_t cell_value;  /* what this module last stored in TMxCNT_L */
    uint32_t residue;     /* cycles not yet worth a tick, below the divider */
    bool was_enabled;     /* the enable bit at the previous advance */
};

static struct timer timers[TIMER_COUNT];
static void (*raise_irq)(enum gba_irq);

static volatile uint16_t *timer_register(unsigned index, uint32_t offset)
{
    return (volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + TIMER_REGISTERS_OFFSET
                                            + index * TIMER_REGISTER_STRIDE + offset);
}

static volatile uint16_t *cnt_l(unsigned index)
{
    return timer_register(index, 0);
}

static volatile uint16_t *cnt_h(unsigned index)
{
    return timer_register(index, TIMER_CNT_H_OFFSET);
}

int timer_init(void (*raise)(enum gba_irq))
{
    if (raise == NULL) {
        raise_irq = NULL;
        return TIMER_ERR_NO_RAISE;
    }
    raise_irq = raise;
    for (unsigned i = 0; i < TIMER_COUNT; i++) {
        uint16_t cell = *cnt_l(i);
        timers[i] = (struct timer){ .counter = cell, .reload = cell, .cell_value = cell };
    }
    return TIMER_OK;
}

void timer_shutdown(void)
{
    raise_irq = NULL;
}

/* Adds `ticks` to the counter, reloading on each overflow; returns how many
 * overflows that was. Wide arithmetic: ticks can be up to UINT32_MAX. */
static uint32_t count_ticks(struct timer *t, uint64_t ticks)
{
    uint64_t to_overflow = TIMER_COUNTER_RANGE - t->counter;
    uint64_t period = TIMER_COUNTER_RANGE - t->reload;

    if (ticks < to_overflow) {
        t->counter = (uint16_t)(t->counter + ticks);
        return 0;
    }
    ticks -= to_overflow;
    t->counter = (uint16_t)(t->reload + ticks % period);
    return (uint32_t)(1 + ticks / period);
}

/* Ticks from `cycles` through the prescaler, carrying the leftover cycles. */
static uint32_t prescaled_ticks(struct timer *t, uint16_t control, uint32_t cycles)
{
    uint32_t divider = prescaler_cycles[control & TIMER_CNT_H_PRESCALER_MASK];
    uint64_t total = (uint64_t)t->residue + cycles;

    t->residue = (uint32_t)(total % divider);
    return (uint32_t)(total / divider);
}

/* Picks up what the game wrote to the shared cell and CNT_H since last time:
 * a changed cell is a new reload; enable 0 to 1 loads the counter from it. */
static void sync_with_game(unsigned index, struct timer *t, bool enabled)
{
    uint16_t cell = *cnt_l(index);

    if (cell != t->cell_value)
        t->reload = cell;
    if (enabled && !t->was_enabled) {
        t->counter = t->reload;
        t->residue = 0;
    }
    t->was_enabled = enabled;
}

/* One timer's share of an advance; returns its overflow count for the next
 * timer's cascade. */
static uint32_t advance_one(unsigned index, uint32_t cycles, uint32_t source_overflows)
{
    struct timer *t = &timers[index];
    uint16_t control = *cnt_h(index);
    bool enabled = (control & TIMER_CNT_H_ENABLE) != 0;
    uint32_t overflows = 0;

    sync_with_game(index, t, enabled);
    if (enabled) {
        bool counts_up = index > 0 && (control & TIMER_CNT_H_COUNT_UP) != 0;
        uint32_t ticks = counts_up ? source_overflows : prescaled_ticks(t, control, cycles);

        overflows = count_ticks(t, ticks);
    }
    *cnt_l(index) = t->counter;
    t->cell_value = t->counter;
    if (overflows > 0 && (control & TIMER_CNT_H_IRQ_ENABLE) != 0)
        raise_irq((enum gba_irq)(GBA_IRQ_TIMER0 + index));
    return overflows;
}

void timer_advance(uint32_t cycles)
{
    uint32_t overflows = 0;

    if (raise_irq == NULL)
        return;
    for (unsigned i = 0; i < TIMER_COUNT; i++)
        overflows = advance_one(i, cycles, overflows);
}
