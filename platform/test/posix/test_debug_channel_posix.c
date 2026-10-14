/*
 * test_debug_channel_posix.c: the debug channel (debug_channel.h) on a pipe,
 * which the hydra reads while the game runs, and on /dev/full, where every
 * write fails. The rest is in ../test_debug_channel.c; the registers and the
 * messages are ../debug_channel_fixture.h's.
 */
// deps: gba_memmap.c debug_channel.c
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "debug_channel_fixture.h"
#include "posix_support.h"
#include "ptest.h"

#define PIPE_WAIT_MILLISECONDS 2000 /* long enough for a slow machine, short enough to fail a test that never writes */

PTEST(every_line_is_flushed_at_once)
{
    /* A fully buffered stream on a pipe: the line must be readable from the
     * other end without closing or flushing the stream here, as the hydra
     * reads it while the game runs (and after a crash). */
    int fds[2];
    PT_ASSERT_EQ_INT(pipe(fds), 0);
    FILE *out = fdopen(fds[1], "w");
    PT_ASSERT(out != NULL);
    static char stream_buffer[BUFSIZ];
    PT_ASSERT_EQ_INT(setvbuf(out, stream_buffer, _IOFBF, sizeof stream_buffer), 0);
    clear_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(debug_channel_init(out), DEBUG_CHANNEL_OK);
    PT_ASSERT(debug_channel_open());
    char captured[CAPTURE_SIZE];
    size_t total = 0;
    for (int line = 0; line < 2; line++) {
        put_message(line == 0 ? ":P" : ":F", 2);
        debug_channel_send(SEND_INFO);
        ssize_t got = psup_read_within(fds[0], captured + total, sizeof captured - total, PIPE_WAIT_MILLISECONDS);
        if (got > 0)
            total += (size_t)got;
    }
    close_channel(out);
    close(fds[0]);
    static const char expected[] = "GBA Debug: :P\nGBA Debug: :F\n";
    PT_ASSERT_EQ_INT((intmax_t)total, sizeof expected - 1);
    PT_ASSERT_MEM_EQ(captured, expected, sizeof expected - 1);
}

PTEST(a_failed_write_drops_the_message_and_carries_on)
{
    /* /dev/full refuses every write with ENOSPC: the reader is gone. */
    FILE *out = fopen("/dev/full", "w");
    if (out == NULL && errno == ENOENT)
        PT_FAIL("/dev/full is missing");
    PT_ASSERT(out != NULL);
    clear_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(debug_channel_init(out), DEBUG_CHANNEL_OK);
    PT_ASSERT(debug_channel_open());
    put_message("lost", 4);
    debug_channel_send(SEND_INFO);
    put_message("lost too", 8);
    debug_channel_send(SEND_INFO);
    bool zero = buffer_is_zero();
    debug_channel_shutdown();
    clearerr(out);
    (void)fclose(out);
    clear_registers();
    PT_ASSERT(zero);
}

PTEST_MAIN()
