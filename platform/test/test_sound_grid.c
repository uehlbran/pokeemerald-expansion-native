/*
 * test_sound_grid.c: the output grid and the FIFO read on it (sound_grid.h).
 *
 * Expected values worked by hand from GBATEK: a frame is 280896 cycles
 * ("LCD Dimensions and Timings"), the grid one sample every 2^24 / 32768 =
 * 512 cycles (SOUNDBIAS, "Resampling to 32.768kHz"), so a frame is 548
 * samples and 320 cycles; FIFO sample i of an n-sample frame is held from
 * timer overflow i to overflow i + 1, every 280896 / n cycles (1254 for the
 * engine's 224, 2926 for 96; src/m4a.c SampleFreqSet, REG_TM0CNT_L =
 * -(280896 / pcmSamplesPerVBlank)). Output sample k of a frame that starts
 * with c cycles carried is read at the last cycle of its span, cycle
 * 512 * (k + 1) - c - 1.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ptest.h"
#include "sound_grid.h"

/* A FIFO frame whose sample i is i, so out[k] is the index read. The
 * largest frame m4a_output passes is the whole ring, 1584 samples. */
#define LARGEST_FIFO_FRAME 1584u
static int8_t ramp[LARGEST_FIFO_FRAME];

static void fill_ramp(size_t samples)
{
    for (size_t i = 0; i < samples; i++)
        ramp[i] = (int8_t)(uint8_t)i;
}

/* The index read for output sample k, from out (an index below 256). */
static unsigned index_read(const int8_t *out, size_t k)
{
    return (uint8_t)out[k];
}

PTEST(the_grid_is_512_cycles_and_a_frame_holds_at_most_549)
{
    PT_ASSERT_EQ_U32(SOUND_GRID_CYCLES_PER_SAMPLE, 512);
    PT_ASSERT_EQ_U32(SOUND_GRID_MAX_SAMPLES_PER_FRAME, 549);
}

PTEST(the_first_frame_is_548_samples_and_carries_320_cycles)
{
    struct sound_grid grid = {0};
    const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
    PT_ASSERT_EQ_U32(frame.carried_cycles, 0);
    PT_ASSERT_EQ_INT(frame.samples, 548); /* 280896 / 512 = 548, remainder 320 */
    PT_ASSERT_EQ_U32(grid.carried_cycles, 320);
}

PTEST(five_frames_are_2743_samples_with_64_cycles_carried)
{
    /* 320 + 280896 = 281216 = 549 * 512 + 128; 128 + 280896 = 548 * 512 +
     * 448; 448 + 280896 = 549 * 512 + 256; 256 + 280896 = 549 * 512 + 64. */
    static const size_t samples[] = {548, 549, 548, 549, 549};
    static const uint32_t carried[] = {0, 320, 128, 448, 256};
    struct sound_grid grid = {0};
    size_t total = 0;
    for (size_t f = 0; f < 5; f++) {
        const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
        PT_ASSERT_EQ_U32(frame.carried_cycles, carried[f]);
        PT_ASSERT_EQ_INT(frame.samples, samples[f]);
        total += frame.samples;
    }
    PT_ASSERT_EQ_INT(total, 2743); /* 5 * 280896 = 1404480 = 2743 * 512 + 64 */
    PT_ASSERT_EQ_U32(grid.carried_cycles, 64);
}

PTEST(eight_frames_are_4389_samples_and_the_grid_is_back_on_the_frame)
{
    /* 8 * 280896 = 2247168 = 4389 * 512 exactly. */
    struct sound_grid grid = {0};
    size_t total = 0;
    for (size_t f = 0; f < 8; f++)
        total += sound_grid_next_frame(&grid).samples;
    PT_ASSERT_EQ_INT(total, 4389);
    PT_ASSERT_EQ_U32(grid.carried_cycles, 0);
}

PTEST(every_carried_value_gives_548_or_549_and_stays_below_512)
{
    for (uint32_t carried = 0; carried < SOUND_GRID_CYCLES_PER_SAMPLE; carried++) {
        struct sound_grid grid = {carried};
        const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
        PT_ASSERT(frame.samples == 548 || frame.samples == 549);
        PT_ASSERT(grid.carried_cycles < SOUND_GRID_CYCLES_PER_SAMPLE);
        /* The cycles are all accounted for. */
        PT_ASSERT_EQ_U32(carried + GBA_CYCLES_PER_FRAME, frame.samples * 512u + grid.carried_cycles);
    }
}

