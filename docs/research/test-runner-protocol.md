# How expansion's test suite runs on GBA, and what the native build must provide

Research notes gathered 2026-10-08 against expansion/1.17.1 and the mGBA commit
the bundled Linux `tools/mgba/mgba-rom-test` was built from
(`dbffb46c4e7d2e7a2cbed7c3488cece4c2176d4c`). Line numbers refer to those
revisions. The synthesis and decisions are in `../RESEARCH.md`; this file is
the evidence.

## 1. Build: `make check`

- Making `check` sets `TEST := 1` (`Makefile:53-55`); objects go to
  `build/<name>-test`; `-DTESTING=$(TEST)` is always passed (`Makefile:159`),
  so `TESTING` is always defined as 0 or 1. LTO is off for tests.
- Test sources: `$(wildcard test/*.c test/*/*.c test/*/*/*.c)` minus `*.inc.c`
  (`Makefile:310-313`), compiled only when `TEST=1` (`Makefile:490-499`).
  All of `src/` compiles in both builds; test-only code sits behind `#if TESTING`.
- Link uses `ld_script_test.ld`, then `gbafix -d0` (enables the illegal
  instruction handler at 0x9FFC000), then
  `patchelf $(TESTELF) gTestRunnerArgv "$(TESTS)\0"`.
- `check` target (`Makefile:362-365`):
  ```
  cp pokeemerald-test.elf pokeemerald-test-headless.elf
  patchelf pokeemerald-test-headless.elf gTestRunnerHeadless '\x01' gTestRunnerSkipIsFail "\x00"
  tools/mgba-rom-test-hydra/mgba-rom-test-hydra tools/mgba/mgba-rom-test arm-none-eabi-objcopy pokeemerald-test-headless.elf
  ```
- `patchelf` overwrites bytes at a `.symtab` symbol's file offset. Patched
  symbols: `gTestRunnerEnabled`, `gTestRunnerN`, `gTestRunnerI`,
  `gTestRunnerArgv[256]` (`test/test_runner_args.c:5-8`, kept in their own
  file so the optimizer cannot fold them), `gTestRunnerHeadless`,
  `gTestRunnerSkipIsFail` (`src/test_runner_stub.c:10-13`).
- `ld_script_test.ld` vs `ld_script_modern.ld`: `gInitialMainCB2 =
  CB2_TestRunner`; IWRAM shortened to 0x7F00 with a 0x100-byte
  `IWRAM_PERSISTENT` region at 0x3007F00 (`.persistent`, NOLOAD); a `tests`
  output section with `__start_tests`/`__stop_tests`, `test_test_runner.o`
  first, then `test/*.o(.tests)`; a `dacs` section at 0x9FFC000.
- Tests register themselves: `TEST(name)` emits a `static const struct Test`
  in section `.tests` (`include/test/test.h:117-127`); `struct Test` is
  `{name, filename, runner, data, u16 sourceLine}` (20 bytes on GBA). The
  runner walks `__start_tests..__stop_tests` as a packed array.

## 2. mgba-rom-test and the hydra

### mgba-rom-test (`src/platform/test/rom-test-main.c`)
- Options beyond mGBA's generic ones: `-S SWI` (exit SWI, default 3) and
  `-R REGISTER` (register returned as exit code). There is no `-N`.
- SWI 3 (BIOS Stop) triggers the core `shutdown` callback, which reads the
  `-R` register as the exit code. Process returns `cleanExit ? exitCode : 1`.
- Prints one line per log message: `"<Category>: <message>\n"`. The debug
  string category is `GBA Debug`; other core logs are `GBA`.
- Debug registers (`include/mgba/internal/gba/io.h:164-166`): STRING at
  0x4FFF600 (256-byte buffer), FLAGS at 0x4FFF700, ENABLE at 0x4FFF780.
  Writing 0xC0DE to ENABLE turns debug on; reading ENABLE then gives 0x1DEA.
  Writing FLAGS with bit 8 set logs the buffer at level `1 << (flags & 7)`
  and zeroes the buffer (`src/gba/gba.c:587-599`). `MGBA_LOG_INFO = 3`.

### mgba-rom-test-hydra (`tools/mgba-rom-test-hydra/main.c`)
- Usage: `mgba-rom-test-hydra <mgba-rom-test> <objcopy> <elf>`.
- Builds a symbol table from `.symtab` to turn `<0xNNNNNNN>` into
  `<symbol+0xoff>` in output.
- Runner count from `-jN` in `MAKEFLAGS`, capped at `MAX_PROCESSES = 32`.
  Per runner i: fork; copy ELF to `/tmp/mgba-rom-test-hydra-<pid>`; run
  `tools/patchelf/patchelf <rom> gTestRunnerN \xNN gTestRunnerI \xII`
  (relative path: hydra must run from repo root); then
  `stdbuf -oL <mgba-rom-test> -l15 -ClogLevel.gba.dma=16 -Rr0 <rom>`.
