/*
 * test_audio_dump_posix.c: a recording (audio_dump.h) on /dev/full, which
 * refuses every write with ENOSPC, as a full disk does. The rest is in
 * ../test_audio_dump.c.
 */
// deps: audio_dump.c
#include <errno.h>
#include <stdbool.h>
#include <string.h>
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

PTEST_MAIN()
