# Modifications to upstream files

Policy. Expansion's files are modified only where a hardware assumption
cannot be isolated by link-time substitution, build-time filtering, or a
shim in `platform/`. Every modification is guarded by `PLATFORM_NATIVE`
(`#if PLATFORM_NATIVE` or `#if !PLATFORM_NATIVE`), is as small as it can
be, and has a row in the table below with the file, the reason, a summary
of the diff, and which milestone added it. The GBA build must produce
the same ROM before and after (check with `make compare` where
applicable, and `make check` always). A modification that changes GBA
behaviour is a bug.

When upgrading expansion (see `UPGRADING.md`), every row is re-verified:
the guard still applies, the surrounding code still has the assumption,
and the modification still compiles on both targets. Rows that become
unnecessary are removed, not left behind.

Flag for 64-bit: a row marked "64-bit" needs attention in the 64-bit
milestone, for example because it fixes a pointer width or an address
range.

Line numbers. The GBA ROM embeds `__LINE__` in assertion messages, so a
guarded block that adds lines to a `src/` file must be followed by
`#line N`, where N is the upstream line number of the first unchanged
line after the block, taken from `git show rhh/<tag>:src/<file>` (or
`main`), never from the physical line of the edited file, and
accounting for any earlier `#line` in the same file. Verify by
preprocessing with and without `-DPLATFORM_NATIVE=1` and comparing the
line markers at a few anchor lines after the block; the GBA ROM's sha1
must not change. Where a guard can replace a blank line on each side of
the original (`#if !PLATFORM_NATIVE` / `#endif`), no `#line` is needed.

## Whole-file replacements and moves

| File | Reason | What changed | Since |
|---|---|---|---|
| `README.md` | This repository's front page describes this project | Upstream's README moved to `docs/upstream-README.md`; new README written | M1 |
| `CONTRIBUTING.md` | Contribution rules differ (C rules, gates) | Upstream's moved to `docs/upstream-CONTRIBUTING.md`; new file written in M6 | M1 |
| `CREDITS.md` | Credit the reference ports and sources | A section added at the top; upstream content kept verbatim below | M1 |
| `LICENSE` | Scoped license for the platform layer | New file; upstream has none | M1 |

## Source modifications

