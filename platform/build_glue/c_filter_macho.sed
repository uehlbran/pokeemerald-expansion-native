# c_filter.sed for the macOS build (Mach-O object files). Build glue; it stands
# in for no GBA hardware. It rewrites the preprocessed text of every game
# object (expansion's src/ and test/), so that expansion's own files stay as
# they are; each rule says what Mach-O lacks. POSIX sed only (no \| and no
# one-line i command): macOS's sed is BSD's.
#
# The game's own variables, bracketed for a soft reset (gba_game.ld on Linux
# selects them by the object's path, which ld64 cannot): every zero-initialised
# variable of a game object goes to __DATA,__game_bss and every initialised one
# to __DATA,__game_data, which platform/gba_game.macho.s orders and the link
# names game_bss_start and game_data_start (platform/target.mk). A variable with
# a section of its own keeps it.
1i\
#pragma clang section bss = "__DATA,__game_bss,zerofill" data = "__DATA,__game_data"
#
# preproc emits each COMPOUND_STRING as an array in a section named
#   .rodata.compound_string.<hash>,\"aM\",%progbits,<size> @
# (see c_filter.sed): ELF's mergeable strings, which Mach-O names otherwise. The
# strings go to __TEXT,__const, the read-only data; the game does not compare
# the addresses of two equal strings.
s/\.rodata\.compound_string\.[0-9a-f]*,\\"aM\\",%progbits,[0-9]* @"/__TEXT,__const"/g
#
# Expansion's section markers (include/gba/defines.h, include/test/test.h,
# include/test/battle.h, test/test_runner.c, src/m4a.c, src/decompress.c,
# src/rom_header_rhh.c, src/rom_header_gf.c): Mach-O names a section
# "segment,section". The GBA regions' and the test table's sections are those
# platform/gba_regions.macho.s and gba_game.macho.s place, and their variables
# are left alone by AddressSanitizer, as GCC's leaves every variable with a
# section of its own, so that EWRAM_DATA's 228 KiB keep their GBA layout in the
# 256 KiB and the test table is an array of whole entries (platform/crt0.c).
s/section("\.tests")/section("__DATA,__tests"), no_sanitize("address")/g
s/section("\.sbss")/section("__GBA_EWRAM,__ewram_data,zerofill"), no_sanitize("address")/g
s/section("\.ewram")/section("__GBA_EWRAM,__ewram_init"), no_sanitize("address")/g
s/section("\.persistent")/section("__GBA_IWRAM,__persistent,zerofill"), no_sanitize("address")/g
s/section("common_data")/section("__DATA,__game_common,zerofill"), no_sanitize("address")/g
# IWRAM_DATA (.bss), BSS_CODE (.bss.code) and IWRAM_INIT (.iwram) are the
# game's own zero-initialised and initialised data, where gba_game.ld's
# brackets put them on Linux: here the attribute goes and the first line's
# pragma places them (clang refuses an explicit section of the pragma's name).
s/__attribute__((section("\.bss")))//g
s/__attribute__((section("\.bss\.code")))//g
s/__attribute__((section("\.iwram")))//g
# Read-only data in the ROM's code sections (the flash and RTC library
# versions, the ROM headers), and a function the GBA copies to IWRAM, which
# runs where it is natively.
s/section("\.text\.consts")/section("__TEXT,__const"), no_sanitize("address")/g
s/section("\.text\.header_rhh")/section("__TEXT,__const"), no_sanitize("address")/g
s/section("\.text\.header_gf")/section("__TEXT,__const"), no_sanitize("address")/g
s/section("\.iwram\.code")/section("__TEXT,__text")/g
#
# src/random.c makes RandomUniform and three others weak aliases of their
# ...Default functions, which the test runner replaces (test/test_runner.c).
# Mach-O has no aliases in C ("aliases are not supported on darwin"): the
# attribute goes, leaving a declaration, and the link names the defaults so
# where the test runner is not linked (Makefile.native, -alias).
s/__attribute__((weak, alias("[A-Za-z0-9_]*")))//g
#
# src/libisagbprn.c stops on a failed assertion with an undefined Thumb
# instruction, ".hword 0xEFFF", which clang's assembler does not take for x86;
# .2byte writes the same two bytes.
s/asm("\.hword \(0x[0-9A-Fa-f]*\)")/asm(".2byte \1")/g
