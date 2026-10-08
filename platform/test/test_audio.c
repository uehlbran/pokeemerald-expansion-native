/*
 * Tests of audio, with SDL's dummy audio driver (set before the first SDL
 * call). The device is left paused so the dummy driver does not drain the
 * queue and the queued sizes are exact.
 */
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "audio.h"
#include "ptest.h"

#define TEST_RATE_HZ 32768u
#define BYTES_PER_SAMPLE_FRAME 4u /* signed 16-bit, two channels */
#define TEST_SAMPLE_FRAMES 4096u

static int16_t samples[TEST_SAMPLE_FRAMES * 2u];

static void use_dummy_driver(void)
{
    PT_ASSERT(setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
}

PTEST(queue_bound_is_four_video_frames_of_samples)
{
    /* 32768 Hz * 280896 / 16777216 = 548.625 -> 549 per video frame; x4. */
    PT_ASSERT(audio_max_queued_frames(TEST_RATE_HZ) == 4u * 549u);
    /* 48000 Hz: 803.67 -> 804 per video frame. */
    PT_ASSERT(audio_max_queued_frames(48000u) == 4u * 804u);
    PT_ASSERT(audio_max_queued_frames(0u) == 0u);
}

PTEST(queue_adds_four_bytes_per_frame)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    PT_ASSERT_EQ_INT(audio_queued_bytes(), 0u);
    int queued = audio_queue(samples, 100);
    uint32_t after_first = audio_queued_bytes();
    int queued_more = audio_queue(samples, 50);
    uint32_t after_second = audio_queued_bytes();
    audio_shutdown();
    PT_ASSERT_EQ_INT(queued, 0);
    PT_ASSERT_EQ_INT(queued_more, 0);
    PT_ASSERT_EQ_U32(after_first, 100u * BYTES_PER_SAMPLE_FRAME);
    PT_ASSERT_EQ_U32(after_second, 150u * BYTES_PER_SAMPLE_FRAME);
}

PTEST(bound_skips_queuing_and_counts_the_drop)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    const size_t max = audio_max_queued_frames(TEST_RATE_HZ);
    PT_ASSERT_EQ_INT(audio_queue(samples, max), 0); /* exactly at the bound fits */
    uint32_t full = audio_queued_bytes();
    PT_ASSERT_EQ_INT(audio_queue(samples, 1), 0);   /* one more does not */
    uint32_t after_drop = audio_queued_bytes();
    uint64_t dropped = audio_dropped_frames();
    audio_shutdown();
    PT_ASSERT_EQ_U32(full, (uint32_t)max * BYTES_PER_SAMPLE_FRAME);
    PT_ASSERT_EQ_U32(after_drop, full);
    PT_ASSERT(dropped == 1u);
}

PTEST(clear_empties_the_queue)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 200), 0);
    audio_clear();
    uint32_t after = audio_queued_bytes();
    int requeue = audio_queue(samples, 10);
    uint32_t after_requeue = audio_queued_bytes();
    audio_shutdown();
    PT_ASSERT_EQ_U32(after, 0u);
    PT_ASSERT_EQ_INT(requeue, 0);
    PT_ASSERT_EQ_U32(after_requeue, 10u * BYTES_PER_SAMPLE_FRAME);
}

PTEST(underrun_counts_when_unpaused_and_queue_is_empty)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 10), 0); /* paused: no underrun */
    uint64_t while_paused = audio_underruns();
    audio_clear();
    PT_ASSERT_EQ_INT(audio_set_paused(false), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 10), 0); /* empty and running: underrun */
    uint64_t after = audio_underruns();
    audio_shutdown();
    PT_ASSERT(while_paused == 0u);
    PT_ASSERT(after == 1u);
}

PTEST(silent_mode_counts_frames_without_a_device)
{
    /* No driver set and no SDL audio call allowed to be needed. */
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, true), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 100), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 25), 0);
    uint64_t total = audio_total_frames();
    uint32_t bytes = audio_queued_bytes();
    PT_ASSERT_EQ_INT(audio_set_paused(false), 0);
    audio_clear();
    audio_shutdown();
    PT_ASSERT(total == 125u);
    PT_ASSERT_EQ_U32(bytes, 0u);
}

PTEST(shutdown_twice_and_without_init_are_safe)
{
    audio_shutdown();
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    audio_shutdown();
    audio_shutdown();
}

PTEST(invalid_arguments_fail_and_leave_a_safe_state)
{
    use_dummy_driver();
    PT_ASSERT(audio_init(0u, false) != 0);
    audio_shutdown();
    PT_ASSERT(audio_init(AUDIO_MAX_SAMPLE_RATE_HZ + 1u, true) != 0);
    audio_shutdown();
    PT_ASSERT(audio_queue(samples, 1) != 0); /* not initialised */
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    PT_ASSERT(audio_queue(NULL, 1) != 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 0), 0); /* zero frames is a no-op */
    PT_ASSERT_EQ_U32(audio_queued_bytes(), 0u);
    audio_shutdown();
}

PTEST(init_twice_is_an_error_and_keeps_the_first)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, false), 0);
    int second = audio_init(TEST_RATE_HZ, false);
    int queued = audio_queue(samples, 5);
    audio_shutdown();
    PT_ASSERT(second != 0);
    PT_ASSERT_EQ_INT(queued, 0);
}

PTEST_MAIN()
