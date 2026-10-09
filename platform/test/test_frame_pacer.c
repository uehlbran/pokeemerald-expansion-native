/*
 * Tests of frame_pacer. The deadline arithmetic is pure and tested with
 * plain numbers; the long-run test checks the 59.7275 Hz period (280896
 * cycles at 16777216 Hz, GBATEK "LCD Dimensions and Timings") accumulates
 * with no drift. Unthrottled mode must return at once.
 */
#include <SDL.h>

#include "frame_pacer.h"
#include "ptest.h"

PTEST(the_unthrottled_option_selects_the_unthrottled_mode)
{
    PT_ASSERT_EQ_INT(frame_pacer_mode_for(true), FRAME_PACER_UNTHROTTLED);
    PT_ASSERT_EQ_INT(frame_pacer_mode_for(false), FRAME_PACER_REALTIME);
}

PTEST(deadline_advances_by_one_period_when_on_time)
{
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 1050, 500) == 1100u);
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 900, 500) == 1100u); /* early */
}

PTEST(deadline_catches_up_after_a_short_stall)
{
    /* Late but within max_stall: the deadline stays on the grid, so the
     * loop runs the frames it owes and the long-run rate is kept. */
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 1300, 500) == 1100u);
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 1600, 500) == 1100u); /* exactly at the limit */
}

PTEST(deadline_resynchronises_after_a_long_stall)
{
    /* Beyond max_stall: restart from now, no burst of back-to-back frames. */
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 1601, 500) == 1701u);
    PT_ASSERT(frame_pacer_next_deadline(1000, 100, 100000, 500) == 100100u);
}

PTEST(period_has_the_gba_frame_rate)
{
    uint32_t rem = 0;
    /* 1 GHz counter: 280896e9/16777216 = 16742706.6 ticks. */
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, &rem) == 16742706u);
    PT_ASSERT(rem != 0u);
}

static void check_no_drift(uint64_t freq)
{
    uint32_t rem = 0;
    uint64_t sum = 0;
    for (unsigned i = 0; i < 1000; i++)
        sum += frame_pacer_period_ticks(freq, &rem);
    uint64_t exact = (1000u * (uint64_t)GBA_CYCLES_PER_FRAME * freq) / (uint64_t)GBA_CPU_HZ;
    PT_ASSERT(sum == exact);
}

PTEST(a_thousand_periods_sum_exactly_with_no_drift)
{
    check_no_drift(1000000000u); /* ns counter */
    check_no_drift(10000000u);   /* 10 MHz counter (common on Windows) */
    check_no_drift(1000u);       /* ms counter: tiny period, large rounding */
}

PTEST(period_is_not_sixty_hertz)
{
    uint32_t rem = 0;
    uint64_t p = frame_pacer_period_ticks(1000000000u, &rem);
    PT_ASSERT(p > 16666667u); /* 1/60 s would be 16666666 */
}

PTEST(unthrottled_wait_returns_at_once)
{
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_UNTHROTTLED), 0);
    uint64_t start = SDL_GetPerformanceCounter();
    for (unsigned i = 0; i < 100; i++)
        frame_pacer_wait();
    uint64_t elapsed = SDL_GetPerformanceCounter() - start;
    frame_pacer_shutdown();
    /* 100 throttled frames would take 1.67 s; allow 0.1 s. */
    PT_ASSERT(elapsed < SDL_GetPerformanceFrequency() / 10u);
}

PTEST(throttled_wait_takes_about_one_frame_period)
{
    /* Deadlines sit on the grid from init, so 7 waits cannot finish before 7
     * periods (117.2 ms) after init, however late any wait woke up. */
    uint64_t start = SDL_GetPerformanceCounter();
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_REALTIME), 0);
    for (unsigned i = 0; i < 7; i++)
        frame_pacer_wait();
    uint64_t elapsed = SDL_GetPerformanceCounter() - start;
    frame_pacer_shutdown();
    uint64_t freq = SDL_GetPerformanceFrequency();
    /* Lower bound tight (never early); upper bound a 10x sanity bound so a
     * loaded machine cannot flake it. */
    PT_ASSERT(elapsed * 1000u >= freq * 117u);
    PT_ASSERT(elapsed * 1000u < freq * 1200u);
}

PTEST(shutdown_twice_and_wait_without_init_are_safe)
{
    frame_pacer_wait(); /* returns at once when not initialised */
    frame_pacer_shutdown();
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_UNTHROTTLED), 0);
    frame_pacer_shutdown();
    frame_pacer_shutdown();
}

PTEST_MAIN()
