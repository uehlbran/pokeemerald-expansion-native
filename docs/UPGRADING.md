# Upgrading the expansion pin

This is the runbook for moving from `expansion/1.17.1` to a newer rh-hideout tag.
It was derived from how the tree is built and from the logs of the first
port; it has not been run yet, so expect to correct it on the first
upgrade and do so in the same change.

Last upgrade: none. The pin is `expansion/1.17.1`, the fork point
(`DECISIONS.md`, "Fork point and remote").

Throughout, `NEW` is the new tag, for example `expansion/1.18.0`, and `OLD`
is the current one. Where a name is used for a directory or a branch, use
the tag without the `expansion/` prefix (`ref-1.18.0`, `upgrade/1.18.0`).
Paths such as `../pen-worktrees` are relative to the main checkout.

## Principles

- History is kept: the new tag is merged into `main`, not rebased onto.
- A conflict is resolved inside the guarded block, never by dropping the
  guard. If upstream rewrote the code a guard wraps, the row is
  re-derived, not patched.
- The GBA build is the reference for everything. The native gates compare
  against a GBA baseline of the new tag, not the old one.
- Nothing is called upgraded until the gates in step 8 are green and
  `STATUS.md` says so.

## 1. Prepare

```
git status                         # clean tree, on main
git fetch rhh --tags
git tag -l 'expansion/*' | sort -V | tail
git diff --stat OLD NEW | tail -1  # how big the move is
```

Read upstream's changelog for the range (`docs/changelogs/` carries
upstream's own). Make a branch for the work: `git switch -c upgrade/NEW`.

Build a reference of the new tag in its own worktree, before merging
anything, so there is a ROM and a baseline that contain none of our
changes:

```
git worktree add ../pen-worktrees/ref-1.18.0 NEW
cd ../pen-worktrees/ref-1.18.0
make -j"$(nproc)" && sha1sum pokeemerald.gba          # the new reference sha1
make -j"$(nproc)" check 2>&1 | tee check.log          # the new baseline (check implies the test build)
cd -                                                  # back to the main checkout
```

`rom.sha1` in the repository root is pret's hash of the original Emerald
ROM, inherited from pret (`f3ae0881...`). `make compare` sets `COMPARE` and
checks against it, so it fails by design for expansion; do not use it. The
reference is the sha1 recorded in `STATUS.md` (`e2d4600d...` for 1.17.1)
and, per upgrade, the fresh build of the new tag made here.

## 2. Merge

```
git merge NEW
```

Conflicts will be in files with rows in `UPSTREAM_MODIFICATIONS.md` and in
the files we moved (`README.md`, `CONTRIBUTING.md`, `CREDITS.md`; the
whole-file table at the top of that file). For the moved files keep ours
and move upstream's new text into `docs/upstream-README.md` or
`docs/upstream-CONTRIBUTING.md` if it changed. For a guarded block, take
upstream's new code for the `#else` / `#if !PLATFORM_NATIVE` side and
re-apply the native side on top of it.

Files that only we have (`platform/`, `tests/`, `Makefile.native`,
`docs/`) cannot conflict unless upstream added the same path; if it did,
decide per file.

## 3. Every row of `UPSTREAM_MODIFICATIONS.md`

For each row of "Source modifications", in table order:

1. Does the code the guard wraps still exist? `git diff OLD NEW -- <file>`
   shows what upstream did to the file.
2. Did upstream fix the site? Where the row has a "Reported upstream" link
   (`grep -o 'issues/[0-9]*' docs/UPSTREAM_MODIFICATIONS.md`), look at the
   issue and at the tag's source for the fixed code. Seven issues were
   filed (rh-hideout #10892 to #10898) and closed by the maintainer (`STATUS.md`,
   "Upstream issues"); check whether the code was fixed independently. A row without a link may be fixed
   too, so read the diff for every row, not just the linked ones.
3. If the site is fixed: delete the guarded block, keep upstream's code,
   remove the row. Policy: a row leaves the table when its modification
   goes; if an upstream issue references it, move a short note to
   "Removed modifications" so the issue's reference still resolves.
4. If the site is not fixed: keep the guard, confirm it still compiles on
   both targets, and re-derive its `#line` values (step 4).
