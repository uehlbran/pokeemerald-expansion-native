# Contributing

This repository is the native PC build of pokeemerald-expansion. It holds two
kinds of code. Expansion's (`src/`, `include/`, `data/`, `test/` and the
rest) is pinned at `expansion/1.17.1`. Ours is `platform/`, `tests/`,
`Makefile.native`, the build glue and the documents. The rules differ for the
two; `docs/ARCHITECTURE.md` explains the boundary.

Upstream's contribution guide is kept at `docs/upstream-CONTRIBUTING.md`. It
applies to rh-hideout's repository, not to this one.

## How this code was written

The platform layer was written with AI coding agents under human direction.
It is held to the gates below: the test suite against the GBA baseline, the
replays and the sanitizers. Independent reviews also read it and applied
mutations to its tests; the commit messages record what they found, and the
mutants they applied and killed (search `git log` for "mutant"). The same
rules apply to any contribution, however it was written.

## Building

`docs/BUILDING.md` has the prerequisites and every command. In short:

```
make -f Makefile.native -j"$(nproc)" native BITS=32
make -C platform test BITS=32
```

## The rules

`docs/C-RULES.md` is the full text and applies to everything we write.
Rules 1 to 4 summarize its sections "Style and static analysis", "C safety
and correctness", "Testing" and "Licensing rule"; rules 5 and 6 are from the policy at the top of
`docs/UPSTREAM_MODIFICATIONS.md`.

1. Warnings are errors in our files: `-std=gnu17 -Wall -Wextra -Wshadow
   -Wstrict-prototypes -Wconversion -Werror`. Expansion's headers are
   included with `-isystem` or `-idirafter`, so their warnings are not ours.
   Do not edit expansion's code to quiet one.
2. Every unit test runs under AddressSanitizer, UndefinedBehaviorSanitizer
   and leak detection. A run with a sanitizer report is not green. A leak
   suppression needs a written reason in the repository.
3. Test first, from GBATEK. The test for a piece of hardware behaviour is
   written from the specification and fails against an empty module; then
   the implementation is made to pass it. The first version includes the
   edge cases: boundary addresses, zero and maximum lengths, wraparound,
   invalid input, error-path cleanup.
4. Do not copy code from the reference ports (pokeemerald-native,
   pokeemerald-multiplatform, pokemon-regionalidades). Two of them credit
   GPL and MPL emulators as the source of their code, and that code cannot
   be relicensed under this project's MIT license. Read the ports for
   structure and pitfalls; write ours from GBATEK and from expansion's own
   sources.
5. One guard macro, `PLATFORM_NATIVE`, for every change to an expansion
   file. The change is as small as it can be, sits inside
   `#if PLATFORM_NATIVE` (or `#if !PLATFORM_NATIVE`), keeps the GBA build's
   output unchanged, and has a row in `docs/UPSTREAM_MODIFICATIONS.md`.
   Where the block adds lines to a `src/` file, `#line N` follows it, N being
   the upstream line number of the first unchanged line after it, so the
   `__LINE__` values baked into the ROM do not move. The top of that file
   gives the convention and how to check it. Before changing an expansion
   file at all, ask whether a link-time substitution, a build-time filter or
   a shim in `platform/` can do the job; the policy prefers those.
6. Where the width of a pointer matters, test it inside the native guard:
   `#if __SIZEOF_POINTER__ == 8` in C and `.ifdef NATIVE_POINTER_SIZE_8` in
   assembly (defined for `BITS=64` only). Do not use `UINTPTR_MAX`: it is
   undefined in a header included before `<stdint.h>`, which makes the test
   silently false. The block keeps the 32-bit text on its own lines, first,
   so that the sanitized 32-bit executable (which embeds line numbers) does
   not change:

   ```
   #if PLATFORM_NATIVE
   #if __SIZEOF_POINTER__ != 8
   #line 123
   <the original text>
   #else
   <the 64-bit text>
   #endif
   #endif
   #line 124
   ```

   Each such block has its row in `docs/UPSTREAM_MODIFICATIONS.md`, marked
   64-bit.

## Gates

A change is merged when the gates that apply to it pass. `docs/BUILDING.md`,
"Gates", names them (a) to (h) and "platform", with the command for each; this
section says when each applies. Documents are part of a change: one that
alters behaviour updates the documents describing it in the same branch
(gate (g)).

- Gate (a), a native build from a clean clone with the documented
  prerequisites, is run before a release and after a large change.
- "platform" (`make -C platform gate`) is run by every change under
  `platform/` or `tools/patchelf`. It covers format, lint, the unit tests at
  both widths and the `tools-test` step, which runs `tools/patchelf` on an
  ELF32 and an ELF64 executable.
- Gates (d) and (e), the two replays, are run plain and sanitized on
  `build/native32/`, `build/native32-san/`, `build/native64/` and
  `build/native64-san/` (with `ASAN_OPTIONS=halt_on_error=0`). The plain and
  sanitized runs, and the two widths, must produce the same dumps and
  screenshots.
- Gate (b), expansion's suite on the native build, must equal the GBA
  baseline (`docs/research/gba-baseline-1.17.1.md`) per test, except the nine
  benchmarks, which are assumption failures natively by decision. The counts
  are the same at `BITS=64`: a width-dependent result is a bug in a guard,
  not a new baseline. A sanitized comparison of widths uses
  `ASAN_OPTIONS=suppress_equal_pcs=0` (`platform/test_runner/SUITE-LOG.md`).
