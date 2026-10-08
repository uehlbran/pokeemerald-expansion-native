# Reference port: fuddlesworth/pokeemerald-native, branch `vanilla`

Research notes gathered 2026-10-08 from commit `a5589b1` (2026-09-27), a
shallow clone. Originally NTx86's SDL2 port of pret/pokeemerald; now a
single-threaded port with a deterministic headless mode, 64-bit by default,
GBA-compatible saves. Line numbers refer to that commit. Decisions drawn from
this are in `../RESEARCH.md`.

Link: https://github.com/fuddlesworth/pokeemerald-native/tree/vanilla

## Layout and build

Platform files are wrapped in `#ifdef PORTABLE` so the GBA build compiles
them to nothing. `src/platform/`: `sdl2.c` (844: main, frame loop, SDL video
and audio, save file, RTC glue, CLI, headless test mode), `gba_easy_draw.c`
(909: scanline PPU), `bios.c` (536: SWI reimplementations), `settings.c`
(420: ini and bindings), `cgb_audio.c` (364: PSG channels and resampler),
`system.c` (164: fake I/O and VRAM arrays, VBlank dispatch, RegisterRamReset
and SoftReset via setjmp/longjmp), `dma.c` (119), `test_state.c` (60).
PC-only files outside it: `src/music_player.c` (872, m4a sequencer in C),
`src/sound_mixer.c` (482, m4a PCM mixer in C), `src/stub.c` (226: link,
multiboot, RFU stubs), `src/agb_flash_dummy.c` (65), `tools/elf2macho`.

One shared `Makefile`: `make linux|winwsl|macos` set `PORTABLE=1`;
`IS64BIT ?= 1`; output `pokeemerald64` or `pokeemerald32`. Flags:
`-D NONMATCHING -D PORTABLE -D UBFIX [-D VER_64BIT] -m64|-m32 -std=gnu99
-fno-common -fno-builtin`, `-Werror=implicit-function-declaration,
incompatible-pointer-types, int-conversion, pointer-to-int-cast,
int-to-pointer-cast`, `-ffp-contract=off` (deterministic hashes), `-O3`.
Linux 64 is PIE with data `.rodata` renamed to `.data.rel.ro`; Linux 32 is
`-no-pie`. SDL2 via `-lSDL2main -lSDL2 -lm`. No linker script on PC.

Pipeline: `cpp | preproc | cc1 | as`, then `objcopy --rename-section
.bss=gba_ram` for every non-platform object (clang: force-included
`#pragma clang section bss="gba_ram"`). PC excludes `src/*.s` (crt0.s,
m4a_1.s, libgcnmultiboot.s, rom_header.s). `data/*.s` and MIDI songs are
assembled by host GNU as with `--defsym PORTABLE=1`; `.4byte`/`.2byte` are
sed-converted to `.int`/`.short`; pointers in data use `ptrvalue` (`.quad`
on 64-bit) and `ptr_align` from `asm/macros/bit_width.inc` (25 data and
macro files, plus `tools/mapjson` and `tools/mid2agb` emit them).

## Entry point and loop

`main()` (`sdl2.c:83-254`) parses args, loads settings, reads the save,
inits SDL (window, `SDL_RENDERER_PRESENTVSYNC`, 240x160 `ABGR1555` streaming
texture, audio), then calls `AgbMain()` once. pret's `AgbMain` has its
`for(;;) MainLoop();` compiled out under PORTABLE (`main.c:184-189`), so it
only initialises. The host loop calls `RunMainLoop()` (`system.c:136-155`),
which `setjmp`s for soft reset and calls `MainLoop()` once per game frame.

Single-threaded. Leftover `SDL_Thread`, semaphore and `DoMain()` are dead.
`VBlankIntrWait()` is empty; `WaitForVBlank` calls it under PORTABLE. After
`MainLoop` returns, the loop renders the frame then calls
`RunDMAsAndVBlank()`, which invokes the VBlank handler directly.

Fixed-timestep accumulator at exactly 60 Hz (not 59.7275); audio rate is
`60 * 701 = 42060` Hz to match, with `gPcmSamplesPerVBlankTable` 704 changed
to 701. No `SDL_Delay`; throttled only by vsync. Accumulator uncapped. Only
the first game step per outer iteration is rendered, so fast-forward skips
HBlank and VCount interrupts and HBlank DMAs. `REG_VCOUNT` is 161 while
logic runs and 0..159 during `DrawFrame`; lines 160-227 are never
simulated, so `SetGpuReg` always writes immediately (`gpu_regs.c:73-77`)
instead of deferring during VDraw as on hardware.

