/*
 * game_sections.h: expansion's section markers (include/gba/defines.h), as a
 * unit test puts its own variable where the game's go: the EWRAM_INIT and
 * EWRAM_DATA parts of EWRAM, and the game's initialised and zero-initialised
 * data that a soft reset restores (IWRAM_INIT, COMMON_DATA).
 *
 * The game's objects get these names from expansion's macros, and on macOS
 * through the C filter (build_glue/c_filter_macho.sed); a unit test is not
 * compiled through either, so it spells them here, once per object format.
 * ELF and COFF take expansion's names, which the linker scripts place
 * (gba_regions.ld, gba_game.ld and their .pe.ld twins). Mach-O names a
 * section "segment,section" and has no scripts: the sections are those
 * gba_regions.macho.s and gba_game.macho.s place.
 *
 * Each variable must be its section's bytes alone, as the game's are:
 * GCC's AddressSanitizer leaves a variable with a section of its own
 * unpadded, clang's pads it unless told not to (no_sanitize, which GCC does
 * not take on a variable).
 */
#ifndef PLATFORM_TEST_GAME_SECTIONS_H
#define PLATFORM_TEST_GAME_SECTIONS_H

#if defined(__clang__)
#define TEST_UNPADDED __attribute__((no_sanitize("address")))
#else
#define TEST_UNPADDED
#endif

#if defined(__MACH__)
#define TEST_EWRAM_INIT __attribute__((section("__GBA_EWRAM,__ewram_init"), used)) TEST_UNPADDED
#define TEST_EWRAM_DATA __attribute__((section("__GBA_EWRAM,__ewram_data,zerofill"))) TEST_UNPADDED
#define TEST_IWRAM_INIT __attribute__((section("__DATA,__game_data"), used)) TEST_UNPADDED
#define TEST_COMMON_DATA __attribute__((section("__DATA,__game_common,zerofill"), used)) TEST_UNPADDED
#else
#define TEST_EWRAM_INIT __attribute__((section(".ewram"), used)) TEST_UNPADDED
#define TEST_EWRAM_DATA __attribute__((section(".sbss"))) TEST_UNPADDED
#define TEST_IWRAM_INIT __attribute__((section(".iwram"), used)) TEST_UNPADDED
#define TEST_COMMON_DATA __attribute__((section("common_data"), used)) TEST_UNPADDED
#endif

#endif /* PLATFORM_TEST_GAME_SECTIONS_H */
