# Status

Last updated 2026-10-08 evening (milestone 2 in progress: platform modules landing).

This file is the honest ledger. A thing is listed as verified only with
the gate that proved it. Anything without a gate is unverified, whatever
we believe about it.

## Verified by a gate

| What | Gate | Result |
|---|---|---|
| GBA target builds from a clean clone of this repository | `make -j32` on 2026-10-08, WSL2 Ubuntu 24.04, gcc-arm-none-eabi 13.2.1 | `pokeemerald.gba`, 32 MiB, exit 0 |
| GBA target passes expansion's test suite | `TEST=1 make -j32 check` on 2026-10-08 | 5862 total: 5414 passed, 18 known failing, 9 expected failing, 421 to-do, exit 0 (`research/gba-baseline-1.17.1.md`) |
| Every native object compiles from a clean clone with the host compiler, 32-bit, TEST=0 and TEST=1 | `make -f Makefile.native -j32 objects BITS=32` (and `TEST=1`) | 928 and 1895 objects, 0 warnings; no link yet |
| GBA ROM unchanged by the guarded modifications | sha1 of `pokeemerald.gba` before and after | identical (e2d4600d4e7b1269ac50c28bc96ece2b758ded9e); `make check` equal to the baseline |
| GBA memory regions can be mapped at their addresses in a native process on 32-bit and 64-bit, with ASan and UBSan | `research/memory-map-experiment.md` | works |
| Platform modules merged so far: `gba_memmap` (region layout via linker script), `ptest.h` self-test, `timer`, `transfer_bounds`, `input`, `rtc`, `video`, `audio`, `frame_pacer` | `make -C platform test BITS=32` and `BITS=64`, every test under ASan+UBSan with leak detection, after a Sonnet first-pass review and orchestrator merge | green on both widths at merge time |

## Not verified

Everything else. There is no native build yet: no executable boots,
renders, sounds or saves. The merged modules are verified only by their
own unit tests, not by running the game. Modules still on worker
branches (interrupt, rtc, psg, flash/save_file, ppu, video/audio/pacer,
dma, bios, the m4a engine translation, Makefile.native) are unverified.

## Known broken

Nothing yet; nothing exists to be broken.

## Gates and their state

| Gate | State |
|---|---|
| (a) native build from a clean clone with documented prerequisites | no native build yet |
| (b) expansion's test suite against the native binary, same results as GBA | not started |
| (h) a save written by the 32-bit native build loads correctly in the 64-bit native build (cross-width round-trip; the save container is versioned so this stays true) | milestone 7, not started |
| (c) GBA target builds and passes the suite | green (above) |
| (d) deterministic input-replay smoke test through the opening | not started |
| (e) debug-menu gate | not started |
| (f) unit tests and smoke boot clean under sanitizers | no tests yet |
| (g) this file | current |

## For the repository owner

- No `origin` remote exists. To publish, run
  `git remote add origin <your-github-url>` and `git push -u origin main`;
  the orchestrator will push at each milestone once it exists.
- GitHub description to set: "Native PC build of pokeemerald-expansion:
  SDL2 platform layer, no emulator". Topics: `pokeemerald`,
  `pokeemerald-expansion`, `decomp`, `sdl2`, `pokemon`.
- The brief asked for DeepSeek 4.1 Flash coding workers; this session's
  agent runtime offers Sonnet, Opus, Haiku and Fable only (checked again
  2026-10-08 evening), so the fallback applies: coding workers default to
  Sonnet, with Opus reserved for the pieces that need it (the m4a
  translation, the test-runner protocol, the first boot). The owner asked
  for the model used per branch to be visible; it is in the table below.

## Which model did what

| Branch | Model | Reviewer |
|---|---|---|
| foundation (ptest.h, platform Makefile, gba_memmap, region layout) | Opus | Sonnet |
| build (Makefile.native, asm filter, eight guarded modifications) | Opus | Sonnet |
| m4a (m4a_1.s translated to C) | Opus | pending |
| timer | Sonnet | Sonnet |
| transfer_bounds | Sonnet | Sonnet |
| input | Sonnet | Sonnet |
| rtc | Sonnet | Sonnet |
| sdlout (video, audio, frame_pacer) | Sonnet | Sonnet |
| interrupt | Opus | Sonnet |
| psg | Opus | pending |
| save (flash, save_file) | Opus | pending |
| ppu | Opus | pending |
| dma | Opus | pending |
| bios | Opus | pending |
| integration (main, frame loop, link) | Opus | pending |
| Research, synthesis, decisions, merges | Fable (orchestrator) | |

## Environment this was verified in

WSL2 Ubuntu 24.04.1, 32 CPUs, 31 GiB RAM, gcc 13.3 with multilib,
clang 18, SDL2 2.30.0 (amd64 and i386), gcc-arm-none-eabi 13.2.1,
valgrind, cppcheck 2.13. 32-bit ASan and UBSan runtimes verified to link
and run with SDL2.
