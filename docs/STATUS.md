# Status

Last updated 2026-10-09 (milestone 6 done: **32-bit done**, every gate green and recorded below; milestone 7, the 64-bit build, started).

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

**In flight.** Milestone 7, the 64-bit build. The Opus inventory is done
(PLAN.md "Milestone 7 plan"; DECISIONS.md has the decisions it forced).
Merged on 2026-10-09, each by a worker in its own worktree, reviewed by a
Sonnet reviewer with mutation testing and gated by the orchestrator:
m7-seed (the trainer seed hashes the same 52 bytes at both widths),
m7-link (the two link assertions, INTR_VECTOR as a 32-bit store, a Union
Room task-data overlay the reviewer found), m7-testtable (the test table
contiguous at 64 bits and checked at start-up; patchelf and the hydra
read ELF64, with a committed test), m7-armdiv (one SIGFPE decoder for
i386 and x86-64, the GBA's 64-bit division-by-zero results measured under
qemu-arm), m7-data (the assembled data emitted in the x86-64 layout, with
a relocation check, a layout check and a classified symbol list run by the
64-bit link), m7-bscript (the battle scripts' 174 pointer operands at 8
bytes, with a checker that parses the macros against the C declarations),
m7-crit (an upstream out-of-bounds read in GetCriticalHitOdds that the
64-bit build turned into a crash, fixed natively to the GBA's result).
**The 64-bit build now plays both replays** (opening and debug menu, all
dumps and screenshots equal to the 32-bit expectations, mixed audio
identical frame by frame) and expansion's suite at 64 bits differs from 32
only in `SaveBlock1 is backwards compatible` (the layout difference gate h
converts); the 64-bit rows join the gate table once the orchestrator's own
run on main is recorded. Running now: m7-align (Sonnet; 8-byte alignment
of the save blocks, the heap, the RFU buffers and task data at 64, which
is what the sanitized 64-bit replays need; in its review-fix round) and
m7-save (Opus; the save converter and gate h). Then: the sanitized gates
at 64, the suite at 64 recorded, the refactoring pass again, 64-bit done.

**Broke and fixed.** The debug-menu gate's two battle screenshots flipped
between male and female for the opponent's Poochyena whenever the binary
was relinked (three refactoring branches hit it). Root cause: expansion
seeds a trainer's party with a CRC over the whole trainer struct, pointer
to the party table included; on the GBA that is a fixed ROM address,
natively it moves with every build. Fixed under a guard (the CRC skips
the pointer; DECISIONS.md "The trainer party seed does not hash the
party table's address"), and the gate's expectations updated once. The
same investigation removed the save file's nine uninitialised bytes
(CreateBoxMon's species-name buffer, now padded): two runs give
byte-identical saves. Earlier breakages are in the milestone logs.

**Needs the owner.** rh-hideout's maintainer replied to all seven upstream
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
| 64-bit, gate (a): the 64-bit native executable links from main with the relocation, layout and battle-script operand checks passing | `make -f Makefile.native -j32 native BITS=64` | green 2026-10-09 on main at b66dbd4385 |
| 64-bit, gate (d): the opening replay on the 64-bit build, every dump and screenshot equal to the 32-bit expectations, the save round trip | `tests/smoke/run.sh build/native64/pokeemerald-native out/` | green 2026-10-09 (27 dumps, 26 screenshots) |
| 64-bit, gate (e): the debug-menu replay on the 64-bit build | `tests/smoke/run_debug_menu.sh build/native64/pokeemerald-native out/` | green 2026-10-09 (20 dumps, 32 screenshots) |
| 64-bit, gate (b): expansion's suite against the 64-bit build: 5866 tests, 5408 passed, 1 failed, 18 known failing, 9 expected failing, 421 to-do, 9 assumption failures; per test equal to the 32-bit run except `SaveBlock1 is backwards compatible` (sizeof 15768 vs 15496: the layout difference gate h converts; the test is expansion's sizeof comparison and stays red at 64 by design unless the owner wants it guarded) | `make -f Makefile.native -j32 check BITS=64` | 2026-10-09, 125 s: red by that one test, which is the expected 64-bit layout |
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
drivers), and 64-bit, which does not link yet (two expansion layout
asserts; milestone 7; the inventory worker's experiment links once they are
disabled, which says nothing about running). Interchange with GBA `.sav` files is not supported
(host struct ABI; DECISIONS.md). The m4a engine is proven against the
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
| (a) native build from a clean clone with documented prerequisites | green for 32-bit (`docs/BUILDING.md`); 64-bit links and runs from main (clean-clone run not yet recorded) |
| (platform) `make -C platform gate`: format, lint, sanitized unit tests at both widths | green |
| (b) expansion's test suite against the native binary, same results as GBA | green on 32-bit, plain and sanitized: per test the GBA baseline but for the 9 benchmarks (assumption failures by decision); at 64-bit plain the same but for `SaveBlock1 is backwards compatible` (the layout test; gate h) |
| (h) a save written by the 32-bit native build loads correctly in the 64-bit native build (cross-width round-trip; the save container is versioned so this stays true) | milestone 7, not started |
| (c) GBA target builds and passes the suite | green (above) |
| (d) deterministic input-replay smoke test through the opening | green on 32-bit, plain and sanitized; green on 64-bit plain (sanitized: m7-align in flight) |
| (e) debug-menu gate | green on 32-bit, plain and sanitized; green on 64-bit plain |
| (f) unit tests and smoke boot clean under sanitizers | unit tests green under ASan+UBSan on 32 and 64-bit; the opening replay and the full suite are clean under ASan+UBSan on 32-bit |
| (g) this file | current |

**32-bit done** (2026-10-09): every gate above that applies to 32-bit is green on main and recorded in "Verified by a gate"; the refactoring pass and the end-to-end read are recorded in "Milestone 6".

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
| battle-glyph (milestone 6: the trainer party seed and the nickname padding, root cause of the screenshot flake) | Opus | orchestrator |
| upstream-report (seven issues drafted, triaged against upstream) | Sonnet | Fable (orchestrator) |
| Research, synthesis, decisions, merges | Fable (orchestrator) | |

## Environment this was verified in

WSL2 Ubuntu 24.04.1, 32 CPUs, 31 GiB RAM, gcc 13.3 with multilib,
clang 18, SDL2 2.30.0 (amd64 and i386), gcc-arm-none-eabi 13.2.1,
valgrind, cppcheck 2.13. 32-bit ASan and UBSan runtimes verified to link
and run with SDL2.