- Gate (c), the GBA target: the ROM is unchanged (`sha1sum pokeemerald.gba`
  against the value for 1.17.1 in `docs/BUILDING.md`, "GBA target"), and
  `TEST=1 make -j"$(nproc)" check` equals the baseline. A change that moves
  the ROM changes GBA behaviour and is a bug.
- Gate (f), the sanitizers, is the sanitized runs of the unit tests, the
  replays and the suite. A sanitizer report fails the run.
- Gate (h), the cross-width save round trip
  (`tests/smoke/run_cross_width.sh`), is run by a change to the save format,
  `platform/save_*` or `SaveBlock1`.

Two more checks have no letter in that table. A change to an expansion file
under the guard must leave the four 32-bit executables (`native32`,
`native32-san`, `native32-test`, `native32-test-san`) byte-identical. They
embed the worktree path, so build the base commit and your branch in the same
worktree and compare those. A change under `platform/` or to the test runner
legitimately changes every executable that links it; then the symbol sets
(`nm`) must differ only by what you added, and gates (b), (d), (e) and
"platform" are green at both widths. The 64-bit link's own checks
(`check_relocations.py`, `check_data_layout.py`,
`check_battle_script_operands.py`) must pass: a new assembled table, a new
battle script command or a new pointer in a saved struct stops the link until
it is classified, widened or given a converter row (`docs/UPGRADING.md`).

Run by hand, because they need tools most machines lack, and recorded in
the repository: after a change to the m4a engine, `make -C platform/m4a/difftest run`
(the C against the original assembly under qemu-arm, needs
`arm-none-eabi-gcc` with newlib and `qemu-arm`); after a change to
`platform/arm_division.c`, `make -C platform/arm_division_libgcc run`
(rebuilds the record of the GBA libgcc's division results and compares it).

Hygiene, not a gate: `git diff --check` is clean.

If a gate result changes on purpose, say why in the commit message.
`tests/smoke/run.sh ... --update` rewrites the expected files; read the diff
of `tests/smoke/expected/` before committing it.

## Adding a platform module

1. `platform/<name>.c` and `platform/<name>.h`. The header is the only
   public interface; everything else in the `.c` is `static`. The file
   header comment says what hardware the module stands in for, what the
   real hardware did, and which GBATEK section (with link) or other
   reference it follows.
2. Named constants for every register, size, timing and mode, with the
   GBATEK section in a comment. Paired `init` and `shutdown`, with
   `shutdown` safe on a partially initialised module. Error codes returned,
   never `exit()`.
3. `platform/test/test_<name>.c`, written first. Other platform modules it
   needs are named on a `// deps:` line near the top, paths relative to
   `platform/` (for example `// deps: gba_memmap.c interrupt.c`). No
   Makefile edit is needed: `platform/Makefile` picks up every
   `platform/*.c` and every `platform/test/test_*.c`.
4. A row in the module table of `docs/ARCHITECTURE.md`, and a section in
   `platform/README.md` if the module has behaviour a reader cannot get
   from its header.
5. If it joins the frame, say where in `frame.h`'s ordered list.

## Adding a replay fact or a smoke step

Facts are what a replay script can `expect` and `wait-until`. A fact that
reads expansion's variables goes in `platform/inspect.c` (or
`inspect_menu.c` for menus), is listed in `platform/inspect.h`, and a fact
with an argument (`flag:ID`) also gets its range in `inspect_args.c`, taken
from expansion's own constants, with a case in
`platform/test/test_inspect_args.c`. A fact that needs no game variable
(decoding, naming) goes in `inspect_decode.c`, which is unit tested.
Facts that read a save block must be safe before the save blocks exist;
`tests/smoke/opening_start.replay` checks that at frame 1.

A smoke step is a command in `tests/smoke/opening.replay` or
`debug_menu.replay`; the grammar is in `platform/replay.h` and the rules
for writing steps (wait for a fact rather than a frame count, comment every
fixed `wait`) are in `tests/smoke/README.md`. After adding one, run the
plain build with `--update`, read the diff of the expectation directory,
then run the sanitized build without `--update`: it must agree.

## Recording a decision

A decision that a later reader would otherwise have to rediscover (an
interpretation of the hardware, a choice between build approaches, a
deliberate divergence from the GBA) is a section in `docs/DECISIONS.md`:
a dated heading, what was chosen, the alternatives that were rejected and
why, and what it costs. Append; do not rewrite an earlier decision, add
a later one that supersedes it. A change to an expansion file also gets
its row in `docs/UPSTREAM_MODIFICATIONS.md`, which says what changed; the
decision says why that approach.

## Upstream

Do not file issues or pull requests upstream on this project's behalf without
the maintainer's decision. Seven reports have been filed and closed
(`docs/UPGRADING.md`); the rows of `docs/UPSTREAM_MODIFICATIONS.md` that say
"Reported upstream" link them. Bugs we find in expansion are fixed under
guards here and recorded in that file.

## What will not be accepted

- Changes to expansion's files outside the policy above: unguarded, not
  listed in `docs/UPSTREAM_MODIFICATIONS.md`, or changing what the GBA
  build produces.
- A ROM, a save from a cartridge, or anything extracted from a cartridge.
  Everything builds from the source and assets in the repository.
- Assets (graphics, audio, text) that are not already expansion's. This
  project distributes no ROM and no assets.
- Code copied from emulators or from the reference ports, in whole or in
  part.
- A test that is skipped, empty, always passes, or covers only the happy
  path.