5. Does the unmodified code still hold the hardware assumption the row
   records? New code may add sites of the same kind; the checklist at the
   end lists the patterns to search for.

A modification that turns out to be unnecessary is removed even if
upstream did nothing, and the row goes with it.

## 4. Re-derive every `#line`

Upstream's line numbers moved wherever it edited above a guarded block, so
every `#line N` in a guarded file is suspect. The convention (top of
`UPSTREAM_MODIFICATIONS.md`): after a guarded block that adds lines, `#line
N`, N being the line number in `git show NEW:<file>` of the first unchanged
line after the block, counting an earlier `#line` in the same file.

List them:

```
git grep -n '^#line ' -- src include test
```

For each, find the line after the block in the merged file, find the same
line in `git show NEW:<file>`, and set N to its number. Where a block can
instead replace a blank line on each side (`#if !PLATFORM_NATIVE` ...
`#endif`), no `#line` is needed and that form is preferred. Two checks follow. The ROM comparison in step 5 catches a wrong value in
`src/` and other files the plain build compiles, because the GBA ROM embeds
`__LINE__` in assertion messages. It does not cover `test/*.c` or
`include/test/*.h`, which only the `TEST=1` build compiles. So for every
file, also do the second check from `UPSTREAM_MODIFICATIONS.md`: preprocess
the file with and without `-DPLATFORM_NATIVE=1` and compare the line
markers at the anchor lines after each block. For the test files this is
the only check, unless you also compare the `TEST=1` ELF built from the tag
with the one built from the merged tree. The native build itself does not
notice a wrong value.

## 5. The GBA build and the ROM

On the merged tree:

```
make -j"$(nproc)"
sha1sum pokeemerald.gba
```

It must equal the sha1 of the reference built in step 1. If not, the
guarded changes altered the GBA build: a wrong `#line`, a guard that
leaves the `#else` side different from upstream, or a stray edit.
`git diff NEW -- <file>` for each file in the table, with
`-DPLATFORM_NATIVE=0` semantics in mind, finds it. Do not continue with a
different sha1.

## 6. The GBA baseline

On the merged tree:

```
TEST=1 make -j"$(nproc)" check 2>&1 | tee check.log
```

Write `docs/research/gba-baseline-<tag>.md` in the form of
`gba-baseline-1.17.1.md`: date, toolchain versions, the five counts
(PASSED, KNOWN_FAILING, EXPECT_FAILING, TO_DO, TOTAL) and the exit code.
It must agree with the reference build in the worktree from step 1;
if it does not, the guards changed what the GBA tests do. Update the
totals quoted in `README.md`, `ARCHITECTURE.md`, `STATUS.md`, `BUILDING.md`,
`platform/test_runner/SUITE-LOG.md`'s header and this file when the new
baseline is the one to compare against (`git grep -n 5862` finds them).

## 7. Native build glue that mirrors upstream

These are copies or translations of upstream files. Diff each against the
old tag and re-apply by hand:

```
git diff OLD NEW -- Makefile make_tools.mk '*_rules.mk' ld_script*.ld config.mk
git diff OLD NEW -- '*.s' include/gba src/m4a.c src/m4a_tables.c
git diff OLD NEW -- tools/mgba-rom-test-hydra tools/patchelf tools/scaninc tools/preproc tools/gbagfx
```

- `Makefile.native`, section "Mirrored from Makefile": the asset pattern
  rules, learnset helpers, `wild_encounters.h`, `script_commands.h`; and
  the `check` recipe, which mirrors the `check` target of `Makefile`.
  Rule fragments are included unchanged, but the variables they expect
  (`GFX`, `MAPJSON`, `C_BUILDDIR`, ...) are defined in `Makefile.native`.
- `platform/gba_game.ld`, `platform/gba_regions.ld` against
  `ld_script_modern.ld` and `ld_script_test.ld`: new sections or symbols.
  `platform/build_glue/LINK-NEEDS.md` lists what the link needs; a new
  undefined symbol shows up at link time and belongs in that table.
- `platform/build_glue/asm_filter.py`, `c_filter.sed`,
  `overworld_macros.py`: new assembler constructs or `preproc` output.
  `asm_syntax.py` reads the comments, statements, labels and directives
  the filter and the battle script checker share.