- Line protocol (`handle_read`, lines 258-395):
  - `GBA: <rest>`: appended to the runner's 1 MiB output buffer.
  - `GBA Debug: :<C><payload>`: command by letter. `N` test name, `L`
    `file:line`, `P` pass, `E` expected fail, `K` known fail, `T` to-do, `U`
    known-failing passed, `V` expected-fail passed, `A` assumption fail, `F`
    fail. Each result letter prints `[i] name: <rest>` plus buffered output.
  - `GBA Debug: <text>` without a leading colon: buffered output.
  - Anything else: copied to stdout.
- Exit code: the largest child `WEXITSTATUS`. Counts are for display only.
- Summary lists `FAILED tests:`, `Tests with ASSUMPTIONS_FAILED:`,
  `KNOWN_FAILING tests PASSING:` then `- Tests FAILED/PASSED/TOTAL ...` lines.

## 3. The ROM side (`test/test_runner.c`)

- `CB2_TestRunner` state machine: INIT, ASSIGN_TEST, RUN_TEST, REPORT_RESULT,
  NEXT_TEST, EXIT. Plain `TEST`s run entirely inside one callback call; the
  loop only yields to the main loop when a test replaces callback2 (battle
  tests set `CB2_InitBattle`).
- INIT: `MgbaOpen_` (exit 2 on failure), filter from `gTestRunnerArgv`
  (`.c` suffix = filename match, `*` prefix = contains, else prefix),
  `MoveSaveBlocks_ResetHeap()`, `gIntrTable[7] = Intr_Timer2`, crash
  recovery.
- ASSIGN_TEST: clears save blocks, prints `:N<name>` and `:L<file>:<line>`
  for every test (even ones another process runs), `timeoutSeconds = 60`
  when headless else UINT_MAX, `InitHeap(gHeap, HEAP_SIZE)`, `ResetTasks()`,
  starts Timer 2 with interrupt, `AssignCostToRunner()`.
- Work split: all N processes compute the same greedy cost table and each
  runs only tests assigned to its index `gTestRunnerI`. `ASSUMPTIONS` run in
  every process. The battle runner's cost function runs the test body to
  count PARAMETRIZE cases and trials.
- RUN_TEST: callbacks reset, `gBattleTypeFlags = 0`, `SeedRng(0)`,
  `SeedRng2(0)`, `runner->setUp`, then `runner->run` unless an ASSUMPTIONS
  block in the same file failed (compares `__FILE__` pointers, relying on
  string interning).
- REPORT_RESULT: `REG_TM2CNT_H = 0`, `tearDown`, leak check
  `TestRunner_CheckMemory` (walks heap blocks, requires every `next` pointer
  to lie in `[EWRAM_START, EWRAM_END)`; leftover allocations or tasks turn a
  pass into FAIL), prints the result line (`:P`, `:U`, `:V`, `:E`, `:A`,
  `:T`, `:K`, `:F`).
- Exit code 1 when a test failed outside EXPECT_FAIL or the result differs
  from the expected one (ASSUMPTION_FAIL exempt unless SkipIsFail). Exit:
  ```c
  register u32 _exitCode asm("r0") = exitCode;
  asm("swi 0x3" :: "r" (_exitCode));   // test_runner.c:815-819
  ```
- Output: `MgbaPutchar_` writes bytes to `REG_DEBUG_STRING[i]`; at i = 255
  it terminates and sends with `REG_DEBUG_FLAGS = MGBA_LOG_INFO | 0x100`.
  `\n` forces a send; leftovers are sent at the end of each printf. Custom
  printf supports `%c %C %s %.*s %S %.*S %U %d %q %p` (`%p` prints
  `<0x` + 7 hex digits + `>`).
- Timeout: Timer 2 reload `UINT16_MAX - 274*60`, prescaler 1024, IRQ on
  (about 1 s of emulated time). `Intr_Timer2` counts `timeoutSeconds` down;
  at zero, if `checkProgress()` says no progress, sets TIMEOUT and overwrites
  the BIOS IRQ stack return address:
  `IRQ_LR (*(vu32 *)0x3007F9C) = ((uintptr_t)JumpToAgbMainLoop & ~1) + 4;`
- Early exit: `Test_ExitWithResult_` records the result and calls
  `JumpToAgbMainLoop()`, naked ARM asm that reloads `sp` from
  `gAgbMainLoop_sp` (captured in `AgbMain` with `__builtin_frame_address(0)`,
  `src/main.c:73,130`) and jumps to `AgbMainLoop`. Every `EXPECT_*`, `ASSUME`,
  `fatalf`, `assertf` reaches it (`include/assertf.h:97-100`).