## Interrupts and BIOS

`gIntrTable[]` entries are called directly and synchronously: VCount before
a line if DISPSTAT matches, HBlank after each of 160 lines, VBlank in
`RunDMAsAndVBlank` if `DISPSTAT_VBLANK_INTR`. `REG_IE/IF/IME` are dead
bytes. Timer, serial, DMA, keypad and gamepak interrupts never fire.
`IntrMain` is an empty function stub declared `extern u32 IntrMain[]`;
`InitIntrHandlers` still copies 2 KiB of it as data. Sound moved out of the
interrupt handlers into `AudioUpdate()` per frame.
`IsDma3ManagerBusyWithBgCopy` on PC runs the whole VBlank handler and
returns FALSE (`bg.c:447-450`, 183 call sites), which calls `Random()` and
the vblank callback extra times: RNG and timing diverge from the GBA.

BIOS calls are C reimplementations ("based on the VBA-M source code",
ArcTan/ArcTan2/Sqrt "taken from mGBA's source"): CpuSet, CpuFastSet,
LZ77UnCompVram/Wram, RLUnCompWram/Vram (source pointer advances in 4-byte
units: bug), BgAffineSet, ObjAffineSet, ArcTan, ArcTan2, Sqrt,
RegisterRamReset, SoftReset (RegisterRamReset + longjmp), VBlankIntrWait
(no-op), MultiBoot (stub). Not implemented: Div, DivArm, Mod, HuffUnComp,
BitUnPack, Diff*UnFilter, MidiKey2Freq, Sound*, IntrWait, Halt (no game
callers found). `DmaCopy*/DmaFill*` macros map to CpuSet-style immediate
copies (`include/gba/macro.h:106-113,160-166`).

## Video

Per-scanline renderer run once per frame after logic (`gba_easy_draw.c`):
for each of 160 lines fire VCount, fill backdrop, `DrawScanline`, fire
HBlank DMA and IRQ. Registers re-read each line so HBlank effects work.
Modes 0 and 1 only (game uses only those); modes 2-5 print "unsupported".
Text BG 4/8bpp, flips, 4 map sizes, mosaic; affine BG with 28-bit
sign-extended reference, recomputed from the register each line (not
latched); OBJ regular, affine, double-size, 4/8bpp, mosaic,
semi-transparent, 1D mapping only; OBJ window; WIN0; WIN1 reads WIN0's
registers (bug); alpha, brighten, darken with no clamping to 16; priority
sort. No forced blank, no OBJ cycle limit, no VRAM index masking (OBJ tile
reads can run past `VRAM_`). OAM read through `struct OamData` bitfields.

