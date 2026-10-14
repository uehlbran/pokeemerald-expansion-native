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
