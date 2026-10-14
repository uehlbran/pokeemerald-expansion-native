/*
 * test_debug_channel.c: mGBA's debug registers (debug_channel.h).
 *
 * Expected behaviour from mGBA (src/gba/gba.c GBADebug, src/gba/io.c, the
 * standard logger in src/core/log.c, at the commit the bundled mgba-rom-test
 * was built from) and from how the hydra runs it (-l15) and reads its lines
 * (tools/mgba-rom-test-hydra/main.c); docs/research/test-runner-protocol.md.
 * Messages are written the way test/test_runner.c's MgbaPutchar_ writes
 * them: bytes into STRING, a NUL, then the send. The tests that need a pipe
 * or /dev/full are in posix/test_debug_channel_posix.c.
 */
// deps: gba_memmap.c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug_channel_fixture.h"
#include "ptest.h"
#include "test_support.h"

PTEST(short_message_is_one_line_with_the_prefix)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message(":Nhello", 7);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + 8);
    PT_ASSERT_MEM_EQ(captured, "GBA Debug: :Nhello\n", PREFIX_LENGTH + 8);
}

PTEST(bytes_above_ascii_pass_through)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    static const char message[] = {'\x1b', '[', '3', '1', 'm', '\xe9', '\x7f'};
    put_message(message, sizeof message);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + sizeof message + 1);
    PT_ASSERT_MEM_EQ(captured + PREFIX_LENGTH, message, sizeof message);
    PT_ASSERT_EQ_INT(captured[length - 1], '\n');
}

PTEST(a_255_byte_message_is_printed_whole)
{
    /* MgbaPutchar_ sends when its index reaches 255, with a NUL at 255. */
    char message[DEBUG_CHANNEL_STRING_SIZE - 1];
    for (size_t i = 0; i < sizeof message; i++)
        message[i] = (char)('a' + i % 26);
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message(message, sizeof message);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + sizeof message + 1);
    PT_ASSERT_MEM_EQ(captured, DEBUG_CHANNEL_LINE_PREFIX, PREFIX_LENGTH);
    PT_ASSERT_MEM_EQ(captured + PREFIX_LENGTH, message, sizeof message);
    PT_ASSERT_EQ_INT(captured[length - 1], '\n');
}

PTEST(a_full_buffer_without_a_nul_prints_256_bytes)
{
    /* GBADebug copies at most 100h bytes and terminates the copy itself. */
    char message[DEBUG_CHANNEL_STRING_SIZE];
    memset(message, 'z', sizeof message);
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message(message, sizeof message);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + DEBUG_CHANNEL_STRING_SIZE + 1);
    PT_ASSERT_MEM_EQ(captured + PREFIX_LENGTH, message, sizeof message);
}

PTEST(an_empty_message_is_an_empty_line)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + 1);
    PT_ASSERT_MEM_EQ(captured, "GBA Debug: \n", PREFIX_LENGTH + 1);
}

PTEST(a_send_zeroes_the_buffer_and_clears_the_send_bit)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    char message[DEBUG_CHANNEL_STRING_SIZE];
    memset(message, 'q', sizeof message);
    put_message(message, sizeof message);
    debug_channel_send(SEND_INFO);
    bool zero = buffer_is_zero();
    uint16_t flags = *debug_flags();
    close_channel(out);
    PT_ASSERT(zero);
    PT_ASSERT_EQ_U32(flags, LOG_INFO);
}

PTEST(the_next_message_has_nothing_of_the_last)
{
    /* The runner ends a line by writing NULs at 254 and 255 only, relying on
     * the send having zeroed the rest. */
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message("abcdef", 6);
    debug_channel_send(SEND_INFO);
    debug_string()[0] = 'x';
    debug_string()[1] = 'y';
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    static const char expected[] = "GBA Debug: abcdef\nGBA Debug: xy\n";
    PT_ASSERT_EQ_INT((intmax_t)length, sizeof expected - 1);
    PT_ASSERT_MEM_EQ(captured, expected, sizeof expected - 1);
}

