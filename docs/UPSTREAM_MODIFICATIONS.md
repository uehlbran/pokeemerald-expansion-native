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
| `include/test/overworld_script.h` | `OVERWORLD_SCRIPT` uses Thumb `mov %0, pc; b 1f` and `.include`s ARM-syntax macro files | Native branch puts the script in `.pushsection .rodata.overworld_script` and loads its address with `mov $1b, %0` (needs the non-PIE link below 2 GiB); `.include`s `overworld_script_macros.inc`, which Makefile.native generates | M2, **64-bit** (absolute 32-bit address load; link fails if the section lands above 2 GiB) |
| `include/gba/macro.h` | A store to `DMAxCNT` cannot start a transfer in plain memory; every DMA macro (`DmaCopy*`, `DmaFill*`, `DmaClear*`, the `Large` and `Defvars` variants, `dma3.h`'s `Dma3*Large_`) reaches `DmaSetUnchecked` | Two `#if PLATFORM_NATIVE` / `#else` / `#endif` blocks, 22 lines added: `#include "platform/dma_hooks.h"` and a native `DmaSetUnchecked` that is `(void)dma_set(dmaNum, (const void *)(uintptr_t)(src), (void *)(uintptr_t)(dest), (u32)(control));` (callers pass integers such as `VRAM` as well as pointers; the result is discarded because the game never checks DMA and `dma_set` reports refusals itself); a native `DmaStop(dmaNum)` that is `dma_stop(dmaNum);`. `#else` branches byte-identical; ROM unchanged. Needs the repo root on the quote include path | M2, **64-bit** (SAD/DAD readback truncated to 32 bits; full addresses kept in module state) |
| `src/siirtc.c` | The S-3511A is reached by bit-banging the cartridge GPIO port at 0x080000C4-0x080000C8, which the native build does not have; the `SiiRtc*` API is replaced at its own level by `platform/rtc` (RESEARCH.md "Timers and RTC") | Under `#if PLATFORM_NATIVE`: includes `platform/rtc.h`; `SiiRtcReset`, `GetStatus`, `SetStatus`, `GetDateTime`, `GetTime`, `SetAlarm` get native bodies that call `rtc_*` and convert to `struct SiiRtcInfo`, originals untouched in `#else`; `Unprotect`/`Protect` skip the GPIO calls; `sLocked` checks kept; `SiiRtcProbe` unchanged; the GPIO macros, `GPIOPortDirection` extern and the `WriteCommand`/`WriteData`/`ReadData`/`Enable/DisableGpioPortRead` helpers wrapped in `#if !PLATFORM_NATIVE`; `_Static_assert`s tie `RTC_STATUS_*` to `SIIRTCINFO_*`. Behaviour note: native `SiiRtcSetAlarm` returns FALSE on invalid BCD where the original returned TRUE (dead code in the game). Native build must put the repo root on the include path for `platform/rtc.h`. | M2 |
