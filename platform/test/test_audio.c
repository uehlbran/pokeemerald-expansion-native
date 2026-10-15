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
#include "sound_grid.h"
#include "test_support.h"

#define TEST_RATE_HZ 65536u
#define BYTES_PER_SAMPLE_FRAME 4u /* signed 16-bit, two channels */
#define TEST_SAMPLE_FRAMES 8192u  /* above the bound at 65536 Hz, 4392 */

static int16_t samples[TEST_SAMPLE_FRAMES * 2u];

static void use_dummy_driver(void)
{
    PT_ASSERT(tsup_set_environment("SDL_AUDIODRIVER", "dummy"));
}

PTEST(queue_bound_is_four_video_frames_of_samples)
{
    /* 65536 Hz * 280896 / 16777216 = 1097.25 -> 1098 per video frame; x4. */
    PT_ASSERT(audio_max_queued_frames(TEST_RATE_HZ) == 4u * 1098u);
    /* 48000 Hz: 803.67 -> 804 per video frame. */
    PT_ASSERT(audio_max_queued_frames(48000u) == 4u * 804u);
    PT_ASSERT(audio_max_queued_frames(0u) == 0u);
}

PTEST(queue_bound_at_the_output_rate_is_17568_bytes_the_same_67_ms_as_before)
{
    /* 4392 stereo frames of 4 bytes. The 32768 Hz grid's 2196 frames were
     * 8784 bytes: the bound is the same four video frames (66.9 ms) at twice
     * the samples. */
    PT_ASSERT(audio_max_queued_frames(TEST_RATE_HZ) * BYTES_PER_SAMPLE_FRAME == 17568u);
    PT_ASSERT(audio_max_queued_frames(32768u) * BYTES_PER_SAMPLE_FRAME == 8784u);
}

PTEST(the_device_opens_at_the_grid_rate_and_at_rates_a_driver_would_convert_from)
{
    /* The obtained spec must equal the request (allowed_changes = 0: SDL
     * converts to the driver's own rate). 65536 Hz is the output's; 48000
     * and 44100 Hz are what most drivers run at, so a request that differs
     * from the driver's takes SDL's conversion. Queued bytes are the
     * request's, counted before conversion. */
    static const uint32_t rates[] = {65536u, 48000u, 44100u};
    PT_ASSERT_EQ_U32(SOUND_GRID_RATE_HZ, rates[0]);
    use_dummy_driver();
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        PT_ASSERT_EQ_INT(audio_init(rates[i], AUDIO_DEVICE), 0);
        PT_ASSERT_EQ_INT(audio_queue(samples, 100), 0);
        PT_ASSERT_EQ_INT(audio_queued_bytes(), 100u * BYTES_PER_SAMPLE_FRAME);
        audio_shutdown();
    }
}

PTEST(queue_adds_four_bytes_per_frame)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
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
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    const size_t max = audio_max_queued_frames(TEST_RATE_HZ);
    PT_ASSERT_EQ_INT(audio_queue(samples, max), 0); /* exactly at the bound fits */
    uint32_t full = audio_queued_bytes();
    PT_ASSERT_EQ_INT(audio_queue(samples, 1), 0); /* one more does not */
    uint32_t after_drop = audio_queued_bytes();
    uint64_t dropped = audio_dropped_frames();
    uint64_t total = audio_total_frames();
    audio_shutdown();
    PT_ASSERT(total == max);
    PT_ASSERT_EQ_U32(full, (uint32_t)max * BYTES_PER_SAMPLE_FRAME);
    PT_ASSERT_EQ_U32(after_drop, full);
    PT_ASSERT(dropped == 1u);
}

PTEST(block_larger_than_the_bound_is_dropped_whole)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    const size_t too_big = audio_max_queued_frames(TEST_RATE_HZ) + 1u;
    PT_ASSERT(too_big <= TEST_SAMPLE_FRAMES);
    int result = audio_queue(samples, too_big);
    uint64_t dropped = audio_dropped_frames();
    uint64_t total = audio_total_frames();
    uint32_t bytes = audio_queued_bytes();
    audio_shutdown();
    PT_ASSERT_EQ_INT(result, 0);
    PT_ASSERT(dropped == too_big);
    PT_ASSERT(total == 0u);
    PT_ASSERT_EQ_U32(bytes, 0u);
}

