/*
 * gba_game.macho.s: gba_game.ld for the macOS (Mach-O, x86_64) executable:
 * what expansion's linker scripts give the game, as an object linked right
 * after gba_regions.macho.s, before everything else (gba_regions.macho.s says
 * why the order of the objects matters to ld64, which has no scripts).
 * Followed: ld_script_modern.ld and ld_script_test.ld, through gba_game.ld,
 * which says why each piece exists; only the differences are said here.
 * What a script would define from a section's bounds, gba_game.macho.aliases
 * names, a list the link hands ld64 with -alias_list (platform/target.mk):
 * __iwram_end, the test table's __start_tests and __stop_tests,
 * PlatformAllocationSiteBase (the image's header, at 0x40000000), and the
 * brackets around the game's variables.
 *
 * Symbols whose address is their value, as gba_game.ld defines them: four
 * music players, no PSG line limit, and the ROM's start as the end of the ROM
 * the debug menu measures. An absolute symbol needs the non-PIE image.
 */
        .globl _gNumMusicPlayers
        .set _gNumMusicPlayers, 4
        .globl _gMaxLines
        .set _gMaxLines, 0
        .globl ___rom_end
        .set ___rom_end, 0x08000000

/*
 * gba_game_objects_linked (crt0.h) is 1 in the game, where Makefile.native
 * gives this symbol that name with -alias, as GNU ld's --defsym does on Linux,
 * and a weak reference left undefined (-U, target.mk), so null, elsewhere.
 */
        .globl _gba_game_objects_linked_in_the_game
        .set _gba_game_objects_linked_in_the_game, 1

/*
 * The game's variables, bracketed for a soft reset as gba_game.ld brackets
 * them: on Mach-O by section, since ld64 cannot select sections by the
 * object's path. The C filter (build_glue/c_filter_macho.sed) puts the
 * game's initialised data in __DATA,__game_data and its zero-initialised
 * data in __DATA,__game_bss (a #pragma clang section on every game object),
 * and expansion's COMMON_DATA in __DATA,__game_common; the converted data
 * objects name theirs the same (build_glue/elf_to_flat.py). game_bss keeps
 * the GBA's order, .bss then COMMON_DATA (gba_game.ld says why): ld64 orders
 * the zero-filled sections of a segment as it first meets each with something
 * in it, so one byte in each, here, fixes the order whatever the first game
 * object holds. The two bytes are zeroed with the rest at a soft reset.
 */
        .zerofill __DATA,__game_bss,gba_game_bss_first,1,0
        .zerofill __DATA,__game_common,gba_game_common_first,1,0

/*
 * The sections whose bounds the alias list names but which an executable may
 * have nothing in (a unit test has no test table and no game data), and
 * __DATA's own __data, whose start host_macos.c reads: declared here, empty,
 * so that each exists in every link. An empty declaration does not decide
 * a section's place (probe M2), so the order above stands.
 */
        .section __DATA,__tests
        .section __DATA,__game_data
        .section __DATA,__data
