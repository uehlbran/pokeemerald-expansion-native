/*
 * debug_channel.c: mGBA's debug registers (debug_channel.h has the contract).
 *
 * Stands in for mGBA's GBADebug (src/gba/gba.c) and the register decoding
 * in src/gba/io.c, with mgba-rom-test's standard logger (src/core/log.c,
 * "%s: %s\n" to stdout, filtered by -l) as the output. Followed: mGBA's
 * behaviour as documented in docs/research/test-runner-protocol.md, written
 * from that description; no mGBA code is copied (docs/C-RULES.md, licensing).
 *
 * The registers live in the mapped debug page (gba_memmap.h): the game's
 * stores into STRING land there directly; this module reads them on a send.
 */
#include "debug_channel.h"

#include <stddef.h>
#include <string.h>

_Static_assert(DEBUG_CHANNEL_STRING_ADDRESS >= GBA_MGBA_DEBUG_BASE
                   && DEBUG_CHANNEL_STRING_ADDRESS + DEBUG_CHANNEL_STRING_SIZE <= DEBUG_CHANNEL_FLAGS_ADDRESS,
               "STRING lies in the debug page, below FLAGS");
_Static_assert(DEBUG_CHANNEL_ENABLE_ADDRESS + sizeof(uint16_t) <= GBA_MGBA_DEBUG_BASE + GBA_MGBA_DEBUG_SIZE,
               "ENABLE lies in the debug page");

static struct {
    FILE *out;    /* NULL: not initialised */
    bool enabled; /* the handshake was made (mGBA's gba->debug) */
    bool reported_write_failure;
} channel;

static volatile uint8_t *string_register(void)
{
    return (volatile uint8_t *)(uintptr_t)DEBUG_CHANNEL_STRING_ADDRESS;
}

static volatile uint16_t *flags_register(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_FLAGS_ADDRESS;
}

static volatile uint16_t *enable_register(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_ENABLE_ADDRESS;
}

static void clear_string(void)
{
    volatile uint8_t *string = string_register();
    for (size_t i = 0; i < DEBUG_CHANNEL_STRING_SIZE; i++)
        string[i] = 0;
}

int debug_channel_init(FILE *out)
{
    debug_channel_shutdown();
    if (out == NULL) {
        fprintf(stderr, "debug_channel: debug_channel_init: no output stream\n");
        return DEBUG_CHANNEL_ERR_NO_STREAM;
    }
    if (gba_memmap_init() != GBA_MEMMAP_OK) {
        fprintf(stderr, "debug_channel: debug_channel_init: the mGBA debug page is not mapped\n");
        return DEBUG_CHANNEL_ERR_LAYOUT;
    }
    clear_string();
    *flags_register() = 0;
    *enable_register() = 0;
    channel.out = out;
    return DEBUG_CHANNEL_OK;
}

void debug_channel_shutdown(void)
{
    channel.out = NULL;
    channel.enabled = false;
    channel.reported_write_failure = false;
}

bool debug_channel_open(void)
{
    *enable_register() = DEBUG_CHANNEL_ENABLE_REQUEST;
    if (channel.out == NULL)
        return false;
    channel.enabled = true;
    *enable_register() = DEBUG_CHANNEL_ENABLE_ACK;
    return *enable_register() == DEBUG_CHANNEL_ENABLE_ACK;
}

/* mGBA: 1 << level, masked to 1Fh; printed if the hydra's -l15 lets it. */
static bool level_is_printed(uint16_t flags)
{
    unsigned level = (1u << (flags & DEBUG_CHANNEL_FLAGS_LEVEL_MASK)) & DEBUG_CHANNEL_LOG_LEVEL_MASK;
    return (level & DEBUG_CHANNEL_PRINTED_LEVELS) != 0;
}

/* The buffer up to its first NUL, at most DEBUG_CHANNEL_STRING_SIZE bytes. */
static size_t copy_message(char *message)
{
    const volatile uint8_t *string = string_register();
    size_t length = 0;
    while (length < DEBUG_CHANNEL_STRING_SIZE && string[length] != 0) {
        message[length] = (char)string[length];
        length++;
    }
    return length;
}

static void print_line(const char *message, size_t length)
{
    static const char prefix[] = DEBUG_CHANNEL_LINE_PREFIX;
    bool written = fwrite(prefix, 1, sizeof prefix - 1, channel.out) == sizeof prefix - 1
                   && fwrite(message, 1, length, channel.out) == length && fputc('\n', channel.out) != EOF;
    if (fflush(channel.out) != 0)
        written = false;
    if (!written && !channel.reported_write_failure) {
        perror("debug_channel: writing a message failed; it and any later failures are dropped");
        channel.reported_write_failure = true;
    }
}

void debug_channel_send(uint16_t flags)
{
    *flags_register() = flags;
    if (!channel.enabled || (flags & DEBUG_CHANNEL_FLAGS_SEND) == 0)
        return;
    char message[DEBUG_CHANNEL_STRING_SIZE];
    size_t length = copy_message(message);
    clear_string();
    if (level_is_printed(flags))
        print_line(message, length);
    *flags_register() = (uint16_t)(flags & ~DEBUG_CHANNEL_FLAGS_SEND);
}
