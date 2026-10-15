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
layout. At 64 bits SaveBlock1 is 15768 and the guarded test holds that size
(`docs/UPSTREAM_MODIFICATIONS.md`, `test/save.c`); the 32-bit layout of a save
is read by `save_convert`.

### 10. Speed: the PPU in a headless run

The first full runs took 15 minutes, almost all of it drawing frames nobody
sees. `frame` no longer draws in `--test-runner` mode (never with a replay
or a screenshot, whose images need every frame); the
suite takes about 40 seconds on 32 cores (the GBA baseline about 2.5 to
3 minutes under mGBA).

### 11. Captures that reach THEN (7 tests): 2 corrected natively, 5 native KNOWN_FAILING (M8)

A battle test's function is called for GIVEN, WHEN and SCENE, and again for
THEN after the battle. `HP_BAR(..., captureDamage: &damage)` and the other
captures hand the runner the address of a variable of the first call, and
the battle wrote the value there after that call had returned; THEN's call
read the variable without setting it. Undefined twice over (a write through
a pointer whose object's lifetime ended, a read of an uninitialised
variable): GCC's code happens to read the value back from the same stack
slot, clang -O2's does not (Beat Up's two tests read 0, which a clang flag,
`-disable-licm-promotion`, only hid). Natively the runner now keeps the
value and runs SCENE again at the start of THEN's call, which writes it
into that call's variable (`test/test_runner_battle.c`, "A capture";
`docs/UPSTREAM_MODIFICATIONS.md`), so THEN reads what the battle captured,
with GCC and with clang alike.

The replay's contract (stated at SCENE in `include/test/battle.h` and at
"A capture"): natively, when SCENE captured into a variable of the test
function, SCENE's whole body runs a second time, in THEN's call, after the
battle. Its queue calls must queue the same events as the first time (a
different count, type, line or group, or a capture that now names another
place, ends the test with ERROR before THEN). Every other statement in it
runs again too, against the state after the battle: what it reads from
GIVEN's recorded party (`PLAYER_PARTY`, `OPPONENT_PARTY`) is unchanged,
what it reads from `gParties`, `gBattleMons` or any other game state is the
battle's, and a variable that only GIVEN's or WHEN's body sets (they do not
run in THEN's call) has the value THEN's call gives it. In 1.17.1's SCENE
blocks, besides the queue calls, are control flow, declarations such as
`s32 maxHP = GetMonData(&PLAYER_PARTY[0], MON_DATA_MAX_HP)`, and five
statements that read state the battle changes: Substitute's and Shed
Tail's `maxHP = GetMonData(&gParties[B_TRAINER_PLAYER][0], MON_DATA_HP)`
(below), Heal Bell's two checks of `gParties` and Thrash's check of
`opponent`, whose SCENEs capture nothing and so run once (a scan of every
SCENE block for game state: `gParties`, `gBattle...`, the battler
pointers, flags, variables, the RNG).

On the GBA a capture into a variable with an initialiser never reaches
THEN: THEN's call runs the initialiser again over the slot the battle wrote.
Seven tests pass there only because of it, their THEN comparing the
initial values (zeros); natively the captures arrive.

