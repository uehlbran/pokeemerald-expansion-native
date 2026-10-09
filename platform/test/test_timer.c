// deps: gba_memmap.c
/*
 * Tests of timer, written from GBATEK "GBA Timers": prescaler 1/64/256/1024,
 * count-up timing, IRQ enable, enable bit, reload on enable and on overflow.
 * The raise function is a counting fake, so the interrupt controller is not
 * linked. Registers are expansion's own REG_TMxCNT macros.
 */
#include <gba/types.h>
#include <gba/defines.h>
#include <gba/io_reg.h>

#include "gba_memmap.h"
#include "ptest.h"
#include "timer.h"

#define CYCLES_1024 1024u
#define CYCLES_64 64u
#define TEST_RUNNER_RELOAD ((uint16_t)(UINT16_MAX - 274 * 60)) /* test/test_runner.c */
#define TIMER_COUNT 4

static unsigned raised[GBA_IRQ_GAMEPAK + 1];

static void fake_raise(enum gba_irq source)
{
    raised[source]++;
}

static void setup(void)
{
    REG_TM0CNT_L = 0; REG_TM0CNT_H = 0;
    REG_TM1CNT_L = 0; REG_TM1CNT_H = 0;
    REG_TM2CNT_L = 0; REG_TM2CNT_H = 0;
    REG_TM3CNT_L = 0; REG_TM3CNT_H = 0;
    for (unsigned i = 0; i < sizeof raised / sizeof raised[0]; i++)
        raised[i] = 0;
    timer_init(fake_raise);
}

static void teardown(void)
{
    timer_shutdown();
    REG_TM0CNT_L = 0; REG_TM0CNT_H = 0;
    REG_TM1CNT_L = 0; REG_TM1CNT_H = 0;
    REG_TM2CNT_L = 0; REG_TM2CNT_H = 0;
    REG_TM3CNT_L = 0; REG_TM3CNT_H = 0;
}

PTEST(init_rejects_null_raise)
{
    PT_ASSERT_EQ_INT(timer_init(NULL), TIMER_ERR_NO_RAISE);
    timer_advance(1000); /* uninitialised: must not crash */
    timer_shutdown();
}

PTEST(shutdown_without_init_and_twice_is_safe)
{
    timer_shutdown();
    timer_shutdown();
    timer_advance(TIMER_CYCLES_PER_FRAME);
    PT_ASSERT_EQ_INT(timer_init(fake_raise), TIMER_OK);
    timer_shutdown();
}

PTEST(advance_before_init_changes_nothing)
{
    REG_TM0CNT_L = 100;
    REG_TM0CNT_H = TIMER_ENABLE;
    timer_advance(500);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 100);
    teardown();
}

PTEST(a_cell_written_before_init_is_the_first_reload)
{
    REG_TM2CNT_L = 0xFF00; /* written before timer_init */
    REG_TM2CNT_H = 0;
    timer_init(fake_raise);
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(0x40);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, 0xFF40);
    teardown();
}

PTEST(disabled_timer_never_changes)
{
    setup();
    timer_advance(TIMER_CYCLES_PER_FRAME);
    timer_advance(UINT32_MAX);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 0);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 0);
    teardown();
}

PTEST(prescaler_1_counts_cycles_exactly)
{
    setup();
    REG_TM0CNT_L = 10;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(0);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 10);
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 11);
    timer_advance(1000);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 1011);
    teardown();
}

PTEST(each_prescaler_divides_cycles)
{
    setup();
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_64CLK;
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_256CLK;
    REG_TM3CNT_H = TIMER_ENABLE | TIMER_1024CLK;
    timer_advance(65536);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0); /* wrapped exactly once to reload 0 */
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 1024);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, 256);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 64);
    teardown();
}

