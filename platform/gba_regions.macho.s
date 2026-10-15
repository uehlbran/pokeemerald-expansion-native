/*
 * gba_regions.macho.s: gba_regions.ld for the macOS (Mach-O, x86_64)
 * executable. Puts the GBA memory regions and the battle runner's stack at
 * their addresses as zero-filled segments, so the kernel maps them when it
 * loads the executable, before any code runs. Addresses and sizes: GBATEK
 * "GBA Memory Map", as in gba_regions.ld; the mGBA debug page: mGBA
 * src/gba/io.c.
 *
 * ld64 has no linker scripts. What gba_regions.ld says is said here in three
 * ways (platform/target.mk has the flags):
 *  - each region is a segment of its own, with its sections declared below;
 *    the link places each segment at its address with -segaddr, in a non-PIE
 *    image at 0x40000000 with a one-page __PAGEZERO and no chained fixups
 *    (with them ld64 ignores the image base; docs/DECISIONS.md, "The macOS
 *    executable's layout");
 *  - the names gba_regions.ld defines (gba_linked_ewram_start, ...) are
 *    ld64's own bounds of a section or a segment (section$start$SEG$SECT,
 *    segment$end$SEG), given those names by gba_regions.macho.aliases, the
 *    list the link hands ld64 with -alias_list;
 *  - the order of the sections in a segment is the order in which the link
 *    first meets each one with something in it, object by object (probe M2:
 *    an empty declaration does not count). So this object is linked first,
 *    and gba_regions_end.macho.s, EWRAM's end, last.
 *
 * EWRAM: expansion's EWRAM_INIT variables first (__ewram_init, initialised,
 * stored in the file; crt0.c keeps a copy for ReInitializeEWRAM), then
 * EWRAM_DATA (__ewram_data, zero-filled), as ld_script_modern.ld places them
 * on the GBA; the build's C filter (build_glue/c_filter_macho.sed) spells
 * expansion's section markers so. The rest of the 256 KiB stays free for
 * gHeap's neighbours and the game's fixed uses. A segment is as long as what
 * it holds, so EWRAM's end is a marker section of its own, linked last
 * (gba_regions_end.macho.s, with a byte before it so that EWRAM is never
 * empty): aligned to 256 KiB, it starts at 0x02040000 while the sections
 * before it fit in EWRAM, and the region's end is its start (gba_memmap_init
 * then finds a region that is too long otherwise).
 * __ewram_init, initialised, comes before the zero-filled sections whatever
 * the order (ld64 puts a segment's zero-fill last); it is declared here only
 * so that a link without it still has its bounds.
 *
 * IWRAM: 0x7F00 bytes, then the test runner's state that survives a reboot
 * (__persistent, expansion's .persistent) at 03007F00h, as ld_script_test.ld
 * places it (IWRAM_PERSISTENT, 100h bytes). The segment ends at the next page
 * boundary, 0x03008000, while the state is at most 100h bytes; with more it
 * would end a page later, and gba_memmap_init refuses the region.
 *
 * The battle runner's stack (gba_regions.ld says why it is here): 1 MiB at
 * 0x01800000.
 *
 * The other regions are each one page-aligned zero-filled section of the
 * region's size in whole 4 KiB pages. The symbols on them are local; the
 * names the platform reads come from the alias list. Alignment 12 is log2 of
 * the 4 KiB page.
 *
 * gba_regions.ld also asserts that the code ends below 0x42000000
 * (platform/script_tag.h); on macOS build_glue/check_macho_layout.py checks
 * that and this file's layout on every link.
 */
        .zerofill __GBA_CALLSTK,__stack,platform_call_stack,0x100000,12

        .section __GBA_EWRAM,__ewram_init
        .zerofill __GBA_EWRAM,__ewram_data

        .zerofill __GBA_IWRAM,__iwram,gba_iwram_below_persistent,0x7F00,12
        .zerofill __GBA_IWRAM,__persistent

        .zerofill __GBA_IO,__io,gba_io,0x1000,12
        .zerofill __GBA_DEBUG,__mgba,gba_mgba_debug,0x1000,12
        .zerofill __GBA_PALETTE,__palette,gba_palette_ram,0x1000,12
        .zerofill __GBA_VRAM,__vram,gba_vram,0x18000,12
        .zerofill __GBA_OAM,__oam,gba_oam,0x1000,12
        .zerofill __GBA_SRAM,__sram,gba_sram,0x20000,12
