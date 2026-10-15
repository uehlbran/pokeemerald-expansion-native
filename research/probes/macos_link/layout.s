/*
 * layout.s: probe M2's layout object (research/probes/README.md, "Probe M2").
 * The Mach-O counterpart of platform/gba_regions.ld and gba_game.ld as the macOS
 * port plans it: every GBA region and the battle runner's stack a segment of
 * its own (placed with -segaddr by build.sh), the sections inside each segment
 * and inside __DATA declared here in the order wanted, linked first so that
 * ld64 meets them first. Addresses and sizes: GBATEK "GBA Memory Map".
 *  - EWRAM: EWRAM_INIT's initialised data (__ewram_init), EWRAM_DATA
 *    (__ewram_data, zero-fill), then a one-byte marker aligned to 256 KiB
 *    (__ewram_end), which starts at 0x02040000 while the two before it fit in
 *    EWRAM and at 0x02080000 once they do not.
 *  - IWRAM: 0x7F00 bytes, then the test runner's persistent state at 0x03007F00.
 *  - __DATA: the game's initialised data, its zero-initialised data, then
 *    COMMON_DATA right after it, and the test table.
 * Plus absolute symbols, as gba_game.ld defines gNumMusicPlayers, and 4- and
 * 8-byte absolute address words, as the converted data holds 41,946 and
 * 19,208 of them (research/m8-inventory.md, section 7).
 */
        .zerofill __GBA_CALLSTK,__stack,_probe_call_stack,0x100000,12

        .section __GBA_EWRAM,__ewram_init
        .zerofill __GBA_EWRAM,__ewram_data
        .zerofill __GBA_EWRAM,__ewram_end,_probe_ewram_end_marker,1,18

        .zerofill __GBA_IWRAM,__iwram,_probe_iwram_low,0x7F00,12
        .zerofill __GBA_IWRAM,__persistent

        .zerofill __GBA_IO,__io,_probe_io,0x1000,12
        .zerofill __GBA_DEBUG,__mgba,_probe_debug,0x1000,12
        .zerofill __GBA_PALETTE,__palette,_probe_palette,0x1000,12
        .zerofill __GBA_VRAM,__vram,_probe_vram,0x18000,12
        .zerofill __GBA_OAM,__oam,_probe_oam,0x1000,12
        .zerofill __GBA_SRAM,__sram,_probe_sram,0x20000,12

        .section __DATA,__game_data
        .zerofill __DATA,__game_bss
        .zerofill __DATA,__game_common
        .section __DATA,__tests
        .section __DATA,__empty_probe

        .globl _gNumMusicPlayers
        .set _gNumMusicPlayers, 4
        .globl _probe_one
        .set _probe_one, 1

        .section __DATA,__const
        .globl _probe_words
        .p2align 3
_probe_words:
        .quad _CB2_Probe
        .long _CB2_Probe
        .long _probe_io
        .long _gNumMusicPlayers
        .long 0
