/*
 * test_sound_speedup.c: n samples of sound into one (sound_speedup.c): the
 * means by hand, rounded toward zero, the channels apart, a group carried
 * from one call into the next, a factor of 1 leaving the sound as it is, a
 * frame of the output grid at each speed, and the factors refused.
 */
#include "ptest.h"
#include "sound_speedup.h"

#include <stdbool.h>
#include <string.h>

/* Started from garbage, as a struct on the stack is. */
static void start(struct sound_speedup *speedup, uint32_t factor)
{
    memset(speedup, 0xA5, sizeof *speedup);
    PT_ASSERT_EQ_INT(sound_speedup_start(speedup, factor), 0);
}

PTEST(a_factor_of_one_leaves_the_sound_as_it_is)
{
    struct sound_speedup s;
    start(&s, 1u);
    int16_t stereo[] = {1, -1, 32767, -32768, 5, 6};
    const int16_t before[] = {1, -1, 32767, -32768, 5, 6};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, stereo, 3), 3u);
    PT_ASSERT_MEM_EQ(stereo, before, sizeof before);
}

/* Pairs: (10 + 20) / 2 = 15 and (-10 + -21) / 2 = -15 (toward zero, not -16); (32767 + 32767) / 2
 * and (-32768 + -32768) / 2 at the extremes, which an int16_t sum would overflow. */
PTEST(at_twice_the_speed_each_pair_is_its_mean_rounded_toward_zero)
{
    struct sound_speedup s;
    start(&s, 2u);
    int16_t stereo[] = {10, -10, 20, -21, 32767, -32768, 32767, -32768};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, stereo, 4), 2u);
    PT_ASSERT_EQ_INT(stereo[0], 15);
    PT_ASSERT_EQ_INT(stereo[1], -15);
    PT_ASSERT_EQ_INT(stereo[2], 32767);
    PT_ASSERT_EQ_INT(stereo[3], -32768);
}

/* Threes: (1 + 2 + 3) / 3 = 2 on the left, (100 + 0 + 0) / 3 = 33 on the right; the seventh
 * sample waits for two more. */
PTEST(at_three_times_the_speed_each_three_are_one_and_the_rest_waits)
{
    struct sound_speedup s;
    start(&s, 3u);
    int16_t first[] = {1, 100, 2, 0, 3, 0, 4, 7};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, first, 4), 1u);
    PT_ASSERT_EQ_INT(first[0], 2);
    PT_ASSERT_EQ_INT(first[1], 33);
    /* (4 + 5 + 6) / 3 = 5 and (7 + 8 + 9) / 3 = 8, across the two calls. */
    int16_t second[] = {5, 8, 6, 9};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, second, 2), 1u);
    PT_ASSERT_EQ_INT(second[0], 5);
    PT_ASSERT_EQ_INT(second[1], 8);
}

/* Fours, the channels kept apart: left 0, 4, 8, 12 is 6; right -1, -1, -1, -2 is -1 (-5 / 4). */
PTEST(at_four_times_the_speed_the_channels_stay_apart)
{
    struct sound_speedup s;
    start(&s, 4u);
    int16_t stereo[] = {0, -1, 4, -1, 8, -1, 12, -2};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, stereo, 4), 1u);
    PT_ASSERT_EQ_INT(stereo[0], 6);
    PT_ASSERT_EQ_INT(stereo[1], -1);
}

/* Eight frames of the 65536 Hz grid are 8778 samples (1097.25 a frame, sound_grid.h): at n
 * times the speed, floor(8778 / n) of them come out in all, as many as the device plays in
 * eight / n frame periods. */
PTEST(a_run_of_grid_frames_at_each_speed_gives_one_in_n)
{
    static int16_t stereo[1098u * SOUND_SPEEDUP_CHANNELS];
    static const size_t grid[] = {1097, 1097, 1097, 1098, 1097, 1097, 1097, 1098};
    for (uint32_t factor = 1; factor <= SOUND_SPEEDUP_MAX_FACTOR; factor++) {
        struct sound_speedup s;
        start(&s, factor);
        size_t in = 0;
        size_t out = 0;
        for (size_t i = 0; i < sizeof grid / sizeof grid[0]; i++) {
            const size_t frames = grid[i];
            memset(stereo, 0, sizeof stereo);
            const size_t got = sound_speedup_run(&s, stereo, frames);
            PT_ASSERT(got <= frames);
            in += frames;
            out += got;
        }
        PT_ASSERT_EQ_U32(in, 8778u);
        PT_ASSERT_EQ_U32(out, 8778u / factor);
    }
}

PTEST(starting_again_forgets_a_partial_group)
{
    struct sound_speedup s;
    start(&s, 2u);
    int16_t one[] = {1000, 1000};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, one, 1), 0u);
    PT_ASSERT_EQ_INT(sound_speedup_start(&s, 2u), 0);
    int16_t two[] = {2, 4, 4, 8};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, two, 2), 1u);
    PT_ASSERT_EQ_INT(two[0], 3);
    PT_ASSERT_EQ_INT(two[1], 6);
}

PTEST(no_samples_give_none)
{
    struct sound_speedup s;
    start(&s, 3u);
    int16_t none[2] = {9, 9};
    PT_ASSERT_EQ_U32(sound_speedup_run(&s, none, 0), 0u);
    PT_ASSERT_EQ_INT(none[0], 9);
}

PTEST(a_factor_of_zero_or_above_the_fastest_is_refused_and_changes_nothing)
{
    struct sound_speedup s;
    start(&s, 2u);
    PT_ASSERT_EQ_INT(sound_speedup_start(&s, 0u), -1);
    PT_ASSERT_EQ_INT(sound_speedup_start(&s, SOUND_SPEEDUP_MAX_FACTOR + 1u), -1);
    PT_ASSERT_EQ_U32(s.factor, 2u);
}

PTEST_MAIN()
