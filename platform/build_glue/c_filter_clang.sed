# c_filter.sed for clang on Linux (ELF), the compiler the macOS build uses,
# run on Linux to rehearse that build (docs/BUILDING.md, "macOS"). Build glue;
# it stands in for no GBA hardware. The first rule is c_filter.sed's; the
# others are c_filter_macho.sed's, for the same reasons, with ELF's section
# names. GCC needs none of them, so its filter stays as it is. POSIX sed only.
#
# preproc's COMPOUND_STRING sections: see c_filter.sed.
s/\(\.rodata\.compound_string\.[0-9a-f]*,\\"aM\\",%progbits,[0-9]*\) @"/\1 #"/g
#
# src/libisagbprn.c stops on a failed assertion with an undefined Thumb
# instruction, ".hword 0xEFFF", which clang's assembler does not take for x86;
# .2byte writes the same two bytes.
s/asm("\.hword \(0x[0-9A-Fa-f]*\)")/asm(".2byte \1")/g
#
# AddressSanitizer: GCC's leaves every variable with a section of its own
# alone; clang's pads each with red zones, and EWRAM_DATA's 228 KiB then
# overflow the 256 KiB (gba_regions.ld) and the test table is no longer an
# array of whole entries (platform/crt0.c). The variables of the sections
# expansion names are left alone here too. Functions with a section of their
# own (.iwram.code) keep their checks.
s/section("\.sbss")/section(".sbss"), no_sanitize("address")/g
s/section("\.ewram")/section(".ewram"), no_sanitize("address")/g
s/section("\.persistent")/section(".persistent"), no_sanitize("address")/g
s/section("common_data")/section("common_data"), no_sanitize("address")/g
s/section("\.tests")/section(".tests"), no_sanitize("address")/g
s/section("\.bss")/section(".bss"), no_sanitize("address")/g
s/section("\.bss\.code")/section(".bss.code"), no_sanitize("address")/g
s/section("\.iwram")/section(".iwram"), no_sanitize("address")/g
s/section("\.text\.consts")/section(".text.consts"), no_sanitize("address")/g
s/section("\.text\.header_rhh")/section(".text.header_rhh"), no_sanitize("address")/g
s/section("\.text\.header_gf")/section(".text.header_gf"), no_sanitize("address")/g
