/*
 * test_audio_dump.c: the WAV recording of the sound output (audio_dump.h).
 *
 * Expected bytes from the RIFF WAVE layout (Microsoft and IBM, "Multimedia
 * Programming Interface and Data Specifications 1.0", 1991): "RIFF", the
 * size of everything after it, "WAVE"; "fmt ", 16, format 1 (PCM), 2
 * channels, the rate, the bytes a second, the block (4), 16 bits; "data",
 * its size; every number little-endian. The failure of a write on a full
 * disk is in posix/test_audio_dump_posix.c.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio_dump.h"
#include "ptest.h"
#include "test_support.h"

#define TEST_RATE_HZ 65536u

/* Larger than the module's 1024 frames per write, and not a multiple of it. */
#define MANY_FRAMES 2500u

/* The bytes of the file at path; returns how many were read (at most size). */
static size_t read_file(const char *path, uint8_t *out, size_t size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    const size_t read = fread(out, 1, size, file);
    (void)fclose(file);
    return read;
}

struct scratch {
    char directory[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE];
};

static bool make_scratch(struct scratch *s)
{
    if (!tsup_make_temp_directory(s->directory, sizeof s->directory, "test_audio_dump"))
        return false;
    return snprintf(s->path, sizeof s->path, "%s/out.wav", s->directory) < (int)sizeof s->path;
}

static void remove_scratch(const struct scratch *s)
{
    (void)tsup_remove_tree(s->directory);
}

PTEST(the_header_of_an_empty_recording)
{
    static const uint8_t expected[AUDIO_DUMP_HEADER_BYTES] = {
        'R',  'I',  'F', 'F', 36,   0,    0, 0,
        'W',  'A',  'V', 'E', /* RIFF, 36 bytes after the size, WAVE */
        'f',  'm',  't', ' ', 16,   0,    0, 0,
        1,    0,    2,   0, /* fmt, 16 bytes, PCM, 2 channels */
        0x44, 0xAC, 0,   0,   0x10, 0xB1, 2, 0,
        4,    0,    16,  0,                     /* 44100 Hz, 176400 bytes a second, block 4, 16 bits */
        'd',  'a',  't', 'a', 0,    0,    0, 0, /* data, 0 bytes */
    };
    uint8_t header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(header, 44100u, 0); /* any rate: this is not the output's */
    PT_ASSERT_MEM_EQ(header, expected, sizeof expected);
}

PTEST(the_header_counts_four_bytes_a_frame)
{
    uint8_t header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(header, TEST_RATE_HZ, 3);
    static const uint8_t riff_size[] = {48, 0, 0, 0};              /* 36 + 12 */
    static const uint8_t rate[] = {0x00, 0x00, 0x01, 0};           /* 65536 */
    static const uint8_t bytes_a_second[] = {0x00, 0x00, 0x04, 0}; /* 262144 */
    static const uint8_t data_size[] = {12, 0, 0, 0};
    PT_ASSERT_MEM_EQ(&header[4], riff_size, 4);
    PT_ASSERT_MEM_EQ(&header[24], rate, 4);
    PT_ASSERT_MEM_EQ(&header[28], bytes_a_second, 4);
    PT_ASSERT_MEM_EQ(&header[40], data_size, 4);
}

PTEST(the_header_describes_at_most_what_32_bits_can)
{
    /* AUDIO_DUMP_MAX_FRAMES = (2^32 - 1 - 36) / 4 = 1073741814: data
     * 4294967256 bytes (FFFFFFD8h), RIFF size 4294967292 (FFFFFFFCh). */
    PT_ASSERT(AUDIO_DUMP_MAX_FRAMES == UINT32_C(1073741814));
    static const uint8_t riff_size[] = {0xFC, 0xFF, 0xFF, 0xFF};
    static const uint8_t data_size[] = {0xD8, 0xFF, 0xFF, 0xFF};
    static const uint64_t lengths[] = {AUDIO_DUMP_MAX_FRAMES, AUDIO_DUMP_MAX_FRAMES + 1u, UINT64_MAX};
    for (size_t i = 0; i < sizeof lengths / sizeof lengths[0]; i++) {
        uint8_t header[AUDIO_DUMP_HEADER_BYTES];
        audio_dump_header(header, TEST_RATE_HZ, lengths[i]);
        PT_ASSERT_MEM_EQ(&header[4], riff_size, 4);
        PT_ASSERT_MEM_EQ(&header[40], data_size, 4);
    }
    uint8_t header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(header, TEST_RATE_HZ, AUDIO_DUMP_MAX_FRAMES - 1u); /* one frame less: 4 bytes less */
    static const uint8_t data_size_less[] = {0xD4, 0xFF, 0xFF, 0xFF};
    PT_ASSERT_MEM_EQ(&header[40], data_size_less, 4);
}

