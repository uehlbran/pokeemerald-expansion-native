/*
 * gba_regions.pe.ld: gba_regions.ld for the Windows (PE, x86-64) executable.
 * Puts the GBA memory regions at their GBA addresses as sections of the image,
 * so the Windows loader maps them before any code runs. Addresses and sizes:
 * GBATEK "GBA Memory Map", as in gba_regions.ld, whose symbols this defines
 * too (gba_memmap.c checks them against gba_memmap.h).
 *
 * Link with the flags of platform/target.mk (TARGET_OS=windows):
 *   -Wl,--image-base=0x01800000 -Wl,--disable-dynamicbase
 *   -Wl,--disable-high-entropy-va -Wl,--disable-reloc-section
 *   -Wl,--section-start=.text=0x40001000 -Wl,-T,gba_regions.pe.ld
 * and platform/gba_gaps.pe.s assembled and made read-only (platform/Makefile).
 * INSERT BEFORE .text augments MinGW's default script as gba_regions.ld does
 * GNU ld's. Written by hand, not generated, like its Linux counterpart; the
 * numbers below, in gba_gaps.pe.s and in gba_regions.ld are kept equal by
 * ASSERTs, not by construction.
 *
 * What differs from gba_regions.ld, and why:
 *  - A PE image's section addresses are offsets above the image base, so the
 *    image cannot start at 0x40000000 with the regions below it. The image
 *    starts below the lowest region, at 0x01800000, and the code is moved to
 *    0x40001000 (the first page of the image is its headers), where the Linux
 *    executable's code is, so the code stays under the 0x42000000 the tag bit
 *    of platform/script_tag.h needs, and above every region.
 *  - The loader maps an image only if its sections are adjacent, in ascending
 *    order (loader probe W1: "as_linked" was refused by Windows,
 *    "filled_gaps" started 100 times in 100). So the stretches between the
 *    regions are sections of their own (.gap0 ..), uninitialised and
 *    read-only, which are neither stored in the file nor charged as memory.
 *  - The battle runner's stack cannot sit at 0x01800000, where the headers
 *    are: it is the first section, at 0x01801000 (still below 2^27, so its
 *    addresses fit the runner's 27-bit fields). The size is the same.
 *  - A section without input sections is uninitialised, readable and
 *    writable, which is what a region is; (NOLOAD) would mark it
 *    discardable here, so it is not used. A PE section is either stored or
 *    not, so EWRAM_INIT's initial data and EWRAM_DATA's zero-filled .sbss share
 *    one section (the file holds the whole 256 KiB when there is any initial
 *    data).
 *  - Section names are at most 8 characters. The ROM's 32 MiB window of the
 *    probes is not here: the Linux layout has none either (docs/DECISIONS.md,
 *    "The link base stays at 0x40000000; no ROM-window mirror").
 */
