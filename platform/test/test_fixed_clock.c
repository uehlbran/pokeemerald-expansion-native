/*
 * test_fixed_clock.c: the replay's deterministic clock (fixed_clock.c).
 */
#include "ptest.h"
#include "fixed_clock.h"

PTEST(whole_seconds_elapse_every_sixty_frames)
{
    PT_ASSERT(fixed_clock_at(1000, 0) == 1000);
    PT_ASSERT(fixed_clock_at(1000, 59) == 1000);
    PT_ASSERT(fixed_clock_at(1000, 60) == 1001);
    PT_ASSERT(fixed_clock_at(1000, 119) == 1001);
    PT_ASSERT(fixed_clock_at(1000, 120) == 1002);
    PT_ASSERT(fixed_clock_at(0, UINT32_MAX) == (int64_t)(UINT32_MAX / 60u));
}

PTEST(a_negative_base_counts_up_from_it)
{
    PT_ASSERT(fixed_clock_at(-10, 600) == 0);
}

PTEST(the_clock_saturates_instead_of_overflowing)
{
    PT_ASSERT(fixed_clock_at(INT64_MAX, 0) == INT64_MAX);
    PT_ASSERT(fixed_clock_at(INT64_MAX - 1, 120) == INT64_MAX);
    PT_ASSERT(fixed_clock_at(INT64_MAX - 2, 120) == INT64_MAX);
    PT_ASSERT(fixed_clock_at(INT64_MAX - 2, 119) == INT64_MAX - 1);
}

PTEST_MAIN()
