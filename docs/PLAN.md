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
| 7 | 64-bit audit and build; every gate again, plus the cross-width save round-trip gate (h); the refactoring pass again; 64-bit done | done 2026-10-09 (every gate green at both widths on main at 94f3c70f62; STATUS.md "Milestone 7") |
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

Done 2026-10-09: the order below was followed (the inventory, then
twelve worker branches, gate (h) as a platform-side converter, every
gate at 64 bits, the refactoring pass in three groups, the end-to-end
read); STATUS.md "Milestone 7" is the record and DECISIONS.md has the
decisions taken on the way. The plan is kept as written for the record.

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

## Milestone 8 plan: CI, Windows and macOS (started 2026-10-09)

What is known at the start: the repository's `.github/workflows/` is
upstream's (build.yml builds the ROM on pushes to `master` and
`upcoming`, branches this repository does not have, so it has never run
here; docs.yml and labels.yml likewise); Actions is enabled on the
private repository; no MinGW cross-compiler, Wine or macOS machine is
available in this environment (`docs/BUILDING.md` lists Linux only).

Order of work:

1. CI on Linux, both targets, in a workflow of this project's own
   (`.github/workflows/native.yml`; upstream's files are not touched):
   one job that is the gate as `docs/STATUS.md` records it, on
   `ubuntu-latest`: the prerequisites of BUILDING.md, the GBA ROM with its
   sha1 compared with the recorded one, `make -C platform gate`, the
   native builds at both widths plain and sanitized, both replays on each,
   `make check` at both widths plain and sanitized, the cross-width script
   both ways. Triggered on pushes to `main` and by hand
   (`workflow_dispatch`), not on every branch: a run is about an hour of
   a 4-core runner and the private repository's Actions minutes are the
   owner's. Tools and converted assets are cached between runs as
   upstream's workflow caches its tools.
   Done 2026-10-14 as two tiers (docs/DECISIONS.md): per push the 64-bit
   build, the platform gate and both 64-bit replays, 591 s warm (31 min
   cold with the suite, which moved to the nightly tier); nightly, six
   parallel jobs, 41 minutes wall clock. Follow-up, done: COMPILE_C writes
   the preprocessed text to a file (`obj.i`) that gcc compiles, so
   the game's C is now cacheable by ccache (only the file symbols differ; the
   effect is measured on the runner after the merge); the
   converted assets are cached too (`.github/actions/native-assets`).
   A small follow-up found while measuring: Makefile.native declares
   `.SECONDARY:` with no names, so every target is secondary and a
   deleted object is not rebuilt while the executable is newer than the
   sources (make's rule for missing intermediates); a fresh checkout is
   unaffected, but `rm` of an object by hand then needs the executable
   removed too. Name the secondary targets (the asset chains) instead.
2. An inventory of the Windows (MinGW) and macOS ports, no code (an Opus
   worker, to the session scratchpad and summarised here): what in
   `platform/` and the build glue is Linux-specific (the regions mapped
   at fixed addresses with mmap, the linker script's absolute sections
   and the non-PIE image at 0x40000000, the SIGFPE handler and its
   ucontext register access, the alternate signal stack, /proc and
   clock_gettime uses, the shell scripts of the gates, SDL2 from apt),
   what each platform offers instead (VirtualAlloc at fixed addresses and
   vectored exception handling on Windows; mmap with MAP_FIXED below the
   4 GiB mark, `-pagezero_size`, Mach exception ports or sigaction on
   macOS; the 16 KiB page size on Apple silicon against the GBA regions'
   sizes), and what can be verified here: a MinGW cross-build needs
   packages the owner must install (the exact commands go to STATUS.md
   "Needs the owner"); macOS can be built only on a GitHub macOS runner,
   whose minutes count tenfold, so nothing is attempted there before the
   inventory says what would run.
3. The Windows port, module by module, once the cross-compiler is
   installed, with the same gates where they can run (a cross-built
   executable cannot be run here without Wine; the CI runner `windows-latest`
   can run it); then macOS by the same method on a macOS runner, each
   gate recorded in STATUS.md as it passes. Neither is declared done
   without the replays and the suite passing on that platform.

### The inventory's findings (2026-10-09, `research/m8-inventory.md`)

Verdicts, each with its evidence in the file: Windows x86_64 can probably
keep the memory model as one PE image based at 0x01800000 with the
regions as sections and the code at 0x40000000 (GNU ld places it; whether
the NT loader accepts a 1000 MiB image with gaps is probe W1); Windows
i686 is not attempted (every C symbol gains an underscore, so the linker
scripts and the data symbols would need a second spelling, and 32-bit is
proven on Linux); macOS x86_64 can probably keep it (`-no_pie`,
`-pagezero_size`, segments at fixed addresses; probe M0), and runs on
Apple silicon through Rosetta; macOS arm64 cannot keep it (arm64
executables are always slid at load, and the build holds 41,946 4-byte
absolute address words, 190 sites that turn them into pointers, the
bit-25 script tag, and no divide trap), so arm64 needs a "rebased" model
where GBA addresses are offsets from a run-time base: its own milestone,
and the owner's decision (STATUS.md "Needs the owner").

Measured: 13 of the 59 platform modules touch the OS beyond stdio and
SDL (16 `sigaction` lines in 5 files, register access by index in two,
3 uses of `/proc/self/exe`, ucontext in `game_hooks`, all moved into the
host layer by task W2 on 2026-10-14; no run-time
`mmap`, no `clock_gettime`); the linker scripts do not work under the PE
linker as they are (`.rodata` is `.rdata`) and ld64 takes no GNU script;
the assembled data carries 28,982 ELF-only `.size` lines, 7 `%progbits`
section lines and 8 16-bit relocations with no COFF counterpart, and
macOS's assembler rejects the GNU macro syntax outright (20,134 errors
on one file), so the data is assembled with GNU as on Linux and
converted; MinGW GCC defaults to `-mms-bitfields` against 1,247 bitfield
members in expansion's headers (`-mno-ms-bitfields` keeps the Linux
layouts, saves included); `ptest.h`'s SIGALRM timeout and 16 POSIX-bound
tests needed Windows variants (done in W3: a flag-setting watchdog thread
and one test directory per OS family); 7 bash scripts (574 lines) with `stat -c`,
`md5sum` and `/proc/$$/fd` as the blockers.

Design: a thin `host` layer, one header per concern (memory, faults,
process, image bounds, stack switch, files) and one source per OS,
rather than `#ifdef` blocks in 13 modules and their tests; the replay's
symbol names from an `nm` map written at link time instead of an ELF
reader. Order: W1 (the Windows loader probe) and M0 (the macOS probes)
on the runners; W2 the host layer on Linux as a pure refactor with every
Linux gate; W3 the test timeout; W4 the cross-build glue (needs the
owner's apt line); W5 to W8 the Windows host, faults, stack switch and
test runner; W9 the Windows CI job; then M1 to M5 for macOS x86_64 by
the same method. Not attempted: Windows i686, sanitizers under MinGW,
building the game on Windows, the GBA ROM there, macOS arm64 before the
owner decides.
