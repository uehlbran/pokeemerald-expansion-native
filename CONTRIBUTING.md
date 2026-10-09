# Contributing

This repository is the native PC build of pokeemerald-expansion. Two kinds
of code live in it: expansion's (`src/`, `include/`, `data/`, `test/` and
the rest, pinned at `expansion/1.17.1`) and ours (`platform/`, `tests/`,
`Makefile.native`, the build glue, the documents). The rules differ for the
two; `docs/ARCHITECTURE.md` explains the boundary.

Upstream's own contribution guide is kept at
`docs/upstream-CONTRIBUTING.md`. It applies to rh-hideout's repository, not
to this one.

## Building

`docs/BUILDING.md` has the prerequisites and every command. In short:

```
make -f Makefile.native -j"$(nproc)" native BITS=32
make -C platform test BITS=32
```

## The rules

`docs/C-RULES.md` is the full text and applies to everything we write.
Rules 1 to 4 below are from it; rule 5 is from the policy at the top of
`docs/UPSTREAM_MODIFICATIONS.md`.

1. Warnings are errors for our files: `-std=gnu17 -Wall -Wextra -Wshadow
   -Wstrict-prototypes -Wconversion -Werror`. Expansion's headers are
   included with `-isystem` or `-idirafter`, so their warnings are not
   ours; do not edit expansion's code to quiet one.
2. Every unit test runs under AddressSanitizer, UndefinedBehaviorSanitizer
   and leak detection. A run with a sanitizer report is not green.
   A leak suppression needs a written reason in the repository.
3. Test first, from GBATEK. The test for a piece of hardware behaviour is
   written from the specification, fails against an empty module, and then
   the implementation is made to pass it. Edge cases are part of the first
   version: boundary addresses, zero and maximum lengths, wraparound,
   invalid input, error-path cleanup.
4. Do not copy code from the reference ports (pokeemerald-native,
   pokeemerald-multiplatform, pokemon-regionalidades). Two of them credit
   GPL and MPL emulators as the source of their code, which cannot be relicensed under this
   project's MIT license. Read them for structure and pitfalls; write ours
   from GBATEK and from expansion's own sources.
5. One guard macro, `PLATFORM_NATIVE`, for every change to an expansion
   file. The change is as small as it can be, sits inside
   `#if PLATFORM_NATIVE` (or `#if !PLATFORM_NATIVE`), keeps the GBA
   build's output unchanged, and has a row in
   `docs/UPSTREAM_MODIFICATIONS.md`. Where the guarded block adds lines
   to a `src/` file it is followed by `#line N`, N being the upstream line
   number of the first unchanged line after it, so the `__LINE__` values
   baked into the ROM do not move. The convention and how to check it are
   at the top of that file. Before changing an expansion file at all, ask
   whether a link-time substitution, a build-time filter or a shim in
   `platform/` can do it; the policy prefers those.

## Gates

A change is merged when all of these pass. `docs/STATUS.md`, "Gates and
their state", is the list of gates (a to h); the commands are in
`docs/BUILDING.md`, the numbers in `docs/STATUS.md`. Gates (a) and (h) are
milestone-level: (a) is a native build from a clean clone with the
documented prerequisites, run before a milestone is declared done, and (h),
the cross-width save round trip, does not exist yet.

1. Platform unit tests, both widths: `make -C platform test BITS=32` and
   `BITS=64`; a change under `platform/` or `tools/patchelf` also passes
   `make -C platform gate`, whose `tools-test` step runs tools/patchelf on
   an ELF32 and an ELF64 executable.
2. The two replays, plain and sanitized: `tests/smoke/run.sh` and
   `tests/smoke/run_debug_menu.sh` on `build/native32/` and on
   `build/native32-san/` (with `ASAN_OPTIONS=halt_on_error=0`). The plain
   and sanitized runs must produce the same dumps and screenshots.
3. Expansion's suite on the native build:
   `make -f Makefile.native -j32 check BITS=32`, and with `SANITIZE=1`.
   Per test it must equal the GBA baseline
   (`docs/research/gba-baseline-1.17.1.md`), except the nine benchmarks,
   which are assumption failures natively by decision.
4. Gate (c), the GBA target: the ROM is unchanged (`make -j"$(nproc)"`, then
   `sha1sum pokeemerald.gba` against the value for 1.17.1 in
   `docs/STATUS.md`; a change that moves it changes GBA behaviour and is a
   bug), and `TEST=1 make -j"$(nproc)" check` equals the baseline.
5. Format and lint: see `docs/BUILDING.md`. `make -C platform gate` is
   meant to run them together once the lint change is merged; it does not
   exist in the tree at the time of writing.

Hygiene, not a gate: `git diff --check` is clean.

If a gate result changes on purpose, say why in the commit message.
`tests/smoke/run.sh ... --update` rewrites the expected files; read the
diff of `tests/smoke/expected/` before committing it.

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

We do not file issues or pull requests upstream without the repository
owner's decision. The reason is recorded in `docs/STATUS.md`
("Upstream issues"): rh-hideout's policy rejects work written with LLM
assistance, and the seven reports already filed were closed on that
ground. Bugs we find in expansion are fixed under guards here and
recorded in `docs/UPSTREAM_MODIFICATIONS.md`; where a report exists, the
row links it.

## What will not be accepted

- Changes to expansion's files outside the policy above: unguarded, not
  listed in `docs/UPSTREAM_MODIFICATIONS.md`, or changing what the GBA
  build produces.
- A ROM, a save from a cartridge, or anything extracted from a cartridge.
  Everything builds from the source and assets in the repository.
- Assets (graphics, audio, text) that are not already expansion's. This
  is the owner's rule from the project brief: no ROM and no assets are
  distributed.
- Code copied from emulators or from the reference ports, in whole or in
  part.
- A test that is skipped, empty, always passes, or covers only the happy
  path.
