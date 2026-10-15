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
 * gba_regions.ld's ASSERT refuses the link.
 *
 * Before it, a byte of its own (__ewram_pad), after EWRAM_DATA for the same
 * reason: an executable with no EWRAM variable at all (most unit tests) would
 * otherwise have the marker at 0x02000000, the segment's first 256 KiB
 * boundary, and an EWRAM of no bytes. With it, EWRAM_INIT and EWRAM_DATA may
 * hold one byte less than EWRAM's 256 KiB. The two bytes cost a page of
 * mapped memory past EWRAM, which nothing addresses.
 */
        .zerofill __GBA_EWRAM,__ewram_pad,gba_ewram_pad,1,0
        .zerofill __GBA_EWRAM,__ewram_end,gba_ewram_end,1,18
