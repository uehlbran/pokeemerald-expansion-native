/*
 * test_debug_channel.c: mGBA's debug registers (debug_channel.h).
 *
 * Expected behaviour from mGBA (src/gba/gba.c GBADebug, src/gba/io.c, the
 * standard logger in src/core/log.c, at the commit the bundled mgba-rom-test
 * was built from) and from how the hydra runs it (-l15) and reads its lines
 * (tools/mgba-rom-test-hydra/main.c); docs/research/test-runner-protocol.md.
 * Messages are written the way test/test_runner.c's MgbaPutchar_ writes
 * them: bytes into STRING, a NUL, then the send.
 */
// deps: gba_memmap.c
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "debug_channel.h"
#include "gba_memmap.h"
#include "ptest.h"

/* include/gba/io_reg.h MGBA_LOG_INFO, as the runner sends it. */
#define LOG_INFO UINT16_C(3)
#define LOG_DEBUG UINT16_C(4)
#define SEND_INFO (uint16_t)(DEBUG_CHANNEL_FLAGS_SEND | LOG_INFO)

#define PREFIX_LENGTH (sizeof DEBUG_CHANNEL_LINE_PREFIX - 1)
#define CAPTURE_SIZE 1024u

static volatile char *debug_string(void)
{
    return (volatile char *)(uintptr_t)DEBUG_CHANNEL_STRING_ADDRESS;
}

static volatile uint16_t *debug_flags(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_FLAGS_ADDRESS;
}

static volatile uint16_t *debug_enable(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_ENABLE_ADDRESS;
}

/* As MgbaPutchar_ and MgbaVPrintf_: the bytes, then a NUL after them unless
 * they fill the buffer. */
static void put_message(const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; i++)
        debug_string()[i] = bytes[i];
    if (length < DEBUG_CHANNEL_STRING_SIZE)
        debug_string()[length] = '\0';
}

static void clear_registers(void)
{
    for (size_t i = 0; i < DEBUG_CHANNEL_STRING_SIZE; i++)
        debug_string()[i] = 0;
    *debug_flags() = 0;
    *debug_enable() = 0;
}

/* A channel open on a fresh temporary file. */
static FILE *open_channel(void)
{
    FILE *out = tmpfile();
    if (out == NULL)
        return NULL;
    clear_registers();
    if (gba_memmap_init() != GBA_MEMMAP_OK || debug_channel_init(out) != DEBUG_CHANNEL_OK || !debug_channel_open()) {
        (void)fclose(out);
        return NULL;
    }
    return out;
}

/* Everything written to out so far; the channel stays usable. */
static size_t read_back(FILE *out, char *buffer, size_t size)
{
    long end = ftell(out);
    if (end < 0 || fseek(out, 0, SEEK_SET) != 0)
        return 0;
    size_t length = fread(buffer, 1, size, out);
    (void)fseek(out, end, SEEK_SET);
    return length;
}

static void close_channel(FILE *out)
{
    debug_channel_shutdown();
    clear_registers();
    if (out != NULL)
        (void)fclose(out);
}

static bool buffer_is_zero(void)
{
    for (size_t i = 0; i < DEBUG_CHANNEL_STRING_SIZE; i++) {
        if (debug_string()[i] != 0)
            return false;
    }
    return true;
}

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
    FILE *out = tmpfile();
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
    FILE *out = tmpfile();
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
    FILE *out = tmpfile();
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
    PT_ASSERT_EQ_INT(fcntl(fds[0], F_SETFL, O_NONBLOCK), 0);
    clear_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(debug_channel_init(out), DEBUG_CHANNEL_OK);
    PT_ASSERT(debug_channel_open());
    char captured[CAPTURE_SIZE];
    size_t total = 0;
    for (int line = 0; line < 2; line++) {
        put_message(line == 0 ? ":P" : ":F", 2);
        debug_channel_send(SEND_INFO);
        ssize_t got = read(fds[0], captured + total, sizeof captured - total);
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
