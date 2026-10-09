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

### 3. Reads and writes through NULL that the GBA serves from its BIOS

On the GBA, address 0 is the BIOS ROM: reads return BIOS bytes (or, outside
the BIOS, the last BIOS opcode fetched) and writes are ignored, so a NULL
pointer dereference goes unnoticed. Natively the first 64 KiB are unmapped
(`vm.mmap_min_addr`), and `MMAP_PAGE_ZERO` needs privileges (checked:
`setarch -Z` still faults), so each site gets a guarded fix that does what
the GBA ends up doing.

| Site | Tests | What the GBA does | Native fix |
|---|---|---|---|
| `CB2_TestRunner` INIT: `MoveSaveBlocks_ResetHeap` copies `*gSaveBlock1Ptr`, NULL at boot | all | copies BIOS bytes, which `ClearSaveBlocks` wipes before every test | `test/test_runner.c`: point `gSaveBlock1Ptr` at `gSaveblock1` first if NULL |
| `FunctionTest_SetUp`: `ClearRiggedRng` before `AllocZeroed` writes through the freed (NULL) `gFunctionTestRunnerState` | every `TEST` | writes ignored | `test/test_runner.c`: allocate first (the allocation is zeroed, which is what `ClearRiggedRng` writes) |
| `CB2_InitBattleInternal` → `BattleSetup_GetEnvironmentId` reads `gMapHeader.mapLayout` (NULL, no map) | every battle test | garbage environment, overwritten by `BATTLE_ENVIRONMENT_BUILDING` for recorded battles | `src/battle_main.c`: not called for recorded battles |
| `IsPokemonCryPlaying`, `m4aMPlayStop` with `gMPlay_PokemonCry` NULL (no cry played; headless) | ~3400 battle tests | `ident` is never `ID_NUMBER`, no cry is playing | `src/m4a.c`: NULL returns FALSE / returns |
| `PlayAnimation`: `*argPtr` with the macros' default `argPtr=NULL` | most battle tests | reads BIOS, value unused | `src/battle_script_commands.c`: reads a zero |

### 4. ROM mirror tags on script function pointers (every script)

Expansion marks script commands, specials and callnatives that "request
effects" by storing `function + ROM_SIZE` (0x2000000): on the GBA that is
the same code in the ROM's first mirror (0x0A000000), and
`(pointer & 0xE000000) == 0xA000000` recognises it. Natively those
addresses were unmapped (crash in `RunScriptCommand`). Fixed in the
platform: the executable is now linked at 0x08000000, the ROM's address
(Makefile.native and platform/Makefile, `-Ttext-segment`), and
`platform/rom_mirror` maps its read-only segments again 0x2000000 higher.
32-bit x86 non-PIC code runs unchanged from the copy (it addresses data
absolutely); the platform library is now compiled `-fno-pie` too. Side
effects of the move: `MemBlockLocation`'s `ROM_START |` encoding,
`SetSpeaker`'s ROM range check and the hydra's 7-digit `%p` symbolisation
all work as on the GBA. Limits: 64-bit (RIP-relative data access) cannot run
code from a mirror (`ROM_MIRROR_ERR_64_BIT`); the sanitized build's code
(about 40 MB) does not fit the 32 MB ROM window.

### 5. The battle runner's 27-bit address fields

`QueuedSubHitEvent.address` is 27 bits: it holds the address of a test
function's local (`SUB_HIT(..., captureDamage: &damage)`), which on the GBA
is in EWRAM. The native call stack was in the executable's `.bss`, above
2^27. Fixed in the platform: gba_regions.ld puts the 1 MiB call stack at
0x01800000 (GBATEK's unused 00004000-01FFFFFF), and transfer_bounds accepts
it as a stack.

### 6. Upstream bugs the GBA survives

| Site | Tests | GBA | Native fix |
|---|---|---|---|
| `src/daycare.c` `GiveParentSharedLevelUpMoves`: the inner loop increments `i`, not `j`, and scans past `sharedMoves` | daycare egg tests | scans memory (stack, IWRAM mirrors, at the latest the learnset in ROM) until a halfword equals `levelupLearnset[0].move`, then adds that move | does the same without the scan |
| `src/party_menu.c` `BufferBattlePartyOrderBySide`: with one Pokémon on a two-battler side (2v1, 1v2) `j` reaches 6 and writes `partyIndexes[6]` | 2v1 and 1v2 battles | one byte of stack padding | bounded; `-fstack-protector` caught it natively |

## Runs

| Run | Total | Passed | Known failing | Expected failing | To-do | Failed | Exit | Notes |
|---|---|---|---|---|---|---|---|---|
| GBA baseline | 5862 | 5414 | 18 | 9 | 421 | 0 | 0 | |
| 1 | 298 | 89 | 0 | 3 | 2 | 204 | 0 | 31 of 32 processes died: crash loops (class 3, 4) |
| 3 | 5635 | 302 | 0 | 3 | 404 | 4926 | 1 | ROM mirror in; battles crash (class 3) |
| 4 | 4033 | 283 | 0 | 3 | 272 | 3475 | 1 | |