PTEST(a_recording_holds_the_samples_little_endian_and_its_sizes)
{
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    static const int16_t first[] = {0, -1, INT16_MAX, INT16_MIN};
    static const int16_t second[] = {1, -2};
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(first, 2), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(second, 1), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    uint8_t file[AUDIO_DUMP_HEADER_BYTES + 64];
    const size_t length = read_file(s.path, file, sizeof file);
    remove_scratch(&s);
    PT_ASSERT_EQ_INT(length, AUDIO_DUMP_HEADER_BYTES + 12u);
    uint8_t expected_header[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(expected_header, TEST_RATE_HZ, 3);
    PT_ASSERT_MEM_EQ(file, expected_header, AUDIO_DUMP_HEADER_BYTES);
    static const uint8_t data_size[] = {12, 0, 0, 0};
    PT_ASSERT_MEM_EQ(&file[40], data_size, 4);
    static const uint8_t samples[] = {0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x80, 0x01, 0x00, 0xFE, 0xFF};
    PT_ASSERT_MEM_EQ(&file[AUDIO_DUMP_HEADER_BYTES], samples, sizeof samples);
}

PTEST(a_long_append_is_written_whole_and_in_order)
{
    static int16_t stereo[MANY_FRAMES * 2];
    for (size_t i = 0; i < MANY_FRAMES * 2; i++)
        stereo[i] = (int16_t)(i * 7u - 9000u);
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, MANY_FRAMES), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    static uint8_t file[AUDIO_DUMP_HEADER_BYTES + MANY_FRAMES * 4 + 4];
    const size_t length = read_file(s.path, file, sizeof file);
    remove_scratch(&s);
    PT_ASSERT_EQ_INT(length, AUDIO_DUMP_HEADER_BYTES + MANY_FRAMES * 4u);
    bool all_equal = true;
    for (size_t i = 0; i < MANY_FRAMES * 2; i++) {
        const uint8_t *at = &file[AUDIO_DUMP_HEADER_BYTES + 2 * i];
        all_equal = all_equal && (uint16_t)(at[0] | at[1] << 8) == (uint16_t)stereo[i];
    }
    PT_ASSERT(all_equal);
    static const uint8_t data_size[] = {0x10, 0x27, 0, 0}; /* 2500 * 4 = 10000 = 2710h */
    PT_ASSERT_MEM_EQ(&file[40], data_size, 4);
}

PTEST(an_empty_recording_is_a_valid_header_alone)
{
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(NULL, 0), 0); /* zero frames: nothing, and no samples needed */
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    uint8_t file[AUDIO_DUMP_HEADER_BYTES + 4];
    const size_t length = read_file(s.path, file, sizeof file);
    remove_scratch(&s);
    uint8_t expected[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(expected, TEST_RATE_HZ, 0);
    PT_ASSERT_EQ_INT(length, AUDIO_DUMP_HEADER_BYTES);
    PT_ASSERT_MEM_EQ(file, expected, AUDIO_DUMP_HEADER_BYTES);
}

PTEST(an_existing_file_is_replaced)
{
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    FILE *old = fopen(s.path, "wb");
    PT_ASSERT(old != NULL);
    static const char junk[200] = "an older, longer file";
    PT_ASSERT_EQ_INT(fwrite(junk, 1, sizeof junk, old), sizeof junk);
    PT_ASSERT_EQ_INT(fclose(old), 0);
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    const intmax_t size = tsup_file_size(s.path);
    remove_scratch(&s);
    PT_ASSERT(size == (intmax_t)AUDIO_DUMP_HEADER_BYTES);
}