| File | Reason | What changed | Since |
|---|---|---|---|
| `include/gba/defines.h` | `ARM_FUNC` is `__attribute__((target("arm")))`, which x86 gcc rejects | `#if PLATFORM_NATIVE` defines `ARM_FUNC` empty; `#else` keeps the original | M2 |
| `include/gba/syscall.h` | The MODERN `CpuFastSet` wrapper asserts `_Alignof >= 4`, true under `-mabi=apcs-gnu` (every struct 4-aligned) but not on x86 (DECISIONS.md "host ABI") | `#if MODERN` becomes `#if MODERN && !PLATFORM_NATIVE` with a comment; the platform's CpuFastSet accepts 2-aligned addresses | M2 |
| `src/random.c` | `Random32` is naked Thumb assembly | Original wrapped in `#if !PLATFORM_NATIVE` on its surrounding blank lines (line numbers unchanged, ROM identical); at end of file `#if PLATFORM_NATIVE` defines `Random32` as `_SFC32_Next(&gRngValue)`, which `test/random.c` checks for equivalence | M2 |
| `src/multiboot.c` | `MultiBootWaitCycles` is a naked Thumb delay loop | Same layout; native version calls `PlatformWaitCycles(cycles)`, declared in the file, provided by the platform | M2 |
| `src/librfu_intr.c` | `Callback_Dummy_M/S/ID` are naked `bx rN` trampolines | Same layout; native versions call the callback with the same arguments (M: (r0, r1) to the r2 callback; S: (r0) to r1; ID: none to r0) | M2 |
| `test/test_runner.c` | `JumpToAgbMainLoop` is naked ARM; `MgbaExit_` uses `register asm("r0")` and `swi 0x3`; `DACSEntry` is naked with `target("arm")` | In place `#if PLATFORM_NATIVE ... #else original #endif`: native versions call `PlatformJumpToAgbMainLoop()`, `PlatformTestRunnerExit(exitCode)`, and `DACSEntry` calls `DACSHandle()`. Shifts one `__LINE__` in the GBA test ELF ("Too many rigged RNGs" message); test results unaffected | M2 |
| `test/test_runner_battle.c` | Five naked Thumb `Invoke*TestFunctionWithStack` stack switches | In place; each native version packs its arguments into a struct and calls `PlatformCallWithStack(entry, &call, stack)` (+68 lines) | M2 |
| `include/test/overworld_script.h` | `OVERWORLD_SCRIPT` uses Thumb `mov %0, pc; b 1f` and `.include`s ARM-syntax macro files; natively, event macros named like x86 instructions (`call`, `nop`, `lock`, `inc`) shadow those instructions for the rest of the object | Native branch puts the script in `.pushsection .rodata.overworld_script` and loads its address with `mov $1b, %0` (needs the non-PIE link below 2 GiB); `.include`s `overworld_script_macros.inc`, which Makefile.native generates with the conflicting macros purged at its end; each `OVERWORLD_SCRIPT` includes `overworld_script_x86_macros.inc` before its script and `overworld_script_x86_purge.inc` after it (`platform/build_glue/overworld_macros.py` derives the conflict list with the host assembler) | M2, **64-bit** (absolute 32-bit address load) |
| `include/gba/macro.h` | A store to `DMAxCNT` cannot start a transfer in plain memory; every DMA macro (`DmaCopy*`, `DmaFill*`, `DmaClear*`, the `Large` and `Defvars` variants, `dma3.h`'s `Dma3*Large_`) reaches `DmaSetUnchecked` | Two `#if PLATFORM_NATIVE` / `#else` / `#endif` blocks, 22 lines added: `#include "platform/dma_hooks.h"` and a native `DmaSetUnchecked` that is `(void)dma_set(dmaNum, (const void *)(uintptr_t)(src), (void *)(uintptr_t)(dest), (u32)(control));` (callers pass integers such as `VRAM` as well as pointers; the result is discarded because the game never checks DMA and `dma_set` reports refusals itself); a native `DmaStop(dmaNum)` that is `dma_stop(dmaNum);`. `#else` branches byte-identical; ROM unchanged. Needs the repo root on the quote include path | M2, **64-bit** (SAD/DAD readback truncated to 32 bits; full addresses kept in module state) |
| `src/agb_flash_1m.c` | `IdentifyFlash` selects the driver by chip ID from `sSetupInfos`; the native chip needs an entry | Under `#if PLATFORM_NATIVE`: `extern const struct FlashSetupInfo NativeFlash;` and `&NativeFlash` as the first `sSetupInfos` entry (`platform/flash.c`, native ID 0x314E, maker byte non-zero because maker 0 ends the table) | M2 |
| `src/agb_flash.c` | `ReadFlashId`, `ReadFlash` and `VerifyFlashSector` write chip command bytes into what is natively the save image, bank-switch with `sectorNum %= 16` (wrong on the flat 128 KiB region) and run copies of their loops from a stack buffer | `ReadFlashId` (with its `DELAY` macro) and the block `ReadFlash_Core` through `VerifyFlashSectorNBytes` are inside `#if PLATFORM_NATIVE` / `#else` / `#endif`; the native branch keeps only a `VerifyFlashSector` prototype; `platform/flash.c` provides `ReadFlashId`, `ReadFlash`, `VerifyFlashSector` (bounds-checked). `VerifyFlashSectorNBytes` has no caller and no native version. `SetReadFlash1` stays compiled (unreachable natively) | M2, **64-bit** (`SetReadFlash1` casts pointers to u32) |
| `src/decompress.c` | Six call sites copy a function (`DecodeLOtANSLoop`, `DecodeSymDeltatANSLoop`, `DecodeInstructions`, `DeltaDecodeTileNumbers`, and the ARM `LZ77UnCompWRAMOptimized` from `src/decompress_asm.s`) into a stack buffer and call the copy; x86 cannot execute data and the lengths are ARM sizes | In place at each site: `#if PLATFORM_NATIVE` passes the function itself to the same `SwitchToArmCall*`, `#else` the original lines, `#endif`, then `#line N`. Natively `LZ77UnCompWRAMOptimized` is `platform/decompress_asm.c`'s C function; only its address is taken (+37 lines; ROM identical). Depends on `platform/decompress_asm.c` | M2 |
| `src/main.c` | The test runner's `JumpToAgbMainLoop` restarts `AgbMainLoop` from `AgbMain`'s frame by reloading `gAgbMainLoop_sp`; natively a `longjmp`, which needs a `setjmp` on that frame | Under `#if PLATFORM_NATIVE && TESTING`: `#include "platform/game_hooks.h"` and `setjmp(*PlatformAgbMainLoopTarget());` between saving `gAgbMainLoop_sp` and calling `AgbMainLoop` (+8 lines; ROM identical) | M2 |
| `src/m4a.c` | `SampleFreqSet` busy-waits for `VCOUNT` to leave and reach 159; natively `VCOUNT` reads 161 during game logic, so it hangs. The unused static `MusicPlayerJumpTableCopy` holds `swi 0x2A`, which x86 `as` rejects at -O0 | Both wrapped in `#if !PLATFORM_NATIVE` / `#endif` written on the blank lines around them (line numbers unchanged; ROM identical) | M2 |
| `src/librfu_stwi.c` | `AgbRFU_SoftReset` (from `InitRFU` at boot) polls a hardware timer; natively timers advance only at frame boundaries, so it hangs | Declares `PlatformWaitCycles` under `#if PLATFORM_NATIVE`; in place the native branch writes `REG_RCNT = 0x80A2` once and calls `PlatformWaitCycles(0x12 * 1024)` (which returns at once: game logic takes no emulated time); `#else` keeps the loop (+14 lines) | M2 |
| `src/librfu_sio32id.c` | `AgbRFU_checkID` (boot) polls a timer for 32 ticks per try: same hang | Same shape: `PlatformWaitCycles(32 * 1024)` in place of the poll loop (+13 lines) | M2 |
| `src/save.c` | `CopySaveSlotData` indexes `locations[id]` with the on-flash sector id before the signature check; an erased chip gives 0xFFFF (UBSan object-size); the GBA read garbage and then failed the signature check | `#if PLATFORM_NATIVE` skips a sector whose `id >= NUM_SECTORS_PER_SLOT` before the checksum; `#line 505` (ROM identical). Upstream hardening candidate | M2 |
| `src/decompress.c` (second change) | The tANS decoder's `LOOP_BITADVANCE` and the odd-count tail fetch one look-ahead word past the end of the bitstream when the last symbol ends on a word boundary; the word is never used, but natively it is a read past the end of the asset (`gIntroRayquaza_Gfx`, `gIntroLightning_Gfx`) | `#if PLATFORM_NATIVE` / `#else` around the macro and the tail: native keeps a file-static `sDataEnd` set in `SmolDecompressData` to `sDataPtr + header->bitstreamSize` and reads 0 at or past it; `#line` after each block (ROM identical). Benign on hardware; low-priority upstream note | M2 |
| `src/quickstart.c` | `gQuickstartHudPal` holds the 6-colour (12-byte) `.pal` but `LoadSpritePalette` loads 16 colours (32 bytes): an upstream bug (garbage in colours 6 to 15 on the GBA, a read past ROM data) | `#if PLATFORM_NATIVE` declares the three variants as `[16]` (zero-padded, first six colours identical); `#else` keeps the originals; `#line 29`. Worth reporting to rh-hideout | M2 |
| `src/siirtc.c` | The S-3511A is reached by bit-banging the cartridge GPIO port at 0x080000C4-0x080000C8, which the native build does not have; the `SiiRtc*` API is replaced at its own level by `platform/rtc` (RESEARCH.md "Timers and RTC") | Under `#if PLATFORM_NATIVE`: includes `platform/rtc.h`; `SiiRtcReset`, `GetStatus`, `SetStatus`, `GetDateTime`, `GetTime`, `SetAlarm` get native bodies that call `rtc_*` and convert to `struct SiiRtcInfo`, originals untouched in `#else`; `Unprotect`/`Protect` skip the GPIO calls; `sLocked` checks kept; `SiiRtcProbe` unchanged; the GPIO macros, `GPIOPortDirection` extern and the `WriteCommand`/`WriteData`/`ReadData`/`Enable/DisableGpioPortRead` helpers wrapped in `#if !PLATFORM_NATIVE`; `_Static_assert`s tie `RTC_STATUS_*` to `SIIRTCINFO_*`. Behaviour note: native `SiiRtcSetAlarm` returns FALSE on invalid BCD where the original returned TRUE (dead code in the game). Native build must put the repo root on the include path for `platform/rtc.h`. | M2 |