PTEST(prescaler_1024_test_runner_reload_overflows_at_the_right_cycle)
{
    const uint32_t interval = (65536u - TEST_RUNNER_RELOAD) * CYCLES_1024;
    setup();
    REG_TM2CNT_L = TEST_RUNNER_RELOAD;
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1024CLK;
    timer_advance(interval - 1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 0);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, UINT16_MAX);
    timer_advance(1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 1);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, TEST_RUNNER_RELOAD);
    timer_advance(interval - 1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 1);
    timer_advance(1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 2);
    teardown();
}

PTEST(test_runner_timeout_arrives_in_the_sixtieth_frame)
{
    setup();
    REG_TM2CNT_L = TEST_RUNNER_RELOAD;
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1024CLK;
    for (int frame = 0; frame < 59; frame++)
        timer_advance(TIMER_CYCLES_PER_FRAME);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 0);
    timer_advance(TIMER_CYCLES_PER_FRAME); /* the 60th frame crosses it */
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 1);
    teardown();
}

PTEST(overflow_raises_only_its_own_timer)
{
    setup();
    REG_TM1CNT_L = UINT16_MAX;
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1CLK;
    timer_advance(1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER1], 1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 0);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER2], 0);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER3], 0);
    teardown();
}

PTEST(overflow_without_irq_enable_raises_nothing_but_reloads)
{
    setup();
    REG_TM0CNT_L = 0xFFF0;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(0x18); /* one overflow, 8 ticks into the next period */
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 0);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0xFFF8);
    teardown();
}

PTEST(many_overflows_in_one_advance_raise_once_and_keep_the_phase)
{
    setup();
    REG_TM0CNT_L = 0xFFFC; /* period 4 ticks */
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1CLK;
    timer_advance(11); /* 2 overflows, 3 ticks into the third period */
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 1);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0xFFFC + 3);
    teardown();
}

PTEST(reload_zero_has_a_period_of_65536)
{
    setup();
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1CLK;
    timer_advance(65535);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 0);
    timer_advance(1);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 1);
    teardown();
}

PTEST(residue_carries_across_advances)
{
    setup();
    REG_TM3CNT_H = TIMER_ENABLE | TIMER_1024CLK;
    timer_advance(CYCLES_1024 - 1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 0);
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 1);
    timer_advance(CYCLES_1024 - 1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 1);
    timer_advance(CYCLES_1024 + 1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 3);
    teardown();
}

PTEST(reload_write_while_running_leaves_counter_and_is_used_at_overflow)
{
    setup();
    REG_TM0CNT_L = 0xFF00;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(0x10);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0xFF10);
    REG_TM0CNT_L = 0x1234; /* a new reload, not a new counter */
    timer_advance(0x10);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0xFF20);
    timer_advance(0xE0); /* reaches 0x10000 exactly: overflow */
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 0x1234);
    teardown();
}

PTEST(enable_loads_the_reload)
{
    setup();
    REG_TM1CNT_L = 500;
    timer_advance(100000); /* disabled: counter stays */
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(7);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 507);
    teardown();
}

PTEST(reload_written_while_disabled_survives_a_frame_boundary)
{
    setup();
    REG_TM1CNT_L = 500;
    timer_advance(1); /* the cell is overwritten with the idle counter */
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(7);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 507);
    teardown();
}

PTEST(disabled_timer_keeps_its_counter_and_reenabling_reloads)
{
    setup();
    REG_TM0CNT_L = 100;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(50);
    REG_TM0CNT_H = 0;
    timer_advance(100000);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 150);
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(5);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 105);
    teardown();
}

PTEST(enable_clears_prescaler_residue)
{
    setup();
    REG_TM3CNT_H = TIMER_ENABLE | TIMER_1024CLK;
    timer_advance(CYCLES_1024 - 1);
    REG_TM3CNT_H = 0;
    timer_advance(1);
    REG_TM3CNT_H = TIMER_ENABLE | TIMER_1024CLK;
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 0);
    teardown();
}

