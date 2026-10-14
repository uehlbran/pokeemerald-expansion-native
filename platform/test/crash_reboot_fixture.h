/*
 * crash_reboot_fixture.h: what test_crash_reboot.c and
 * posix/test_crash_reboot_posix.c share: the persistent region as the game
 * sees it, two byte patterns to put in it, and the state as the reboot passes
 * it on the command line. Static, so that a binary need not use them all.
 */
#ifndef PLATFORM_TEST_CRASH_REBOOT_FIXTURE_H
#define PLATFORM_TEST_CRASH_REBOOT_FIXTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "crash_reboot.h"

static inline volatile uint8_t *region(void)
{
    return (volatile uint8_t *)(uintptr_t)CRASH_REBOOT_PERSISTENT_ADDRESS;
}

static inline uint8_t pattern_a(size_t i)
{
    return (uint8_t)(i ^ 0x5Au);
}
static inline uint8_t pattern_b(size_t i)
{
    return (uint8_t)(i * 7u + 1u);
}

static inline void fill(uint8_t (*pattern)(size_t))
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        region()[i] = pattern(i);
}

static inline bool holds(uint8_t (*pattern)(size_t))
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        if (region()[i] != pattern(i))
            return false;
    }
    return true;
}

static inline void clear_region(void)
{
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        region()[i] = 0;
}

/* A call through a NULL function pointer, as "Tests resume after CRASH". */
static void (*volatile null_function)(void);

static inline void crash_by_null_call(void)
{
    null_function();
}

static char state_text[CRASH_REBOOT_STATE_HEX_LENGTH + 1] __attribute__((unused));

static inline const char *hex_of(uint8_t (*pattern)(size_t))
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++) {
        state_text[2 * i] = digits[pattern(i) >> 4];
        state_text[2 * i + 1] = digits[pattern(i) & 0x0Fu];
    }
    state_text[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
    return state_text;
}

static const char *self_name __attribute__((unused)) = "test_crash_reboot";

#endif /* PLATFORM_TEST_CRASH_REBOOT_FIXTURE_H */
