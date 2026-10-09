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
     * It starts with expansion's EWRAM_INIT variables (section .ewram,
     * include/gba/defines.h), the only EWRAM data with initial values, as
     * ld_script_modern.ld places them on the GBA. Their section is loaded, so
     * the ELF loader gives them their initial values where crt0.s copied them
     * from ROM; platform/crt0.c keeps a copy for ReInitializeEWRAM, between
     * gba_linked_ewram_init_start and _end. Executables without such data
     * (the unit tests) get an empty, discarded section and the symbols only.
     * Then expansion's EWRAM_DATA, __attribute__((section(".sbss"))), not
     * loaded: zero-filled, at the bottom of the rest of the region as on the
     * GBA; the rest of the 256 KiB stays free for gHeap and the game's fixed
     * uses. MAX keeps an oversized .sbss from stopping the layout with
     * "cannot move location counter backwards"; the ASSERT after the section
     * then fails the link with a message that says what is wrong. */
    .gba_ewram_init 0x02000000 :
    {
        gba_linked_ewram_start = .;
        gba_linked_ewram_init_start = .;
        *(.ewram .ewram.*)
        gba_linked_ewram_init_end = .;
    }
    /* At an explicit address: without EWRAM_INIT data the section above is
     * discarded, and a section with no address would then follow the
     * previous one elsewhere. The symbol keeps its value either way. */
    .gba_ewram gba_linked_ewram_init_end (NOLOAD) :
    {
        *(.sbss .sbss.*)
        HIDDEN(gba_ewram_data_end = .);
        . = MAX(., gba_linked_ewram_start + 0x40000);
        gba_linked_ewram_end = .;
    }
    ASSERT(gba_ewram_data_end <= gba_linked_ewram_start + 0x40000,
           "gba_regions.ld: EWRAM_DATA (.sbss) is larger than the 256 KiB of EWRAM")

    /* IWRAM: GBATEK "GBA Memory Map", 03000000-03007FFF, 32 KBytes. The
     * game's IWRAM variables stay in the native .bss (docs/RESEARCH.md,
     * "Memory model"); only the test runner's state that survives a reboot,
     * section .persistent, has a fixed address, 03007F00h, as
     * ld_script_test.ld places it (IWRAM_PERSISTENT, 100h bytes). */
    .gba_iwram 0x03000000 (NOLOAD) :
    {
        gba_linked_iwram_start = .;
        . = gba_linked_iwram_start + 0x7F00;
        *(.persistent)
        HIDDEN(gba_iwram_persistent_end = .);
        . = MAX(., gba_linked_iwram_start + 0x8000);
        gba_linked_iwram_end = .;
    }
    ASSERT(gba_iwram_persistent_end <= gba_linked_iwram_start + 0x8000,
           "gba_regions.ld: the test runner's .persistent state is larger than the 100h bytes at 03007F00h")

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

    /* Not a GBA region: the stack platform/game_hooks.c runs the battle test
     * runner's functions on (PlatformCallWithStack). The runner keeps the
     * addresses of those functions' locals in 27-bit fields
     * (include/test/battle.h, QueuedSubHitEvent.address), which on the GBA
     * hold EWRAM addresses, where its DATA.stack is; the executable's own
     * .bss is above 08000000h and would not fit, so the stack sits in
     * GBATEK's unused 00004000-01FFFFFF, clear of the page the m4a tests map
     * at 01000000h (platform/m4a/test_m4a_support.h). 1 MiB (game_hooks.h). */
    .platform_call_stack 0x01800000 (NOLOAD) :
    {
        platform_call_stack_start = .;
        . += 0x100000;
        platform_call_stack_end = .;
    }

    /* Back to where the default script was, so _end and the sections after
     * .bss are unaffected. */
    . = gba_regions_saved_dot;
}
INSERT AFTER .bss;
