/*
 * gba_regions.ld: puts the GBA memory regions at their GBA addresses in the
 * native executable, as zero-filled, writable, not-loaded (NOBITS) sections,
 * so the kernel's ELF loader maps them before any code runs. Addresses and
 * sizes: GBATEK "GBA Memory Map"; the mGBA debug page: mGBA src/gba/io.c and
 * docs/research/test-runner-protocol.md. Why this and not runtime mmap:
 * platform/gba_memmap.c.
 *
 * Link with: -no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,gba_regions.ld
 * INSERT AFTER .bss makes this augment the default linker script rather than
 * replace it. platform/Makefile links every test this way; Makefile.native
 * must link the game the same way.
 *
 * These numbers duplicate platform/gba_memmap.h (a linker script cannot
 * include a C header); gba_memmap_init() checks the gba_linked_* symbols below
 * against the header and fails if they disagree.
 *
 * GNU ld sorts the segments by address, so the ELF and program headers end up
 * in the page below EWRAM (0x01FFF000), in GBATEK's unused 0x00004000-0x01FFFFFF.
 */
SECTIONS
{
    HIDDEN(gba_regions_saved_dot = .);

    /* EWRAM: GBATEK "GBA Memory Map", 02000000-0203FFFF, 256 KBytes.
     * Expansion's EWRAM_DATA is __attribute__((section(".sbss")))
     * (include/gba/defines.h), so the game's EWRAM variables land here at the
     * bottom of the region, as ld_script_modern.ld places them on the GBA; the
     * rest of the 256 KiB stays free for gHeap and the game's fixed uses.
     * MAX keeps an oversized .sbss from stopping the layout with "cannot move
     * location counter backwards"; the ASSERT after the section then fails
     * the link with a message that says what is wrong. */
    .gba_ewram 0x02000000 (NOLOAD) :
    {
        gba_linked_ewram_start = .;
        *(.sbss .sbss.*)
        HIDDEN(gba_ewram_data_end = .);
        . = MAX(., gba_linked_ewram_start + 0x40000);
        gba_linked_ewram_end = .;
    }
    ASSERT(gba_ewram_data_end <= gba_linked_ewram_start + 0x40000,
           "gba_regions.ld: EWRAM_DATA (.sbss) is larger than the 256 KiB of EWRAM")

    /* IWRAM: GBATEK "GBA Memory Map", 03000000-03007FFF, 32 KBytes. */
    .gba_iwram 0x03000000 (NOLOAD) :
    {
        gba_linked_iwram_start = .;
        . += 0x8000;
        gba_linked_iwram_end = .;
    }

    /* I/O registers: GBATEK "GBA Memory Map", 04000000-040003FE, 1 KiB; one 4 KiB page. */
    .gba_io 0x04000000 (NOLOAD) :
    {
        gba_linked_io_start = .;
        . += 0x1000;
        gba_linked_io_end = .;
    }

    /* mGBA debug registers 04FFF600-04FFF781 (mGBA src/gba/io.c); their 4 KiB page. */
    .gba_mgba_debug 0x04FFF000 (NOLOAD) :
    {
        gba_linked_mgba_debug_start = .;
        . += 0x1000;
        gba_linked_mgba_debug_end = .;
    }

    /* Palette RAM: GBATEK "GBA Memory Map", 05000000-050003FF, 1 Kbyte; one 4 KiB page. */
    .gba_palette_ram 0x05000000 (NOLOAD) :
    {
        gba_linked_palette_ram_start = .;
        . += 0x1000;
        gba_linked_palette_ram_end = .;
    }

    /* VRAM: GBATEK "GBA Memory Map", 06000000-06017FFF, 96 KBytes. */
    .gba_vram 0x06000000 (NOLOAD) :
    {
        gba_linked_vram_start = .;
        . += 0x18000;
        gba_linked_vram_end = .;
    }

    /* OAM: GBATEK "GBA Memory Map", 07000000-070003FF, 1 Kbyte; one 4 KiB page. */
    .gba_oam 0x07000000 (NOLOAD) :
    {
        gba_linked_oam_start = .;
        . += 0x1000;
        gba_linked_oam_end = .;
    }

    /* SRAM window: GBATEK "GBA Cart Backup Flash ROM", 128 KBytes (1 Mbit), both banks. */
    .gba_sram 0x0E000000 (NOLOAD) :
    {
        gba_linked_sram_start = .;
        . += 0x20000;
        gba_linked_sram_end = .;
    }

    /* Back to where the default script was, so _end and the sections after
     * .bss are unaffected. */
    . = gba_regions_saved_dot;
}
INSERT AFTER .bss;
