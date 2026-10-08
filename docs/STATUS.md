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
| GBA memory regions can be mapped at their addresses in a native process on 32-bit and 64-bit, with ASan and UBSan | `research/memory-map-experiment.md` | works |
| Platform modules merged so far: `gba_memmap` (region layout via linker script), `ptest.h` self-test, `timer`, `transfer_bounds`, `input` | `make -C platform test BITS=32` and `BITS=64`, every test under ASan+UBSan with leak detection, after a Sonnet first-pass review and orchestrator merge | green on both widths at merge time |

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
  agent runtime offers Sonnet, Opus, Haiku and Fable only, so coding work
  runs on Sonnet and Opus. The orchestrator checks again at each
  milestone.

## Environment this was verified in

WSL2 Ubuntu 24.04.1, 32 CPUs, 31 GiB RAM, gcc 13.3 with multilib,
clang 18, SDL2 2.30.0 (amd64 and i386), gcc-arm-none-eabi 13.2.1,
valgrind, cppcheck 2.13. 32-bit ASan and UBSan runtimes verified to link
and run with SDL2.
