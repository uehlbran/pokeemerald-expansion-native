# regions.s: one input section per GBA region, zero-filled (COFF "b" = uninitialised
# data, no bytes in the file), with the protection the region has on the GBA.
# Flags are the assembler's COFF section flags: b uninitialised, w writable. It cannot
# say "uninitialised and read-only", so build.sh runs objcopy --set-section-flags
# .gbarom=alloc,readonly on the object.
# Addresses and sizes: GBATEK "GBA Memory Map" (platform/gba_memmap.h, which these
# sizes duplicate, as platform/gba_regions.ld does). The linker script
# layout_b.ld puts each section at its GBA address.
# The sections have names of at most 8 characters: a PE section name is 8 bytes.
        .macro region name, symbol, size, flags
        .section \name, "\flags"
        .globl \symbol
\symbol:
        .skip \size
        .endm

        region .gbaewrm, gba_linked_ewram_start,   0x40000, bw # EWRAM 256 KiB
        region .gbaiwrm, gba_linked_iwram_start,   0x8000,  bw # IWRAM 32 KiB
        region .gbaio,   gba_linked_io_start,      0x1000,  bw # I/O, one page
        region .gbadbg,  gba_linked_mgba_start,    0x1000,  bw # mGBA debug page
        region .gbapal,  gba_linked_palette_start, 0x1000,  bw # palette, one page
        region .gbavram, gba_linked_vram_start,    0x18000, bw # VRAM 96 KiB
        region .gbaoam,  gba_linked_oam_start,     0x1000,  bw # OAM, one page
        region .gbarom,  gba_linked_rom_start,     0x2000000, bw # ROM 32 MiB; build.sh makes it read-only
        region .gbasram, gba_linked_sram_start,    0x20000, bw # SRAM 128 KiB
