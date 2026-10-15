/*
 * test_host_console_posix.c: the POSIX side of the console modes
 * (host_console.h, host_posix.c). Every stream is a console there: what the
 * parent gave is what the C library's streams already are, so nothing is
 * attached and main never routes a Linux player's output into the log. A
 * stream reported as anything else would send it to the per-user directory
 * while every other test, which reads no stdout, stayed green.
 */
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include "host_console.h"
#include "ptest.h"

PTEST(both_standard_streams_are_reported_as_a_console)
{
    PT_ASSERT_EQ_INT(host_console_stream_kind(HOST_STREAM_OUT), HOST_STREAM_CONSOLE);
    PT_ASSERT_EQ_INT(host_console_stream_kind(HOST_STREAM_ERR), HOST_STREAM_CONSOLE);
}

PTEST(there_is_no_console_to_attach_to_and_nothing_is_changed)
{
    PT_ASSERT(!host_console_attach_parent());
    PT_ASSERT_EQ_INT(host_console_stream_kind(HOST_STREAM_OUT), HOST_STREAM_CONSOLE);
}

/* The stream was the write end of a pipe: afterwards its writes succeed and reach neither the pipe nor anywhere
 * else, and the pipe's reader sees the end of file. */
PTEST(a_stream_discarded_on_a_pipe_writes_nowhere)
{
    int ends[2];
    PT_ASSERT_EQ_INT(pipe(ends), 0);
    FILE *const stream = fdopen(ends[1], "w");
    PT_ASSERT(stream != NULL);
    const int discarded = host_console_discard_stream(stream);
    const int written = fprintf(stream, "lost\n");
    const int flushed = fflush(stream);
    char byte;
    const ssize_t read_bytes = read(ends[0], &byte, sizeof byte);
    const int closed = fclose(stream);
    (void)close(ends[0]);
    PT_ASSERT_EQ_INT(discarded, 0);
    PT_ASSERT(written > 0);
    PT_ASSERT_EQ_INT(flushed, 0);
    PT_ASSERT_EQ_INT(read_bytes, 0);
    PT_ASSERT_EQ_INT(closed, 0);
}

PTEST_MAIN()