- Main loop: `AgbMainLoop` = ReadKeys, callbacks, PlayTimeCounter, MapMusic,
  `WaitForVBlank` (BIOS `VBlankIntrWait`). `VBlankIntr` runs the vblank
  callback, copies buffered registers, runs queued DMA3 transfers and
  `m4aSoundMain`. `AdvanceRandom()` is skipped each vblank while the runner is
  enabled (`src/main.c:377`).
- Crashes: `DACSEntry` in `.dacs` at 0x9FFC000 sets CRASH and patches the
  return address at 0x3007FEC. The bundled Linux and Windows mGBA builds do
  not jump to the debug handler (only the mac build does), so illegal
  instruction recovery is a no-op there. A jump to NULL reboots the ROM;
  `gPersistentTestRunnerState` at 0x3007F00 (`{address:28, state:1,
  expectCrash:1}`) survives `RegisterRamReset(0xFF)` because mGBA keeps the
  last 0x200 bytes of IWRAM, and INIT replays the cost assignment up to the
  crashed test and reports CRASH. `SoftReset` is never used by the runner.
- RNG rigging: `RandomUniform`, `RandomUniformExcept`, `RandomWeightedArray`,
  `RandomElementArray` are strong definitions in `test_runner.c:1080-1114`
  overriding weak aliases in `src/random.c:162-172`.

### Battle tests (`test/test_runner_battle.c`)
- Runner state is placed over `sBackupMapData` (0x5000 bytes) to avoid the
  heap. The test function runs on a 1024-byte stack inside that state,
  switched with naked Thumb asm (lines 78-195) so locals survive between
  calls.
- A test runs once to build parties, actions and expected events; flags
  `IS_MASTER | RECORDED_IS_MASTER | RECORDED_LINK | TRAINER` (never
  `BATTLE_TYPE_LINK`); `StartBattle` sets callback2 to `CB2_InitBattle`;
  recorded actions play back; `TestRunner_Battle_AfterLastTurn` runs THEN and
  FINALLY. Engine hooks: `RecordMessage`, `RecordHP`, `RecordAnimation`, etc.
  (no-ops in non-test builds via `include/test_runner.h:53-75`).
- RNG seed defaults to zero; `PASSES_RANDOMLY` runs 50 trials or one per
  tag value; pass ratio within about 2%. Timeout counts emulated seconds and
  `BattleTest_CheckProgress` resets it whenever a parameter, trial or turn
  advances. Nothing depends on wall-clock rate, only on frames advancing.

## 4. Hardware dependencies the native build must satisfy

1. Debug log channel with mGBA's register semantics and the hydra's
   `GBA Debug: ` line format.
2. Exit through `swi 0x3` with r0 as the exit code.
3. Timer 2 interrupt at roughly 1 Hz of emulated time, dispatched through
   `gIntrTable[7]`.
4. A way back to the main loop from any depth: direct call, from the timer
   interrupt (IRQ_LR patch), and from the crash handler.
5. Illegal-instruction handler (only matters for KNOWN_CRASHING tests; the
   bundled Linux mGBA does not implement it either).
6. Restart after a crash with the 0x100-byte persistent region intact;
   `PersistentTestRunnerState.address` is a 28-bit field and cannot hold a
   64-bit pointer.
7. Linker support for `.tests`: `__start_tests`/`__stop_tests`,
   `test_test_runner.o` first, packed entries, per-file ASSUMPTIONS before
   tests (`no_reorder`), interned `__FILE__`.
8. ELF-patched constants: `gTestRunnerN`, `gTestRunnerI`,
   `gTestRunnerArgv`, `gTestRunnerHeadless`, `gTestRunnerSkipIsFail`.
9. Memory map: EWRAM 256 KiB at 0x2000000, IWRAM 0x7F00 + 0x100 persistent,
   ROM 32 MiB; `INTR_CHECK` 0x3007FF8, `INTR_VECTOR` 0x3007FFC. In the
   compiled test ELF: `.ewram.sbss` 0x37650, `.iwram.bss` 0x6638,
   `gHeap` 0x1C500 at 0x02014c1c, `sBackupMapData` 0x5000.
10. Heap: `InitHeap(gHeap, HEAP_SIZE)` per test; leak check needs heap
    pointers inside the EWRAM range; `MoveSaveBlocks_ResetHeap` uses `gHeap`
    as scratch and calls `Random32`.
11. Battle runner: state over `sBackupMapData`, private 1 KiB stack switched
    in asm.
12. VBlank interrupt and `VBlankIntrWait` to advance frames; vblank runs DMA3
    queue and `m4aSoundMain`.
