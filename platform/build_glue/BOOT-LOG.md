# Boot log: getting the native executable to link and run

Every link failure, crash and hang met while bringing up
`build/native32/pokeemerald-native` on 2026-10-08 (expansion 1.17.1, Linux
x86, gcc 13.3, binutils 2.42), with its root cause and what fixed it. Newest
at the bottom. Gate command:

```
make -f Makefile.native -j$(nproc) native BITS=32 [SANITIZE=1]
./build/native32/pokeemerald-native --headless --silent --unthrottled \
    --frames 600 --screenshot boot.ppm --save t.sav --rtc-offset t.rtc
```

## Link

1. **`src/siirtc.c`: `platform/rtc.h: No such file or directory`** (compile,
   not link). The repository root was not on the quote include path, which
   the guarded `#include "platform/..."` lines need. Fixed in
   `Makefile.native` (`-iquote .`, and `-I .` for scaninc); main had the same
   fix by the time it merged.
2. **`undefined reference to LZ77UnCompWRAMOptimized_end`** (the only
   unresolved symbol once the platform library existed). `src/decompress.c`
   copies the ARM routine `LZ77UnCompWRAMOptimized` into a stack buffer,
   using the end symbol for its length, and runs the copy; it also copies four
   of its own functions the same way. Natively code cannot run from a data
   buffer and the copied lengths are meaningless. Fixed by a guarded change
   that passes each function itself (`docs/UPSTREAM_MODIFICATIONS.md`); the
   C `LZ77UnCompWRAMOptimized` is in `platform/decompress_asm.c`.

## Run

3. **CpuSet and DMA refused destinations at 0x41cf....** (stderr floods,
   then a hang, see 4). `platform/gba_game.ld` inserted `game_bss` after
   `.bss`, which put COMMON_DATA past `_end`, outside the image range
   `transfer_bounds` allows; and the default script's `.bss` claimed the
   game's `.bss` first. Fixed: `game_data` and `game_bss` are inserted
   *before* `.data` and `.bss`, so their patterns win and they lie inside the
   image. (`transfer_bounds` now also accepts `game_data`, which lies just
   below `__data_start`.)
4. **Hang in `AgbRFU_SoftReset`** (`InitRFU` from `AgbMain`, gdb
   backtrace). `src/librfu_stwi.c:69` busy-waits until a hardware timer
   counts past 0x11; natively the timers advance only at frame boundaries,
   inside `VBlankIntrWait`, so game logic polling one never sees it move.
   `src/librfu_sio32id.c:46` (`AgbRFU_checkID`, called right after) has the
   same loop. Fixed by guarded changes that wait through
   `PlatformWaitCycles` (which returns: game logic takes no emulated time).
   Two more such loops exist and are unreachable without a wireless adapter
   (`src/librfu_rfu.c:345`, only after an adapter answered;
   `src/librfu_intr.c:319,323`, in the serial interrupt, never raised).
5. **Endless soft resets** (gdb samples: `RegisterRamReset` from
   `restart_after_soft_reset`, `m4aSoundInit`, again and again). The I/O page
   starts zeroed, and KEYINPUT reads 0 = every button held until the frame
   loop first writes it; `AgbMainLoop`'s first `ReadKeys` saw
   A+B+Start+Select and called `DoSoftReset`, every time. Fixed:
   `frame_power_on()` writes KEYINPUT (and puts the LCD in V-Blank at line
   161) before each entry into `AgbMain`, as the hardware shows the buttons
   all along.
6. **600 frames, exit 0, 0.6 s.** Screenshot: the Game Freak logo scene of
   the intro. 3000 frames: the clouds-and-sun scene; the m4a mix is not
   silent (peak 1855 at frame 700).

Prevented before the first run, from reading the code: `src/m4a.c`
`SampleFreqSet` waits for VCOUNT to leave and reach 159 (never, as VCOUNT
reads 161 during game logic) and `MusicPlayerJumpTableCopy` holds a
`swi 0x2A` that the x86 assembler rejects at -O0: both guarded. VCOUNT is
161, not 160, during game logic because `src/gpu_regs.c` `SetGpuReg` writes
through only for VCOUNT 161..225; at 160 every register write would land
one frame late.

## Unit tests

7. **Every test binary failed to link: "EWRAM_DATA (.sbss) is larger than
   the 256 KiB of EWRAM".** With no EWRAM_INIT data, `.gba_ewram_init` is
   discarded and `.gba_ewram`, which had no address, followed the previous
   section elsewhere. Fixed: `.gba_ewram` is placed at
   `gba_linked_ewram_init_end`.
8. **`test_transfer_bounds`: a test's `.data` was refused for writes.** The
   `*/test/*.o` pattern in `gba_game.ld` also matched the platform's test
   objects. Fixed: the patterns name Makefile.native's object directories
   (`*native*/src/*.o`, ...); `transfer_bounds` uses `game_data_start` only
   when the section is non-empty.