PTEST(a_store_without_the_send_bit_prints_nothing)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message("kept", 4);
    debug_channel_send(LOG_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    bool kept = debug_string()[0] == 'k' && debug_string()[3] == 't';
    uint16_t flags = *debug_flags();
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, 0);
    PT_ASSERT(kept);
    PT_ASSERT_EQ_U32(flags, LOG_INFO);
}

PTEST(levels_outside_the_hydras_filter_are_dropped_but_still_clear)
{
    /* -l15: FATAL, ERROR, WARN and INFO print; DEBUG (4) does not, nor do 5-7,
     * whose level is masked to 0. Every send zeroes the buffer. */
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    bool cleared = true;
    for (uint16_t level = 0; level <= DEBUG_CHANNEL_FLAGS_LEVEL_MASK; level++) {
        put_message("L", 1);
        debug_string()[1] = (char)('0' + level);
        debug_channel_send((uint16_t)(DEBUG_CHANNEL_FLAGS_SEND | level));
        cleared = cleared && buffer_is_zero();
    }
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    static const char expected[] = "GBA Debug: L0\nGBA Debug: L1\nGBA Debug: L2\nGBA Debug: L3\n";
    PT_ASSERT(cleared);
    PT_ASSERT_EQ_INT((intmax_t)length, sizeof expected - 1);
    PT_ASSERT_MEM_EQ(captured, expected, sizeof expected - 1);
}

PTEST(the_level_bits_above_two_are_ignored)
{
    FILE *out = open_channel();
    PT_ASSERT(out != NULL);
    put_message("hi", 2);
    debug_channel_send((uint16_t)(DEBUG_CHANNEL_FLAGS_SEND | 0x00F8u | LOG_INFO));
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, PREFIX_LENGTH + 3);
}

PTEST(the_handshake_answers_once_initialised)
{
    FILE *out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    clear_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    /* Before init: a GBA without mGBA; ENABLE keeps what was written. */
    bool before = debug_channel_open();
    uint16_t enable_before = *debug_enable();
    int init = debug_channel_init(out);
    bool opened = debug_channel_open();
    uint16_t enable_open = *debug_enable();
    debug_channel_shutdown();
    bool after = debug_channel_open();
    uint16_t enable_after = *debug_enable();
    close_channel(out);
    PT_ASSERT(!before);
    PT_ASSERT_EQ_U32(enable_before, DEBUG_CHANNEL_ENABLE_REQUEST);
    PT_ASSERT_EQ_INT(init, DEBUG_CHANNEL_OK);
    PT_ASSERT(opened);
    PT_ASSERT_EQ_U32(enable_open, DEBUG_CHANNEL_ENABLE_ACK);
    PT_ASSERT(!after);
    PT_ASSERT_EQ_U32(enable_after, DEBUG_CHANNEL_ENABLE_REQUEST);
}

PTEST(a_send_before_the_handshake_prints_nothing)
{
    FILE *out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    clear_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(debug_channel_init(out), DEBUG_CHANNEL_OK);
    put_message("early", 5);
    debug_channel_send(SEND_INFO);
    char captured[CAPTURE_SIZE];
    size_t length = read_back(out, captured, sizeof captured);
    bool kept = debug_string()[0] == 'e';
    close_channel(out);
    PT_ASSERT_EQ_INT((intmax_t)length, 0);
    PT_ASSERT(kept);
}

PTEST(init_zeroes_the_registers)
{
    FILE *out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    put_message("stale", 5);
    *debug_flags() = SEND_INFO;
    *debug_enable() = DEBUG_CHANNEL_ENABLE_ACK;
    int init = debug_channel_init(out);
    bool zero = buffer_is_zero();
    uint16_t flags = *debug_flags();
    uint16_t enable = *debug_enable();
    close_channel(out);
    PT_ASSERT_EQ_INT(init, DEBUG_CHANNEL_OK);
    PT_ASSERT(zero);
    PT_ASSERT_EQ_U32(flags, 0);
    PT_ASSERT_EQ_U32(enable, 0);
}

PTEST(init_without_a_stream_fails)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(debug_channel_init(NULL), DEBUG_CHANNEL_ERR_NO_STREAM);
    PT_ASSERT(!debug_channel_open());
    debug_channel_shutdown();
    debug_channel_shutdown(); /* twice is safe */
    clear_registers();
}

PTEST_MAIN()
