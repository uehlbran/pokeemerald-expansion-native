/*
 * gba_game.ld: what expansion's linker scripts give the game, for the native
 * executable. A GNU ld fragment used with gba_regions.ld (which places the
 * GBA regions, EWRAM_INIT, EWRAM_DATA and the test runner's .persistent
 * state); Makefile.native links the game with
 *   -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,platform/gba_regions.ld -Wl,-T,platform/gba_game.ld
 * and defines gInitialMainCB2 itself (--defsym), because its value depends
 * on TEST.
 *
 * Followed: ld_script_modern.ld and ld_script_test.ld.
 */

/* Symbols whose address is their value (include/gba/m4a_internal.h:
 * NUM_MUSIC_PLAYERS is (u16)gNumMusicPlayers, MAX_LINES (u32)gMaxLines), as
 * the GBA scripts define them: four music players, the entries of gMPlayTable
 * in sound/music_player_table.inc, and no PSG line limit. Absolute values
 * need the non-PIE link. */
gNumMusicPlayers = 4;
gMaxLines = 0;

/* src/debug.c's CheckROMSize reports __rom_end - ROM_START as the ROM used.
 * There is no ROM image natively, so it is the ROM's start (GBATEK "GBA
 * Memory Map": 08000000h): the debug menu shows 0 KiB used. */
__rom_end = 0x08000000;

/* src/assertf.c's crash screen estimates its free stack as the frame address
 * minus __iwram_end, the end of the GBA's IWRAM data below its stack. The
 * native stack is the host's, far above the IWRAM region, so the end of the
 * region makes the estimate large and the crash screen takes its backup on
 * the stack, which the host's stack has room for. */
__iwram_end = gba_linked_iwram_end;

/* The test runner walks the tests as one array of struct Test from
 * __start_tests to __stop_tests (test/test_runner.c), the sanity checks of
 * test/test_test_runner.c first, as ld_script_test.ld orders them. GCC does
 * not pad variables with an explicit section (not even for ASan), and each
 * object's .tests is a whole number of entries at the struct's alignment, so
 * the entries are contiguous. GNU ld does not define __start_/__stop_ for
 * ".tests", which is not a C identifier, so they are defined here. Empty, and
 * discarded, without TEST=1. */
SECTIONS
{
    tests :
    {
        __start_tests = .;
        KEEP(*test_test_runner.o(.tests))
        KEEP(*(.tests))
        __stop_tests = .;
    }
}
INSERT AFTER .rodata;

/* The game's own mutable data, bracketed so that platform/crt0.c can do what
 * src/crt0.s does when SoftReset restarts the ROM: on the GBA every game
 * variable is in IWRAM or EWRAM, which crt0's RegisterRamReset(RESET_ALL)
 * zeroes before it copies the initialised data back. Natively the game's
 * variables are in the executable's .data and .bss, beside the platform's,
 * which must survive the reset. So the game's initialised data (in the
 * sanitized build that includes every instrumented global: AddressSanitizer
 * moves them to .data with their redzones) goes in game_data, of which crt0
 * keeps a copy, and its zero-initialised data in game_bss, which crt0
 * zeroes.
 *
 * Which data is the game's: the .data and .bss of the objects under src/,
 * data/, sound/ and test/ of Makefile.native's object directories
 * (build/native<bits>[-test][-san]/; the platform's come from
 * libplatform.a, and its unit tests' objects from platform/build/), and,
 * from any object, the sections only expansion's markers produce: .iwram
 * (IWRAM_INIT) and common_data (COMMON_DATA), which also lets a unit test
 * place a variable in each bracket. If the object paths ever stop matching,
 * the game's .bss would silently stay out of game_bss; game_bss_objects_end
 * marks the end of the part the paths select, and crt0_init fails when that
 * part is empty in the game executable (gba_game_objects_linked, set by
 * Makefile.native, says it is the game).
 *
 * Both go before the default script's .data and .bss (INSERT BEFORE), whose
 * catch-all patterns would otherwise take the game's sections first;
 * transfer_bounds counts game_data_start as the start of the writable
 * globals for that reason.
 *
 * game_bss keeps the GBA's order, .bss then COMMON_DATA: the m4a engine
 * breaks channel-stealing ties by track address, and its tracks are in .bss
 * (sound/music_player_table.inc) and common_data (gPokemonCryTracks). Every
 * COMMON_DATA definition is zero, and the GBA scripts do not load the
 * section, so it is not loaded here either.
 *
 * Outside both brackets: script_data (data/*.s, 1.2 MiB of event, battle
 * and AI scripts, flagged "aw" by expansion's sources). On the GBA it is in
 * ROM, where a write does nothing, so the game cannot depend on writing it
 * and never does; a soft reset has nothing to restore there. (Checked: after
 * 9000 frames of the intro and title screen its bytes equal the file's.) */
PROVIDE(gba_game_objects_linked = 0);

SECTIONS
{
    game_data :
    {
        game_data_start = .;
        *native*/src/*.o(.data .data.*)
        *native*/data/*.o(.data .data.*)
        *native*/sound/*.o(.data .data.*)
        *native*/test/*.o(.data .data.*)
        *(.iwram)
        game_data_end = .;
    }
}
INSERT BEFORE .data;

SECTIONS
{
    game_bss (NOLOAD) :
    {
        game_bss_start = .;
        *native*/src/*.o(.bss .bss.*)
        *native*/data/*.o(.bss .bss.*)
        *native*/sound/*.o(.bss .bss.*)
        *native*/test/*.o(.bss .bss.*)
        game_bss_objects_end = .;
        *(common_data)
        game_bss_end = .;
    }
}
INSERT BEFORE .bss;
