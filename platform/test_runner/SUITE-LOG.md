# Expansion's suite on the native build: running log

The native TEST=1 binary run through expansion's own hydra
(`make -f Makefile.native -j32 check BITS=32`), compared with the GBA
baseline (`docs/research/gba-baseline-1.17.1.md`: 5862 total, 5414 passed,
18 known failing, 9 expected failing, 421 to-do, exit 0). Each failure
class: how many tests it took, the root cause, and the fix or status.
Newest runs last.

## How a failure is found

`crash_reboot` prints `signal N at <pc>, fault address <addr>` on stderr for
every crash; the hydra shows the test as `CRASH`. The crash sites of a run
are counted by symbolising those program counters against the executable's
symbol table, and each site is reproduced with one partition of the suite
under gdb (`gTestRunnerN`/`gTestRunnerI` patched as the hydra does), which
also prints the running test's name from `gTestRunnerState`.

## Classes

### 1. The debug channel did not exist (all tests)

Before this work the TEST=1 binary exited 2 after one frame: `MgbaOpen_`
found no mGBA. Fixed by `platform/debug_channel` and the guarded
`MgbaOpen_`/send in `test/test_runner.c`.

### 2. The hydra's copy cannot be executed (all tests)

The hydra copies the test ELF to `/tmp/mgba-rom-test-hydra-<pid>` with mode
0600 and never closes the descriptor it wrote it with; mgba-rom-test only
reads the file, but executing it fails (`EACCES`, then `ETXTBSY`). Fixed in
the launcher, `platform/test_runner/native-rom-test`: `chmod u+x`, and its
copies of that descriptor are closed before the exec. The hydra is
unchanged.

### 3. Reads and writes the GBA serves from memory that is not there natively

On the GBA every address reads something: address 0 is the BIOS ROM (reads
outside the BIOS return its last fetched opcode, writes are ignored), unused
bus space returns open-bus values, EWRAM mirrors every 256 KiB and the ROM
answers far past any table. Code that dereferences NULL, indexes past a
table or wraps a counter therefore goes unnoticed. Natively the first 64 KiB
are unmapped (`vm.mmap_min_addr`; `MMAP_PAGE_ZERO` needs privileges,
checked: `setarch -Z` still faults) and so is everything around the image.
Each site gets the smallest guarded fix that does what the GBA ends up
doing; where the GBA's value is unknowable (an IWRAM byte, open bus), what
a correct program would do.