9. **`test_game_hooks`: a battle test function's local did not keep its
   value between two calls.** Before and after the entry, `run_entry` calls
   AddressSanitizer's fiber functions, whose frames go below its own, where
   the entry's frame is: they overwrote the locals. Fixed: the entry runs
   below a 64 KiB gap (`GAME_HOOKS_CALL_STACK_GAP`). The battle runner's
   1 KiB `DATA.stack` is not used natively (x86 frames, larger under ASan,
   would overflow it into the runner's state); every call starts at the top
   of one 1 MiB platform stack, so frames land at the same addresses.

## TEST=1

10. **`pokeemerald-native-test` exits with status 2 after one frame.**
    Expected for now: `CB2_TestRunner` checks the mGBA debug registers
    (`MgbaOpen_`), which nothing answers natively until the debug channel
    module exists, and exits with 2 through `PlatformTestRunnerExit`, which
    unwinds to main as designed. The tests table is packed: 6294 entries of
    20 bytes, `test_test_runner.o`'s first.

11. **The sanitized TEST=1 executable crashed before `main`** (SEGV in an
    ASan module constructor, `_sub_I_00099_1` of `test/bag.o`). Its `call
    __asan_init` had been assembled as the event script command `call`
    (the byte 04h and a word): `include/test/overworld_script.h` gives the
    tests' inline assembly the event macros through one top-level
    `.include`, and GNU as looks macros up before instructions, so every
    `call` (and `nop`, `lock`, `inc`) gcc emitted after it in a test object
    became script bytes. The unsanitized TEST=1 build had the same broken
    code and only got past it by exiting at frame 1. Fixed:
    `platform/build_glue/overworld_macros.py` asks the host assembler which
    macro names are instructions, purges those at the end of the top-level
    include and puts them in two files that each `OVERWORLD_SCRIPT` block
    includes around its script (`Makefile.native`; the header's native
    branch). `test/bag.o` now has 110 `call` instructions instead of 2.

## Soft reset

12. Exercised under gdb by holding A+B+Start+Select from frame 300
    (`input_script_set(15)`): `DoSoftReset` -> `SoftReset` -> `game_hooks_unwind` ->
    `restart_after_soft_reset` (RegisterRamReset(RESET_ALL), game variables
    zeroed and re-initialised by `crt0_restart`, interrupt/timer/dma/ppu/psg
    restarted) -> `AgbMain` again; the run reached 1200 frames and exited 0,
    plain and sanitized (no report beyond finding A below, once per boot).

## Sanitizer findings (SANITIZE=1, 32-bit)

Run with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` (expansion's objects
recover; the platform library does not). No leak, no report in platform
code except where the platform is the one doing the game's copy (C).

- **A. UBSan object-size, `src/save.c:505:75`, `CopySaveSlotData`**: "load
  of address ... with insufficient space for an object of type 'u16'". With
  an erased flash chip (a new save) the sector id read at line 502 is
  0xFFFF (line 501), and line 505 reads `locations[id].size` before the
  signature check at line 508, far past the 14-entry `locations` array. On the GBA it reads
  ROM. Expansion's code: validate `id < NUM_SECTORS_PER_SLOT` (or the
  signature) before indexing. Not fixed.
- **B. ASan global-buffer-overflow, READ of 4, `DecodeLOtANSLoop`
  (`src/decompress.c:429`, the `currBits = *data++` of `LOOP_BITADVANCE`)**,
  twice in the intro's third scene: one word past the end of the
  smol-compressed `gIntroRayquaza_Gfx` (1080 bytes) and `gIntroLightning_Gfx`
  (360 bytes) (`src/data/graphics/intro_scene.h:34`, `:31`). The tANS loop
  fetches the next word before it knows it needs it. Harmless on the GBA
  (ROM), an over-read of a global natively. Expansion's code (the decoder
  runs as on the GBA: the guarded change only stops it being copied).
  Not fixed.
- **C. ASan global-buffer-overflow, READ of 32, in the BIOS module's
  `CpuFastSet` (`platform/bios_memory.c:94`)** from `LoadPaletteFast`
  (`src/palette.c:54`) <- `DoLoadSpritePalette` (`src/sprite.c:1698`) <-
  `LoadSpritePalette` <-
  `CreateQuickstartHud` (`src/quickstart.c`), at the title screen.
  `gQuickstartHudPal` (`src/quickstart.c:27`) holds 6 colours (12 bytes) and
  `LoadSpritePalette` always loads `PLTT_SIZE_4BPP` (32 bytes): 20 bytes past
  the array are read, and written into the palette buffer. Expansion's code:
  pad the palette to 16 colours. Because the platform library is not built
  to recover, this report ends the sanitized run at the title screen.
- Not seen because `-fno-sanitize=shift-base` is on for expansion's objects
  (DECISIONS: signed shifts into the sign bit): `src/m4a.c` `TrkVolPitSet`
  shifts a negative `keyShift` left (reported by the m4a author).

The ASan runtime prints "ASan doesn't fully support makecontext/swapcontext
functions and may produce false positives" once when the battle stack is
first used (unit tests, and the test build later); the switches carry the
fiber annotations, and no report has followed.

## 64-bit

`make -f Makefile.native native BITS=64` stops at two expansion objects
whose layout assertions fail with 8-byte pointers: `src/list_menu.c:18`
(`ListMenuTooLargeForTaskData`: `struct ListMenu` no longer fits a task's
`data`) and `src/recorded_battle.c:34` (`RecordedBattleSaveFreeSpace`: the
recorded battle no longer fits its save sector). Every other object
compiles, and a trial link without those two fails only on the symbols they
define (no relocation or layout error). 64-bit milestone work, not looked
at further.

## Replay bring-up (milestone 3): playing the opening

Everything that stopped `tests/smoke/opening.replay` (or that the sanitized
run reported along it), with its root cause. The opening now runs from boot
through the first battle, a save, a cold restart and Continue, plain and
sanitized, with the same dumps and screenshots and an empty stderr.

13. **SEGV in `Task_HandleMonAnimation` during Birch's speech** (the Lotad's
    animation). `src/pokemon_animation.c` reads
    `gBattleStruct->battlerKOAnimsRunning` after every animation; outside a
    battle `gBattleStruct` is NULL and the GBA read the BIOS region at 0x1E8
    (open bus) instead of faulting. Guarded NULL check.
14. **SEGV in `SpriteCB_Cursor`, then `VBlankCB_NamingScreen`, when the name
    is confirmed.** `MainState_Exit` frees `sNamingScreen` from inside
    `RunTasks`; the same frame's `AnimateSprites` (the sprite callbacks) and
    the next V-Blank callback still read it, through NULL, which the GBA also
    read from the BIOS region. Two guarded early returns.
15. **SEGV at the first script command (`NewGameInitData`).** Expansion lists
    a script command, native or special that is instrumented for effect
    analysis as its function's address plus `ROM_SIZE` (0x02000000), which on
    the GBA is the ROM's wait-state mirror and so also callable
    (`platform/script_tag.h`). Natively nothing is mapped there. The tag is
    now kept as bit 25 and removed where the pointers are called (guarded
    changes in `src/script.c`, `src/scrcmd.c`, `include/script.h`). A second
    mapping of the code at +0x02000000 was tried and dropped: it works only for
    code without program-counter-relative data access, which is the 32-bit build.
16. **Hang in `DoMapLoadLoop`** (`CB2_NewGame`). `FreeTempTileDataBuffersIfPossible`
    waits for the DMA3 manager, whose queue the V-Blank interrupt drains; the
    loop never waits for a frame, and natively time passes only in
    `VBlankIntrWait`. Guarded: the loop waits a frame per iteration.
17. **Text drawn with a ghost glyph** ("ye e", "So,, ,"). `GLYPH_COPY`
    (`src/text.c`) shifts by `32 - shift0`, which is 32 for a glyph that
    starts on a tile boundary: 0 on ARM, but x86 masks the count to 5 bits, so
    the glyph was also copied into the next tile. The same file's
    `1 << (width * 4)` is 32 for a full-width glyph. Guarded.
18. **Sanitizer findings along the opening** (all benign on the GBA):
    `BuildDoorTiles` (`src/field_door.c`) reads 8 palette entries at offsets
    4, 8 and 12 of 8-entry tables (reads of the next object; tile 0, so
    invisible); `sMegaIndicatorPal` (`src/data/graphics/gimmicks.h`) has 15
    colours and is loaded as 16 (the same pattern as the quickstart palette
    before); `GetBattlerPartyStateByPokemon` (`src/pokemon.c`) takes the address of
    a member of NULL `gBattleStruct` outside a battle; and `if_comptime`
    (`include/metaprogram.h`) puts `*(int *)0` in the arm of a `?:` that UBSan
    checks when the compiler has not folded it (a false report for every
    `RandomPercentage` with a run-time argument).
19. **Keys lost.** Not a bug: a press in the frame a screen becomes ready is
    ignored by the game (the naming screen's input is enabled the frame after
    its fade ends; a map's arrival script locks the controls a few frames
    after the map is up; a turn on the spot swallows an A). The script settles
    with a `wait N` at those points, commented in `opening.replay`.
20. **The save file's bytes differ between two runs** (dumps and screenshots
    do not). Nine bytes of the first party Pokemon in SaveBlock1 (the last
    byte of the nickname, and encrypted fields with the checksum that
    follows) hold uninitialised stack contents that depend on the stack's
    address, which ASLR changes; `setarch -R` makes two runs byte-identical.
    The values are never read by the game. Not fixed: it needs the
    expansion's mon creation to initialise its locals; matters only if the save's
    bytes are ever compared (the cross-width gate (h) compares facts).
