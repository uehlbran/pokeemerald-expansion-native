/*
 * test_audio_dump_linux.c: a recording (audio_dump.h) on /dev/full, Linux's
 * device that refuses every write with ENOSPC, as a full disk does (macOS has
 * none): from the start, and from the middle of a recording (the recording's
 * descriptor moved onto /dev/full with dup2 and back). The rest is in
 * ../test_audio_dump.c.
 */
// deps: audio_dump.c
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "audio_dump.h"
#include "ptest.h"
#include "test_support.h"

#define FULL_DEVICE "/dev/full"
#define TEST_RATE_HZ 32768u

static int start_result;

static void start_on_full_device(void *context)
{
    (void)context;
    start_result = audio_dump_start(FULL_DEVICE, TEST_RATE_HZ);
}

PTEST(a_disk_that_takes_nothing_fails_the_start_and_names_the_file)
{
    if (access(FULL_DEVICE, W_OK) != 0 && errno == ENOENT)
        PT_FAIL(FULL_DEVICE " is missing");
    char text[512];
    PT_ASSERT(tsup_capture_stderr(start_on_full_device, NULL, text, sizeof text));
    PT_ASSERT_EQ_INT(start_result, -1);
    PT_ASSERT(strstr(text, FULL_DEVICE) != NULL);
    /* Nothing was started: appending and finishing have nothing to do. */
    static const int16_t stereo[] = {1, 1};
    PT_ASSERT_EQ_INT(audio_dump_append(stereo, 1), 0);
    PT_ASSERT_EQ_INT(audio_dump_finish(), 0);
}

static const int16_t two_frames[] = {100, -100, 200, -200};
static int append_results[2];

/* The append whose write fails, then the next one. */
static void append_twice(void *context)
{
    (void)context;
    append_results[0] = audio_dump_append(two_frames, 2);
    append_results[1] = audio_dump_append(two_frames, 2);
}

static int finish_result;

static void finish(void *context)
{
    (void)context;
    finish_result = audio_dump_finish();
}

/* How many times needle occurs in text. */
static int occurrences(const char *text, const char *needle)
{
    int count = 0;
    for (const char *at = strstr(text, needle); at != NULL; at = strstr(at + 1, needle))
        count++;
    return count;
}

/* True if descriptor is open on the file at path. */
static bool open_on(int descriptor, const char *path)
{
    struct stat open_file, named_file;
    return fstat(descriptor, &open_file) == 0 && stat(path, &named_file) == 0 && open_file.st_dev == named_file.st_dev
           && open_file.st_ino == named_file.st_ino;
}

PTEST(a_write_that_fails_mid_recording_stops_it_once_and_fails_the_finish)
{
    char directory[TSUP_PATH_SIZE], path[TSUP_PATH_SIZE];
    PT_ASSERT(tsup_make_temp_directory(directory, sizeof directory, "test_audio_dump_posix"));
    PT_ASSERT(snprintf(path, sizeof path, "%s/out.wav", directory) < (int)sizeof path);
    /* open(2) gives the lowest free descriptor: the one the recording gets. */
    const int recording = open(tsup_null_device(), O_RDONLY);
    PT_ASSERT(recording >= 0 && close(recording) == 0);
    PT_ASSERT_EQ_INT(audio_dump_start(path, TEST_RATE_HZ), 0);
    PT_ASSERT(open_on(recording, path));
    PT_ASSERT_EQ_INT(audio_dump_append(two_frames, 2), 0); /* written: 8 bytes after the header */
    /* The disk fills: the recording's descriptor now writes to /dev/full. */
    const int file = dup(recording);
    const int full = open(FULL_DEVICE, O_WRONLY);
    PT_ASSERT(file >= 0 && full >= 0);
    PT_ASSERT_EQ_INT(dup2(full, recording), recording);
    PT_ASSERT_EQ_INT(close(full), 0);
    char appended[512];
    const bool captured_appends = tsup_capture_stderr(append_twice, NULL, appended, sizeof appended);
    /* Room again, for the header (a rewrite needs no new space on a disk). */
    PT_ASSERT_EQ_INT(dup2(file, recording), recording);
    PT_ASSERT_EQ_INT(close(file), 0);
    char finished[512];
    const bool captured_finish = tsup_capture_stderr(finish, NULL, finished, sizeof finished);
    uint8_t bytes[AUDIO_DUMP_HEADER_BYTES + 16];
    FILE *written = fopen(path, "rb");
    const size_t length = written != NULL ? fread(bytes, 1, sizeof bytes, written) : 0;
    if (written != NULL)
        (void)fclose(written);
    (void)tsup_remove_tree(directory);
    PT_ASSERT(captured_appends);
    PT_ASSERT_EQ_INT(append_results[0], -1);
    PT_ASSERT_EQ_INT(append_results[1], 0); /* nothing more is recorded, and nothing is reported */
    PT_ASSERT_EQ_INT(occurrences(appended, "the recording stops here"), 1);
    PT_ASSERT(strstr(appended, path) != NULL);
    PT_ASSERT(captured_finish);
    PT_ASSERT_EQ_INT(finish_result, -1);
    PT_ASSERT(strstr(finished, "stopped after 2 stereo frames") != NULL);
    PT_ASSERT_EQ_INT(length, AUDIO_DUMP_HEADER_BYTES + 8u);
    uint8_t expected[AUDIO_DUMP_HEADER_BYTES];
    audio_dump_header(expected, TEST_RATE_HZ, 2); /* the header describes what was written */
    PT_ASSERT_MEM_EQ(bytes, expected, AUDIO_DUMP_HEADER_BYTES);
}

PTEST_MAIN()
