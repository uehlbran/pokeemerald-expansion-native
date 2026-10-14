/*
 * debug_channel_fixture.h: what test_debug_channel.c and
 * posix/test_debug_channel_posix.c share: the debug registers as the game
 * writes them, messages put into them the way test/test_runner.c's
 * MgbaPutchar_ writes them, and a channel open on a stream. Static, so that
 * a binary need not use them all.
 */
#ifndef PLATFORM_TEST_DEBUG_CHANNEL_FIXTURE_H
#define PLATFORM_TEST_DEBUG_CHANNEL_FIXTURE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debug_channel.h"
#include "gba_memmap.h"
#include "test_support.h"

/* include/gba/io_reg.h MGBA_LOG_INFO, as the runner sends it. */
#define LOG_INFO UINT16_C(3)
#define LOG_DEBUG UINT16_C(4)
#define SEND_INFO (uint16_t)(DEBUG_CHANNEL_FLAGS_SEND | LOG_INFO)

#define PREFIX_LENGTH (sizeof DEBUG_CHANNEL_LINE_PREFIX - 1)
#define CAPTURE_SIZE 1024u

static inline volatile char *debug_string(void)
{
    return (volatile char *)(uintptr_t)DEBUG_CHANNEL_STRING_ADDRESS;
}

static inline volatile uint16_t *debug_flags(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_FLAGS_ADDRESS;
}

static inline volatile uint16_t *debug_enable(void)
{
    return (volatile uint16_t *)(uintptr_t)DEBUG_CHANNEL_ENABLE_ADDRESS;
}

/* As MgbaPutchar_ and MgbaVPrintf_: the bytes, then a NUL after them unless
 * they fill the buffer. */
static inline void put_message(const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; i++)
        debug_string()[i] = bytes[i];
    if (length < DEBUG_CHANNEL_STRING_SIZE)
        debug_string()[length] = '\0';
}

static inline void clear_registers(void)
{
    for (size_t i = 0; i < DEBUG_CHANNEL_STRING_SIZE; i++)
        debug_string()[i] = 0;
    *debug_flags() = 0;
    *debug_enable() = 0;
}

/* A channel open on a fresh temporary file. */
static inline FILE *open_channel(void)
{
    FILE *out = tsup_open_temp_stream();
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
static inline size_t read_back(FILE *out, char *buffer, size_t size)
{
    long end = ftell(out);
    if (end < 0 || fseek(out, 0, SEEK_SET) != 0)
        return 0;
    size_t length = fread(buffer, 1, size, out);
    (void)fseek(out, end, SEEK_SET);
    return length;
}

static inline void close_channel(FILE *out)
{
    debug_channel_shutdown();
    clear_registers();
    if (out != NULL)
        (void)fclose(out);
}

static inline bool buffer_is_zero(void)
{
    for (size_t i = 0; i < DEBUG_CHANNEL_STRING_SIZE; i++) {
        if (debug_string()[i] != 0)
            return false;
    }
    return true;
}

#endif /* PLATFORM_TEST_DEBUG_CHANNEL_FIXTURE_H */