Two of them read the maximum HP at a moment it is not there. Substitute's
and Shed Tail's SCENE set `maxHP` from `gParties`' current HP. SCENE's
first run is in the first call, before `StartBattle` copies GIVEN's party
into `gParties` (`SetVariablesForRecordedBattle`), where the party is the
one the previous battle's teardown zeroed: it reads 0 (measured, a build
that kept the first run's value: 0 HP, 0 maximum HP). The replay reads it
after the battle: the HP less the cost (368 for 490). With the captured
cost reaching THEN, either value fails the test (`EXPECT_EQ(0, 122)` with
the first run's, `EXPECT_EQ(92, 122)` with the replay's): the replay does
not cause the failure, a capture that arrives does, and any harness in
which captures reach THEN (upstream's own, fixed) fails them alike. They
are corrected natively, so that they check the cost and pass as on the GBA:
`maxHP` comes from GIVEN's party's maximum HP (`PLAYER_PARTY[0]`,
`MON_DATA_MAX_HP`, the form fifty other SCENE blocks use), the same in both
runs; `#if PLATFORM_NATIVE` / `#else` / `#endif` with `#line`, the GBA's
text unchanged. Both PASS natively, the cost (122 of 490, 245 of 490)
compared with the maximum HP; the capture replay is what delivers it, so
removing the replay's write fails them.

The other five are KNOWN_FAILING natively (a guarded `KNOWN_FAILING;` in
each), so the native suite's results differ from the GBA baseline in these
5 as in the 9 benchmarks. Each was re-derived from the test and the values
the replay delivers; the values are the battle's, the tests' expectations
are wrong:

| Test | File | Native failure | Why |
|---|---|---|---|
| "Shell Bell recovers 1/8 of HP from after the last hit from all hits of a multi hit move" | `test/battle/hold_effect/shell_bell.c` | `EXPECT_EQ(0, 6)` | SCENE sums the hits' captured damage before the battle has captured any (`totalDamage` stays 0, in the first run and in the replay, whose sum runs before CloseScene writes the captures); the recovery does arrive |
| "Shell Bell does not activate on Future Sight if the original user is on the field" (the second of two tests of that name, line 175) | `shell_bell.c` | `EXPECT_MUL_EQ(40, 0.75, -5)` | the captured heal is -5 for 40 damage, the item's 1/8; the test expects another fraction |
| "Lash Out damage is only boosted on the turn that Intimidate switches in" (parameter 1 of 2) | `test/battle/move_effect/lash_out.c` | `EXPECT_EQ(54, 36)` | 54 then 36 is Intimidate's attack drop, which the test does not allow for |
| "Lash Out damage is boosted on turn 1 by switch in abilities" (2 of 4, Intimidate) | `lash_out.c` | `EXPECT_MUL_EQ(36, 1.33, 36)` | the same attack drop |
| "Lash Out damage is boosted by Cotton Down activation in doubles" (2 of 2) | `lash_out.c` | `EXPECT_MUL_EQ(150, 2.0, 76)` | 150 then 76 is the boost on turn 1; the check's operands are the wrong way round |

An upstream report would say: a capture into a variable with an initialiser
is lost on the GBA, so these tests check nothing; once captures reach THEN,
five fail on their expectations and two (Substitute's and Shed Tail's cost)
on a SCENE that reads `gParties` before the battle has copied the party in,
whose correction is the maximum HP of GIVEN's party (drafted here, not
posted; upstream reports go out only with the owner's approval).

Not a capture, and left as it is: "Dynamax: Dynamax expires after three
turns and correctly converts HP according to Dynamax Level" reads
`capturedDamage` for its Dynamax parameters, where SCENE queues no capture
into it (`EXPECT_GE(finalHP, 200 - capturedDamage)`), so it reads what the
first parameter's call left in that stack slot, on the GBA and natively.
Found by filling the battle runner's stack with 0xA5 before every THEN call
(a diagnostic build, not committed): that test failed ("2/12",
`EXPECT_GE(190, 23331)`), and no other result changed, so no other test
depends on a stale stack slot, the captures included.

## Clang's function and pointer-overflow checks (M8)

The sanitized build made with clang (the macOS build's compiler, rehearsed
on Linux) checks two things GCC's UndefinedBehaviorSanitizer does not: a
call through a pointer of another function type (`function`), and
arithmetic on a null pointer (`pointer-overflow`, which GCC's check of the
same name leaves out for a zero offset). Review ran the opening replay of
that build without `-fno-sanitize=function,pointer-overflow`, which had hidden
both for every expansion object, and found 8 sites in expansion's code; the
debug menu replay adds a ninth (`src/debug.c`). Each, by the site the
sanitizer names (the line numbers are upstream's):

| Site | Check | Root cause | Native fix |
|---|---|---|---|
| `src/decompress.c:516` `SwitchToArmCallLOtANS` | function | `DecodeLOtANS`'s native branch passed `DecodeLOtANSLoop(const u32 *, u32 *, u8 *, u8 *)` as `(void *)` to the helper, which calls it as `(…, void *, void *)` | the native branch calls `DecodeLOtANSLoop` directly; the helper is under `#if !PLATFORM_NATIVE` |
| `src/decompress.c:596` `SwitchToArmCallDecodeSymtANS` | function | the same function as `(…, u16 *, u16 *)` | the same, its `u16` buffer passed as the `u8 *` the loop takes |
| `src/decompress.c:932` `SwitchToArmCallDecodeInstructions` | function | `DecodeInstructions(u32, const u8 *, const u16 *, u16 *)` as `(…, void *)` | the same |
| `src/event_object_movement.c:6817` `UpdateObjectEventCurrentMovement` | function | the player's callback, `ObjectEventCB2_NoMovement2`, a `u8 (void)` cast to `bool8 (*)(struct ObjectEvent *, struct Sprite *)` (`src/field_player_avatar.c:352`); called every frame | natively it has the callback's type (`src/field_player_avatar.c`) |
| `src/event_object_movement.c:7004` `InitMovementNormal` | function | `sDirectionAnimFuncsBySpeed`, `u8 (*)(enum Direction)`, copied into an array of `u8 (*)(u8)` and called through it | natively the copy has the table's type |
| `src/scrcmd.c:146` `ScrCmd_special` | function | `gSpecials` (data/specials.inc, assembled) holds specials of every return type, called as `u16 (*)(void)`; here `InitSecretBaseDecorationSprites`, a `void (void)`, whose result is discarded | none possible without a type per special, which the table does not have: `scrcmd.o` is built without the `function` check (`Makefile.native`) |
| `src/field_effect.c:897` `FieldEffectScript_CallNative` | function | every field effect native (data/field_effect_scripts.s) called as `u32 (*)(void)`; 43 of 93 return `u8` or `bool8`, and on x86-64 the upper bits of EAX after such a return are not defined (on the GBA the callee extends them), so the stored value could hold garbage. Today it does not where it matters (Ash Puff's sprite id, stored in an `s16`: GCC's `CreateSpriteAtEnd` returns a value it has compared in full against 63) | natively only the low byte is kept, all any native returns (sprite ids, task ids, bools); the call's type still differs, so `field_effect.o` is built without the `function` check |
| `src/debug.c:1665` `DebugTask_HandleMenuInput_General` | function | the debug menu's options hold their action as `const void *` and call one that takes a parameter as `void (*)(u8, const void *)`; the actions declare their own pointer type (here `DebugAction_OpenSubMenuFlagsVars(u8, const struct DebugMenuOption *)`; others a flag, a script), a dozen in one field | none short of giving every action the table's type: `debug.o` is built without the `function` check |
| `src/save.c:702` `UpdateSaveAddresses` | pointer-overflow | the save blocks' pointers are NULL the first time it runs at boot, and it adds each sector's offset to them | natively the addresses are computed as integers, the same values |

The sanitized suite built by clang (the same checks; `docs/BUILDING.md`, "The
clang rehearsal on Linux") reports these sites that the GCC-built sanitized
suite does not, besides the ones above:

| Site | Check | Root cause | Native fix |
|---|---|---|---|
| `test/test_runner_battle.c:112`, `:118` (native lines) `CallSingleTestFunction`, `CallDoubleTestFunction` | function | every battle test's function takes its own `struct ResultN *` first; the test table holds it as a `SingleBattleTestFunction` or `DoubleBattleTestFunction`, whose first parameter is `void *` (`include/test/battle.h` casts it there), and the GBA's Thumb routines call it with no type | these two native functions, the platform's replacement for the Thumb routines, are `no_sanitize("function")` under clang (`CALLS_TEST_FUNCTION`), and nothing else is |
| `src/ow_abilities.c:118` `GetSynchronizedNature` | function | `sSynchronizeModes` (and `sCuteCharmModes`, not reached) hold `bool32 (enum Species)` functions as `const bool32 (*)(enum Species)`: a const-qualified return type, another function type to clang (and its `-Wincompatible-function-pointer-types`) | natively both tables have the functions' type |
| `test/test_test_runner.c:33` "Tests resume after CRASH" | function | the test calls a null function pointer on purpose (it is `KNOWN_CRASHING`); the check reports the call before the crash | none: the crash is the test |
| `test/test_runner_battle.c:728`, `:790` (`battleRecordTurns[...][...]`), `:1591` (`expectedAiScores[...][...][...]`) | bounds (index) | the runner takes the address of a turn's record past `MAX_TURNS` (16) when a battle runs longer; clang checks the index of an address it takes, GCC only of a load | not fixed: upstream's runner, the address is not read; present before this work too |

No blanket exclusion remains: every other expansion object keeps both
checks, on Linux and on macOS.

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
| SANITIZE=1 | 5862 | 5402 | 18 | 9 | 421 | 3 | 1 | the sanitized run below, before the palettes were padded: 3 CRASH (fatal AddressSanitizer reports), 9 assumption failures; 78 s |
| SANITIZE=1 | 5862 | 5405 | 18 | 9 | 421 | 0 | 0 | eight battle-animation palettes padded; per test the GBA baseline but for the 9 benchmarks; 76 s |
| M8 review, 64 bits (GCC, and clang plain and sanitized: the same, test by test) | 5884 | 5417 | 28 (25 KNOWN_FAILING, 3 expected ERROR) | 9 | 421 | 0 | 0 | the capture replay (class 11): 7 tests KNOWN_FAILING natively; 9 native tests of the replay (`test/capture_replay.c`), 3 of which expect ERROR and are counted with the known failures; the 13 native-only tests of earlier milestones; 9 assumption failures (class 8) |

## Notes from review

- `src/battle_script_commands.c` `Cmd_jumpifarrayequal`: the guard covers
  the default configuration only. `BattleScript_DynamaxBegins_SwitchIn`
  (data/battle_scripts_1.s) says `jumpifbyteequal B_SHOW_DYNAMAX_MESSAGE,
  FALSE`, which passes the two constants as array addresses. With the
  default FALSE both are NULL and the guard jumps without reading. With
  `B_SHOW_DYNAMAX_MESSAGE` TRUE they are 1 and 0: the GBA reads both from
  its BIOS (and, finding them equal, never shows the message), the native
  build faults (crash_reboot reports it). The upstream bug is
  `jumpifbyteequal` with two constants; an assembly-time `.if` is the fix.
- Persistent state: natively the 28-bit `address` field of
  `gPersistentTestRunnerState` holds `PersistentTestId(test)`, the test's
  index in `.tests` plus one (0 still means none), since a native test
  address does not fit it.
- `Intr_Timer2`'s timeout jump (`PlatformJumpToAgbMainLoop`) leaves
  `interrupt_dispatch` without its restore of IE and IME. Harmless: IME was
  1 when it was saved (Timer 2 is taken only with IME set) and the handler
  leaves IE alone (commented at the jump site and in game_hooks.h).
- `arm_division` reports every divide error it passes on (one line with
  the program counter) and refuses 66h/67h-prefixed forms. Since
  milestone 7 it decodes x86-64 too (arm_division.h).

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

Result, against the GBA baseline: 5862 total, 5405 passed, 18 known
failing, 9 expected failing, 421 to-do, 9 assumption failures
(benchmarks, class 8), 0 failed, exit 0: per test the GBA baseline but
for the benchmarks. Before the eight battle-animation palettes in the
first row were padded, the platform's `CpuFastSet` reported their
over-reads fatally and three tests were CRASH ("Gimmick Form Change
animations work", "Move Animations work 2" and "4"); padding the first
three (MegaParticles, Feint, WringOut) uncovered the next two (MegaStone,
TrumpCard) in the same tests, so every battle-animation palette shorter
than 16 colours is padded.

Every finding, by the site the sanitizer names (line numbers are
upstream's, as `#line` keeps them), with its root cause. None comes from
the platform: the two `platform/bios_memory.c` sites are the BIOS copy
reading what the game asked it to. "Same on GBA" means the GBA performs
the same out-of-bounds access and survives it; where the bytes it reads or
writes differ natively (another neighbour), it says so. Not fixed (game
code) except the fatal palette over-reads; none changes a test result.
The list is incomplete: the 32-bit run keeps one AddressSanitizer report
per access size per runner, and the three sites it hid or this table
missed are in the table of "The sanitized run at 64 bits" below.

| Site | Sanitizer | Root cause | GBA, and native difference |
|---|---|---|---|
| `src/graphics.c:32`, `:35`, `:38`, `:386`, `:389`, `:392`, `:398`, `:442` (`gBattleAnimSpritePal_MegaStone` 8 colours, `_MegaParticles` 10, `_MegaSymbol` 10, `_Feint` 5, `_TrumpCard` 4, `_TrumpCardParticles` 4, `_WringOut` 7, `_SmallCloud` 5) via `src/sprite.c` `LoadSpritePalette` -> `src/palette.c:58` `LoadPaletteFast` -> `CpuFastSet` | ASan global-buffer-overflow (fatal) | palettes with fewer than 16 colours, always loaded as 32 bytes | reads the next ROM bytes into colours the sprite does not use; natively the next rodata. Fixed: each is declared `[16]` (zero-padded) under the guard, as main's `sMegaIndicatorPal` |
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

## The sanitized run at 64 bits

`make -f Makefile.native -j32 check BITS=64 SANITIZE=1 DINFO=1` on main
at 902eaf796b, compared with the same run at `BITS=32`, report by report
and test by test. The runners share the hydra's stderr, which interleaves
their reports line by line (UBSan stack frames such as the recorded
controllers' `HandleChooseMove`, `HandleAction_ActionFinished`,
`DoAttackCanceler` and `SetBallThrowShakes` can land inside another
runner's AddressSanitizer report); so for this comparison a copy of
`native-rom-test` (not committed) sent each runner's stdout and stderr,
in order, to a file of its own, and each report is attributed to the test
whose `:N` line precedes it.

Result: per test the 32-bit sanitized results except `SaveBlock1 is
backwards compatible` (gate h): 5866 total, 5408 passed, 18 known
failing, 9 expected failing, 421 to-do, 9 assumption failures, 1 failed.
UBSan's reports are the same at both widths, test by test (347 reports
at 32 sites, all in the 32-bit table but `src/battle_anim_effects_3.c:2790`
below). **No AddressSanitizer report is new at 64 bits.**

The 32-bit run lists fewer AddressSanitizer reports (25 at 5 sites,
against 56 at 9) for a reason of its own. With the outline checks, the
32-bit libasan reports its own `__asan_loadN_noabort` as the program
counter (frame #0 is `asan_rtl.cpp:185` or `:186`), the same for every
report of a size, and `suppress_equal_pcs` (on by default) drops every
later report with that program counter: each runner keeps the first
1-byte and the first 2-byte report of its partition. At 64 bits the
program counter is the game's and each site is reported once per runner.
Run with `ASAN_OPTIONS=suppress_equal_pcs=0`, both widths report every
access, 56930 reports at 32 bits and 56921 at 64, and the two lists are
identical test by test but for the last row (9 reports at 32 bits only).
Compare widths that way.

| Site (first game frame) | Report | Tests | At 32 bits | Root cause |
|---|---|---|---|---|
| `src/pokemon.c:2609` `SetBoxMonData` (the nickname loop), from `src/daycare.c:1058` `SetInitialEggData` | global-buffer-overflow, 1-byte reads 0 to 5 bytes after `sJapaneseEggNickname` | the 11 egg tests of `test/daycare.c` ("(Daycare) ...", "InheritPokeball ...") and their variants | same report | the 32-bit row (a 4-byte name copied as 12) |
| `src/pokemon.c:2613`, `:2615` (`data[10]`, `data[11]`), the same call | 6 and 7 bytes after `sJapaneseEggNickname` | the same | same reports, hidden by the suppression | the same root cause, two more lines of the copy |
| `src/battle_script_commands.c:1536` `Cmd_printfromtable` | 2-byte read just past `gTerrainStringIds` | "Flower Gift reverts Cherrim back after Teraform Zero clears weather", "Forecast reverts Castform back after Teraform Zero clears weather", "Teraform Zero doesn't reactivate when Terapagos-Stellar switches back in" | same report | the 32-bit row |
| `src/sprite.c:1172` `JumpToTopOfAffineAnimLoop` | 2-byte read of `[-1]` of `gMetronomeFingerAffineAnimCmds2`, `gTeaAffineAnimCmds2`, `sAffineAnim_QuestionMark` | "Front anims work", "Gimmick Form Change animations work", "Move Animations work 1" to "4", "Tera Blast animations work", "Z-Moves animations work" | same report | the 32-bit row; the Tea and question-mark tables also loop with no `LOOP(0)` start |
| `src/trig.c:523` `Cos`, from `src/battle_anim_electric.c:726` `AnimZapCannonSpark_Step` | 2-byte read of `gSineTable[320]` (UBSan's `[336]` lands inside the next object) | "Move Animations work 1", "3" | UBSan at this line in both runs; this report hidden by the suppression | the 32-bit `Sin`/`Cos` row |
| `src/battle_anim_effects_3.c:2790` `AnimTask_DoomDesireLightBeam` | 1-byte read of `gDoomDesireLightBeamCoordTable[4]`; UBSan index 4 of `s8[4]` at the same line | "Move Animations work 1", "Z-Moves animations work" | same reports (ASan and UBSan); missing from the 32-bit table | upstream, both widths: state 1 runs five times (`data[2]` 0 to 4, as `gDoomDesireLightBeamDelayTable` has five entries) over a four-entry table. The GBA reads the delay table's first byte, 0 (0x083FC72C). **Fixed (M7):** the native build now reads the GBA's value, 0, at every width (it was 0x01 in the plain 32-bit build: `gBattle_BG1_X` one pixel off for one frame); the sanitizer report goes away. See `docs/UPSTREAM_MODIFICATIONS.md` |
| `src/battle_anim_psychic.c:1214` `AnimTask_ExtrasensoryDistortion_Step` | 2-byte read of `gSineTable[320]` | "Move Animations work 2", "4" | same report, hidden by the suppression (UBSan cannot see it: `include/trig.h` declares `gSineTable[]` without a size) | upstream, both widths: stage 1 starts at sine index 192 and adds 2 per scanline over the 65 scanlines `yOffset - 32` to `yOffset + 32`, so the last reads index 320, one past the table. The GBA reads 0x041E (0x08DE3B78, the low half of a code address in a jump table) and shifts that scanline 32 to 35 pixels for the effect's 24 frames. **Fixed (M7):** the native build now reads the GBA's 0x041E (it was 0 when sanitized, 0x7018 plain 32-bit, 0x9AF0 at 64 bits); the sanitizer report goes away. `Cos`'s read of the same index 320 (Zap Cannon, `trig.c:523`) is fixed the same way, so UBSan's "index 320" report there is gone too; its "index 336" (and Sin's -64) reports remain, unchanged. See `docs/UPSTREAM_MODIFICATIONS.md` |
| `src/sprite.c:1076` `JumpToTopOfAnimLoop` | 2-byte read of `[-1]` of `gEclipsingOrbAnimCmds`, `gKinesisZapEnergyAnimCmds` | "Move Animations work 1", "2", "3" | same report, hidden by the suppression | upstream, both widths: the affine row's bug in the non-affine twin (reads `[animCmdIndex - 1]` before testing `animCmdIndex == 0`; both tables end with `ANIMCMD_LOOP(1)` and have no start). Either way `animCmdIndex` becomes 255 and `ContinueAnim` restarts at command 0, so the value (on the GBA 0xCDA0 and 0xC45C, the low half of the animation table's pointer) does not matter. Not fixed |
| `src/pokemon.c:2901` `SetBoxMonData` (OT name), from `test/test_runner_battle.c:2725` `OTName_` | stack-buffer-overflow, 32 bits only | "Large exp gains are supported" | 32 bits only | the 32-bit row: `MON_DATA_OT_NAME` copies 7 bytes from `&otName`, a 4-byte pointer at 32 bits and an 8-byte one at 64, so at 64 bits the read stays inside it. The OT name is the pointer's bytes at both widths; the test only needs it to differ |

No report involves a host stack address cut to 32 bits (inventory risk
R1). The same run with ASLR off (`setarch x86_64 -R`) and
`ASAN_OPTIONS=detect_stack_use_after_return=1`, which moves the game's
address-taken locals into fake frames above 4 GiB (UBSan's
`CanPledgeComboAttackHappen` reports show them at 0x7ffff3e1df28 and
nearby), gave the same per-test results and the same reports, and its
only `crash_reboot` was "Tests resume after CRASH" (address 0, on
purpose).