- `platform/build_glue/elf_object.py` (reads an ELF64 object for the data
  checks and the converter below), `check_support.py` (errors, report cap, options and rule
  matching the checkers share) and `preprocessor_conditions.py` (finds a C
  constant's value under its `#if`s, for `Z_EFFECT_BS_LENGTH`).
- `platform/build_glue/data_address_words.txt`: the address words of the
  assembled data, per object and label. A new table or data object fails
  the 64-bit link's relocation check (`check_relocations.py`) until it is
  classified there, and laid out for 8-byte pointers if C reads it as
  pointers.
- `platform/build_glue/elf_to_flat.py` and `check_flat.py`, with
  `macho_object.py` and `coff_object.py`: the 64-bit data converted for
  macOS and Windows. Run `make -f Makefile.native -j"$(nproc)" check-flat
  TARGET=macho-x86_64`, then `TARGET=coff-x86_64`. A new data section (its
  ELF type and flags in `SECTION_TYPES`, its spelling per target in
  `TARGETS`), a relocation other than an 8-, 4- or 16-bit absolute word, or
  a 16-bit word whose symbol is not an absolute constant of a data object
  stops the conversion, naming the object, section, offset and symbol.
- `platform/gba_regions.pe.ld`, `platform/gba_game.pe.ld` and
  `platform/gba_gaps.pe.s`: the PE counterparts of the two linker scripts, for
  the Windows cross-build (`docs/BUILDING.md`). A change to `gba_regions.ld`'s
  regions or to `gba_game.ld` is made in both; the sizes in `gba_gaps.pe.s`
  and `platform/build_glue/check_pe_layout.py` follow `gba_regions.pe.ld`, and
  the link fails (an `ASSERT`, or the checker) when they do not. A weak
  definition added to expansion's code (`__attribute__((weak))` on a
  definition) is undefined in the Windows link until `gba_game.pe.ld` defines
  it; a new include of a header MinGW lacks goes in
  `platform/build_glue/mingw_include/`; a new ELF section-flag trick in
  preproc's output goes in `c_filter_coff.sed`. `gba_game.pe.ld` also moves
  every initialised data section after the read-only ones (`DECISIONS.md`,
  "The Windows image links its writable sections last"): a new writable
  section name in expansion's output (as `script_data` is) is placed there
  too; where the linker would put it before `.rdata`, `check_pe_layout.py`
  fails the link with "is read-only but follows the writable". A format string checked by `format(printf, ...)` in our
  code takes `%llu` with a cast, not `%zu`: MinGW checks it against msvcrt's
  formats. The pinned SDL2 for MinGW (`SDL2_MINGW_VERSION` and its sha256 in
  `Makefile.native`) is bumped by hand.
- The assembly translated or replaced in `platform/`: `src/m4a_1.s`
  (`platform/m4a/`; its differential test against the ARM original is run by
  hand: `make -C platform/m4a/difftest run`, needs qemu-arm, see
  `platform/m4a/README.md`),
  `src/crt0.s`, `src/decompress_asm.s`, `src/libgcnmultiboot.s`,
  `src/rom_header.s`. A change in any of them is a change in a platform
  module.
- The ARM toolchain's libgcc, or the `-mabi` expansion compiles with:
  `platform/arm_division` gives x86's divide errors the results libgcc
  returns on the GBA. `make -C platform/arm_division_libgcc run` (needs
  qemu-arm) runs libgcc's division routines and compares them with the
  record the module follows; a difference changes the module and its test.
