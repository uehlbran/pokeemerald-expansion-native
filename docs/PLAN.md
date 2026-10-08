# Plan

The orchestrator's working plan. `STATUS.md` says what is verified;
this file says what is being done and in what order. A fresh session
resumes by reading both and first posting what it believes is in flight.

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Research, `RESEARCH.md`, per-subsystem decisions, license | done 2026-10-08 |
| 2 | Fork, build system, 32-bit build that boots and plays the opening | in progress |
| 3 | Input-replay smoke test, save round-trip, root-cause fixes | not started |
| 4 | Native test-runner integration; suite matches the GBA baseline | not started |
| 5 | Debug-menu gate | not started |
| 6 | Unit tests under sanitizers, clang-tidy, cppcheck, docs; 32-bit done | not started |
| 7 | 64-bit audit and build; every gate again; 64-bit done | not started |
| 8 | CI for both targets; Windows (MinGW) and macOS | later |

## Milestone 2 work breakdown

State on 2026-10-08 evening: merged on main: foundation, timer,
transfer_bounds, input. In review or fixing: sdlout, rtc. Working:
interrupt, psg, save, ppu, dma, bios, W1-build, W1-m4a. Next: merge each
as it lands, then main/frame loop and the first link.

Wave 1 (no dependencies between them):

- W1-foundation: `platform/Makefile` with the warning and sanitizer
  flags, `platform/test/ptest.h`, `platform/gba_memmap.{h,c}` with the
  region constants and the fixed-address mapping, and its tests. Opus.
- W1-build: `Makefile.native` compiling expansion's `src/**/*.c` with the
  host compiler, assembling `data/*.s` and song data with the host
  assembler after the build-time filter, producing objects for a 32-bit
  link (link fails until the platform modules exist; the target is
  "every object compiles"). Opus.
- W1-m4a: C translation of `src/m4a_1.s` into `platform/m4a/`, with
  static assertions against `include/gba/m4a_internal.h`. Opus.

Wave 2 (after W1-foundation merged): `transfer_bounds` (Sonnet, shared
range policy for DMA and CpuSet), `interrupt` (Opus), `timer` (Sonnet),
`rtc` (Sonnet, includes the guarded src/siirtc.c change), `input`
(Sonnet), `psg` (Opus), `flash`+`save_file` (Opus, includes the guarded
agb_flash changes), `ppu` (Opus), `video`+`audio`+`frame_pacer` (Sonnet).
Then, after `transfer_bounds` merges: `dma` (Opus, includes the guarded
include/gba/macro.h change) and `bios` (Opus). Then `main` and the frame
loop, and the link.

Wave 3: integration and boot debugging on main (orchestrator plus one
Opus worker), recorded modifications, `STATUS.md`.

## Worker protocol

Each worker gets its own worktree under `../pen-worktrees/<task>` on
branch `work/<task>`, the C rules verbatim, the module contract from
`ARCHITECTURE.md`, and writes `../pen-worktrees/<task>.status` as a
single line. Diffs get mechanical checks (both targets build where the
toolchain exists, all tests pass under sanitizers, no new warnings) and a
Sonnet first-pass review; the orchestrator reads what is flagged and
merges.