PTEST(without_a_recording_nothing_is_written_and_nothing_fails)
{
    static const int16_t stereo[] = {5, 5};
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, 1), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    audio_dump_shutdown();
    audio_dump_shutdown();
}

PTEST(bad_arguments_start_nothing)
{
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    char missing[TSUP_PATH_SIZE];
    PT_ASSERT(snprintf(missing, sizeof missing, "%s/no/such/dir.wav", s.directory) < (int)sizeof missing);
    struct tsup_stderr saved;
    FILE *null_device = fopen(tsup_null_device(), "w");
    PT_ASSERT(null_device != NULL);
    PT_ASSERT(tsup_redirect_stderr(&saved, null_device));
    const int no_path = audio_dump_start(NULL, TEST_RATE_HZ);
    const int no_rate = audio_dump_start(s.path, 0);
    const int rate_too_high = audio_dump_start(s.path, AUDIO_DUMP_MAX_SAMPLE_RATE_HZ + 1u);
    const int no_directory = audio_dump_start(missing, TEST_RATE_HZ);
    const int no_samples_without_recording = audio_dump_append(NULL, 1); /* nothing to record: fine */
    (void)tsup_restore_stderr(&saved);
    (void)fclose(null_device);
    const bool nothing_created = !tsup_exists(s.path);
    /* Nothing was started: a good start still works. */
    const int good = audio_dump_start(s.path, AUDIO_DUMP_MAX_SAMPLE_RATE_HZ);
    const int finished = audio_dump_finish();
    remove_scratch(&s);
    PT_ASSERT_EQ_INT(no_path, -1);
    PT_ASSERT_EQ_INT(no_rate, -1);
    PT_ASSERT_EQ_INT(rate_too_high, -1);
    PT_ASSERT_EQ_INT(no_directory, -1);
    PT_ASSERT_EQ_INT(no_samples_without_recording, 0);
    PT_ASSERT(nothing_created);
    PT_ASSERT_EQ_INT(good, 0);
    PT_ASSERT_EQ_INT(finished, 0);
}

PTEST(a_second_start_is_refused_and_the_first_recording_goes_on)
{
    struct scratch s;
    char other[TSUP_PATH_SIZE];
    PT_ASSERT(make_scratch(&s));
    PT_ASSERT(snprintf(other, sizeof other, "%s/other.wav", s.directory) < (int)sizeof other);
    static const int16_t stereo[] = {300, -300};
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    struct tsup_stderr saved;
    FILE *null_device = fopen(tsup_null_device(), "w");
    PT_ASSERT(null_device != NULL);
    PT_ASSERT(tsup_redirect_stderr(&saved, null_device));
    const int second = audio_dump_start(other, TEST_RATE_HZ);
    const int no_samples = audio_dump_append(NULL, 1);
    (void)tsup_restore_stderr(&saved);
    (void)fclose(null_device);
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, 1), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    const intmax_t size = tsup_file_size(s.path);
    const bool other_created = tsup_exists(other);
    remove_scratch(&s);
    PT_ASSERT_EQ_INT(second, -1);
    PT_ASSERT_EQ_INT(no_samples, -1);
    PT_ASSERT(!other_created);
    PT_ASSERT(size == (intmax_t)AUDIO_DUMP_HEADER_BYTES + 4);
}

PTEST(a_finished_recording_can_be_followed_by_another)
{
    struct scratch s;
    PT_ASSERT(make_scratch(&s));
    static const int16_t stereo[] = {1, 2, 3, 4};
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, 2), 0);
    audio_dump_shutdown(); /* finishes */
    const intmax_t first = tsup_file_size(s.path);
    PT_ASSERT_EQ_INT(audio_dump_start(s.path, TEST_RATE_HZ), 0);
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, 1), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
    uint8_t file[AUDIO_DUMP_HEADER_BYTES + 16];
    const size_t length = read_file(s.path, file, sizeof file);
    remove_scratch(&s);
    PT_ASSERT(first == (intmax_t)AUDIO_DUMP_HEADER_BYTES + 8);
    PT_ASSERT_EQ_INT(length, AUDIO_DUMP_HEADER_BYTES + 4u); /* counted afresh */
    static const uint8_t data_size[] = {4, 0, 0, 0};
    PT_ASSERT_MEM_EQ(&file[40], data_size, 4);
}

PTEST_MAIN()
