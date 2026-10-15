/*
 * test_crt0.c: the stand-in for src/crt0.s (crt0.h): IntrMain, the EWRAM and
 * game variable initial copies, and their restart.
 *
 * The expected behaviour comes from expansion's src/crt0.s and the linker
 * scripts (gba_regions.ld, gba_game.ld here).
 */
// deps: gba_memmap.c
#include <stdint.h>
#include <string.h>

#include "crt0.h"
#include "game_sections.h"
#include "gba_memmap.h"
#include "ptest.h"

static uintptr_t address_of(const volatile void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

PTEST(intr_main_is_0x800_bytes_of_data)
{
    PT_ASSERT(sizeof IntrMain == 0x800);
    for (size_t i = 0; i < sizeof IntrMain / sizeof IntrMain[0]; i++)
        PT_ASSERT_EQ_U32(IntrMain[i], 0);
}

/* An EWRAM_INIT variable (include/gba/defines.h): gba_regions.ld puts it at
 * the bottom of EWRAM with its initial value. A word: a COFF assembler pads
 * each section to its alignment, 4 bytes for data, so a byte's section would
 * be 4 bytes long on Windows and 1 on Linux. */
static TEST_EWRAM_INIT volatile uint32_t ewram_init_word = 0x5A;

PTEST(ewram_init_data_sits_at_the_bottom_of_ewram)
{
    PT_ASSERT(address_of(&ewram_init_word) == GBA_EWRAM_BASE);
    PT_ASSERT(crt0_ewram_init_size() == sizeof ewram_init_word);
}

PTEST(reinitialize_ewram_restores_the_initial_value)
{
    ReInitializeEWRAM(); /* before crt0_init: nothing to restore, nothing happens */
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK);
    ewram_init_word = 0x00; /* RegisterRamReset(RESET_EWRAM) */
    ReInitializeEWRAM();
    PT_ASSERT_EQ_INT(ewram_init_word, 0x5A);
    ewram_init_word = 0x77;
    crt0_restart();
    PT_ASSERT_EQ_INT(ewram_init_word, 0x5A);
    crt0_shutdown();
    ewram_init_word = 0x33;
    ReInitializeEWRAM(); /* after shutdown: nothing happens */
    PT_ASSERT_EQ_INT(ewram_init_word, 0x33);
    ewram_init_word = 0x5A;
    crt0_shutdown(); /* twice is safe */
}

/* A COMMON_DATA and an IWRAM_INIT variable (include/gba/defines.h):
 * gba_game.ld puts them in game_bss and game_data whatever object they come
 * from, as it does the game's. */
static TEST_COMMON_DATA volatile uint32_t game_bss_word;
static TEST_IWRAM_INIT volatile uint32_t game_data_word = UINT32_C(0x1234ABCD);

extern unsigned char game_data_start[], game_data_end[], game_bss_start[], game_bss_end[];

PTEST(game_variables_are_bracketed)
{
    PT_ASSERT(address_of(&game_bss_word) >= address_of(game_bss_start));
    PT_ASSERT(address_of(&game_bss_word) + sizeof game_bss_word <= address_of(game_bss_end));
    PT_ASSERT(address_of(&game_data_word) >= address_of(game_data_start));
    PT_ASSERT(address_of(&game_data_word) + sizeof game_data_word <= address_of(game_data_end));
    PT_ASSERT(crt0_game_bss_size() >= sizeof game_bss_word);
    PT_ASSERT(crt0_game_data_size() >= sizeof game_data_word);
}

PTEST(a_second_crt0_init_replaces_the_copy_and_leaks_nothing)
{
    /* The first copy of the game's initialised data is malloc'd: dropping the
     * shutdown at the top of crt0_init would leave it for LeakSanitizer. */
    PT_ASSERT(crt0_game_data_size() > 0);
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK);
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK);
    game_data_word = 7;
    crt0_restart();
    PT_ASSERT_EQ_U32(game_data_word, UINT32_C(0x1234ABCD));
    crt0_shutdown();
    game_data_word = UINT32_C(0x1234ABCD);
}

