# What the native link needs

**Status (2026-10-08): all of it is provided.** `make -f Makefile.native
native` links `build/native<bits>[-test][-san]/pokeemerald-native[-test]`
(32-bit; 64-bit stops at two expansion layout assertions, see
`BOOT-LOG.md`). Where each need below is met:

| Need | Provided by |
|---|---|
| Link command | `Makefile.native` `native`: `-no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,platform/gba_regions.ld -Wl,-T,platform/gba_game.ld -Wl,--defsym,gInitialMainCB2=...`, SDL2 and libm |
| `.ewram` (EWRAM_INIT) | `gba_regions.ld` `.gba_ewram_init`, loaded at 0x02000000 ahead of `.sbss`; `platform/crt0.c` keeps its initial image for `ReInitializeEWRAM` |
| `.iwram`, `.iwram.code`, `script_data`, compound strings, `.rodata.overworld_script` | the default script (`.iwram` goes into `gba_game.ld`'s `game_data`) |
| `common_data` | `gba_game.ld` `game_bss`, after the game's `.bss`, not loaded, as on the GBA |
| `.persistent` | `gba_regions.ld`, inside `.gba_iwram` at 0x3007F00 |
| `.tests`, `__start_tests`, `__stop_tests` | `gba_game.ld` `tests`, `test_test_runner.o` first; packed (checked: 6294 entries of 20 bytes) |
| `gInitialMainCB2` | `--defsym` in `Makefile.native` (`CB2_TestRunner` with TEST=1) |
| `gNumMusicPlayers`, `gMaxLines` | `gba_game.ld`, absolute 4 and 0 |
| `__iwram_end`, `__rom_end` | `gba_game.ld`: the end of the IWRAM region, and 0x08000000 (ROM size reads 0 in the debug menu) |
| BIOS calls | `platform/bios_*.c` (`bios.h`) |
| `IntrMain`, `ReInitializeEWRAM` | `platform/crt0.c`. Expansion 1.17.1 only stores `IntrMain`'s address (no `IntrMain_Buffer` copy); it is 0x800 bytes of data |
| `src/m4a_1.s` symbols | `platform/m4a/` |
| `FastUnsafeCopy32`, `LZ77UnCompWRAMOptimized` | `platform/decompress_asm.c`; `src/decompress.c` (guarded) no longer copies code, so natively only `LZ77UnCompWRAMOptimized` is referenced and `LZ77UnCompWRAMOptimized_end` is not needed |
| `GameCubeMultiBoot_*` | `platform/gcn_multiboot.c` (no GameCube: progress stays "none") |
| `RomHeaderGameCode`, `RomHeaderSoftwareVersion` | `platform/rom_header.c` ("BPEE", 0) |
| `PlatformWaitCycles`, `PlatformJumpToAgbMainLoop`, `PlatformTestRunnerExit`, `PlatformCallWithStack`, `PlatformDebugOpen`, `PlatformDebugSend`, `PlatformIsRomPointer` | `platform/game_hooks.c` (`PlatformDebugOpen` and `PlatformDebugSend` reach `platform/debug_channel.c`) |
| `__executable_start` (`src/malloc.c`'s allocation-site offsets) | GNU ld's default script |
| `platform_call_stack_start`/`_end` (the battle runner's stack, below 2^27) | `gba_regions.ld` `.platform_call_stack` at 0x01800000 |
| `main` | `platform/main.c` |

The measurements and reasoning below are from before the link existed and
are kept as the record of why each piece is there.

`make -f Makefile.native objects` compiles every object; it does not link.
This is the list of what the final link must supply, measured on
2026-10-08 against expansion 1.17.1 by a trial link of every 32-bit
object (`TEST=1`, the superset) against the host C library:

```
make -f Makefile.native -j$(nproc) objects BITS=32 TEST=1
gcc -m32 -no-pie -Wl,-Ttext-segment=0x40000000 -o /dev/null \
    $(find build/native32-test -name '*.o') -Wl,--unresolved-symbols=report-all
```

`TEST=0` needs the same list minus the rows marked "test only". Each row
says what provides the symbol on the GBA. "Platform" means a module under
`platform/` (docs/ARCHITECTURE.md, "Modules").

## Link command

From the foundation worker (`platform/gba_regions.ld`):

```
-no-pie -Wl,-Ttext-segment=0x40000000 -Wl,-T,platform/gba_regions.ld
```

The fragment is `INSERT AFTER .bss`: it places `.sbss` (`EWRAM_DATA`) at
the bottom of a NOLOAD EWRAM section at 0x02000000, asserts it fits in
256 KiB, maps the other regions, and defines the `gba_linked_*_start` and
`gba_linked_*_end` symbols (EWRAM, IWRAM, I/O, mGBA debug, palette RAM,
VRAM, OAM, SRAM), which `platform/gba_memmap.c` checks. Those symbols are
provided; nothing below needs them.

Measured `.sbss` (the sum of the objects' sections, before alignment
padding): 221.9 KiB on 32-bit and 225.8 KiB on 64-bit, so it fits the
256 KiB on both (`docs/RESEARCH.md`, open question 2).

## Sections the link has to place

| Section | From | Size (32-bit) | On GBA | Native need |
|---|---|---|---|---|
| `.ewram` | `EWRAM_INIT`: `sNameboxWindowId` in `src/field_name_box.c` | 1 byte | `.ewram` at the start of EWRAM, loaded from ROM by `crt0.s` | A **loaded** section in EWRAM ahead of `.sbss`. `gba_regions.ld` makes EWRAM NOLOAD, which would zero it. `ReInitializeEWRAM` (below) must restore its initial value. |
| `.iwram` | `IWRAM_INIT`: one 4-byte variable in `src/load_save.c` | 4 bytes | `.iwram`, loaded from ROM | Ordinary initialised data; no fixed address is used. |
| `.iwram.code` | `CopyTable` in `src/decompress.c` | 36 bytes | IWRAM code | Ordinary code (an orphan section after `.text`). |
| `common_data` | `COMMON_DATA` globals (`gRngValue`, `gAgbMainLoop_sp`, ...) | 19.1 KiB | IWRAM | Ordinary data. |
| `script_data` | `data/*.s` scripts | 1.2 MiB | ROM | Ordinary read-only data. |
| `.rodata.compound_string.*` | `COMPOUND_STRING` | | ROM, merged (`SHF_MERGE`) | Kept mergeable by `platform/build_glue/c_filter.sed`; the default script merges them. |
| `.rodata.overworld_script` | `OVERWORLD_SCRIPT` in tests (native branch) | | (in `.text` on GBA) | Ordinary read-only data. Test only. |
| `.persistent` | `gPersistentTestRunnerState` in `test/test_runner.c` | 4 bytes | `.iwram.persistent` at 0x3007F00, NOLOAD, survives a reboot | Must be at 0x3007F00 inside the mapped IWRAM. Test only. |
| `.tests` | every `TEST`, `ASSUMPTIONS` | 123 KiB | ROM, `test_test_runner.o` first | `__start_tests`/`__stop_tests` around it, with `test/test_test_runner.o` first. GNU ld does not define `__start_`/`__stop_` for a section whose name is not a C identifier, so the linker script must. Test only. |

## Symbols

### Linker-script symbols (`ld_script_modern.ld`, `ld_script_test.ld`)

| Symbol | Used by | On GBA | Native |
|---|---|---|---|
| `gInitialMainCB2` | `src/main.c` | `= CB2_InitCopyrightScreenAfterBootup` (`= CB2_TestRunner` in the test ELF) | `-Wl,--defsym,gInitialMainCB2=...`, same values |
| `gNumMusicPlayers` | `src/m4a.c` | `= 4`; the address is the value (`m4a_internal.h`: `(u16)gNumMusicPlayers`) | `-Wl,--defsym,gNumMusicPlayers=4` (absolute; needs the non-PIE link) |
| `gMaxLines` | `src/m4a.c` | `= 0`, same trick | `-Wl,--defsym,gMaxLines=0` |
| `__iwram_end` | `src/assertf.c`, free-stack estimate (`frame address - __iwram_end`) | end of IWRAM data; the GBA stack sits above it | Any address below the native stack makes the estimate large; a guarded change or a symbol at the stack's low end gives a true one |
| `__rom_end` | `src/debug.c`, ROM size in the debug menu | end of the ROM image | Any address; the debug menu's ROM-size line is meaningless natively |
| `__start_tests`, `__stop_tests` | `test/test_runner.c` | around `.tests` | see `.tests` above. Test only. |

### BIOS calls (`include/gba/syscall.h`; `libagbsyscall` on GBA; platform `bios`)

| Symbol | Used by |
|---|---|
| `ArcTan2` | `src/battle_anim_mons.c` |
| `BgAffineSet` | `src/bg.c`, `src/intro.c`, `src/util.c` |
| `BitUnPack` | `src/assertf.c` |
| `CpuFastSet` | 30 objects |
| `CpuSet` | 93 objects |
| `Div` | `src/librfu_rfu.c` |
| `LZ77UnCompVram` | `src/decompress.c`, `test/compression/smol.c` |
| `LZ77UnCompWram` | `test/compression/smol.c`. Test only. |
| `MultiBoot` | `src/multiboot.c` |
| `ObjAffineSet` | `src/battle_anim_mons.c`, `src/pokedex_cry_screen.c`, `src/pokemon_animation.c`, `src/sprite.c` |
| `RegisterRamReset` | `src/reload_save.c` |
| `SoftReset` | `src/main.c`, `src/credits.c`, `src/battle_anim_effects_1.c` |
| `Sqrt` | `src/battle_script_commands.c` |
| `VBlankIntrWait` | `src/main.c`, `src/assertf.c`, `src/pokemon_sprite_visualizer.c`, tests |

`CpuSet` and `CpuFastSet` on x86: a struct is aligned to its largest
member, not to 4 bytes as under the GBA's `-mabi=apcs-gnu`, so the game
passes 2-aligned addresses where the GBA guaranteed 4 (this is why the
`CpuFastSet` alignment assertions are off natively, see
`docs/UPSTREAM_MODIFICATIONS.md`). The BIOS ignores the low address bits;
the native implementation must not, or it writes below the destination.

### `src/crt0.s` (not built; platform)

| Symbol | Used by | On GBA |
|---|---|---|
| `IntrMain` | `src/main.c` (`INTR_VECTOR = IntrMain`) | the IRQ dispatcher the BIOS jumps to through 0x3007FFC; platform `interrupt` |
| `ReInitializeEWRAM` | `src/reload_save.c` | re-copies `.ewram` (`EWRAM_INIT`) from its ROM image with `CpuSet` after `RegisterRamReset(RESET_EWRAM)` |

### `src/m4a_1.s` (not built; the C translation in `platform/m4a/`)

`MPlayJumpTableCopy`, `MPlayMain`, `RealClearChain`, `SoundMain`,
`SoundMainBTM`, `TrackStop`, `m4aSoundVSync`, `umul3232H32`, and the
sequencer commands `ply_bend`, `ply_bendr`, `ply_endtie`, `ply_fine`,
`ply_goto`, `ply_keysh`, `ply_lfodl`, `ply_lfos`, `ply_mod`, `ply_modt`,
`ply_note`, `ply_pan`, `ply_patt`, `ply_pend`, `ply_port`, `ply_prio`,
`ply_rept`, `ply_tempo`, `ply_tune`, `ply_voice`, `ply_vol` (referenced by
`src/m4a.c`, `src/m4a_tables.c` and `src/main.c`).

### `src/decompress_asm.s` (not built)

| Symbol | Used by | On GBA |
|---|---|---|
| `FastUnsafeCopy32` | `src/decompress.c` (`CopyFuncToIwram`) | copies code into a stack buffer |
| `LZ77UnCompWRAMOptimized`, `LZ77UnCompWRAMOptimized_end` | `src/decompress.c` (`FastLZ77UnCompWram`) | ARM LZ77 decoder, copied to the stack and run there |

Providing these symbols is not enough: `src/decompress.c` copies functions
(`DecodeLOtANSLoop`, `DecodeInstructions`, `DeltaDecodeTileNumbers`, ...)
into stack buffers and calls the copies. On x86 the stack is not
executable and copied code is not position-independent, so these call
sites need a guarded change that calls the functions directly (the
regionalidades port did the same). This is a runtime matter, not a link
one.

### `src/libgcnmultiboot.s` (not built; stubs)

`GameCubeMultiBoot_ExecuteProgram`, `GameCubeMultiBoot_HandleSerialInterrupt`,
`GameCubeMultiBoot_Init`, `GameCubeMultiBoot_Main`, `GameCubeMultiBoot_Quit`
(`src/intro.c`, `src/intro_frlg.c`): the GameCube link boot. No link
hardware natively.

### `src/rom_header.s` (not built)

| Symbol | Used by | On GBA |
|---|---|---|
| `RomHeaderGameCode` | `src/mystery_gift.c` | `const u8[4]`, the header's game code (`BPEE`, written by `gbafix`) |
| `RomHeaderSoftwareVersion` | `src/mystery_gift.c` | `const u8`, the header's version (0) |

### Functions the guarded modifications call (new platform API)

| Symbol | Declared in | Replaces |
|---|---|---|
| `void PlatformWaitCycles(u32 cycles)` | `src/multiboot.c` | `MultiBootWaitCycles`, a Thumb delay loop |
| `void PlatformJumpToAgbMainLoop(void)` | `test/test_runner.c` | `JumpToAgbMainLoop`: reload `sp` from `gAgbMainLoop_sp` and jump to `AgbMainLoop` (a `longjmp`). Test only. |
| `void PlatformTestRunnerExit(u32 exitCode)` | `test/test_runner.c` | `swi 0x3` (BIOS Stop) with the exit code in r0, which mgba-rom-test turns into its exit status. Test only. |
| `void PlatformCallWithStack(void (*entry)(void *context), void *context, void *stackTop)` | `test/test_runner_battle.c` | the battle runner's stack switch: call `entry(context)` with the stack pointer at `stackTop` (the top of the 1 KiB `DATA.stack`) and return when it returns. 1 KiB is a Thumb-sized stack; x86 frames are larger, so the platform may need to size it differently. Test only. |

### Platform entry point

`main`: the platform's `main()` (`AgbMain` is expansion's, in `src/main.c`).

### Resolved by the host C library and libgcc (nothing to do)

`memchr`, `memcpy`, `memmove`, `memset`, `strcmp`, `strlen`, `strstr`,
`__divdi3`, `__udivdi3` (64-bit division on 32-bit x86),
`__stack_chk_fail` (Ubuntu's gcc enables `-fstack-protector-strong`).
No symbol defined by expansion's objects collides with one exported by
the 32-bit `libc.so.6` or `libm.so.6`.

### Undefined but unreferenced (nothing to do)

`ANIM_BATTLER` (`data/battle_anim_scripts.o`) and `STR_VAR_1`,
`STR_VAR_2`, `STR_VAR_3` (`data/event_scripts.o`, `data/mystery_gift.o`)
appear as undefined symbols with no relocation against them; the linker
ignores them.

## Runtime hazards found while compiling (not link errors)

- `INTR_VECTOR` is `*(void **)0x3007FFC`. On 64-bit it stores 8 bytes at
  0x3007FFC, past the end of IWRAM at 0x3008000. 64-bit.
- `test/test_runner.c` sets `IRQ_LR` (0x3007F9C) and `DACS_LR`
  (0x3007FEC) to `((uintptr_t)JumpToAgbMainLoop & ~1) + 4`: an ARM return
  address into the routine, skipping its first instruction. The native
  timer and crash paths have to recognise this rather than jump there; the
  value is truncated to 32 bits on 64-bit.
- `src/m4a.c` keeps `MusicPlayerJumpTableCopy` with `asm("swi 0x2A")`; it
  is an unused static function that gcc drops at `-O1` and above, but
  `-O0` emits it and the x86 assembler rejects `swi`. The m4a work's
  guarded call removes it.
- Integer division by zero (and `INT_MIN / -1`) returns on the GBA
  (libgcc) and traps on x86; `platform/arm_division` completes the trap
  with libgcc's results by decoding the faulting `DIV`/`IDIV`: 32-bit
  operands on i386, and on x86-64 also REX prefixes, 64-bit operands
  (libgcc's `__udivdi3`/`__divdi3` results) and RIP-relative operands.
  On i386 a 64-bit division is a call to the host libgcc's `__udivdi3` or
  `__divdi3`, whose own 32-bit `DIV` by zero the handler completes with the
  32-bit results, so a 64-bit division by zero does not give the GBA's
  result there (u64 5 / 0 is 0 instead of 15). No game site reaches it.
  The 64-bit divisions in `src/` (all but MathUtil_Mul32's call
  `__udivdi3` or `__divdi3` in the GBA ROM):
  - `src/battle_script_commands.c:9312` (ApplyExperienceMultipliers):
    `sExperienceScalingFactors[faintedLevel + expGetterLevel + 10]`, an
    index of at least 10, where the table is 79 and up: never zero;
  - `src/pokemon_size_record.c:95-96` (GetMonSize): `sBigMonSizeTable`'s
    `unk2`, 1 to 150, and the constant 10;
  - `src/pokedex.c:4386` (ConvertMonWeightToImperialString) and
    `src/math_util.c:30` (MathUtil_Mul32, compiled to a shift): constant
    divisors;
  - `src/math_util.c:50` (MathUtil_Div32): returns 0 before it when y is 0.

  `src/pokemon_summary_screen.c:3079` stores into an `s64` but divides
  `u32`s (`__udivsi3`, the 32-bit results at both widths); its divisor is 0
  only for corrupt data, since every growth rate's experience table rises
  strictly from level 0 to 100. At 64 bits the decoder gives the GBA's
  64-bit results anyway.
- `src/libisagbprn.c` assertion paths emit `.hword 0xEFFF`, an ARM
  undefined instruction used as a breakpoint. On x86 those two bytes are
  an invalid instruction too, so a failed assertion still stops, with
  SIGILL.
