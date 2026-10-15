/*
 * test_debug_channel_linux.c: the debug channel (debug_channel.h) on a
 * stream whose every write fails with ENOSPC, a full disk, which Linux's
 * /dev/full gives (macOS has no such device); the message is dropped and the
 * game carries on. The same on a pipe whose reader has gone (EPIPE) is in
 * ../posix/test_debug_channel_posix.c, the rest in ../test_debug_channel.c.
 */
// deps: gba_memmap.c debug_channel.c
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "debug_channel_fixture.h"
#include "ptest.h"

PTEST(a_write_to_a_full_disk_drops_the_message_and_carries_on)
{
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
    const bool first_failed = ferror(out) != 0;
    clearerr(out);
    put_message("lost too", 8);
    debug_channel_send(SEND_INFO);
    const bool second_failed = ferror(out) != 0;
    const bool zero = buffer_is_zero();
    debug_channel_shutdown();
    clearerr(out);
    (void)fclose(out);
    clear_registers();
    PT_ASSERT(first_failed);
    PT_ASSERT(second_failed);
    PT_ASSERT(zero);
}

PTEST_MAIN()
