# regions.s: one Mach-O segment per GBA region, zero-filled (.zerofill: no bytes in
# the file, demand-zero pages at load). build.sh places each segment at its GBA
# address with -Wl,-segaddr. Addresses and sizes: GBATEK "GBA Memory Map"
# (platform/gba_memmap.h, which these sizes duplicate, as platform/gba_regions.ld
# does). The I/O, mGBA debug, palette and OAM regions are the 4 KiB page that holds
# them. There is no ROM here: nothing is mapped at 0x08000000 in the model
# (research/m8-inventory.md 2.1 item 7). Alignment 12 is log2 of the 4 KiB page of
# x86_64 macOS.
        .macro region segment, section, symbol, size
        .globl \symbol
        .zerofill \segment, \section, \symbol, \size, 12
        .endm

        region __GBAEWRAM, __ewram,   _gba_linked_ewram_start,   0x40000   # EWRAM 256 KiB
        region __GBAIWRAM, __iwram,   _gba_linked_iwram_start,   0x8000    # IWRAM 32 KiB
        region __GBAIO,    __io,      _gba_linked_io_start,      0x1000    # I/O, one page
        region __GBADEBUG, __debug,   _gba_linked_mgba_start,    0x1000    # mGBA debug page
        region __GBAPAL,   __palette, _gba_linked_palette_start, 0x1000    # palette, one page
        region __GBAVRAM,  __vram,    _gba_linked_vram_start,    0x18000   # VRAM 96 KiB
        region __GBAOAM,   __oam,     _gba_linked_oam_start,     0x1000    # OAM, one page
        region __GBASRAM,  __sram,    _gba_linked_sram_start,    0x20000   # SRAM 128 KiB