PTEST(the_engines_224_samples_on_the_first_frame)
{
    /* Read at 511, 1023, 1535, 2047, 2559, 3071, 3583, 4095, 4607, 5119;
     * the FIFO's overflows at 1254, 2508, 3762, 5016. */
    static const unsigned expected[] = {0, 0, 1, 1, 2, 2, 2, 3, 3, 4};
    fill_ramp(224);
    struct sound_grid grid = {0};
    const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    sound_grid_hold_fifo(&frame, ramp, 224, out);
    for (size_t k = 0; k < sizeof expected / sizeof expected[0]; k++)
        PT_ASSERT_EQ_U32(index_read(out, k), expected[k]);
    /* The last: 548 * 512 - 1 = 280575, in 223's interval [279642, 280896). */
    PT_ASSERT_EQ_U32(index_read(out, 547), 223);
    PT_ASSERT_EQ_U32(index_read(out, 546), 223); /* 280063 */
    PT_ASSERT_EQ_U32(index_read(out, 545), 222); /* 279551 */
}

PTEST(the_carried_cycles_shift_the_next_frames_reads)
{
    /* The second frame starts with 320 cycles carried: read at 512 * (k + 1)
     * - 321 = 191, 703, 1215, 1727, 2239, 2751; its 549th sample at 280767. */
    static const unsigned expected[] = {0, 0, 0, 1, 1, 2};
    fill_ramp(224);
    struct sound_grid grid = {0};
    (void)sound_grid_next_frame(&grid);
    const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
    PT_ASSERT_EQ_U32(frame.carried_cycles, 320);
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    sound_grid_hold_fifo(&frame, ramp, 224, out);
    for (size_t k = 0; k < sizeof expected / sizeof expected[0]; k++)
        PT_ASSERT_EQ_U32(index_read(out, k), expected[k]);
    PT_ASSERT_EQ_U32(index_read(out, 548), 223);
    PT_ASSERT_EQ_U32(index_read(out, 546), 223); /* 279743, past 223's overflow at 279642 */
    PT_ASSERT_EQ_U32(index_read(out, 545), 222); /* 279231 */
}

PTEST(a_span_ending_on_the_frames_last_cycle_reads_the_last_fifo_sample)
{
    /* With 192 cycles carried (the eighth frame), the 549th span ends at
     * 549 * 512 - 192 = 280896, the frame's end: read at 280895, sample 223,
     * never 224 (the next frame's). */
    fill_ramp(224);
    const struct sound_grid_frame frame = {192, 549};
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME + 1];
    memset(out, 0x55, sizeof out);
    sound_grid_hold_fifo(&frame, ramp, 224, out);
    PT_ASSERT_EQ_U32(index_read(out, 548), 223);
    PT_ASSERT_EQ_INT(out[549], 0x55);        /* nothing written past the frame's samples */
    PT_ASSERT_EQ_U32(index_read(out, 0), 0); /* 319 */
}

PTEST(the_slowest_rate_96_samples_holds_each_for_2926_cycles)
{
    /* SOUND_MODE_FREQ_05734: overflows every 2926 cycles. Read at 511 ...
     * 2559 (sample 0), 3071 (1), 5631 (1), 6143 (2); the last, 280575, in
     * 95's interval [277970, 280896). */
    fill_ramp(96);
    struct sound_grid grid = {0};
    const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    sound_grid_hold_fifo(&frame, ramp, 96, out);
    for (size_t k = 0; k <= 4; k++)
        PT_ASSERT_EQ_U32(index_read(out, k), 0);
    PT_ASSERT_EQ_U32(index_read(out, 5), 1);
    PT_ASSERT_EQ_U32(index_read(out, 10), 1);
    PT_ASSERT_EQ_U32(index_read(out, 11), 2);
    PT_ASSERT_EQ_U32(index_read(out, 547), 95);
}

/* The FIFO index read for each output sample of frame, into indexes: a
 * FIFO of n samples whose values give the index, low 7 bits then high. */
