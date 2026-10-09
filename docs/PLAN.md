# Plan

The orchestrator's working plan. `STATUS.md` says what is verified;
this file says what is being done and in what order. A fresh session
resumes by reading both and first posting what it believes is in flight.

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Research, `RESEARCH.md`, per-subsystem decisions, license | done 2026-10-08 |
| 2 | Fork, build system, 32-bit build that boots to the title screen (plain and sanitized); playing the opening needs milestone 3's replay | done 2026-10-09 |
| 3 | Input-replay smoke test, save round-trip, root-cause fixes | done 2026-10-09 |
| 4 | Native test-runner integration; suite matches the GBA baseline | done 2026-10-09 (per test the GBA baseline but for the 9 benchmarks; sanitized run clean) |
| 5 | Debug-menu gate | done 2026-10-09 |
| 6 | Unit tests under sanitizers, clang-tidy, cppcheck, `.clang-format` enforced in the gate, docs, the refactoring pass (below); 32-bit done | done 2026-10-09 (every gate green on main at 61ae8181f7 and recorded in STATUS.md) |
| 7 | 64-bit audit and build; every gate again, plus the cross-width save round-trip gate (h); the refactoring pass again; 64-bit done | in progress 2026-10-09 (inventory first, see "Milestone 7 plan") |
| 8 | CI for both targets; Windows (MinGW) and macOS | later |

## Milestone 2 work breakdown

State on 2026-10-09: milestone 2 done; the native build boots to the
title screen plain and sanitized. Milestone 3 (replay smoke test,
worktree replay, Sonnet) and the milestone 4 test-runner protocol
(worktree testrunner, Opus; independent files, so started in parallel)
are in progress.

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

Platform gate (every change under `platform/`): `make -C platform gate`,
which is `format-check` (clang-format, `.clang-format`), `lint` at both
widths (clang-tidy with `.clang-tidy`, cppcheck; any finding fails) and
the sanitized unit tests at both widths. It comes before the native
build, the two replays, the sanitized replays and `make check` that the
other gates run.

## Milestone 7 plan: the 64-bit build (started 2026-10-09)

Order of work. First an inventory, no code (an Opus worker, written to
the session scratchpad and then summarised here): every place the 32-bit
assumption is flagged ("flagged 64-bit" in the platform sources, the
m4a layout contract's second group, the `.4byte` pointers the assembled
data keeps, the two expansion layout asserts that stop the 64-bit link,
expansion's own pointer-to-int casts the sanitized 32-bit run did not
reach, `arm_division`'s i386-only decoder, `symbols.c`'s ELF class), and
for each the fix that makes the native build do what the GBA ends up
doing. Then the worker tasks, module-sized, reviewed like everything
else, in this order:

1. the link: the two layout asserts, guarded, with the GBA result kept
   (a struct whose size the GBA code hard-codes keeps that meaning
   through a native-only accessor, not a changed assert);
2. the data: assembled data with 4-byte pointers (`.4byte` in data/*.s
   and the songs) against 8-byte C pointers, and the m4a layout contract;
3. the platform modules that convert between addresses and integers
   (`gba_memmap`, `transfer_bounds`, `script_tag`, `dma`, `bios`,
   `symbols`, `arm_division` on x86-64, the INTR_VECTOR write);
4. the save container: gate (h), a save written by the 32-bit build
   loads in the 64-bit build and back, through the versioned container
   (`SAVE_FILE_LAYOUT_ID`) and a layout conversion where the host struct
   layouts differ;
5. every gate at 64 bits (a, b plain and sanitized, d, e plain and
   sanitized, f, h), then the refactoring pass again, then "64-bit done"
   in STATUS.md.

What is known before the inventory: the 64-bit build compiles every
object; the link fails on two expansion `_Static_assert`s about struct
sizes; with those disabled as an experiment it links (not run). The
platform's unit tests already pass at 64 bits.
