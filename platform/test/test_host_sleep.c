/*
 * test_host_sleep.c: host_sleep_us (host_sleep.h) on the host the test runs
 * on: 0 returns at once, a sleep lasts at least what was asked, and short
 * sleeps do not round up to a coarse system tick, which at Windows' 15.6 ms
 * would oversleep every fast-forward frame.
 */
#include "host_sleep.h"
#include "ptest.h"

#include <SDL_timer.h>

/* Microseconds since an earlier SDL_GetPerformanceCounter reading. */
static uint64_t elapsed_us(uint64_t since)
{
    return (SDL_GetPerformanceCounter() - since) * UINT64_C(1000000) / SDL_GetPerformanceFrequency();
}

PTEST(zero_returns_at_once)
{
    const uint64_t start = SDL_GetPerformanceCounter();
    host_sleep_us(0);
    PT_ASSERT(elapsed_us(start) < 1000u);
}

PTEST(a_sleep_lasts_at_least_what_was_asked)
{
    const uint64_t start = SDL_GetPerformanceCounter();
    host_sleep_us(3000u);
    const uint64_t slept = elapsed_us(start);
    PT_ASSERT(slept >= 2900u); /* the clocks' rounding */
    PT_ASSERT(slept < 500000u);
}

/* Twenty sleeps of 2 ms: about 40 ms with a fine timer, 312 ms with a 15.6 ms
 * tick. The bound leaves room for a loaded machine but not for the tick. */
PTEST(short_sleeps_do_not_round_up_to_a_coarse_tick)
{
    const uint64_t start = SDL_GetPerformanceCounter();
    for (int i = 0; i < 20; i++)
        host_sleep_us(2000u);
    const uint64_t slept = elapsed_us(start);
    PT_ASSERT(slept >= 39000u);
    PT_ASSERT(slept < 200000u);
}

PTEST_MAIN()
