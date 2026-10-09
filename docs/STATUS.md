# Status

Last updated 2026-10-09 (milestone 4 done: expansion's test suite runs natively and matches the GBA baseline; milestone 5 in progress).

This file is the honest ledger. A thing is listed as verified only with
the gate that proved it. Anything without a gate is unverified, whatever
we believe about it.

## Verified by a gate

| What | Gate | Result |
|---|---|---|
| GBA target builds from a clean clone of this repository | `make -j32` on 2026-10-08, WSL2 Ubuntu 24.04, gcc-arm-none-eabi 13.2.1 | `pokeemerald.gba`, 32 MiB, exit 0 |
| GBA target passes expansion's test suite | `TEST=1 make -j32 check` on 2026-10-08 | 5862 total: 5414 passed, 18 known failing, 9 expected failing, 421 to-do, exit 0 (`research/gba-baseline-1.17.1.md`) |
| Gate (d): the scripted, deterministic input replay drives the 32-bit build headless from boot through title, New Game, naming, the truck, Littleroot, the rival, the first battle, Birch's lab, saving, a process restart and Continue, verifying state after each step (27 dumps, 26 screenshots, name/map/species/party equal across the restart) | `tests/smoke/run.sh build/native32/pokeemerald-native out/` | green 2026-10-09, 35 s |
| The same replay on the sanitized build is clean (identical dumps and screenshots, empty stderr) | `tests/smoke/run.sh build/native32-san/pokeemerald-native out/` with `ASAN_OPTIONS=halt_on_error=0` | green 2026-10-09, after nine root-cause fixes in expansion's code (rows in `UPSTREAM_MODIFICATIONS.md`) |
| The 32-bit native executable links from a clean clone and boots headless through the intro to the title screen (9000 frames, exit 0, screenshot of the Emerald title screen) | `make -f Makefile.native -j32 native BITS=32` then `./build/native32/pokeemerald-native --headless --silent --unthrottled --frames 9000 --screenshot out.ppm` | green 2026-10-09 |
| The same boot under AddressSanitizer and UndefinedBehaviorSanitizer is clean through the title screen (no report; screenshot byte-identical to the plain build) | `make -f Makefile.native -j32 native BITS=32 SANITIZE=1` then the same run with `ASAN_OPTIONS=halt_on_error=0` | green 2026-10-09, after three root-cause fixes in expansion's code (see `UPSTREAM_MODIFICATIONS.md`) |
| Gate (b): expansion's test suite (5862 tests) against the native 32-bit build through expansion's own hydra: 5405 passed, 18 known failing, 9 expected failing, 421 to-do, 9 assumption failures, 0 failed, exit 0; per test equal to the GBA baseline except the 9 benchmarks, which cannot run natively (DECISIONS.md) | `make -f Makefile.native -j32 check BITS=32` | green 2026-10-09, 44 s (`platform/test_runner/SUITE-LOG.md`) |
| The same suite on the sanitized build gives the same per-test results; every sanitizer report has a root cause in expansion's code and is listed with it (SUITE-LOG.md, "The sanitized run") | `make -f Makefile.native -j32 check BITS=32 SANITIZE=1` | green 2026-10-09, 76 s |
| The native runner's crash recovery (fatal signal, re-exec with the persistent state) works | the suite's own "Tests resume after CRASH" and "fatalf counts as CRASH" tests, in every suite run | green |
| The native runner's 60-second (emulated) test timeout works for function and battle tests | a throwaway probe run by the test-runner worker (SUITE-LOG.md, "The timeout, end to end"); not part of the suite | observed once |
| Soft reset (A+B+Start+Select) re-enters AgbMain and continues | held under gdb from frame 300, plain and sanitized, 1200 frames exit 0 (integration worker's run, not yet a scripted gate) | observed once |
| Audio is mixed and non-silent in the intro | peak sample 1855 at frame 700 (integration worker's measurement) | observed once |
| Every native object compiles from a clean clone with the host compiler, 32-bit, TEST=0 and TEST=1 | `make -f Makefile.native -j32 objects BITS=32` (and `TEST=1`) | 928 and 1895 objects, 0 warnings; no link yet |
| GBA ROM unchanged by the guarded modifications | sha1 of `pokeemerald.gba` before and after | identical (e2d4600d4e7b1269ac50c28bc96ece2b758ded9e); `make check` equal to the baseline |
| GBA memory regions can be mapped at their addresses in a native process on 32-bit and 64-bit, with ASan and UBSan | `research/memory-map-experiment.md` | works |
| Every platform module is merged: `gba_memmap` (region layout via linker script), `ptest.h` self-test, `timer`, `transfer_bounds`, `input`, `rtc`, `video`, `audio`, `frame_pacer`, `interrupt`, `psg`, `ppu`, `dma`, `flash`, `save_file`, `bios`, `lcd_status`, `m4a_output`, `sound_mix`, `replay`, `inspect`, `restart`, `symbols`, `debug_channel`, `crash_reboot`, `arm_division`, and the `m4a` engine translation (also proven by a differential test against the ARM original under qemu-arm, run by hand) | `make -C platform test BITS=32` and `BITS=64`, every test under ASan+UBSan with leak detection, after a Sonnet first-pass review and orchestrator merge | green on both widths at merge time |

## Not verified

The engine's logic is now covered by expansion's suite (gate b) and the
opening by the replay (gate d); what no gate covers: the renderer and the
sound mixer beyond the opening's screenshots and the intro's audio
(expansion's suite runs headless and does not look at the frame buffer),
everything past Birch's lab in play, every menu the replay does not open
(the debug menu gate, milestone 5, covers the main ones), the windowed
path (a real SDL window and audio device ran only with SDL's dummy
drivers), and 64-bit, which does not link yet (two expansion layout
asserts; milestone 7). Interchange with GBA `.sav` files is not supported
(host struct ABI; DECISIONS.md). The m4a engine is proven against the
ARM original by a differential test run by hand, not by the standard
gate. Known: save-file bytes differ between runs by nine uninitialised
bytes in the first party Pokémon (expansion's creation code reads
uninitialised stack; dumps are unaffected). Known cosmetic: a crashing
runner's `crash_reboot` line on stderr can interleave with the hydra's
stdout line for another runner.

## Known broken

Nothing yet; nothing exists to be broken.

## Gates and their state

| Gate | State |
|---|---|
| (a) native build from a clean clone with documented prerequisites | green for 32-bit (`docs/BUILDING.md`); 64-bit not yet |
| (b) expansion's test suite against the native binary, same results as GBA | green on 32-bit, plain and sanitized: per test the GBA baseline but for the 9 benchmarks (assumption failures by decision) |
| (h) a save written by the 32-bit native build loads correctly in the 64-bit native build (cross-width round-trip; the save container is versioned so this stays true) | milestone 7, not started |
| (c) GBA target builds and passes the suite | green (above) |
| (d) deterministic input-replay smoke test through the opening | green on 32-bit, plain and sanitized |
| (e) debug-menu gate | not started |
| (f) unit tests and smoke boot clean under sanitizers | unit tests green under ASan+UBSan on 32 and 64-bit; the opening replay and the full suite are clean under ASan+UBSan on 32-bit |
| (g) this file | current |

## For the repository owner

- Published at https://github.com/uehlbran/pokeemerald-expansion-native
  (private for now); `origin` is that repository and main is pushed at
  every milestone, never force-pushed.
- GitHub description and topics are set as the brief asked ("Native PC
  build of pokeemerald-expansion: SDL2 platform layer, no emulator";
  `pokeemerald`, `pokeemerald-expansion`, `decomp`, `sdl2`, `pokemon`).
- The brief asked for DeepSeek 4.1 Flash coding workers; this session's
  agent runtime offers Sonnet, Opus, Haiku and Fable only (checked again
  2026-10-08 evening), so the fallback applies: coding workers default to
  Sonnet, with Opus reserved for the pieces that need it (the m4a
  translation, the test-runner protocol, the first boot). The owner asked
  for the model used per branch to be visible; it is in the table below.
- Upstream bugs found by the native suite, worth reporting to rh-hideout:
  the rows marked "worth reporting upstream" in `UPSTREAM_MODIFICATIONS.md`
  and the two tables in `platform/test_runner/SUITE-LOG.md` (class 6 and
  "The sanitized run"). None changes a test result on the GBA.

## Which model did what

| Branch | Model | Reviewer |
|---|---|---|
| foundation (ptest.h, platform Makefile, gba_memmap, region layout) | Opus | Sonnet |
| build (Makefile.native, asm filter, eight guarded modifications) | Opus | Sonnet |
| m4a (m4a_1.s translated to C) | Opus | Sonnet |
| timer | Sonnet | Sonnet |
| transfer_bounds | Sonnet | Sonnet |
| input | Sonnet | Sonnet |
| rtc | Sonnet | Sonnet |
| sdlout (video, audio, frame_pacer) | Sonnet | Sonnet |
| interrupt | Opus | Sonnet |
| psg | Opus | Sonnet |
| save (flash, save_file) | Opus | Sonnet |
| ppu | Opus | Sonnet |
| dma | Opus | Sonnet |
| bios | Opus | Sonnet |
| integration (main, frame loop, link, first boot) | Opus | Sonnet (two passes) |
| sanfix (three sanitizer findings in game code) | Sonnet | Sonnet |
| replay (milestone 3: replay, inspect, restart, smoke test) | Sonnet | Sonnet (two passes) |
| testrunner (milestone 4: debug channel, crash reboot, arm_division, hydra launcher, the suite's fixes) | Opus | Sonnet |
| debugmenu (milestone 5: debug-menu gate) | Sonnet | pending |
| Research, synthesis, decisions, merges | Fable (orchestrator) | |

## Environment this was verified in

WSL2 Ubuntu 24.04.1, 32 CPUs, 31 GiB RAM, gcc 13.3 with multilib,
clang 18, SDL2 2.30.0 (amd64 and i386), gcc-arm-none-eabi 13.2.1,
valgrind, cppcheck 2.13. 32-bit ASan and UBSan runtimes verified to link
and run with SDL2.
