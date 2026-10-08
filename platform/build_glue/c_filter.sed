# Rewrites tools/preproc's C output for the host compiler. Build glue; it
# stands in for no GBA hardware.
#
# preproc emits each COMPOUND_STRING as an array in a section named
#   .rodata.compound_string.<hash>,\"aM\",%progbits,<size> @
# gcc copies a section name into its assembly verbatim and appends its own
# flags; the trailing " @" turns those into an ARM assembler comment, which
# leaves the section mergeable (SHF_MERGE) with an entry size of <size>.
# '@' is not a comment on x86, where '#' is, so the '@' becomes '#' and the
# section keeps the same flags. See tools/preproc/c_file.cpp.
s/\(\.rodata\.compound_string\.[0-9a-f]*,\\"aM\\",%progbits,[0-9]*\) @"/\1 #"/g
