/*
 * Tests of frame_pacer. The deadline arithmetic is pure and tested with
 * plain numbers; the long-run test checks the 59.7275 Hz period (280896
 * cycles at 16777216 Hz, GBATEK "LCD Dimensions and Timings") accumulates
 * with no drift, at the normal speed and at each fast-forward multiplier,
 * whose periods are worked by hand below. Unthrottled mode and the
 * unlimited speed must return at once.
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
    /* 1 GHz counter: 280896e9/16777216 = 16742706.299 ticks; the 0.299 is
     * 5013504/16777216 (280896e9 - 16742706 * 16777216 = 5013504). */
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, FRAME_PACER_NORMAL_SPEED, &rem) == 16742706u);
    PT_ASSERT_EQ_U32(rem, 5013504u);
}

/* The period at n times the speed is the GBA's divided by n, its fraction
 * carried in units of 1/(16777216 n) tick. At 1 GHz (280896e9 ticks of
 * 1/16777216):
 *   2x: 280896e9 / 33554432 = 8371353 rem 5013504   (8371353.149)
 *   3x: 280896e9 / 50331648 = 5580902 rem 5013504   (5580902.100)
 *   4x: 280896e9 / 67108864 = 4185676 rem 38567936  (4185676.575)
 * and at 4x the second period carries the first's 0.575: (280896e9 +
 * 38567936) / 67108864 = 4185677 rem 10027008. */
PTEST(the_period_at_each_multiplier_is_the_gbas_divided_by_it)
{
    uint32_t rem = 0;
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, 2u, &rem) == 8371353u);
    PT_ASSERT_EQ_U32(rem, 5013504u);
    rem = 0;
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, 3u, &rem) == 5580902u);
    PT_ASSERT_EQ_U32(rem, 5013504u);
    rem = 0;
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, 4u, &rem) == 4185676u);
    PT_ASSERT_EQ_U32(rem, 38567936u);
    PT_ASSERT(frame_pacer_period_ticks(1000000000u, 4u, &rem) == 4185677u);
    PT_ASSERT_EQ_U32(rem, 10027008u);
}

/* A 1 kHz counter, where the fraction is most of a tick: 280896000 /
 * 50331648 = 5 rem 29237760 (5.581), so at 3x the periods go 5, 6, ... and
 * a thousand of them are 5580 ticks, not the 5000 of the whole ticks alone. */
PTEST(at_three_times_the_remainder_is_carried)
{
    uint32_t rem = 0;
    PT_ASSERT(frame_pacer_period_ticks(1000u, 3u, &rem) == 5u);
    PT_ASSERT_EQ_U32(rem, 29237760u);
    PT_ASSERT(frame_pacer_period_ticks(1000u, 3u, &rem) == 6u);
    PT_ASSERT_EQ_U32(rem, 8143872u);
}

static void check_no_drift(uint64_t freq, uint32_t multiplier, uint64_t exact)
{
    uint32_t rem = 0;
    uint64_t sum = 0;
    for (unsigned i = 0; i < 1000; i++)
        sum += frame_pacer_period_ticks(freq, multiplier, &rem);
    PT_ASSERT(sum == exact);
    PT_ASSERT(sum == (1000u * (uint64_t)GBA_CYCLES_PER_FRAME * freq) / ((uint64_t)GBA_CPU_HZ * multiplier));
}

/* floor(1000 * 280896 * hz / (16777216 * n)), worked by hand for each. */
PTEST(a_thousand_periods_sum_exactly_with_no_drift)
{
    check_no_drift(1000000000u, 1u, UINT64_C(16742706298)); /* ns counter */
    check_no_drift(10000000u, 1u, UINT64_C(167427062));     /* 10 MHz counter (common on Windows) */
    check_no_drift(1000u, 1u, UINT64_C(16742));             /* ms counter: tiny period, large rounding */
}