| Site | Tests | What the GBA does | Native fix |
|---|---|---|---|
| `test/test_runner.c` INIT: `MoveSaveBlocks_ResetHeap` copies `*gSaveBlock1Ptr`, NULL at boot | all | copies BIOS bytes, which `ClearSaveBlocks` wipes before every test | point `gSaveBlock1Ptr` at `gSaveblock1` first if NULL |
| `test/test_runner.c` `FunctionTest_SetUp`: `ClearRiggedRng` before `AllocZeroed` writes through the freed (NULL) `gFunctionTestRunnerState` | every `TEST` | writes ignored | allocate first (the allocation is zeroed, which is what `ClearRiggedRng` writes) |
| `src/battle_main.c` `CB2_InitBattleInternal` → `BattleSetup_GetEnvironmentId` reads `gMapHeader.mapLayout` (NULL, no map) | every battle test | garbage environment, overwritten by `BATTLE_ENVIRONMENT_BUILDING` for recorded battles | not called for recorded battles |
| `src/m4a.c` `IsPokemonCryPlaying`, `m4aMPlayStop` with `gMPlay_PokemonCry` NULL (headless tests play no cry) | ~3400 battle tests | `ident` is never `ID_NUMBER`; no cry playing | NULL returns FALSE / returns |
| `src/battle_script_commands.c` `PlayAnimation`: `*argPtr` with the macros' default `argPtr=NULL` | most battle tests | reads BIOS, value unused | reads a zero |
| `src/battle_script_commands.c` `Cmd_jumpifarrayequal`: `jumpifbyteequal B_SHOW_DYNAMAX_MESSAGE, FALSE` passes two constants (NULL, NULL) as arrays | 108 Dynamax tests | NULL equals NULL: jumps | the same pointer twice jumps without reading |
| `test/test_runner_battle.c` `ClosePokemon`: a `PLAYER(...)` with no body and no `;` takes the next one as its body and closes twice, the second time with `currentMon` NULL | 36 battle tests (`trace.c` and others) | reads and writes BIOS, no effect | returns when `currentMon` is NULL |
| `src/battle_util.c` Hex/Venoshock (`EFFECT_DOUBLE_POWER_ON_ARG_STATUS`): `GetMoveAdditionalEffectById(move, 0)` on a move with none | 4 | BIOS byte, never `MOVE_EFFECT_REMOVE_STATUS` | checks the count first |
| `src/battle_util.c` `GetIllusionMonPtr` outside a battle (`gBattleStruct` NULL; `test/text.c` expands battle strings) | 1 | no illusion | NULL returns NULL |
| `src/m4a.c` `m4aSongNumStart`: `PhantomForceAttack` plays song `SOUND_PAN_ATTACKER` (-64: 0xFFC0) | 2 animation tests | entry read from ROM far past `gSongTable`, no player there, nothing starts | song numbers past the table start nothing |
| `src/battle_anim_ice.c` `InitIceBallAnim`: the fifth roll (`rolloutTimer` 4) takes affine animation 5 of 5 | 1 animation test | the word after the table, 0x01C001C0, as the animation's address: open bus | the last animation |
| `src/recorded_battle.c` `RecordedBattle_ClearBattlerAction`: clearing more than was recorded wraps the size to 0xFFFF and writes 0xFF 64 KiB past the record | 1 AI doubles test | EWRAM's mirror folds the write onto another EWRAM variable | an empty record stays empty |
| `test/text.c` "Map names fit in popup": `MAP_GROUP_COUNT` counts FireRed's groups, NULL in Emerald's `gMapGroups` | 1 | header addresses from BIOS, headers from IWRAM | NULL groups skipped |

### 4. ROM mirror tags on script function pointers (every script)

Expansion marks script commands, specials and callnatives that "request
effects" by storing `function + ROM_SIZE` (0x2000000): on the GBA that is
the same code in the ROM's first mirror (0x0A000000), and
`(pointer & 0xE000000) == 0xA000000` recognises it. Natively those
addresses are unmapped: a crash in `RunScriptCommand` for every
`RUN_OVERWORLD_SCRIPT` test and every overworld script, and
`Script_HasNoEffect` (test/script.c) does not see the tag.

For a while this branch linked the executable at 0x08000000 and mapped its
read-only segments again 0x2000000 higher (a ROM mirror). That was
withdrawn (coordinator's decision, 2026-10-08): it capped the native image
at 32 MiB (the plain build already used 28 MiB of it, TEST=1 26 MiB, and the
sanitized build only fit with outline AddressSanitizer checks and its
read-only data moved out of the window), and a 64-bit build cannot run code
from a mirror. The executable is linked at 0x40000000 again. The fix is
work/replay's untagging (src/script.c, src/scrcmd.c, include/script.h,
platform/script_tag.h), which works on both widths; until main has it,
this class stays open here (run 10: 95 tests, all `RUN_OVERWORLD_SCRIPT`
and script tests).

