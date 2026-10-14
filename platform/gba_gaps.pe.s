# gba_gaps.pe.s: the filler between the GBA regions in the Windows executable.
# One empty section per gap, sized as gba_regions.pe.ld places it (the script
# asserts every size; a gap that is too short or too long fails the link), from
# the end of one region to the start of the next. Why they exist: the Windows
# loader refuses an image whose sections are not adjacent, and a writable gap
# would be charged to the process's commit charge (about 1 GiB), a read-only
# uninitialised one costs neither file space nor commit charge
# (research/probes/README.md, probe W1, "filled_gaps"; its gaps.s is the model).
#
# The assembler cannot say "uninitialised and read-only": the Makefiles make the
# sections read-only with objcopy --set-section-flags NAME=alloc,readonly for
# every `gap` line below (platform/Makefile, TARGET_OS=windows).
        .macro gap name, size
        .section \name, "bw"
        .skip \size
        .endm

        gap .gap0, 0x6FF000        # 0x01901000 - 0x02000000, after the battle runner's stack
        gap .gap1, 0xFC0000        # 0x02040000 - 0x03000000, after EWRAM
        gap .gap2, 0xFF8000        # 0x03008000 - 0x04000000, after IWRAM
        gap .gap3, 0xFFE000        # 0x04001000 - 0x04FFF000, after the I/O page
        gap .gap4, 0xFFF000        # 0x05001000 - 0x06000000, after palette RAM
        gap .gap5, 0xFE8000        # 0x06018000 - 0x07000000, after VRAM
        gap .gap6, 0x6FFF000       # 0x07001000 - 0x0E000000, after OAM (the ROM's 32 MiB window is not mapped)
        gap .gap7, 0x31FE1000      # 0x0E020000 - 0x40001000, after SRAM, up to the code