PTEST(crt0_restart_zeroes_game_bss_and_restores_game_data)
{
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK); /* not the game: no object check */
    game_bss_word = UINT32_C(0xDEADBEEF);
    game_data_word = 7;
    crt0_restart();
    PT_ASSERT_EQ_U32(game_bss_word, 0);
    PT_ASSERT_EQ_U32(game_data_word, UINT32_C(0x1234ABCD));
    crt0_shutdown();
    game_data_word = 9;
    crt0_restart(); /* after shutdown: nothing happens */
    PT_ASSERT_EQ_U32(game_data_word, 9);
    game_data_word = UINT32_C(0x1234ABCD);
}

/* What the test runner build defines (test/test_runner.c), at 64 bits: the
 * entry size, and the walk of the table that returns the index of the first
 * entry with a null name, filename or runner, or -1. Here the walk returns
 * what a test sets. */
const size_t gPlatformTestEntrySize = 40;
static int bad_test_table_entry = -1;

int PlatformTestTableBadEntry(void)
{
    return bad_test_table_entry;
}

PTEST(test_table_of_whole_entries_is_counted)
{
    size_t count = 99;
    PT_ASSERT_EQ_INT(crt0_check_test_table(6294 * 40, 40, &count), CRT0_OK);
    PT_ASSERT(count == 6294);
    PT_ASSERT_EQ_INT(crt0_check_test_table(0, 40, &count), CRT0_OK);
    PT_ASSERT(count == 0);
}

PTEST(test_table_with_padded_entries_is_refused)
{
    size_t count = 99;
    /* 64-byte spacing for 40-byte entries (GCC's 32-byte alignment of a
     * 40-byte global, no -malign-data=abi): the table is 6294 x 64 - 24. */
    PT_ASSERT_EQ_INT(crt0_check_test_table(6294 * 64 - 24, 40, &count), CRT0_ERR_BAD_TEST_TABLE);
    PT_ASSERT(count == 99);
    PT_ASSERT_EQ_INT(crt0_check_test_table(40, 0, &count), CRT0_ERR_BAD_TEST_TABLE);
    PT_ASSERT_EQ_INT(crt0_check_test_table(40, 40, NULL), CRT0_ERR_BAD_TEST_TABLE);
}

PTEST(test_table_shorter_than_one_entry_or_one_byte_over_is_refused)
{
    size_t count = 99;
    PT_ASSERT_EQ_INT(crt0_check_test_table(39, 40, &count), CRT0_ERR_BAD_TEST_TABLE);
    PT_ASSERT_EQ_INT(crt0_check_test_table(41, 40, &count), CRT0_ERR_BAD_TEST_TABLE);
    PT_ASSERT_EQ_INT(crt0_check_test_table(SIZE_MAX, 40, &count), CRT0_ERR_BAD_TEST_TABLE);
    PT_ASSERT_EQ_INT(crt0_check_test_table(SIZE_MAX, 1, &count), CRT0_OK);
    PT_ASSERT(count == SIZE_MAX);
    PT_ASSERT_EQ_INT(crt0_check_test_table(0, 0, &count), CRT0_ERR_BAD_TEST_TABLE);
}

PTEST(crt0_init_checks_the_empty_test_table_of_a_test_runner_link)
{
    /* gba_game.ld's .tests is empty here: 0 tests of 40 bytes. */
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK);
    crt0_shutdown();
}

PTEST(crt0_init_refuses_a_test_table_whose_entries_are_padded_and_keeps_no_copy)
{
    ewram_init_word = 0x5A;
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_OK); /* an earlier good init, dropped by the next */
    bad_test_table_entry = 0;               /* the first entry; the index only goes into the report */
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_ERR_BAD_TEST_TABLE);
    bad_test_table_entry = 1000;
    PT_ASSERT_EQ_INT(crt0_init(), CRT0_ERR_BAD_TEST_TABLE);
    /* No copy is in use after the failure: a restart changes nothing. */
    ewram_init_word = 0x77;
    game_data_word = 7;
    crt0_restart();
    ReInitializeEWRAM();
    PT_ASSERT_EQ_INT(ewram_init_word, 0x77);
    PT_ASSERT_EQ_U32(game_data_word, 7);
    bad_test_table_entry = -1;
    ewram_init_word = 0x5A;
    game_data_word = UINT32_C(0x1234ABCD);
    crt0_shutdown(); /* safe after a failed init */
}

PTEST_MAIN()
