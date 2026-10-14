# c_filter.sed for the Windows build (COFF object files). Build glue; it stands
# in for no GBA hardware.
#
# preproc emits each COMPOUND_STRING as an array in a section named
#   .rodata.compound_string.<hash>,\"aM\",%progbits,<size> @
# (see c_filter.sed). The flags in that name are ELF's, mergeable strings with
# an entry size, and MinGW's assembler does not know them, so the strings go
# to .rdata, COFF's read-only data. Nothing is lost: the game does not compare
# the addresses of two equal strings.
s/\.rodata\.compound_string\.[0-9a-f]*,\\"aM\\",%progbits,[0-9]* @"/.rdata"/g
#
# include/test/overworld_script.h (the TEST=1 objects) puts each script in
# .rodata.overworld_script between .pushsection and .popsection, which only
# ELF's assembler has; COFF's has no section stack and no .previous to return
# to the section gcc was in (a function's cold part is in another). So here
# the script stays in the function's own section, read-only code that the
# script engine reads as data, and the code jumps over it; %= numbers the
# label per asm statement (gcc's extended asm), since the event macros use
# the numeric labels themselves.
s/"\.pushsection \.rodata\.overworld_script, \\"a\\"\\n"/"jmp .Loverworld_script_end%=\\n"/g
s/"\.popsection\\n" "mov \$1b, %0\\n"/".Loverworld_script_end%=:\\n" "mov $1b, %0\\n"/g