PTEST(a_thousand_periods_at_each_multiplier_sum_exactly)
{
    check_no_drift(1000000000u, 2u, UINT64_C(8371353149));
    check_no_drift(1000000000u, 3u, UINT64_C(5580902099));
    check_no_drift(1000000000u, 4u, UINT64_C(4185676574));
    check_no_drift(10000000u, 2u, UINT64_C(83713531));
    check_no_drift(10000000u, 3u, UINT64_C(55809020));
    check_no_drift(10000000u, 4u, UINT64_C(41856765));
    check_no_drift(1000u, 2u, UINT64_C(8371));
    check_no_drift(1000u, 3u, UINT64_C(5580));
    check_no_drift(1000u, 4u, UINT64_C(4185));
}

PTEST(period_is_not_sixty_hertz)
{
    uint32_t rem = 0;
    uint64_t p = frame_pacer_period_ticks(1000000000u, FRAME_PACER_NORMAL_SPEED, &rem);
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

/* At 4x, 28 waits fit in the time of 7 at the normal speed (117.2 ms), and
 * clearly less than 28 of those (470 ms): the frames come four times as
 * often. The bounds are as loose as the test above allows. */
PTEST(at_four_times_the_waits_are_a_quarter_as_long)
{
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_REALTIME), 0);
    uint64_t start = SDL_GetPerformanceCounter();
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(4u), 0);
    for (unsigned i = 0; i < 28; i++)
        frame_pacer_wait();
    uint64_t elapsed = SDL_GetPerformanceCounter() - start;
    const uint32_t multiplier = frame_pacer_multiplier();
    frame_pacer_shutdown();
    uint64_t freq = SDL_GetPerformanceFrequency();
    PT_ASSERT_EQ_U32(multiplier, 4u);
    PT_ASSERT(elapsed * 1000u >= freq * 117u);
    PT_ASSERT(elapsed * 1000u < freq * 400u);
}

PTEST(unlimited_wait_returns_at_once_and_normal_speed_waits_again)
{
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_REALTIME), 0);
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(FRAME_PACER_UNLIMITED), 0);
    uint64_t start = SDL_GetPerformanceCounter();
    for (unsigned i = 0; i < 100; i++)
        frame_pacer_wait();
    uint64_t unlimited = SDL_GetPerformanceCounter() - start;
    /* Back at the normal speed the schedule starts again from now: no frames
     * owed from the unlimited stretch, and no burst to catch up on. */
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(FRAME_PACER_NORMAL_SPEED), 0);
    start = SDL_GetPerformanceCounter();
    for (unsigned i = 0; i < 3; i++)
        frame_pacer_wait();
    uint64_t normal = SDL_GetPerformanceCounter() - start;
    frame_pacer_shutdown();
    uint64_t freq = SDL_GetPerformanceFrequency();
    PT_ASSERT(unlimited < freq / 10u);       /* 100 frames would take 1.67 s */
    PT_ASSERT(normal * 1000u >= freq * 50u); /* 3 periods: 50.2 ms */
}

/* A change restarts the schedule; setting the same speed again does not.
 * Above FRAME_PACER_MAX_MULTIPLIER is refused and keeps the speed. */
PTEST(the_multiplier_is_checked_kept_and_reset_by_init)
{
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_UNTHROTTLED), 0);
    PT_ASSERT_EQ_U32(frame_pacer_multiplier(), FRAME_PACER_NORMAL_SPEED);
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(3u), 0);
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(FRAME_PACER_MAX_MULTIPLIER + 1u), -1);
    PT_ASSERT_EQ_U32(frame_pacer_multiplier(), 3u);
    frame_pacer_shutdown();
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_UNTHROTTLED), 0);
    PT_ASSERT_EQ_U32(frame_pacer_multiplier(), FRAME_PACER_NORMAL_SPEED);
    frame_pacer_shutdown();
}

/* Unthrottled (the gates) stays unthrottled at any speed. */
PTEST(unthrottled_ignores_the_multiplier)
{
    PT_ASSERT_EQ_INT(frame_pacer_init(FRAME_PACER_UNTHROTTLED), 0);
    PT_ASSERT_EQ_INT(frame_pacer_set_multiplier(2u), 0);
    uint64_t start = SDL_GetPerformanceCounter();
    for (unsigned i = 0; i < 100; i++)
        frame_pacer_wait();
    uint64_t elapsed = SDL_GetPerformanceCounter() - start;
    frame_pacer_shutdown();
    PT_ASSERT(elapsed < SDL_GetPerformanceFrequency() / 10u);
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
