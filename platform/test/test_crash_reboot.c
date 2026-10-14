/*
 * test_crash_reboot.c: the reboot after a crash (crash_reboot.h).
 *
 * Expected behaviour from expansion's test runner (test/test_runner.c: the
 * 100h bytes at 03007F00h must come back after the reboot, and a reboot
 * that makes no progress would loop) and GBATEK "BIOS Reset Functions"
 * (RegisterRamReset keeps the last 200h bytes of IWRAM).
 *
 * The state's restore and the argument checks are here. The reboots
 * themselves are real and need a fork: posix/test_crash_reboot_posix.c.
 */
// deps: gba_memmap.c
#include <string.h>

#include "crash_reboot.h"
#include "crash_reboot_fixture.h"
#include "gba_memmap.h"
#include "ptest.h"

PTEST(restore_puts_the_bytes_back)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    clear_region();
    int result = crash_reboot_restore(hex_of(pattern_b));
    bool restored = holds(pattern_b);
    clear_region();
    PT_ASSERT_EQ_INT(result, CRASH_REBOOT_OK);
    PT_ASSERT(restored);
}

PTEST(restore_rejects_a_wrong_length_or_a_non_hex_digit)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    clear_region();
    char text[CRASH_REBOOT_STATE_HEX_LENGTH + 2];
    memcpy(text, hex_of(pattern_a), sizeof state_text);
    int too_short = crash_reboot_restore("00ff");
    text[CRASH_REBOOT_STATE_HEX_LENGTH] = '0';
    text[CRASH_REBOOT_STATE_HEX_LENGTH + 1] = '\0';
    int too_long = crash_reboot_restore(text);
    text[CRASH_REBOOT_STATE_HEX_LENGTH] = '\0';
    text[CRASH_REBOOT_STATE_HEX_LENGTH - 1] = 'g';
    int not_hex = crash_reboot_restore(text);
    text[CRASH_REBOOT_STATE_HEX_LENGTH - 1] = 'A';
    int upper_case = crash_reboot_restore(text);
    int null_text = crash_reboot_restore(NULL);
    bool untouched = true;
    for (size_t i = 0; i < CRASH_REBOOT_PERSISTENT_SIZE; i++)
        untouched = untouched && region()[i] == 0;
    PT_ASSERT_EQ_INT(too_short, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(too_long, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(not_hex, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(upper_case, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT_EQ_INT(null_text, CRASH_REBOOT_ERR_STATE);
    PT_ASSERT(untouched);
}

PTEST(init_rejects_no_arguments_and_too_many)
{
    static char *many[CRASH_REBOOT_MAX_ARGS];
    for (size_t i = 0; i < CRASH_REBOOT_MAX_ARGS; i++)
        many[i] = (char *)self_name;
    char *one[] = {(char *)self_name, NULL};
    PT_ASSERT_EQ_INT(crash_reboot_init(0, one), CRASH_REBOOT_ERR_ARGS);
    PT_ASSERT_EQ_INT(crash_reboot_init(1, NULL), CRASH_REBOOT_ERR_ARGS);
    /* The reboot adds two: CRASH_REBOOT_MAX_ARGS - 2 of the caller's fit. */
    PT_ASSERT_EQ_INT(crash_reboot_init((int)CRASH_REBOOT_MAX_ARGS - 1, many), CRASH_REBOOT_ERR_ARGS);
    PT_ASSERT_EQ_INT(crash_reboot_init((int)CRASH_REBOOT_MAX_ARGS - 2, many), CRASH_REBOOT_OK);
    crash_reboot_shutdown();
}

PTEST_MAIN()