static void read_indexes(const struct sound_grid_frame *frame, size_t n, int16_t *indexes)
{
    static int8_t fifo[LARGEST_FIFO_FRAME];
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    for (size_t i = 0; i < n; i++)
        fifo[i] = (int8_t)(i & 0x7Fu);
    sound_grid_hold_fifo(frame, fifo, n, out);
    for (size_t k = 0; k < frame->samples; k++)
        indexes[k] = out[k];
    for (size_t i = 0; i < n; i++)
        fifo[i] = (int8_t)(i >> 7);
    sound_grid_hold_fifo(frame, fifo, n, out);
    for (size_t k = 0; k < frame->samples; k++)
        indexes[k] = (int16_t)(indexes[k] + out[k] * 128);
}

PTEST(every_rate_of_the_engines_table_is_read_in_order)
{
    /* src/m4a_tables.c gPcmSamplesPerVBlankTable; each divides 280896, so the
     * timer's interval is whole. Over eight frames (every carried value the
     * grid takes) the reads never go back and advance by at most
     * ceil(512 / interval) samples. Up to 528 samples a frame (31536 Hz) the
     * interval is at least 512 cycles: every FIFO sample is read, from 0 to
     * the last. Above it (608, 672, 704: up to 42048 Hz) the grid is coarser
     * than the FIFO, and some samples fall between two reads, as on the GBA
     * (GBATEK: DMA sound above 32.768kHz loses quality in the resampling). */
    static const size_t counts[] = {96, 132, 176, 224, 264, 304, 352, 448, 528, 608, 672, 704};
    for (size_t c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        const size_t n = counts[c];
        const uint32_t interval = GBA_CYCLES_PER_FRAME / (uint32_t)n;
        const int16_t max_step = (int16_t)((SOUND_GRID_CYCLES_PER_SAMPLE + interval - 1u) / interval);
        PT_ASSERT_EQ_U32(GBA_CYCLES_PER_FRAME % n, 0);
        bool in_order = true, all_read = true;
        struct sound_grid grid = {0};
        for (size_t f = 0; f < 8; f++) {
            const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
            static int16_t indexes[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
            read_indexes(&frame, n, indexes);
            in_order = in_order && indexes[0] >= 0 && indexes[0] < max_step
                       && indexes[frame.samples - 1] > (int16_t)(n - 1) - max_step
                       && indexes[frame.samples - 1] <= (int16_t)(n - 1);
            for (size_t k = 1; k < frame.samples; k++)
                in_order = in_order && indexes[k] >= indexes[k - 1] && indexes[k] - indexes[k - 1] <= max_step;
            if (interval >= SOUND_GRID_CYCLES_PER_SAMPLE) {
                all_read = all_read && indexes[0] == 0 && indexes[frame.samples - 1] == (int16_t)(n - 1);
                for (size_t k = 1; k < frame.samples; k++)
                    all_read = all_read && indexes[k] - indexes[k - 1] <= 1;
            }
        }
        PT_ASSERT(in_order);
        PT_ASSERT(all_read);
    }
}

PTEST(a_one_sample_fifo_frame_is_held_the_whole_frame)
{
    const int8_t only[1] = {-77};
    const struct sound_grid_frame frame = {511, 549};
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    sound_grid_hold_fifo(&frame, only, 1, out);
    bool all = true;
    for (size_t k = 0; k < frame.samples; k++)
        all = all && out[k] == -77;
    PT_ASSERT(all);
}

PTEST(the_ring_size_frame_is_read_without_overflow)
{
    /* The largest FIFO frame m4a_output passes, the whole 1584-byte ring:
     * cycle 280575 * 1584 needs more than 32 bits' care, and the read stays
     * inside the frame. */
    for (size_t i = 0; i < LARGEST_FIFO_FRAME; i++)
        ramp[i] = (int8_t)(i == LARGEST_FIFO_FRAME - 1 ? 99 : 0);
    struct sound_grid grid = {0};
    const struct sound_grid_frame frame = sound_grid_next_frame(&grid);
    static int8_t out[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
    sound_grid_hold_fifo(&frame, ramp, LARGEST_FIFO_FRAME, out);
    /* 280575 * 1584 / 280896 = 1582.19: sample 1582, not the last. */
    PT_ASSERT_EQ_INT(out[547], 0);
    const struct sound_grid_frame aligned = {192, 549}; /* read at 280895: 1583.99 -> 1583 */
    sound_grid_hold_fifo(&aligned, ramp, LARGEST_FIFO_FRAME, out);
    PT_ASSERT_EQ_INT(out[548], 99);
}

PTEST_MAIN()