13. BIOS calls on these paths: `RegisterRamReset`, `VBlankIntrWait`, `CpuSet`
    (`CpuFill16` in `ClearSav1/2/3`), `CpuFastSet`, `LZ77UnCompWram/Vram`
    (`test/compression/smol.c` also uses VRAM at 0x6000000 as a buffer),
    Stop (SWI 3).
14. DMA3 for crt0's initial data copy and the vblank queue.
15. Timer 3 for the eight `BENCHMARK` uses.
16. Assembly inside tests: `OVERWORLD_SCRIPT` uses `mov %0, pc` inline asm
    (`include/test/overworld_script.h:29-60`); `Random32` is naked Thumb asm
    (`src/random.c:44-71`).
17. `gInitialMainCB2` comes from the linker script.

## 5. What `TESTING` changes in game code

About 160 lines. Highlights: `include/config/test.h` (about 565 overrides:
every form and family enabled, `POKEMON_NAME_LENGTH 12`); test flags from
0x5000 and vars from 0x9000 backed by `sTestFlags`/`sTestVars`;
`DIFFICULTY_TEST`; runtime config overrides via `gConfigChangesTestOverride`;
sprite allocation counters and assertions; allocation records file and line;
`assertf.h` maps asserts to `Test_ExitWithResult`; `gTrainers` and
`gBattlePartners` supplied by the runner; forced abilities, SET battle style,
test trainer pictures, gimmick checks skipped, prompts auto-advance, AI score
logging, hyper-trained IVs not applied in battle. Runtime
`gTestRunnerEnabled` checks: no `AdvanceRandom` in vblank, no heap reset in
`CB2_InitBattle`, leftover tasks cleaned in `CB2_QuitRecordedBattle`.

## 6. Debug menu

- `DEBUG_OVERWORLD_MENU` defaults to `DISABLED_ON_RELEASE`
  (`include/config/debug.h:5`), i.e. on unless `RELEASE` is defined.
  Trigger: hold R (`DEBUG_OVERWORLD_HELD_KEYS`) and press Start
  (`src/field_control_avatar.c:151-157, 243-248`). Battle debug menu on
  Select (`DEBUG_BATTLE_MENU`).
- `Debug_ShowMainMenu` (`src/debug.c:848-853`); main list: Utilities, PC/Bag,
  Party, Give X, Player, Scripts, Trainers, Encounters, Flags & Vars, Sound,
  ROM Info, Cancel. List menus: A selects, B backs out. Numeric entry: up/down
  change a digit, left/right move between digits, A confirms.
- Give X: item id + quantity then `AddBagItem`; basic Pokémon (species, level)
  via `ScriptGiveMon`; complex Pokémon (gender, shiny, ability, nature, moves,
  IVs, EVs, Dynamax level, Gigantamax, Tera type) via
  `GiveScriptedMonToPlayer`. Party: heal, clear, set from debug trainer,
  start debug battle. Flags & Vars: `FlagToggle`, `VarSet`. Utilities: warp
  to map warp (`SetWarpDestinationToMapWarp`, `DoWarp`).
- "Cheat start" runs `Debug_CheatStart` (`data/scripts/debug.inc:11-73`):
  story flags, five level-20 Pokémon, Acro Bike, all badges.
- `Debug_EventScript_Script_1..8` are empty placeholders reachable from the
  Scripts submenu, usable for a scripted gate.
- The script-test DSL `OVERWORLD_SCRIPT`/`RUN_OVERWORLD_SCRIPT` runs scripts
  through `RunScriptImmediately` without a loaded overworld (used about 130
  times for `givemon`).

## 7. Headless mode

`gTestRunnerHeadless` (patched to 1 by `make check`) sets a 60-second
emulated timeout, re-runs a battle once on the first SCENE mismatch (FLAKY if
different), and skips presentation: move animations (except Transform and
Ally Switch), message delays, hit animations, sounds, popups, pauses, intro
slides and fades, ball throw delays, shiny animation, cries, Pokémon
animations, fanfare tasks.

## 8. Counts and time

Source counts under `test/` (967 files, 946 under `test/battle`): `TEST(`
920, `SINGLE_BATTLE_TEST` 3496, `DOUBLE_BATTLE_TEST` 953, AI variants 609,
others about 100, `TO_DO_BATTLE_TEST` 421; 6502 definitions plus 433
`ASSUMPTIONS` blocks. The compiled ELF's `.tests` section holds 6294 entries
of 20 bytes. Uses: `PARAMETRIZE` 5606, `PASSES_RANDOMLY` 348,
`KNOWN_FAILING` 22, `EXPECT_FAIL` 13, `BENCHMARK` 8, `KNOWN_CRASHING` 2.
Upstream CI's build-plus-run "Test" step takes 18 to 25 minutes on
`ubuntu-latest` (recent master runs).
