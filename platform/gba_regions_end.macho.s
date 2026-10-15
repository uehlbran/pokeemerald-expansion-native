/*
 * gba_regions_end.macho.s: the end of EWRAM in the macOS executable, linked
 * after every other object (gba_regions.macho.s says why it is a file of its
 * own): a one-byte zero-filled section aligned to 256 KiB, EWRAM's size
 * (GBATEK "GBA Memory Map", 02000000-0203FFFF), which ld64 puts after
 * EWRAM_INIT's and EWRAM_DATA's sections because it meets it after them. It
 * starts at 0x02040000 while they fit in EWRAM, and the link names its start
 * gba_linked_ewram_end (gba_regions.macho.aliases); were they larger, it
 * would start at 0x02080000, and gba_memmap_init and
 * build_glue/check_macho_layout.py would refuse the layout, as
 * gba_regions.ld's ASSERT refuses the link. The byte costs a page of mapped
 * memory past EWRAM, which nothing addresses.
 */
        .zerofill __GBA_EWRAM,__ewram_end,gba_ewram_end,1,18