SECTIONS
{
    /* The battle runner's stack (see gba_regions.ld): 1 MiB, the first section
     * of the image. */
    .callstk 0x01801000 :
    {
        platform_call_stack_start = .;
        . += 0x100000;
        platform_call_stack_end = .;
    }
    .gap0 platform_call_stack_end : { *(.gap0) }
    ASSERT(ADDR(.gap0) + SIZEOF(.gap0) == 0x02000000, "gba_regions.pe.ld: .gap0 (gba_gaps.pe.s) does not reach EWRAM")

    /* EWRAM: GBATEK "GBA Memory Map", 02000000-0203FFFF, 256 KBytes. First
     * expansion's EWRAM_INIT variables (section .ewram, include/gba/defines.h),
     * the only EWRAM data with initial values, as ld_script_modern.ld places
     * them on the GBA; platform/crt0.c keeps a copy for ReInitializeEWRAM,
     * between gba_linked_ewram_init_start and _end. Then expansion's
     * EWRAM_DATA, section .sbss, zero-filled, as on the GBA; the rest of the
     * 256 KiB stays free for gHeap and the game's fixed uses. MAX keeps an
     * oversized .sbss from stopping the layout with "cannot move location
     * counter backwards"; the ASSERT after the section fails the link with a
     * message that says what is wrong. */
    .gbaewrm 0x02000000 :
    {
        gba_linked_ewram_start = .;
        gba_linked_ewram_init_start = .;
        *(.ewram .ewram.*)
        gba_linked_ewram_init_end = .;
        *(.sbss .sbss.*)
        HIDDEN(gba_ewram_data_end = .);
        . = MAX(., gba_linked_ewram_start + 0x40000);
        gba_linked_ewram_end = .;
    }
    ASSERT(gba_ewram_data_end <= gba_linked_ewram_start + 0x40000,
           "gba_regions.pe.ld: EWRAM_DATA (.sbss) is larger than the 256 KiB of EWRAM")
    .gap1 gba_linked_ewram_end : { *(.gap1) }
    ASSERT(ADDR(.gap1) + SIZEOF(.gap1) == 0x03000000, "gba_regions.pe.ld: .gap1 (gba_gaps.pe.s) does not reach IWRAM")

    /* IWRAM: GBATEK "GBA Memory Map", 03000000-03007FFF, 32 KBytes. Only the
     * test runner's state that survives a reboot, section .persistent, has a
     * fixed address, 03007F00h, as ld_script_test.ld places it
     * (IWRAM_PERSISTENT, 100h bytes). */
    .gbaiwrm 0x03000000 :
    {
        gba_linked_iwram_start = .;
        . = gba_linked_iwram_start + 0x7F00;
        *(.persistent)
        HIDDEN(gba_iwram_persistent_end = .);
        . = MAX(., gba_linked_iwram_start + 0x8000);
        gba_linked_iwram_end = .;
    }
    ASSERT(gba_iwram_persistent_end <= gba_linked_iwram_start + 0x8000,
           "gba_regions.pe.ld: the test runner's .persistent state is larger than the 100h bytes at 03007F00h")
    .gap2 gba_linked_iwram_end : { *(.gap2) }
    ASSERT(ADDR(.gap2) + SIZEOF(.gap2) == 0x04000000, "gba_regions.pe.ld: .gap2 (gba_gaps.pe.s) does not reach the I/O registers")

    /* I/O registers: GBATEK "GBA Memory Map", 04000000-040003FE, 1 KiB; one 4 KiB page. */
    .gbaio 0x04000000 :
    {
        gba_linked_io_start = .;
        . += 0x1000;
        gba_linked_io_end = .;
    }
    .gap3 gba_linked_io_end : { *(.gap3) }
    ASSERT(ADDR(.gap3) + SIZEOF(.gap3) == 0x04FFF000, "gba_regions.pe.ld: .gap3 (gba_gaps.pe.s) does not reach the mGBA debug page")

    /* mGBA debug registers 04FFF600-04FFF781 (mGBA src/gba/io.c); their 4 KiB page. */
    .gbadbg 0x04FFF000 :
    {
        gba_linked_mgba_debug_start = .;
        . += 0x1000;
        gba_linked_mgba_debug_end = .;
    }

    /* Palette RAM: GBATEK "GBA Memory Map", 05000000-050003FF, 1 Kbyte; one 4 KiB page. */
    .gbapal 0x05000000 :
    {
        gba_linked_palette_ram_start = .;
        . += 0x1000;
        gba_linked_palette_ram_end = .;
    }
    .gap4 gba_linked_palette_ram_end : { *(.gap4) }
    ASSERT(ADDR(.gap4) + SIZEOF(.gap4) == 0x06000000, "gba_regions.pe.ld: .gap4 (gba_gaps.pe.s) does not reach VRAM")

    /* VRAM: GBATEK "GBA Memory Map", 06000000-06017FFF, 96 KBytes. */
    .gbavram 0x06000000 :
    {
        gba_linked_vram_start = .;
        . += 0x18000;
        gba_linked_vram_end = .;
    }
    .gap5 gba_linked_vram_end : { *(.gap5) }
    ASSERT(ADDR(.gap5) + SIZEOF(.gap5) == 0x07000000, "gba_regions.pe.ld: .gap5 (gba_gaps.pe.s) does not reach OAM")

    /* OAM: GBATEK "GBA Memory Map", 07000000-070003FF, 1 Kbyte; one 4 KiB page. */
    .gbaoam 0x07000000 :
    {
        gba_linked_oam_start = .;
        . += 0x1000;
        gba_linked_oam_end = .;
    }
    .gap6 gba_linked_oam_end : { *(.gap6) }
    ASSERT(ADDR(.gap6) + SIZEOF(.gap6) == 0x0E000000, "gba_regions.pe.ld: .gap6 (gba_gaps.pe.s) does not reach the SRAM window")

    /* SRAM window: GBATEK "GBA Cart Backup Flash ROM", 128 KBytes (1 Mbit), both banks. */
    .gbasram 0x0E000000 :
    {
        gba_linked_sram_start = .;
        . += 0x20000;
        gba_linked_sram_end = .;
    }
    .gap7 gba_linked_sram_end : { *(.gap7) }
    ASSERT(ADDR(.gap7) + SIZEOF(.gap7) == 0x40001000, "gba_regions.pe.ld: .gap7 (gba_gaps.pe.s) does not reach the code")
}
INSERT BEFORE .text;

/* platform/script_tag.h keeps expansion's ROM_SIZE tag on event script
 * function pointers as bit 25 (0x02000000); that only works while no code
 * address has it set, that is while the code ends below 0x42000000 (the code
 * starts at 0x40001000; data is never tagged). */
ASSERT(etext < 0x42000000, "the code reaches 0x42000000: platform/script_tag.h's tag bit would be ambiguous");