The two other places that take a pointer to be in the cartridge ROM are
guarded on this branch: `src/malloc.c` stores a heap block's allocation
site as an offset from `__executable_start` (29 bits, using the header's
4 unused bits, so under 512 MiB) instead of the address without
`ROM_START`, so the runner's leak report (`test/test_runner.c`) does not
read address 0x08xxxxxx; `src/field_name_box.c` `SetSpeaker` asks
`PlatformIsRomPointer` (the executable's read-only segments) instead of
testing `ROM_START..ROM_END`. Left as is: the hydra symbolises `%p` output
only for 7-digit addresses (`<0x\d{7}>`, GBA addresses), so a native
failure message that prints a pointer shows the raw 8-digit address.

### 5. The battle runner's 27-bit address fields

`QueuedSubHitEvent.address` is 27 bits: it holds the address of a test
function's local (`SUB_HIT(..., captureDamage: &damage)`), which on the GBA
is in EWRAM. The native call stack was in the executable's `.bss`, above
2^27. Fixed in the platform: gba_regions.ld puts the 1 MiB call stack at
0x01800000 (GBATEK's unused 00004000-01FFFFFF), and transfer_bounds accepts
it as a stack.

### 6. Upstream bugs the GBA survives that x86 does not

| Site | Tests | GBA | Native fix |
|---|---|---|---|
| `src/daycare.c` `GiveParentSharedLevelUpMoves`: the inner loop increments `i`, not `j`, and scans past `sharedMoves` | daycare egg tests | scans memory (stack, IWRAM mirrors, at the latest the learnset in ROM) until a halfword equals `levelupLearnset[0].move`, then adds that move | does the same without the scan |
| `src/party_menu.c` `BufferBattlePartyOrderBySide`: with one Pokémon on a two-battler side (2v1, 1v2) `j` reaches 6 and writes `partyIndexes[6]` | 2v1 and 1v2 battles | one byte of stack padding | bounded (`-fstack-protector` aborted natively) |
| `test/pokemon.c` "BoxPokemon encryption works": `u8 nickname[12]` receives 12 characters and an EOS | 1 | stack padding | `nickname[POKEMON_NAME_LENGTH + 1]` |
| Integer division by zero: `ComputeBallShakeOdds` (a ball that cannot catch), `GetHealthPercentage` (empty battler), `AnimSparkElectricityFlashing_Step` (`% data[4]` with 0) | 9 | libgcc's `__udivsi3`/`__divsi3`/`__modsi3` return (0xFFFFFFFF or INT_MAX/INT_MIN or 0) instead of trapping | platform: `arm_division` completes x86's divide error with libgcc's results (no game change) |

### 7. The game's own mGBA printf

`src/libisagbprn.c` `MgbaPrintf` (expansion's `DebugPrintf`) writes the
debug string and stores FLAGS itself; natively the store was plain memory,
so the text was never printed and stayed in the buffer in front of the
runner's next message (seen as a garbled test name). Guarded:
`MgbaOpen`/`MgbaPrintf` call `PlatformDebugOpen`/`PlatformDebugSend`.

### 8. Benchmarks (9 tests): documented divergence

`BENCHMARK` (`include/test/test.h`) counts GBA cycles with Timer 3 around a
block and `EXPECT_FASTER` compares two counts. Natively the game's code
takes no emulated time (time advances at frame boundaries), so both counts
were 0 and the tests failed; x86 timings of a few hundred nanoseconds would
say nothing about the GBA's cycles and would be noise. Guarded:
`BenchmarkStart` ends the test as an assumption failure natively, with the
reason. The hydra lists them under ASSUMPTIONS_FAILED and exits 0
(`gTestRunnerSkipIsFail` is 0, as in expansion outside rh-hideout's CI).
Tests: "BuildOamBuffer faster ..." (5, `test/sprite.c`), "Optimised
GetMonData", "Optimised SetMonData" (`test/pokemon.c`), "RandomUniform
mul-based faster than mod-based" (2, `test/random.c`).

### 9. Save struct sizes (3 tests)

`test/save.c` checks `sizeof` of the save blocks against the GBA's numbers;
natively structs follow the host ABI (docs/DECISIONS.md, "Native structs
follow the host ABI"), which does not round every struct to 4 bytes as
`-mabi=apcs-gnu` does: SaveBlock1 15496 (GBA 15568), SaveBlock2 3848
(3884), SaveBlock3 1 (4); PokemonStorage is 34144 on both. The test's own
comment says to update the values for a hack's layout; guarded, the native
build checks its own 32-bit sizes, so the test guards the native save
layout. Flagged for 64-bit.

### 10. Speed: the PPU in a headless run

The first full runs took 15 minutes, almost all of it drawing frames nobody
sees. `frame` no longer draws when headless without a screenshot; the
suite takes about 35 seconds on 32 cores (the GBA baseline about 2.5
minutes under mGBA).

## Runs

The GBA baseline was rerun for its per-test results (2 minutes 36 seconds,
the same totals as `docs/research/gba-baseline-1.17.1.md`), and each native
run compared with it test by test.

| Run | Total | Passed | Known failing | Expected failing | To-do | Failed | Exit | Notes |
|---|---|---|---|---|---|---|---|---|
| GBA baseline | 5862 | 5414 | 18 | 9 | 421 | 0 | 0 | |
| 1 | 298 | 89 | 0 | 3 | 2 | 204 | 0 | 31 of 32 processes died: crash loops (class 3, 4) |
| 3 | 5635 | 302 | 0 | 3 | 404 | 4926 | 1 | ROM mirror in; battles crash (class 3) |
| 4 | 4033 | 283 | 0 | 3 | 272 | 3475 | 1 | NULL music player (class 3) |
| 5 | 5633 | 5032 | 14 | 9 | 404 | 174 | 1 | runner 19 died of SIGABRT (class 6); 15 minutes (class 10) |
| 6 | 5862 | 5392 | 18 | 9 | 421 | 22 | 1 | |
| 7 | 5862 | 5405 | 18 | 9 | 421 | 0 | 0 | 9 assumption failures (class 8); one garbled test name (class 7) |
| 8 | 5862 | 5405 | 18 | 9 | 421 | 0 | 0 | 9 assumption failures; per test equal to the GBA baseline but for those 9 |
| 9 | 5862 | 5405 | 18 | 9 | 421 | 0 | 0 | as run 8, after the GLYPH_COPY and mega-indicator palette fixes |
| 10 | 5862 | 5310 | 18 | 9 | 421 | 95 | 1 | linked at 0x40000000 again, without the ROM mirror: class 4 reopened until work/replay's untagging is merged |
| 11 | 5862 | 5405 | 18 | 9 | 421 | 0 | 0 | after merging main (work/replay's script-tag untagging): class 4 closed; per test equal to the GBA baseline but for the 9 benchmarks; 44 s |
| SANITIZE=1 | 5862 | 5402 | 18 | 9 | 421 | 3 | 1 | the sanitized run below: 3 CRASH (fatal AddressSanitizer reports), 9 assumption failures; 78 s |

## The timeout, end to end

No test in the suite times out, so the Timer 2 path was checked with a
throwaway file (not committed), `TESTS=test/zz_timeout_probe.c`: a function
test and a battle test whose bodies loop on `VBlankIntrWait`, each followed
by a passing test. The function test reported `TIMEOUT` after 60 emulated
seconds; the battle body (run by every runner while it sizes the battle
tests, so 32 times) reported `TIMEOUT` from the battle runner's stack, and
the runners went on to pass the tests after them (3.5 s for the run).
`Intr_Timer2` calls `PlatformJumpToAgbMainLoop` from inside the interrupt
dispatch, which leaves the frame loop and the battle stack as the GBA's
rewritten IRQ return does.

## The sanitized run (SANITIZE=1)

`make -f Makefile.native -j32 check SANITIZE=1 [DINFO=1]` (DINFO=1 gives
file:line in the reports). The recipe runs the hydra with
`ASAN_OPTIONS=halt_on_error=0:abort_on_error=1` and
`UBSAN_OPTIONS=print_stacktrace=1`: the game's reports are recoverable and
the runner carries on; a report that stops (the platform library is built
without recovery) aborts, `crash_reboot` reboots the runner, and the hydra
counts that one test as CRASH instead of losing the rest of the runner's
partition (without `abort_on_error` three runners exited and 107 tests
never ran). AddressSanitizer's checks are outline calls in this build
(Makefile.native): inline, the TEST=1 code is about 40 MB and passes
0x42000000, which gba_regions.ld's ASSERT for platform/script_tag.h
forbids; outline it ends at 0x41BB12F8.

Result, against the GBA baseline: 5862 total, 5402 passed, 18 known
failing, 9 expected failing, 421 to-do, 9 assumption failures
(benchmarks, class 8), 3 failed, exit 1. The three are CRASH from the
palette over-reads in the first row below, which the platform's
`CpuFastSet` reports fatally: "Gimmick Form Change animations work",
"Move Animations work 2" (move 100 of 222), "Move Animations work 4"
(move 96 of 222). Every other test has its baseline result.

Every finding, by the site the sanitizer names (line numbers are
upstream's, as `#line` keeps them), with its root cause. None comes from
the platform: the two `platform/bios_memory.c` sites are the BIOS copy
reading what the game asked it to. "Same on GBA" means the GBA performs
the same out-of-bounds access and survives it; where the bytes it reads or
writes differ natively (another neighbour), it says so. Not fixed (game
code); none changes a test result except the three crashes.

| Site | Sanitizer | Root cause | GBA, and native difference |
|---|---|---|---|
| `src/graphics.c:35`, `:386`, `:398` (`gBattleAnimSpritePal_MegaParticles`, `_Feint`, `_WringOut`, 10, 5 and 7 colours) via `src/sprite.c` `LoadSpritePalette` -> `src/palette.c:58` `LoadPaletteFast` -> `CpuFastSet` | ASan global-buffer-overflow (fatal: the 3 CRASH) | palettes with fewer than 16 colours, always loaded as 32 bytes | reads the next ROM bytes into colours the sprite does not use; natively the next rodata. main's `sMegaIndicatorPal[16]` guard is the same fix |
| `src/data/graphics/pokemon.h` `gMonPalette_TatsugiriCurly` (15 colours), `_Okidogi`, `_Meowscarada` (14), `_Nacli` (12) via `src/battle_gfx_sfx_util.c:667` `BattleLoadMonSpriteGfx` -> `src/palette.c:49` `LoadPalette` -> `CpuSet` | ASan global-buffer-overflow | as above, `PLTT_SIZE_4BPP` for every species | as above |
| `src/battle_util.c:7895` `GetCriticalHitOdds`, from `src/battle_ai_util.c:821` `ShouldCalcCritDamage` | UBSan index 4294967295 of `u32[5]`; ASan | `CalcCritChanceStage` returns `CRITICAL_HIT_BLOCKED` (-1), which the AI passes as a table index (only `CRITICAL_HIT_ALWAYS` is checked) | reads the word before `sGen7CriticalHitOdds`; the AI computes crit damage if it is 1. Natively a different word |
| `src/battle_util.c:1004`, `:1010`, `:1011`, `:1013` (`HandleAction_NothingIsFainted`, `HandleAction_ActionFinished`), `src/battle_main.c:5052` (`RunTurnActionsFunctions`) | UBSan index 4 of `[4]`, index 12 of `SpecialStatus[4]`; ASan | after the turn's last action `gCurrentTurnActionNumber` is `gBattlersCount`, and `gActionsByTurnOrder`, `gBattlerByTurnOrder` (and `[+1]` at 1010) are read one past the end; 1010 then indexes `gSpecialStatuses` with the byte read | reads the next EWRAM variable: on GBA `gBattleTurnCounter`/`gLastUsedBallMenuPresent`, natively `gBattlerPositions`/`gActionsByTurnOrder` (the native EWRAM order differs). `afterYouActive` then only gates an empty loop. If `RunTurnActionsFunctions` runs again at that index with no battle outcome set, the byte read from `gActionsByTurnOrder[4]` selects the action function, which would differ natively; no test result differs |
| `src/battle_util.c:1844` `HandleFaintedMonActions`, `src/battle_script_commands.c:2173` `Cmd_getexp` | UBSan index 2 of `u8[2]` | `givenExpMons[GetBattlerTrainer(b) & BIT_FLANK]`: `BIT_FLANK` is 2, so the right-hand trainer gives index 2 (should be `!= 0` or `>> 1`) | reads and sets a bit of the next field, `expSentInMons`, on both (same struct layout): an exp bookkeeping bug in battles against two trainers |
| `src/battle_script_commands.c:1536` `Cmd_printfromtable` | ASan global-buffer-overflow | `RemoveAllTerrains` (`src/battle_script_commands.c:4920`) sets `MULTISTRING_CHOOSER` to `B_MSG_TERRAIN_COUNT` as a "failsafe" when there is no terrain, but `gTerrainStringIds` has exactly `B_MSG_TERRAIN_COUNT` entries | prints the string id in the next halfword of ROM; natively of rodata |
| `src/battle_move_resolution.c:1642` `CanPledgeComboAttackHappen` | UBSan index 2 of `u32[2]`; ASan | `newTurnOrder[i] != 0xFF && i < 2`: the element is read before the bound | reads the stack word after the array; the result does not depend on it |
| `src/battle_controller_recorded_player.c:373`, `_opponent.c:352`, `_partner.c:273` | UBSan index 128/129 of `Move[4]`; ASan in rodata | the recorded move slot carries `RET_GIMMICK` (0x80) and indexes `moveInfo->moves` unmasked, then `gMovesInfo[that halfword]` for the default target | reads stale controller buffer, then a move entry past the table: the default target of a recorded gimmick move can differ natively |
| `src/battle_interface.c:2106` `UpdateHealthboxAttribute` | UBSan index 101 of `u32[101]`; ASan | `gExperienceTables[rate][level + 1]` for a level 100 Pokémon | the next rate's level 0 (0), or for the last rate whatever follows the table in ROM (natively in rodata): the exp bar of a level 100 Pokémon |
| `src/pokemon.c:2609` `SetBoxMonData` (nickname), from `src/daycare.c:1058` `SetInitialEggData` | ASan global-buffer-overflow | `sJapaneseEggNickname` (`src/daycare.c:110`) is 4 bytes; `MON_DATA_NICKNAME` copies `POKEMON_NAME_LENGTH` | the bytes after the EOS come from the next ROM (natively rodata) bytes; not displayed |
| `src/sprite.c:1172` `JumpToTopOfAffineAnimLoop` | ASan global-buffer-overflow | the loop reads `[animCmdIndex - 1]` before testing `animCmdIndex == 0`; `gMetronomeFingerAffineAnimCmds2` (`src/battle_anim_effects_1.c:2234`) has `AFFINEANIMCMD_LOOP(2)` without a `LOOP(0)` start, so the search reaches index 0 | reads the command before the array; both outcomes restart at command 0, so the value does not matter |
| `test/test_runner_battle.c:2725` `OTName_` (test "Large exp gains are supported", `test/battle/exp.c`) | ASan stack-buffer-overflow | `SetMonData(..., MON_DATA_OT_NAME, &otName)` passes the address of the pointer, not the name | the OT name becomes the pointer's 4 bytes and 3 stack bytes on both; the test only needs a different OT name, so it passes either way |
| `src/battle_anim_flying.c:375`-`379` `AnimTask_AnimateGustTornadoPalette_Step` | UBSan index 4336 of `u16[512]`, load and store | `IndexOfSpritePaletteTag(ANIM_TAG_GUST)` is 0xFF (palette not loaded): `OBJ_PLTT_ID(0xFF)` is 4336 | writes 16 bytes 8.5 KiB past `gPlttBufferFaded`, inside `gSaveDataBuffer` on both (a scratch buffer used only while saving) |
| `src/palette.c:49`, `:50` `LoadPalette`, from `src/battle_anim_effects_2.c:2982` `AnimTask_LoadMusicNotesPals` | UBSan index 4336 of `u16[512]` | `IndexOfSpritePaletteTag(ANIM_TAG_MUSIC_NOTES_2)` or `AllocSpritePalette` gives 0xFF | writes 32 bytes 8.5 KiB past both palette buffers, inside `gSaveDataBuffer` on both |
| `src/battle_script_commands.c:9394` `BS_ItemCureStatus`, `src/pokemon.c:3905` `HealStatusConditions` | UBSan index 4 of `BattlePokemon[4]`, of `u16[4]` | a bag item cures a Pokémon that is not on the field: `targetBattler` stays `MAX_BATTLERS_COUNT` and is used as an index | clears the nightmare bit of `gBattleMons[4]`: a bit of `gDisplayedStringBattle` on GBA, of the last byte of `gBattleTextBuff3` natively; reads one past `gBattlerPartyIndexes` for a text buffer |
| `src/battle_anim_effects_1.c:6382`, `:6384` `AnimTask_MoonlightEndFade` | UBSan shift exponent 255 | `0x10000 << IndexOfSpritePaletteTag(...)` with 0xFF (palette not loaded) | ARM's shift by 255 gives 0; x86 shifts by 31, which for 0x10000 also gives 0: same result |
| `src/trig.c:517`, `:523` `Sin`/`Cos`, from `src/battle_anim_electric.c:789` `AnimSparkElectricityFlashing_Step`, `:726` `AnimZapCannonSpark_Step`, `src/battle_anim_ghost.c:1450` `AnimPoltergeistItem` | UBSan index -64, 320, 336 of `s16[320]` | the first step uses the angle argument before masking it with `& 0xFF`; the Poltergeist item reaches angle 256 before its stop test | reads next to `gSineTable` in ROM, natively in rodata: one frame's sprite offset can differ |
| `src/battle_ai_util.c:428` `GetHealthPercentage` | UBSan division by zero | `maxHP` 0 for an empty battler (switch-in candidate data) | libgcc returns; natively `arm_division` gives libgcc's result |
| `src/battle_script_commands.c:8159` `ComputeBallShakeOdds` | UBSan division by zero | odds 0 for a ball that cannot catch | as above |
| `src/battle_anim_electric.c:793` `AnimSparkElectricityFlashing_Step` | UBSan division by zero | `% data[4]`, where `data[4]` is `arg7 & 0x7FFF`, 0 in the scripts that pass 0x8000 or 0 | as above |
| `src/pokemon_animation.c:1243` `VerticalShakeTwice`, `:3674` `VerticalShakeLowTwice` | UBSan division by zero | the tables end with `{-1, 0}`, and the division by the second value comes before the test for -1 | as above; the result is discarded |
| `src/battle_anim_water.c:1579` `CreateWaterSpoutRainDroplet` | UBSan signed overflow | `ISO_RANDOMIZE2` multiplies an `int` by 1103515245 | wraps on both (GCC emits a plain multiply) |
| `src/battle_controllers.c:1837`, `:1838`, `:1840` `SetBattlerMonData` | UBSan misaligned member access | `moveData` points 3 bytes into the controller buffer, misaligned for `struct MovePPInfo` | only addresses are formed; `SetMonData` reads them bytewise (`SET16`): no misaligned load on either |

Gone since the run before the merge (fixed on main): the `if_comptime`
false positives (`include/metaprogram.h`: `*(int *)0` inside
`__builtin_constant_p` was instrumented and reported as a load of NULL at
every `RandomPercentage`/`RandomChance` site), `src/pokemon.c:6278`
(`gBattleStruct` NULL outside battle), `src/text.c` `GLYPH_COPY`'s shifts
by 32 and `sMegaIndicatorPal`.
