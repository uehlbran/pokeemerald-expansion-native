# Status

Last updated 2026-10-14 (milestone 7 done: **64-bit done**, every gate green at both widths on main at 94f3c70f62; milestone 8 in progress: Linux CI on main, the probes decided, the host layer and the data converter in review).

This file is the honest ledger. A thing is listed as verified only with
the gate that proved it. Anything without a gate is unverified, whatever
we believe about it.

## Summary for the owner

Read this first; the sections below are the ledger behind it.

**Finished.** Milestones 1 to 6. **32-bit is done**: the native build
boots and plays the opening (gate d) and the debug menu (gate e), plain and
sanitized; expansion's test suite runs natively and matches the GBA
baseline, plain and sanitized (gate b); the GBA ROM is byte-identical (gate
c); the platform layer passes its own gate (clang-format, clang-tidy,
cppcheck, the sanitized unit tests at both widths); the refactoring pass
over everything we wrote happened in seven reviewed groups and the
orchestrator read the platform layer end to end afterwards (the "Milestone
6" section below says what changed). Every gate was run on main in one
sequence after the last merge and recorded in the table below. Seven
upstream issues were filed (rh-hideout #10892 to #10898, approved text) and
closed by the maintainer. Every commit is authored by uehlbran.

**Finished, continued.** Milestone 7, the 64-bit build: **64-bit is
done**. Twelve worker branches (each in its own worktree, reviewed by a
Sonnet reviewer with mutation testing, gated by the orchestrator) and the
refactoring pass in three reviewed groups were merged on 2026-10-09, then
the orchestrator read every milestone 7 change in `platform/` and the
build glue end to end (no defect; three comments corrected), then every
gate ran once more on main at 94f3c70f62 in one sequence and was green:
the platform gate; at 32 bits the build, both replays plain and
sanitized, both suites (5409 passed) and the GBA ROM (sha1 unchanged); at
64 bits the build with its three link-time data checks, both replays
plain and sanitized, both suites (5409 passed, test for test the 32-bit
results); and the cross-width save gate (h) in both directions, plain and
sanitized. The "Milestone 7" section and the table below are the ledger.

**In flight.** Milestone 8 (CI for both targets, Windows, macOS) started
after the 64-bit declaration: a Linux workflow of this project's own
(`.github/workflows/native.yml`, the gate as recorded below, on pushes to
`main` and by hand; PLAN.md "Milestone 8 plan" and DECISIONS.md say why
Linux only and why not every push) and, from the Opus inventory of the Windows and macOS ports
(`research/m8-inventory.md`; PLAN.md has its findings and the task
order), the first tasks: the loader probes on the Windows and macOS
runners (W1, M0) and the host layer on Linux as a pure refactor (W2).

**Broke and fixed.** Milestone 7: the 64-bit plain debug-menu replay
failed on main right after the alignment merge while the sanitized one
passed. Cause: the platform's inspect objects (the replay facts, which read
the game's structs) were compiled before task.h changed and their
dependency files, written with -MMD, listed no game header, so they kept
the old struct Task layout; the alignment branch had switched them to -MD,
which only takes effect once the objects are rebuilt. Fixed by removing
the stale objects and making those objects depend on the Makefile and the
game flags (3249f05722). Milestone 6's screenshot flake (the trainer party
seed hashing a pointer) is recorded in the milestone 6 section and in
DECISIONS.md. Earlier breakages are in the milestone logs.

**Needs the owner.** Nothing new. The three milestone 8 items were
settled on 2026-10-14: the MinGW cross-compiler is installed (GCC 13 for
x86_64-w64-mingw32, binutils 2.41; Wine is not, so cross-built
executables run only on the Windows runner); macOS ships as x86_64
under Rosetta and arm64 is not started (DECISIONS.md, with Apple's
Rosetta wind-down recorded as the known revisit); the CI minutes are
accepted and the Linux workflow is on main. Standing: upstream
rh-hideout's maintainer replied to all seven upstream
issues citing their LLM policy and said they will close them (details and a
recommendation under "Upstream issues" below). Nothing was posted in reply.
Decide whether to engage personally; no action is taken here without you.

## Upstream issues

2026-10-09 03:37 UTC: a maintainer (pkmnsnfrn) commented on all seven issues
(#10892 carries the text, the others point to it). What they said, in
substance: rh-hideout's LLM policy (from their pull-request template) says
pull requests with code, comments or descriptions written by an LLM, or with
commits co-authored by AI, are closed; they admit the issue templates do not
state this yet and will add it in a few days, apologise for the
inconvenience, say they believe very strongly that these reports were
entirely LLM generated, and that they will close them; they invite questions
on Discord (`pkmnsnfrn`). State when checked: 7 of 7 closed (details
in the session's scratchpad; recheck with `gh issue view`).

No reply is drafted: the comment asks no factual question, and under the
approve-before-posting rule nothing is posted. Nothing was posted or opened
after the comments. Recommendation for the owner: do not reopen or argue on
GitHub; the seven reports remain the record of the bugs (each row in
`UPSTREAM_MODIFICATIONS.md` keeps its link, and the bugs stay fixed under
guards here); if the owner wants rh-hideout to take the fixes, that is a
conversation for the owner to have personally, on Discord as invited, and
any pull request would have to be the owner's own work under their policy.
No further issues or pull requests will be filed upstream by this project
without an explicit owner decision.

## Milestone 6: the refactoring pass, the lint gate and the end-to-end read

The owner's standing instruction (PLAN.md) asked for a refactoring pass
over everything we wrote before 32-bit is declared done, dispatched as
module-sized reviewed tasks, then one end-to-end read by the orchestrator
recorded here. Both happened on 2026-10-08 and 2026-10-09.

**The pass.** Seven worker branches, each reviewed by a Sonnet reviewer
with mutation testing, fixed by its author and merged after the
orchestrator's own gate (unit tests at both widths, the native build, the
two replays plain and sanitized, `make check`):

| Group | Merge | What changed |
|---|---|---|
| audio (psg, sound_mix, m4a_output, the m4a translation) | 3e1f9111f7 | a `ComposeLe32` helper in the m4a translation, const-correct pointers, the standalone m4a Makefile links again, a DAC enum in the PSG internals, mutation-driven tests for the DC filter, saturation, sweep boundaries, FIFO bounds and PSG lengths; difftest identical before and after |
| video (ppu, video, frame_pacer) | 7e7c88c25f | shared PPU helpers for the 4bpp index, the palette colour and the mosaic block, named priority, sign, affine-group and dimension constants, the window control byte split without a flag parameter, mutation-driven tests for blend bounds, mosaic sizes, unused window bits, affine PB and the blend target guards |
| store (flash, save_file, rtc, build glue) | 899bc8510a | rtc.c split into the register model and rtc_calendar (13 tests of its own), save-file tests for magic bytes, future versions, unopenable files, a pinned erased-chip header and foreign-width relabelling, the palette checker and macro splitter fail loudly on malformed input, Makefile.native shares one compile and one scan recipe |
| bios and debug (bios_*, arm_division, symbols, debug_channel, crash_reboot) | 24ec50ceaa | ArcTan's Horner loop simplified and verified over every input, the arm_division decoder split with named ModRM/SIB constants and tests that pin every operand form, symbols.c's ELF checks pinned by tests |
| core (main, frame, interrupt, timer, dma, transfer_bounds, crt0, game_hooks) | e88b8d1f00 | the command-line parsing moved from main.c to options.c with 15 tests, shared `gba_io_u8/u16/u32` accessors replace hand-written I/O casts, crt0_init shuts down a previous copy first, tests for save retry timing, exit codes above 127 and address-space boundaries |
| tools (replay, inspect, restart, the smoke scripts) | 409522abfe | every save-block fact guarded before the blocks exist (pre-boot values checked by the opening replay at frame 1), lib.sh under `set -u` with every failure counted, parser tests for CRLF, ceilings, overflow, duplicate keys and releasing unheld keys |
| modaudit (the modifications to expansion's files) | f67328ba16 | every row of UPSTREAM_MODIFICATIONS.md re-read against expansion/1.17.1: nothing removable; three `#line` gaps filled; rows corrected (the "M6 audit" section there) |

The pass also found the screenshot flake's root cause (battle-glyph,
42c784c353; "Broke and fixed" above) and the follow-ups it could not do
inside one group (an enum for the pacer mode and the OBJ mapping, crt0's
own test file, a per-test timeout in ptest.h, `gba_io_*` in the BIOS and
LCD status files), which the lint branch did.

**The lint gate** (61ae8181f7): `.clang-format` derived from the existing
style and applied once to every platform file; `.clang-tidy` with a
curated check list and a reason for every exclusion (plus narrower
configurations for the tests and the difftest); `make -C platform format`,
`format-check`, `lint` (clang-tidy at both widths and cppcheck, any
finding fails, a missing tool fails) and `gate` (format-check, lint, then
the sanitized unit tests at both widths; 1 min 13 s). Fixing the findings
made 223 discarded return values explicit, bounded the unbounded copies,
made widening casts explicit and split options_parse. The docs branch
(aac126caa1) added CONTRIBUTING.md, the UPGRADING.md runbook, the
ARCHITECTURE.md accuracy pass (every module in the table, the test runner,
replay and facts, the asset pipeline, the save container, the known
divergences) and the README.

**The end-to-end read.** After the lint merge the orchestrator read every
file of `platform/` (the modules, their headers, the BIOS, the PPU, the
PSG, the replay, inspection and save code, the m4a translation's README,
layout contract, engine header and mixer, ptest.h, both Makefiles, the
linker fragments and the build glue) on main at 61ae8181f7. No defect was
found. Five small things were fixed in the commit that records this read:
a stale sentence in gba_memmap.c's header (EWRAM_INIT is already loaded by
gba_regions.ld), a duplicated comment in timer_init, one unchecked printf
in main.c, symbols.c reading the symbol type through the 32-bit macro on
both widths (the width-matched macro now), and a note in debug_channel.c
that its gba_memmap_init call is a layout check (main has already
initialised the regions). Notes that are not defects: ppu_obj.c reads all
128 OAM entries once per line (performance only); bios_soft_reset relies on
game_hooks_unwind being noreturn, which it is declared to be.

## Milestone 7: the 64-bit build

Started 2026-10-09 from an Opus inventory of what the 64-bit build
needed (the pinned 32-bit layouts and the address words in the assembled
data, measured rather than guessed; DECISIONS.md "Milestone 7 starts
from a measured inventory"). The order was the plan's: the link, the
data, the address and integer modules, gate (h), the gates at 64, the
refactoring pass. Twelve worker branches, each in its own worktree,
reviewed by a Sonnet reviewer with mutation testing, fixed by its
author, gated by the orchestrator (the platform gate, the 32-bit build
and replays like for like, the 64-bit build and replays, `make check`
at both widths, the GBA ROM) and merged with `--no-ff`:

| Branch | Merge | What changed |
|---|---|---|
| m7-seed | ea4fee53cb | the trainer party seed hashes the same 52 bytes at both widths (a pointer had been hashed; the M6 flake's root cause, finished for 64) |
| m7-link | f9ebd50dd2 | the 64-bit build links and boots: `INTR_VECTOR` as a 32-bit store, `RecordedBattleSave`'s u64 at 4-byte alignment, the Union Room's task-data overlay, `ListMenu` in a task's data (`nativeDataSpill`) |
| m7-testtable | f96b68877a | the `.tests` table at 64 (`-malign-data=abi` for test objects, crt0 walks the entries), `tools/patchelf` and the hydra read ELF64 |
| m7-armdiv | 9629ad33a4 | one SIGFPE division decoder for i386 and x86-64; the GBA's 64-bit division-by-zero results measured under qemu-arm (`platform/arm_division_libgcc/`) |
| m7-data | 53841d35de | the assembled data emitted in the x86-64 layout at 64 (`.ifdef NATIVE_POINTER_SIZE_8` in the macros, `asm_filter.py --pointer-size 8`), `check_relocations.py` with the classified list `data_address_words.txt` and a 4-byte reference assembly |
| m7-bscript | f53de560e5 | battle script pointer operands at 8 bytes (read through packed `CMD_ARGS` structs), `check_battle_script_operands.py`, `check_data_layout.py` with the host probe; the `table` and `operands` classes |
| m7-crit | 8436c38f61 | `GetCriticalHitOdds` returns the GBA's words for an out-of-table index (ROM evidence; crashed seven AI tests at 64) with `test/crit_odds.c` |
| m7-align | 7280370c21 | 8-byte alignment at 64: the save blocks (32 positions in 8-byte steps), the heap, the RFU buffer carve (fixed offsets, static asserts), task data |
| m7-san | 6fa225a3f8 | the sanitized suite at 64 compared with 32 report by report (`suppress_equal_pcs=0`): the same accesses, nothing new at 64 (SUITE-LOG.md) |
| m7-save | e2d220a406 | gate (h): `platform/save_convert` between the two SaveBlock1 layouts (15496 and 15768 bytes), both directions, `tests/smoke/run_cross_width.sh` |
| m7-anim | 99d36a6290 | two out-of-table animation reads given the GBA's value at every width (Doom Desire's coordinate table, `gSineTable[320]` from Extrasensory and `Cos`) |
| m7-docs | 5c17faf576 | BUILDING, ARCHITECTURE ("The 64-bit build"), CONTRIBUTING, the smoke README, the platform comments brought to the 64-bit state |

What the inventory predicted and what held: the memory model needed no
change (every region below 4 GiB, the executable at 0x40000000, the tag
bit); the assembled data was the bulk of the work (61,154 address words,
19,208 widened, 41,946 left at 4 bytes as bytecode); only one save
block's layout differs between the widths. What it did not predict:
the battle scripts' operands are read through packed structs and had to
widen (m7-bscript), the test table's entries are spaced differently by
GCC's data alignment (m7-testtable), and three upstream out-of-table
reads that are harmless at 32 bits crash or change a frame at 64
(m7-crit, m7-anim). Every decision taken on the way is in DECISIONS.md
(the entries dated 2026-10-09 from "Milestone 7 starts from a measured
inventory" on).

**The refactoring pass** (the standing instruction, run again over what
milestone 7 added): three groups, each reviewed by a Sonnet reviewer with
mutation testing, fixed by its author and merged after the orchestrator's
own checks:

| Group | Merge | What changed |
|---|---|---|
| platform (the save converter, the division decoder, crt0's table check, main's layout adoption, the libgcc record) | aae4fef03c | production code unchanged apart from comments and the record program's shared printers; tests added for the decoder's refusals at both widths, the prefix limit from both sides, a non-divide SIGFPE and a plain previous handler, the default action in a forked child (bounded by a timeout, core dumps off), the si_code check, the crt0 table walk (short, long and padded tables), and the converter's edges (the counter, the last sector of the save area, sectors beyond the slots, ids 0, 5 and 0xFFFF, a double round trip, the width-confusion limit pinned byte for byte) |
| build glue (the three checkers, the filter, the Makefile stamps, the smoke library, the tools) | 94f3c70f62 | four shared modules with self-tests (`elf_object.py`, one ELF64 reader; `check_support.py`, errors, caps, options and rules; `asm_syntax.py`, comments, statements and labels; `preprocessor_conditions.py`, a recursive-descent reader in place of `eval()`); the checkers split into one function per problem, every message naming file, symbol and line; the 64-bit checks as separate Makefile stamps with one self-test stamp; `glue-test` in the platform gate (the eight self-tests ran in no gate before); `play`, `fail` and `same_facts` shared in lib.sh; the probe's words named; filtered output, objects and both executables byte-identical before and after |
| expansion (the audit of every guarded block milestone 7 added) | 9e3ce40979 | no defect; one latent wrong `#line` pair in test/save.c (13/14, now 7/8), five blocks brought to the canonical shape, a 25-line comment cut to six, rows corrected (`#line` values, the test-table sentinel, the 64-bit mark on six width rows); every `#line` verified against expansion/1.17.1 by script (96 values), the preprocessed text identical at both widths, the four 32-bit executables and the ROM byte-identical |

Decisions taken in the pass: the tools (patchelf, the hydra) keep the
milestone 7 form rather than a helper layer the glue worker had written
(expansion's files take the smallest clear change; about 75 lines per
tool for no behaviour change); the `CRITICAL_HIT_ALWAYS` branch in
`GetCriticalHitOdds`, unreachable today, stays (two lines, ROM evidence,
and removing it changes the 32-bit executables); the 174 repeated
`.ifdef` blocks in battle_script.inc stay (a helper macro would rewrite
upstream's lines, and the operand checker guards them).

**The end-to-end read.** After the three merges the orchestrator read
every file milestone 7 added or changed in `platform/` and the build glue
on main at 94f3c70f62: the save converter and its two layout tables (the
segment table, the sector search, the chunk sizes at each width, the
Enigma checksum rule), the division decoder (prefixes, REX, ModRM, SIB,
RIP-relative addressing, the 32- and 64-bit libgcc rules), the libgcc
record program, crt0's table check and main's layout adoption, the m4a
layout assertions, the three checkers and the four shared modules, the
filter's 8-byte rules, Makefile.native's 64-bit section and stamps, the
smoke library and the cross-width script, the patchelf test and the new
unit tests' names. No defect was found. One thing was corrected in the
commit that records this read (117af1db62, 384f044cd9): three comments
in save_file.h, save_layout32.h and save_convert.h said the 32-bit native
SaveBlock1 layout is the GBA's, which DECISIONS.md contradicts (the GBA
rounds the block to 15568 bytes, i386 to 15496); they now say an
emulator's save is in neither width's layout and is not supported. Also
settled on the way: at 64 bits `Clear64byte` clears `MusicPlayerInfo` only
up to `tone`, which is enough (the players are zero at every `MPlayOpen`,
`MPlayOpen` writes `ident` and the links, `MPlayStart` writes `tone`
before it is read); a static assertion in m4a_layout.h pins that field
order (531011a558).

## Milestone 8: CI, Windows and macOS

Started 2026-10-09 after "64-bit done". PLAN.md "Milestone 8 plan" has
the order and the inventory's findings; `research/m8-inventory.md` is the
inventory; `research/probes/` holds the loader probes and their README
records each run.

**Linux CI.** `.github/workflows/native.yml` on the branch `ci/native`
(pushes to `main` and by hand; two jobs: the GBA ROM with its sha1, and
the native gate at both widths). On the runner image the 32-bit SDL2
development package cannot be installed (its glib dependency clashes
with the image's), so the job takes the i386 headers and link files from
the downloaded package without its dependencies; and the platform gate
runs after the 32-bit build, which generates the game headers the lint
reads. The GBA job is green (the ROM's sha1 equal to the recorded one);
both jobs are green on the branch's third run (37920736586, 2026-10-09:
the GBA ROM with the recorded sha1; the native gate end to end, about
95 minutes on the runner), and the workflow is merged into main on
2026-10-14, where every push now runs it.

**The loader probes** (`research/probes/`, a manual workflow on the
Windows and macOS runners, two runs on 2026-10-09; the README has the
log references):

- Windows x86_64: a PE image based at 0x01800000 with the GBA regions as
  sections at their addresses and the code at 0x40001000 is refused by
  the loader when the sections leave gaps, and loads at its base with
  every region committed and writable (the ROM read-only) when the gaps
  are filled with uninitialised read-only sections. The run-time
  VirtualAlloc fallback also gets every region (the debug page rounded
  to the 64 KiB granule). The memory model is kept on Windows as one
  image with filled gaps. The third run (the second hung in an extra
  diagnostic command) confirmed it: Windows itself reports the image
  with gaps as "not a valid application for this OS platform"; the
  filled image started 100 times of 100 at its base with every check
  passing (40 of 40), and its commit charge is 3100 KiB against 604 KiB
  for the same program without regions, so the 1 GiB of read-only gaps
  costs nothing; the run-time fallback also started 100 of 100 (49 of
  49 checks), committing the 32 MiB ROM.
- macOS x86_64: with `-no_pie`, `-pagezero_size 0x1000`, `-image_base
  0x40000000`, the regions as segments at their addresses and
  `-no_fixup_chains` (or a minimum OS version before chained fixups),
  the image loads with the header at 0x40000000, a zero slide and every
  region at its address, 100 starts of 100, 61 checks of 61, on the
  Intel runner (macOS 15.7, Xcode 16.4) and under Rosetta on the Apple
  silicon runner. Without `-no_fixup_chains` the current linker ignores
  the image base (its warning says so) while still placing the regions.
  A run-time `mmap` of the regions is unreliable there (16 starts of
  100 with a small page zero, 0 with the default), so the image is the
  mechanism. The memory model is kept on macOS x86_64.
- macOS arm64: the linker says it outright, "-no_pie ignored for
  arm64", and the 16 KiB page size rejects the debug page's address;
  a page zero under 4 GiB is killed at start. The inventory's verdict
  stands: the fixed-address model cannot hold on arm64 (the owner's
  decision, above).

**In flight.** The host layer on Linux (task W2, a pure refactor under
every Linux gate).

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
| Gate (e): the debug-menu replay drives the 32-bit build headless through the opening, the debug menu (flags, vars, badges, six items, twelve Pokémon across nine generations with forms, shiny, Dynamax, Gigantamax, Tera and a chosen move; two complex gives into the PC), the party, summary and bag screens, two trainer battles to the action prompt, a save, a process restart and Continue, checking the state after each step (20 dumps, 32 screenshots, extras and moves equal across the restart) | `tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/` | green 2026-10-09, 28 s |
| The same replay on the sanitized build is clean (identical dumps and screenshots, no report) | `tests/smoke/run_debug_menu.sh build/native32-san/pokeemerald-native out/` with `ASAN_OPTIONS=halt_on_error=0` | green 2026-10-09, 75 s |
| The first native build's asset conversion and the seeded one | `make -f Makefile.native -j32 native BITS=32` from nothing, and after `seed-assets` | 34 s wall (10.6 CPU-minutes) and 15 s, measured 2026-10-09 |
| Soft reset (A+B+Start+Select) re-enters AgbMain and continues | held under gdb from frame 300, plain and sanitized, 1200 frames exit 0 (integration worker's run, not yet a scripted gate) | observed once |
| Audio is mixed and non-silent in the intro | peak sample 1855 at frame 700 (integration worker's measurement) | observed once |
| Every native object compiles from a clean clone with the host compiler, 32-bit, TEST=0 and TEST=1 | `make -f Makefile.native -j32 objects BITS=32` (and `TEST=1`) | 928 and 1895 objects, 0 warnings; no link yet |
| GBA ROM unchanged by the guarded modifications | sha1 of `pokeemerald.gba` before and after | identical (e2d4600d4e7b1269ac50c28bc96ece2b758ded9e); `make check` equal to the baseline |
| GBA memory regions can be mapped at their addresses in a native process on 32-bit and 64-bit, with ASan and UBSan | `research/memory-map-experiment.md` | works |
| The platform gate: clang-format (`.clang-format`) finds nothing to change, clang-tidy (`.clang-tidy`) and cppcheck report nothing at both widths, every unit test passes under ASan+UBSan at both widths | `make -C platform gate` | green 2026-10-09, 79 s, on main at 61ae8181f7 (and again after the end-to-end read's five cleanups) |
| Every gate in one sequence on main after the last milestone 6 merge (61ae8181f7): the platform gate, the 32-bit build, the opening and debug-menu replays plain (27 dumps, 26 screenshots; 20 dumps, 32 screenshots; both save round trips) and sanitized (identical), `make check` plain and sanitized (5862 tests: 5405 passed, 18 known failing, 9 expected failing, 421 to-do, 9 assumption failures, 0 failed), the GBA ROM rebuilt | the ten steps in order, each exit 0 | green 2026-10-09; 79 s, 2 s, 29 s, 28 s, 3 s, 75 s, 73 s, 38 s, 116 s, 1 s; ROM sha1 e2d4600d4e7b1269ac50c28bc96ece2b758ded9e |
| 64-bit, gate (a): the 64-bit native executable links from main with the three link-time data checks passing (the relocation check against `data_address_words.txt` and the 4-byte reference, the layout check against the probe, the battle script operand check) | `make -f Makefile.native -j32 native BITS=64` (and `SANITIZE=1`) | green 2026-10-09 on main at 94f3c70f62, 16 s and 24 s |
| 64-bit, gates (d) and (e): the opening and debug-menu replays on the 64-bit build, every dump and screenshot equal to the 32-bit expectations, both save round trips | `tests/smoke/run.sh` and `run_debug_menu.sh` with `build/native64/pokeemerald-native` | green 2026-10-09 on main at 94f3c70f62 (27 dumps, 26 screenshots; 20 dumps, 32 screenshots), 24 s and 23 s |
| 64-bit, gates (d) and (e) sanitized: both replays on the 64-bit sanitized build, identical dumps and screenshots, no sanitizer report | the same with `build/native64-san/pokeemerald-native`, `ASAN_OPTIONS=halt_on_error=0` | green 2026-10-09 on main at 94f3c70f62, 57 s and 55 s |
| 64-bit, gate (b): expansion's suite against the 64-bit build: 5866 tests, 5409 passed, 0 failed, 18 known failing, 9 expected failing, 421 to-do, 9 assumption failures (the benchmarks), exit 0; test for test the 32-bit results (`test/save.c` expects the 64-bit SaveBlock1 size under the guard) | `make -f Makefile.native -j32 check BITS=64` | green 2026-10-09 on main at 94f3c70f62, 64 s |
| 64-bit, gate (b) sanitized: the same suite on the 64-bit sanitized build, the same per-test results; the sanitizer reports are the 32-bit run's, access by access, when compared with `ASAN_OPTIONS=suppress_equal_pcs=0` (SUITE-LOG.md, "The sanitized run at 64 bits"); nothing new at 64 bits | `make -f Makefile.native -j32 check BITS=64 SANITIZE=1` | green 2026-10-09 on main at 94f3c70f62, 104 s |
| Gate (h): a save written by the 32-bit build loads in the 64-bit build and back: the opening up to the save by one width, Continue by the other (the dumps and screenshots after the restart as expected, the loaded state equal to the saved one, the file unchanged until the game saves), the bare image (an older 32-bit build's), a refused foreign layout id, and a save made by the second build loaded again by it; both directions, plain and sanitized | `tests/smoke/run_cross_width.sh build/native32/pokeemerald-native build/native64/pokeemerald-native out/` and the reverse, then the `-san` builds | green 2026-10-09 on main at 94f3c70f62: 39 s, 34 s, 99 s, 84 s |
| Every gate in one sequence on main after the last milestone 7 merge (94f3c70f62): the platform gate (format, lint at both widths, the sanitized unit tests at both widths, tools-test, glue-test); at 32 bits the build, the two replays plain and sanitized, `make check` plain and sanitized (5409 passed), the GBA ROM rebuilt; at 64 bits the build plain and sanitized, the two replays plain and sanitized, `make check` plain and sanitized (5409 passed); the cross-width script both ways, plain and sanitized | the 22 steps in order, each exit 0 | green 2026-10-09; the 32-bit half 11 min, the 64-bit half 6 min, the cross-width runs 4 min; ROM sha1 e2d4600d4e7b1269ac50c28bc96ece2b758ded9e |
| Every platform module is merged: `gba_memmap` (region layout via linker script), `ptest.h` self-test, `timer`, `transfer_bounds`, `input`, `rtc`, `video`, `audio`, `frame_pacer`, `interrupt`, `psg`, `ppu`, `dma`, `flash`, `save_file`, `bios`, `lcd_status`, `m4a_output`, `sound_mix`, `replay`, `inspect`, `restart`, `symbols`, `debug_channel`, `crash_reboot`, `arm_division`, and the `m4a` engine translation (also proven by a differential test against the ARM original under qemu-arm, run by hand) | `make -C platform test BITS=32` and `BITS=64`, every test under ASan+UBSan with leak detection, after a Sonnet first-pass review and orchestrator merge | green on both widths at merge time |

## Not verified

The engine's logic is now covered by expansion's suite (gate b) and the
opening by the replay (gate d); what no gate covers: the renderer and the
sound mixer beyond the opening's screenshots and the intro's audio
(expansion's suite runs headless and does not look at the frame buffer),
everything past Birch's lab in play, every menu neither replay opens
(the debug menu gate covers the party, summary and bag screens and
trainer battles; not the debug menu's warps, encounters, sound, cheats,
eggs or decorations), the windowed
path (a real SDL window and audio device ran only with SDL's dummy
drivers). At 64 bits the same gates cover the same ground as at 32
(the "Milestone 7" section); what no gate covers there either: the
debug menu's other screens and play past Birch's lab, and a save that
has been played far (the cross-width gate converts the opening's save,
whose object event templates and Enigma Berry record are mostly empty;
the converter's unit tests cover filled ones). Interchange with GBA
`.sav` files is not supported (host struct ABI; DECISIONS.md; an
emulator's save is in neither native layout). The m4a engine is proven against the
ARM original by a differential test run by hand, not by the standard
gate. The save-file nondeterminism (nine uninitialised bytes in the first
party Pokémon) is fixed in milestone 6: two runs give byte-identical
files. Known cosmetic: a crashing
runner's `crash_reboot` line on stderr can interleave with the hydra's
stdout line for another runner.

## Known broken

Nothing yet; nothing exists to be broken.

## Gates and their state

| Gate | State |
|---|---|
| (a) native build from a clean clone with documented prerequisites | green at both widths (`docs/BUILDING.md`; the 64-bit build runs its three link-time data checks) |
| (platform) `make -C platform gate`: format, lint, sanitized unit tests at both widths, tools-test, glue-test | green |
| (b) expansion's test suite against the native binary, same results as GBA | green at both widths, plain and sanitized: per test the GBA baseline but for the 9 benchmarks (assumption failures by decision) |
| (h) a save written by the 32-bit native build loads correctly in the 64-bit native build (cross-width round-trip; the save container is versioned so this stays true) | green, both directions, plain and sanitized |
| (c) GBA target builds and passes the suite | green (above) |
| (d) deterministic input-replay smoke test through the opening | green at both widths, plain and sanitized |
| (e) debug-menu gate | green at both widths, plain and sanitized |
| (f) unit tests and smoke boot clean under sanitizers | green: the unit tests, both replays and the full suite are clean under ASan+UBSan at both widths (the suite's known reports are listed with their root causes in SUITE-LOG.md) |
| (g) this file | current |

**32-bit done** (2026-10-09): every gate above that applies to 32-bit is green on main and recorded in "Verified by a gate"; the refactoring pass and the end-to-end read are recorded in "Milestone 6".

**64-bit done** (2026-10-09): every gate above is green at 64 bits on main at 94f3c70f62 and recorded in "Verified by a gate"; the milestone 7 refactoring pass and the end-to-end read are recorded in "Milestone 7".

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
- Upstream bug reports (2026-10-09): nine rows of `UPSTREAM_MODIFICATIONS.md`
  marked worth reporting were triaged against `rhh/upcoming` and
  rh-hideout's issues and pull requests (none already fixed, none already
  reported) and filed as seven issues, rh-hideout/pokeemerald-expansion
  #10892 to #10898; each row links its issue, so the upgrade runbook
  knows which rows should disappear when upstream merges a fix. Further
  candidates (the sanitized-run table in `platform/test_runner/SUITE-LOG.md`,
  milestone 5's rows) are not filed yet. Every issue offers a pull
  request; none is opened until the owner has seen the maintainers'
  request, which is recorded here when it comes.

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
| debugmenu (milestone 5: debug-menu gate, replay include and facts, palette padding) | Sonnet | Sonnet |
| refactor-audio, refactor-video, refactor-store, refactor-bios, refactor-core, refactor-tools, refactor-modaudit (milestone 6 refactoring pass) | Sonnet | Sonnet (mutation testing), orchestrator merge gate |
| lint (milestone 6: .clang-format, .clang-tidy, cppcheck, the gate targets, the findings fixed) | Sonnet | Sonnet, orchestrator merge gate |
| docs (milestone 6: CONTRIBUTING, UPGRADING, ARCHITECTURE, README) | Sonnet | orchestrator |
| the end-to-end read of platform/ and its five cleanups (milestone 6) | Fable (orchestrator) | the full gate |
| m7-inventory (milestone 7: the 64-bit inventory, no code) | Opus | orchestrator |
| m7-seed, m7-link, m7-testtable, m7-armdiv, m7-data, m7-bscript, m7-crit, m7-align, m7-san, m7-save, m7-anim (milestone 7, the 64-bit build) | Sonnet; Opus for m7-data and m7-bscript (one worker), m7-armdiv, m7-san and m7-save | Sonnet (mutation testing), orchestrator merge gate |
| m7-docs (milestone 7: BUILDING, ARCHITECTURE, CONTRIBUTING, the platform comments at 64 bits) | Sonnet | orchestrator |
| refactor7-platform, refactor7-glue, refactor7-expansion (milestone 7 refactoring pass) | Sonnet | Sonnet (mutation testing), orchestrator merge gate |
| the end-to-end read of the milestone 7 changes and its three comment corrections | Fable (orchestrator) | the full gate at both widths |
| battle-glyph (milestone 6: the trainer party seed and the nickname padding, root cause of the screenshot flake) | Opus | orchestrator |
| upstream-report (seven issues drafted, triaged against upstream) | Sonnet | Fable (orchestrator) |
| Research, synthesis, decisions, merges | Fable (orchestrator) | |

## Environment this was verified in

WSL2 Ubuntu 24.04.1, 32 CPUs, 31 GiB RAM, gcc 13.3 with multilib,
clang 18, SDL2 2.30.0 (amd64 and i386), gcc-arm-none-eabi 13.2.1,
valgrind, cppcheck 2.13. 32-bit ASan and UBSan runtimes verified to link
and run with SDL2.
