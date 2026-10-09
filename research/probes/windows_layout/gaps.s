# gaps.s: the second variant of probe W1. One uninitialised input section per gap
# between the sections of layout B, from the end of the headers (0x01801000) to the
# code (0x40001000). build.sh makes each one read-only with
# objcopy --set-section-flags NAME=alloc,readonly (the assembler cannot express
# "uninitialised and not writable"), so the image has no hole in its address range.
# Each size is the next section's address minus the end of the previous one
# (layout_b_filled.ld has the addresses).
        .macro gap name, size
        .section \name, "bw"
        .skip \size
        .endm

        gap .gap0, 0x7FF000        # 0x01801000 - 0x02000000
        gap .gap1, 0xFC0000        # 0x02040000 - 0x03000000
        gap .gap2, 0xFF8000        # 0x03008000 - 0x04000000
        gap .gap3, 0xFFE000        # 0x04001000 - 0x04FFF000
        gap .gap4, 0xFFF000        # 0x05001000 - 0x06000000
        gap .gap5, 0xFE8000        # 0x06018000 - 0x07000000
        gap .gap6, 0xFFF000        # 0x07001000 - 0x08000000
        gap .gap7, 0x4000000       # 0x0A000000 - 0x0E000000
        gap .gap8, 0x31FE1000      # 0x0E020000 - 0x40001000