PTEST(cascade_counts_source_overflows_not_cycles)
{
    setup();
    REG_TM0CNT_L = 0xFFFF - 99; /* period 100 ticks */
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    REG_TM1CNT_L = 0;
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_COUNTUP | TIMER_1024CLK | TIMER_INTR_ENABLE;
    timer_advance(99);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 0);
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 1);
    timer_advance(250); /* two more overflows, 50 ticks into the next */
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 3);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER1], 0);
    teardown();
}

PTEST(cascade_overflow_reloads_and_raises)
{
    setup();
    REG_TM0CNT_L = 0xFFFF; /* overflows every tick */
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    REG_TM1CNT_L = 0xFFFE;
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_COUNTUP | TIMER_INTR_ENABLE;
    timer_advance(2);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER1], 1);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 0xFFFE);
    teardown();
}

PTEST(cascade_chains_through_three_timers)
{
    setup();
    REG_TM0CNT_L = 0xFFFF;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    REG_TM1CNT_L = 0xFFFF;
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_COUNTUP;
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_COUNTUP;
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, 1);
    teardown();
}

PTEST(cascade_with_a_disabled_source_never_counts)
{
    setup();
    REG_TM1CNT_H = TIMER_ENABLE | TIMER_COUNTUP;
    timer_advance(TIMER_CYCLES_PER_FRAME);
    PT_ASSERT_EQ_INT(REG_TM1CNT_L, 0);
    teardown();
}

PTEST(timer0_ignores_the_count_up_bit)
{
    setup();
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_COUNTUP;
    timer_advance(10);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 10);
    teardown();
}

PTEST(a_frame_on_prescaler_64_counts_4389)
{
    setup();
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_64CLK;
    timer_advance(TIMER_CYCLES_PER_FRAME);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, TIMER_CYCLES_PER_FRAME / CYCLES_64);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 4389);
    teardown();
}

PTEST(counter_reads_back_from_cnt_l_after_advance)
{
    setup();
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(1234);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, 1234);
    timer_advance(1);
    PT_ASSERT_EQ_INT(REG_TM2CNT_L, 1235);
    teardown();
}

PTEST(huge_advance_does_not_overflow_arithmetic)
{
    const uint64_t ticks = ((uint64_t)UINT32_MAX + (CYCLES_1024 - 1)) / CYCLES_1024;
    setup();
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_INTR_ENABLE | TIMER_1024CLK;
    timer_advance(CYCLES_1024 - 1); /* residue 1023, no tick yet */
    timer_advance(UINT32_MAX);
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER0], 1);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, ticks % 65536u);
    teardown();
}

PTEST(disabled_timer_cell_shows_the_frozen_counter_after_a_reload_write)
{
    setup();
    REG_TM0CNT_L = 100;
    REG_TM0CNT_H = TIMER_ENABLE | TIMER_1CLK;
    timer_advance(50);
    REG_TM0CNT_H = 0;
    REG_TM0CNT_L = 9; /* a reload for the next start, not a counter */
    timer_advance(1000);
    PT_ASSERT_EQ_INT(REG_TM0CNT_L, 150);
    teardown();
}

PTEST(cascade_into_timer_3_with_a_nonzero_reload_wraps)
{
    setup();
    REG_TM2CNT_L = 0xFFFF; /* overflows every tick */
    REG_TM2CNT_H = TIMER_ENABLE | TIMER_1CLK;
    REG_TM3CNT_L = 0xFFFD; /* period 3 */
    REG_TM3CNT_H = TIMER_ENABLE | TIMER_COUNTUP | TIMER_INTR_ENABLE;
    timer_advance(4); /* 4 ticks: overflow after 3, then 1 into the period */
    PT_ASSERT_EQ_INT(raised[GBA_IRQ_TIMER3], 1);
    PT_ASSERT_EQ_INT(REG_TM3CNT_L, 0xFFFE);
    teardown();
}

PTEST_MAIN()
