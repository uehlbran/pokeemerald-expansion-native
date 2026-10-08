# Plan

The orchestrator's working plan. `STATUS.md` says what is verified;
this file says what is being done and in what order. A fresh session
resumes by reading both and first posting what it believes is in flight.

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Research, `RESEARCH.md`, per-subsystem decisions, license | done 2026-10-08 |
| 2 | Fork, build system, 32-bit build that boots to the title screen (plain and sanitized); playing the opening needs milestone 3's replay | done 2026-10-09 |
| 3 | Input-replay smoke test, save round-trip, root-cause fixes | in progress |
| 4 | Native test-runner integration; suite matches the GBA baseline | not started |
| 5 | Debug-menu gate | not started |
| 6 | Unit tests under sanitizers, clang-tidy, cppcheck, `.clang-format` enforced in the gate, docs, the refactoring pass (below); 32-bit done | not started |
| 7 | 64-bit audit and build; every gate again, plus the cross-width save round-trip gate (h); the refactoring pass again; 64-bit done | not started |
| 8 | CI for both targets; Windows (MinGW) and macOS | later |

## Milestone 2 work breakdown

State on 2026-10-09: milestone 2 done; the native build boots to the
title screen plain and sanitized. Milestone 3 (replay smoke test) is
being dispatched to one worker in worktree replay.

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

## Milestone 3 design: the input-replay smoke test (gate d)

The replay is a text script of frames and key states consumed by the
input module's SCRIPT source, plus checkpoints that read game state
through a small platform inspection API. One worker (Sonnet) builds:

- `platform/replay.{h,c}`: parses a script (`frame N keys A,START`,
  `hold`/`release` forms, `check <name>` lines), feeds
  `input_script_set` each frame, and at each `check` calls an
  inspection hook; deterministic: the RTC time source is fixed by a
  `--rtc-fixed EPOCH` option, the frame pacer is unthrottled, audio
  silent, video headless.
- `platform/inspect.{h,c}`: reads named facts from expansion's globals
  through the symbols the game exports (`gMain.callback2` address
  compared against known callbacks, `gSaveBlock2Ptr->playerName`,
  `gSaveBlock1Ptr->location`, party count and species, the current map
  group and number, the save sector's counter) and writes them as
  `key=value` lines for the script's `expect` checks. Guarded reads
  only; no modification of expansion.
- `tests/smoke/opening.replay`: boot, title, new game, the professor's
  speech, truck, Littleroot, the first battle (Poochyena in the
  professor's bag), save, then `restart` (a process re-exec with the
  same save path) and load, with `expect` checks after each step.
- `tests/smoke/run.sh`: runs the plain and the sanitized builds and
  compares the per-step screenshots and state dumps with the checked-in
  expectations; the gate.

Bugs found on that path are fixed at the root and recorded.

## Worker protocol

Each worker gets its own worktree under `../pen-worktrees/<task>` on
branch `work/<task>`, the C rules verbatim, the module contract from
`ARCHITECTURE.md`, and writes `../pen-worktrees/<task>.status` as a
single line. Diffs get mechanical checks (both targets build where the
toolchain exists, all tests pass under sanitizers, no new warnings) and a
Sonnet first-pass review; the orchestrator reads what is flagged and
merges.

## Standing instruction: the refactoring pass (owner, 2026-10-09)

Before declaring 32-bit done (end of milestone 6) and again before
declaring 64-bit done (end of milestone 7), run a dedicated refactoring
pass over everything we wrote: the platform layer, build glue and tests,
reviewed as if it were someone else's code submitted for merge into a
codebase maintained for decades, against `docs/C-RULES.md`. Look for:
logic that exists in two modules because two workers needed it; named
constants that should be shared; functions that do more than one thing;
error paths patched rather than designed; anything marked temporary or
TODO; test files that drifted from the structure of the module they
test; headers exposing more than the module's real interface. Remove
dead code and anything kept just in case. Every change is
behaviour-preserving and proven by the existing tests and gates; where a
refactor reveals a missing test, write it first. Do not touch expansion's
own files in this pass; any upstream modification that turns out to be
unnecessary is removed and struck from `UPSTREAM_MODIFICATIONS.md`.
Dispatch the pass as module-sized worker tasks reviewed like everything
else, then the orchestrator reads the platform layer end to end once and
records in `STATUS.md` that the pass happened and what it changed.
Milestone 6 also adds a checked-in `.clang-format` applied to our
directories only, enforced in the gate.