- `include/gba/*.h` (register names, `m4a_internal.h`'s `SoundInfo`,
  `flash_internal.h`'s `FlashSetupInfo`): `gba_memmap.h`, `m4a_output.c`,
  `flash.c` assert or depend on them.
- `test/test_runner.c` and `test/test_runner_battle.c`: the protocol the
  native runner implements (`docs/research/test-runner-protocol.md`) and
  the guarded sites in these files.

## 8. The native gates, in order

```
make -C platform test BITS=32
make -C platform test BITS=64
make -f Makefile.native -j"$(nproc)" native BITS=32
make -f Makefile.native -j"$(nproc)" native BITS=32 SANITIZE=1
make -f Makefile.native -j32 check BITS=32
make -f Makefile.native -j32 check BITS=32 SANITIZE=1
```

(The first build converts every graphic; `make -f Makefile.native
seed-assets` after the GBA build makes it faster. `BUILDING.md`.)

The Windows cross-build, here, and its tests, on Windows (`BUILDING.md`,
"Running it on Windows"):

```
make -f Makefile.native -j"$(nproc)" native BITS=64 TARGET_OS=windows
make -C platform test TARGET_OS=windows BITS=64
make -C platform lint TARGET_OS=windows BITS=64
```

- The suite must equal the new baseline per test, except the benchmarks
  (`DECISIONS.md`, "Benchmarks are assumption failures natively"). The
  expected totals are the new baseline's, with the benchmark count moved
  from PASSED to assumption failures: for 1.17.1 that is 5405 passed, 9
  assumption failures against 5414 passed. The benchmark count can change
  with the tag.
- A new failure class: use `platform/test_runner/SUITE-LOG.md`, "How a
  failure is found" to locate the site, then fix it under a guard with a
  row (the checklist at the end says what to look for).
- The two replays, plain and sanitized:

  ```
  tests/smoke/run.sh build/native32/pokeemerald-native out/
  tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/
  ASAN_OPTIONS=halt_on_error=0 tests/smoke/run.sh build/native32-san/pokeemerald-native out-san/
  ASAN_OPTIONS=halt_on_error=0 tests/smoke/run_debug_menu.sh build/native32-san/pokeemerald-native out-san/
  ```

  A replay that fails because a screen, a string or a script changed on
  purpose is not a regression, but understand the change first: read the
  screenshot, `out/work/failed.ppm`, and the dump diff. Only then run the
  plain build with `--update`, read `git diff tests/smoke/expected*`, and
  confirm that the sanitized build agrees. Facts that read expansion's
  structures (`platform/inspect*.c`) and the argument ranges in
  `inspect_args.c` follow expansion's constants and are compiled against
  its headers, so a renamed variable is a compile error there, but a
  moved callback is not: check `wait-until callback2=...` names.
- The trainer party seed no longer depends on the build's layout
  (`DECISIONS.md`, "The trainer party seed does not hash the party table's
  address"); other code that hashes or compares pointers into the image
  would reintroduce the flake, so a gate that changes without a code
  change is worth a look for that.

## 9. Palettes

```
make -f Makefile.native check-palette-padding
```

(it also runs on every link). The native asset directory pads every
`.gbapal` of fewer than 16 colours to whole banks, except the ones in
`platform/build_glue/unpadded_palettes.txt`, whose size the game takes. A
new upstream palette that the game sizes with `sizeof` or `ARRAY_COUNT`
fails the check with its file and symbol; add it to the list with the
reason (the checker explains which direction is wrong). A listed file
that upstream removed or padded fails the same way: delete the line.
The check does not catch a palette read past its end that is not short:
the sanitized replay does.

## 10. Save layout

The native save is the host ABI's layout (`DECISIONS.md`, "Native structs
follow the host ABI").

- `test/save.c`: under `PLATFORM_NATIVE` it holds the native sizes
  of `SaveBlock1`, `SaveBlock2`, `SaveBlock3` and `PokemonStorage` (15496,
  3848, 1, 34144 for 1.17.1; `SaveBlock1` is 15768 at 64 bits). If
  upstream added fields, the suite fails there. The sizes must be re-measured, and so must the offsets that
  matter to the layout id below.
- `SAVE_FILE_LAYOUT_ID` in `platform/save_file.h`: a change to the
  layout of anything written to the flash image (a save block,
  `PokemonStorage`, a special sector) makes older native save files
  load wrongly. Bump the id; the loader then reports
  `SAVE_FILE_FOREIGN_LAYOUT` and `main` refuses to start on such a file
  rather than overwrite it. A conversion for the old layout is a
  separate piece of work.
- `platform/save_layout32.h` and `save_layout64.h`: where `SaveBlock1`
  keeps the members `save_convert` moves (its object event templates, its
  Enigma Berry record, the members around them). Each is pinned by static
  assertions in `platform/save_convert.c`, compiled with the game's flags,
  so a change to `SaveBlock1`, `ObjectEventTemplate` or `EnigmaBerry` stops
  the build of that width with the message "update
  platform/save_layout<bits>.h". Re-measure with offsetof and sizeof at
  `-m32` and `-m64` and update the table; if upstream added a pointer to a
  saved struct, the converter needs a new segment row (or the fallback of
  `DECISIONS.md`, "Gate (h) is a platform-side save converter").
- The cross-width gate (h): write a save in each width, load it in the
  other: `tests/smoke/run_cross_width.sh` from 32 to 64 bits and back
  (`tests/smoke/README.md`).

## 11. Finish

- Update `docs/STATUS.md` with what was verified by which gate, the date,
  and the new tag.
- Update the README's and `BUILDING.md`'s pin and any 1.17.1 references:
  `git grep -n '1\.17\.1'`.
- Set the "Last upgrade" line at the top of this file: tag, date, what
  broke, what this runbook got wrong.
- Merge `upgrade/NEW` into `main` and remove the reference worktree
  (`git worktree remove ../pen-worktrees/ref-NEW`).

## Checklist: what has broken before

Search the new code for each pattern. Each was a real failure in the
first port; the logs are `platform/build_glue/BOOT-LOG.md` and
`platform/test_runner/SUITE-LOG.md`, the rows are in
`UPSTREAM_MODIFICATIONS.md`.

- Loops that wait for the DMA3 queue or another thing only an interrupt
  advances. The game's V-Blank interrupt runs only inside
  `VBlankIntrWait`, so `while (IsDma3ManagerBusyWithBgCopy())` and
  `while (TRUE)` setup state machines hang. Fix: `VBlankIntrWait()` in
  the loop body (`DECISIONS.md`, "Setup loops that spin on the DMA3
  queue yield a frame"; `src/overworld.c`, `party_menu.c`, `item_menu.c`
  and others; the DECISIONS entry counts the `while (TRUE)` loops that
  existed in 1.17.1).
- Polling `VCOUNT` or a timer in game code: natively `VCOUNT` reads 161
  during game logic and timers advance at frame boundaries only
  (`m4a.c` `SampleFreqSet`, `librfu_stwi.c`, `librfu_sio32id.c`). Fix:
  `PlatformWaitCycles`.
- Palettes of fewer than 16 colours loaded as whole banks, and the
  reverse, a palette sized by `sizeof` that padding would break (step 9).
- Reads through NULL or past an array that the GBA survives because
  address 0 is its BIOS ROM and unused space reads open bus: structures
  freed and then read in the same frame (`naming_screen.c`,
  `battle_transition.c`), `gBattleStruct`, `gMPlay_PokemonCry`,
  `gMapHeader.mapLayout` outside a battle or map, table indexes at the
  limit (`text.c`, `battle_interface.c`, `debug.c`). The platform does not
  map page zero and will not; the fault is how these are found. A fix
  does what the GBA ends up doing, or what a correct program would
  where that is unknowable (`DECISIONS.md`, "Native fixes do what the
  GBA ends up doing").
- Assumptions that a pointer is a ROM address: `ROM_START..ROM_END`
  checks, 28-bit location encodings (`malloc.c`), the `ROM_SIZE` tag on
  script pointers (`platform/script_tag.h`; the executable must end
  below 0x42000000), the trainer party seed hashing a pointer. Anything
  new that stores or compares code or data addresses belongs here.
- Naked or inline assembly: `__attribute__((naked))` functions, `register
  asm`, `swi`, Thumb `bx`, `.include`d ARM macros, and code copied into
  a stack buffer and called (x86 cannot execute data, and the lengths
  are ARM's). Fix: a native branch calling a platform function
  (`platform/game_hooks.h`).
- Benchmarks: `BENCHMARK` and `EXPECT_FASTER` read GBA cycle counts.
  New ones are assumption failures natively; the gate's expected counts
  change by that many.
- Struct sizes and layout: `-mabi=apcs-gnu` rounds every GBA struct to
  4 bytes, x86 does not. New asserts of `sizeof`, `_Alignof`, save-block
  sizes, or code that reads a struct as `u32`s will differ natively.
- Shifts by 32 or more (zero on ARM, masked on x86; `text.c`), signed
  overflow, and other undefined behaviour that the GBA compiler happens
  to define; the sanitized runs report these.
- Uninitialised bytes that reach the save file (`CreateBoxMon`'s
  nickname tail). The save file must be byte-identical between runs.