PTEST(clear_empties_the_queue)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
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
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    PT_ASSERT_EQ_U32(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO, SDL_INIT_AUDIO);
    PT_ASSERT_EQ_INT(audio_queue(samples, 10), 0); /* paused: no underrun */
    uint64_t while_paused = audio_underruns();
    audio_clear();
    PT_ASSERT_EQ_INT(audio_set_paused(false), 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 10), 0); /* empty and running: underrun */
    uint64_t after = audio_underruns();
    PT_ASSERT_EQ_INT(audio_queue(samples, 10), 0); /* data queued: no new underrun */
    uint64_t after_second = audio_underruns();
    audio_shutdown();
    PT_ASSERT(after_second == 1u);
    PT_ASSERT(while_paused == 0u);
    PT_ASSERT(after == 1u);
}

/* The device starts paused (audio.h); paused reads back what was set last. */
PTEST(paused_reads_what_was_set_last)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    const bool at_start = audio_paused();
    PT_ASSERT_EQ_INT(audio_set_paused(false), 0);
    const bool running = audio_paused();
    PT_ASSERT_EQ_INT(audio_set_paused(true), 0);
    const bool paused_again = audio_paused();
    audio_shutdown();
    PT_ASSERT(at_start);
    PT_ASSERT(!running);
    PT_ASSERT(paused_again);
    PT_ASSERT(audio_paused());
}

PTEST(silent_mode_counts_frames_without_a_device)
{
    /* No driver set and no SDL audio call allowed to be needed. */
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_SILENT), 0);
    PT_ASSERT_EQ_U32(SDL_WasInit(SDL_INIT_AUDIO), 0u);
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

PTEST(failed_driver_init_fails_and_leaves_no_subsystem)
{
    PT_ASSERT(tsup_set_environment("SDL_AUDIODRIVER", "no_such_driver"));
    int result = audio_init(TEST_RATE_HZ, AUDIO_DEVICE);
    Uint32 inited = SDL_WasInit(SDL_INIT_AUDIO);
    audio_shutdown();
    PT_ASSERT(result != 0);
    PT_ASSERT_EQ_U32(inited, 0u);
    PT_ASSERT_EQ_U32(SDL_WasInit(SDL_INIT_AUDIO), 0u);
}

PTEST(shutdown_twice_and_without_init_are_safe)
{
    audio_shutdown();
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    audio_shutdown();
    audio_shutdown();
    PT_ASSERT_EQ_U32(SDL_WasInit(SDL_INIT_AUDIO), 0u);
}

PTEST(invalid_arguments_fail_and_leave_a_safe_state)
{
    use_dummy_driver();
    PT_ASSERT(audio_init(0u, AUDIO_DEVICE) != 0);
    audio_shutdown();
    PT_ASSERT(audio_init(AUDIO_MAX_SAMPLE_RATE_HZ + 1u, AUDIO_SILENT) != 0);
    audio_shutdown();
    PT_ASSERT(audio_queue(samples, 1) != 0); /* not initialised */
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    PT_ASSERT(audio_queue(NULL, 1) != 0);
    PT_ASSERT_EQ_INT(audio_queue(samples, 0), 0); /* zero frames is a no-op */
    PT_ASSERT_EQ_U32(audio_queued_bytes(), 0u);
    audio_shutdown();
}

PTEST(init_twice_is_an_error_and_keeps_the_first)
{
    use_dummy_driver();
    PT_ASSERT_EQ_INT(audio_init(TEST_RATE_HZ, AUDIO_DEVICE), 0);
    int second = audio_init(TEST_RATE_HZ, AUDIO_DEVICE);
    int queued = audio_queue(samples, 5);
    audio_shutdown();
    PT_ASSERT(second != 0);
    PT_ASSERT_EQ_INT(queued, 0);
}

PTEST_MAIN()