Memory: static arrays in `system.c`: `REG_BASE[0x400]`, `PLTT[0x400]`,
`VRAM_[0x18000]`, `OAM[0x400]`, `FLASH_BASE[131072]`. `REG_BASE` is an
`unsigned char[]`, so every `REG_X` stays pret's `(*(vu16 *)(REG_BASE +
off))` with no side effects. EWRAM and IWRAM are not modeled:
`EWRAM_DATA`/`IWRAM_DATA` are empty; game globals are ordinary C globals
whose `.bss` is renamed to `gba_ram` so soft reset can zero it.

## Audio

ARM `m4a_1.s` is not built. `music_player.c` reimplements the sequencer,
`sound_mixer.c` the mixer (float, linear-interpolating resampler, reverse
and BDPCM-compressed cries), pret's `m4a.c` kept with PORTABLE edits.
`STATIC_ASSERT`s pin m4a and mixer struct aliases to the same layout.
`TrackOrder` reproduces the GBA's track memory order for channel-steal ties.
CGB channels synthesised at 65536 Hz from cycle timers with a 512 Hz frame
sequencer, resampled with a 64-tap 512-phase windowed sinc; own Taylor sine
because libm is banned for determinism. FIFO, DMA1/2 and timer 0 are not
emulated. SDL side: push model, `SDL_OpenAudio` 42060 Hz `AUDIO_F32` stereo
1024 samples, `SDL_QueueAudio` per frame; drops frames above 8 queued,
queues 3 frames of silence on underrun. No callback, no locking.

## Input, timers, RTC, DMA, save

Input: `ReadKeys` calls `Platform_GetKeyInput()` under PORTABLE
(`main.c:271-277`); `SDL_GetKeyboardState` plus up to four game controllers,
polled once per frame. Defaults: A=Z, B=X, Start=Return, Select=Backspace,
L=A, R=S, arrows, Space fast-forward.

Timers: not emulated; trainer ID from `Random()` instead of `REG_TM1CNT_L`.
RTC: `siirtc.c` replaced at the API level; `Platform_Get*` read host
`time()`; setters are effectively no-ops; test mode uses a fixed base time
plus frame/60.

DMA: macros are immediate copies; direct `DmaSet` calls go through
`dma.c` which stores pointers in C structs (32-bit registers cannot hold
64-bit pointers), writes only the control word, and runs NOW immediately
and HBLANK after each line; VBLANK and SPECIAL never trigger; count 0 is not
treated as max.

Save: `pokeemerald.sav` in the SDL pref path, 128 KiB flat flash image with
the GBA sector layout; `ReadFlashId` returns 0xCCCC selecting a DUMMY
driver (32 x 4096-byte sectors, memcpy/memset, no bank switch). Reads go
through a second FILE handle on disk while writes go to RAM and are flushed
only from `HandleSavingData`, `TrySavingData` and the clear-save screen: link
and record-mixing saves and `TryWriteSpecialSaveSector` are never flushed.
No fflush, no temp file, no rename. `EraseFlashSector_DUMMY` erases to 0x00
(`u8 clearBuffer[0x1000] = { 0xFF }` sets one byte). On 64-bit `SaveBlock1`
keeps the GBA layout via an `sSaveBlock1Saved` conversion (pointers become
4-byte zeros), so GBA, 32-bit and 64-bit saves interchange.

## Upstream modifications

Guard `PORTABLE` (plus `VER_64BIT`, `UBFIX`, `NONMATCHING`). Shared files
touched: `include/gba/{defines,flash_internal,io_reg,m4a_internal,macro,
syscall}.h`, `include/{global,m4a,task}.h`, `sound/music_player_table.inc`,
and about 45 `src/*.c` files (agb_flash*, battle_main, battle_transition,
bg, load_save, m4a, m4a_tables, main, malloc, multiboot, new_game,
naming_screen, save, siirtc, start_menu, title_screen, link*, librfu*, many
UI files for use-after-free and NULL guards). Behaviour changes: title no
longer returns to intro when BGM ends; evolution scene does not wait for
cry; link probe returns FALSE.

## 64-bit

Default target. Needed: `ptrvalue`/`ptr_align` in assembled data,
`T1/T2_READ_PTR` via `memcpy` (`global.h:131-168`), padding fields in m4a
structs (`voice_size` 0x18 vs 0xC), `Clear64byte` replaced by `sizeof`
fills, SaveBlock1 conversion layer, 8-byte alignment for heap, task data
and save blocks, `-Werror=pointer-to-int-cast` plus PIE, macOS
`-no_fixup_chains` because data holds about 28000 unaligned pointers.

## License

No LICENSE file and no README statement. Code credits VBA-M (GPL-2.0+),
mGBA (MPL-2.0) and NanoBoyAdvance (GPL-3.0) as sources. Copying the
platform files would bring GPL or MPL obligations with unclear provenance.

## Worth reusing as ideas

Soft reset by zeroing a dedicated `gba_ram` section and `longjmp`ing to the
loop; deterministic headless mode with per-frame FNV hashes, fixed clock,
PPM screenshots and JSON state; static asserts pinning aliased struct
layouts; `TrackOrder`; band-limited CGB synthesis; GBA-compatible saves
across bit widths; a build-flags stamp file.

## Defects found by reading

WIN1 uses WIN0 registers; `RLUnComp*` source stride; `EraseFlashSector_DUMMY`
erases to zero; save read/write split with silent failure; fast-forward
skips HBlank and VCount; VBlank DMA never triggered; no forced blank; 60 Hz
not 59.7275; `CPURead*/CPUWrite*` type-pun arbitrary pointers at `-O3`
without `-fno-strict-aliasing`; `ArcTan2` shifts negative values
(`shift-base` suppressed in the sanitizer build); `gIntrTable` redeclared
const in two files but defined non-const; a `static` function declared in a
public header; offsets stored in an `s8 *`; duplicate
`MIXED_AUDIO_BUFFER_SIZE`; `ReadSaveFile` dereferences NULL if both fopen
calls fail.
