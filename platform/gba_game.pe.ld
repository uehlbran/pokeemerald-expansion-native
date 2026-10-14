/*
 * gba_game.pe.ld: gba_game.ld for the Windows (PE, x86-64) executable: what
 * expansion's linker scripts give the game, as a fragment used with
 * gba_regions.pe.ld (which places the GBA regions); Makefile.native links the
 * game with the flags of platform/target.mk and
 *   -Wl,-T,platform/gba_regions.pe.ld -Wl,-T,platform/gba_game.pe.ld
 * and defines gInitialMainCB2 itself (--defsym), because its value depends
 * on TEST. The same link also defines __executable_start as an alias of
 * __ImageBase (--defsym, platform/target.mk): src/malloc.c measures the
 * heap's allocation sites from it, GNU ld defines it for ELF, and a PE image
 * starts at __ImageBase.
 *
 * Followed: ld_script_modern.ld and ld_script_test.ld, through gba_game.ld,
 * which says why each piece exists; only the differences are said here.
 *  - MinGW's default script names the read-only data .rdata (ELF: .rodata).
 *  - A PE section name has 8 characters, so the output sections game_data
 *    and game_bss are .gamedat and .gamebss; the symbols around them keep
 *    their names.
 *  - No (NOLOAD): in PE it marks a section discardable. A section made of
 *    uninitialised input only is not stored in the file anyway.
 *  - Each section starts at a multiple of the section alignment, which the
 *    loader requires (BLOCK, as the default script has it).
 *  - A COFF weak external is not a definition (below).
 *  - The image's writable sections come last, after its read-only ones: the
 *    default script puts .data before .rdata, .pdata and .xdata and .bss
 *    after them, so this script moves every initialised data input after
 *    .xdata (below), and the program's mutable data is one stretch from
 *    __data_start__ to __bss_end__, as an ELF image's is from __data_start to
 *    _end (host_image.h's writable part, host_windows.c).
 * The layout the loader accepted: research/probes/windows_layout.
 */

/* Symbols whose address is their value, as in gba_game.ld: four music players
 * and no PSG line limit (include/gba/m4a_internal.h). Absolute values need an
 * image without base relocations (target.mk). */
gNumMusicPlayers = 4;
gMaxLines = 0;

/* What expansion and crt0.c define or use weakly, which a PE link cannot do:
 * a COFF weak external is a reference with a fallback, not a definition the
 * linker offers to other objects' references (binutils 2.41), so the
 * references below stay undefined. Each is given what the ELF link gives it,
 * and what only a test runner build defines is left to its object, which
 * defines it strongly.
 *  - src/random.c's RandomUniform, RandomUniformExcept, RandomWeightedArray
 *    and RandomElementArray are aliases of their ...Default functions that
 *    the test runner may replace.
 *  - src/test_runner_stub.c's gTestRunnerEnabled is FALSE in the game; the
 *    byte is borrowed from gTestRunnerSkipIsFail, the next constant that file
 *    defines and also FALSE (test/test_runner_args.c defines the real one in
 *    the test runner build, where this would not be used).
 *  - platform/crt0.c's references to gPlatformTestEntrySize and
 *    PlatformTestTableBadEntry are null unless the test runner defines them. */
PROVIDE(RandomUniform = RandomUniformDefault);
PROVIDE(RandomUniformExcept = RandomUniformExceptDefault);
PROVIDE(RandomWeightedArray = RandomWeightedArrayDefault);
PROVIDE(RandomElementArray = RandomElementArrayDefault);
PROVIDE(gTestRunnerEnabled = gTestRunnerSkipIsFail);
PROVIDE(gPlatformTestEntrySize = 0);
PROVIDE(PlatformTestTableBadEntry = 0);

/* The same values as gba_game.ld, for the same reasons: the ROM's start for
 * src/debug.c's CheckROMSize and the end of IWRAM for src/assertf.c's
 * estimate of the free stack. */
__rom_end = 0x08000000;
__iwram_end = gba_linked_iwram_end;

/* The test runner's table of tests, from __start_tests to __stop_tests, the
 * sanity checks of test/test_test_runner.c first (gba_game.ld says why the
 * symbols are defined here). Empty, and discarded, without TEST=1; the
 * section would be .tests, and a PE name has 8 characters. */
SECTIONS
{
    tests BLOCK(__section_alignment__) :
    {
        __start_tests = .;
        KEEP(*test_test_runner.o(.tests))
        KEEP(*(.tests))
        __stop_tests = .;
    }
}
INSERT AFTER .rdata;

/* Where the program's own image starts: its code. Below it are the image's
 * headers, the battle runner's stack and the GBA regions with the gaps between
 * them (gba_regions.pe.ld), none of them the program's code or data
 * (host_image.h, host_windows.c). */
__text_start__ = ADDR(.text);

/* The game's own mutable data, bracketed so that platform/crt0.c can do what
 * src/crt0.s does when SoftReset restarts the ROM (gba_game.ld says why and
 * how the game's objects are told from the platform's). The same path
 * patterns select the C objects' sections; the data objects' sections are
 * named game_data and game_bss already, because the converter that makes
 * their COFF listings (platform/build_glue/elf_to_flat.py) brackets them
 * itself. Outside both brackets, as in gba_game.ld: script_data. */
PROVIDE(gba_game_objects_linked = 0);

/* The initialised data, after the read-only sections (above): first the
 * game's, then everyone else's, which the default script's .data would hold
 * before .rdata. A pattern here takes an input section before the default
 * script's do, so that .data is left empty and dropped; its __data_start__
 * and __data_end__ are defined again around what it held, or they would be
 * left undefined in the symbol table. */
SECTIONS
{
    .gamedat BLOCK(__section_alignment__) :
    {
        game_data_start = .;
        *native*/src/*.o(.data .data.*)
        *native*/data/*.o(.data .data.* game_data)
        *native*/sound/*.o(.data .data.* game_data)
        *native*/test/*.o(.data .data.*)
        *(.iwram)
        game_data_end = .;
    }
    .rwdata BLOCK(__section_alignment__) :
    {
        __data_start__ = .;
        *(.data .data.* .data2 SORT(.data$*))
        KEEP(*(.jcr))
        __data_end__ = .;
        *(.data_cygwin_nocopy)
    }
    script_data BLOCK(__section_alignment__) :
    {
        *(script_data)
    }
}
INSERT BEFORE .bss;

/* game_bss keeps the GBA's order, .bss then COMMON_DATA (gba_game.ld says
 * why). The COFF assembler stores common_data's zeros in the file, so unlike
 * the ELF section this one is stored. */
SECTIONS
{
    .gamebss BLOCK(__section_alignment__) :
    {
        game_bss_start = .;
        *native*/src/*.o(.bss .bss.*)
        *native*/data/*.o(.bss .bss.* game_bss)
        *native*/sound/*.o(.bss .bss.* game_bss)
        *native*/test/*.o(.bss .bss.*)
        game_bss_objects_end = .;
        *(common_data)
        game_bss_end = .;
    }
}
INSERT BEFORE .bss;
