# Reference port: gradenGnostic/pokeemerald-multiplatform, commit 2f35335e

Research notes gathered 2026-10-08 at the pinned commit
`2f35335eff69ea1a08ebf19e64ea4d97ff6c0a05` (2026-07-14). Lineage: pret
master `208f97e1` (2025-08-21) merged into Kurausukun's `pc_port` branch
(started 2021-03, the original NTx86 lineage), plus seven commits by this
fork. The code was read, not built; no `.sav` was compared. Line numbers
refer to the pinned commit. Decisions drawn from this are in
`../RESEARCH.md`.

Link: https://github.com/gradenGnostic/pokeemerald-multiplatform/tree/2f35335eff69ea1a08ebf19e64ea4d97ff6c0a05

## Headline facts

- 32-bit only: data `.s` files emit 4-byte pointers, `VRAM` is `(u32)VRAM_`,
  DMA registers store pointers as `vu32`.
- Two threads: the game runs on its own thread; the SDL main thread renders
  each frame and runs every interrupt handler while the game thread blocks
  on a semaphore.
- I/O registers are one 1 KiB byte array with no write side effects.
- No emulation of timers, sound FIFOs, REG_IF/IME, or HBlank lines 160-227.
- Audio is a C float mixer pushed with `SDL_QueueAudio` at 42060 Hz.
- Save is a raw 128 KiB flash image.
- Bugs found by reading: WIN1 reads WIN0's registers; `CopyTrainerId`
  copies nothing when `src != NULL`; flash erase writes zeros; soft reset
  exits the process; `MathUtil_*32` lost its 64-bit intermediate (affects
  the GBA build too); `mostlyclean` expands to `rm -f /*.bin`.

## Layout and build

`src/platform/`: `nostd.c` (40, `NO_STD_LIB_ENABLED`), `dma.c` (109),
`cgb_audio.c` (257), `bios.c` (544), `win32.c` (798, `PLATFORM_WIN32`),
`gba_easy_draw.c` (940, `RENDERER_EASY_DRAW`), `sdl2.c` (1200,
`PLATFORM_SDL2`), `gba_fast_draw.c` (2722, `RENDERER_FAST_DRAW`). Also
`src/music_player.c` (837), `src/sound_mixer.c` (488), `src/stub.c` (227),
`src/agb_flash_dummy.c` (64), `include/cgb_tables.h` (2349), an Android
project with an SDL2 submodule.

Build entry: `Makefile_pc` (370 lines); the GBA `Makefile` is unchanged
from pret (and would pick up the unguarded `src/platform/bios.c`, so the
GBA build is probably broken; CI only builds GBA). Windows by default
(`i686-w64-mingw32-`), `make -f Makefile_pc linux` recurses with
`NATIVE_LINUX=1`. CFLAGS: `-m32 -std=gnu99 -fno-dce -fno-builtin
-DPORTABLE -DNONMATCHING -DUBFIX -DMODERN=1 -O3`; Linux `-fno-pie`/`-no-pie`.
Pipeline: `cpp | preproc | cc1 or gcc -S | as --32`. `src/*.s` are collected
but never linked, so the ARM m4a assembly is not built. `data/*.s` and
songs are assembled for x86 after `sed 's/\.4byte/\.int/;s/\.2byte/\.short/'`
and the asm macro files were rewritten for x86 GAS (`@` comments to
`/* */`, `%progbits` dropped). No per-file special flags. `ld_script_pc.ld`
is referenced but does not exist.

## Entry point and threads

`main()` (`sdl2.c:90`): SDL init, read save and config, 1280x720 window,
vsync renderer, border textures, 240x160 ARGB8888 streaming texture,
semaphore, audio device, one initial `VDraw`, then
`SDL_CreateThread(DoMain, "AgbMain")` where `DoMain` calls `AgbMain()`. RTC
struct initialised after the thread starts (race).

Handshake: `VBlankIntrWait()` does `SDL_AtomicSet(&isFrameAvailable, 1);
SDL_SemWait(vBlankSemaphore)` (`sdl2.c:1097-1101`); `WaitForVBlank` calls
it under PORTABLE; `IsDma3ManagerBusyWithBgCopy` also calls it so polling
loops yield (`bg.c:444-446`). Main loop: fixed-timestep accumulator at
60.0 Hz with `dt = (1/60)/timeScale`, clamped when `deltaTime > 5*dt`;
while a frame is available: `VDraw`, compose border, set
`REG_DISPSTAT |= INTR_FLAG_VBLANK`, `RunDMAs(DMA_HBLANK)`, call
`gIntrTable[4]` if the DISPSTAT VBlank IRQ bit is set, clear, `SDL_SemPost`.
Busy-spins with no sleep while waiting for the game thread. No `SDL_Delay`.
`DrawFrame` iterates lines 0-159 setting `REG_VCOUNT`, firing VCount and
HBlank per line; lines 160-227 never simulated; `VDraw` leaves
`REG_VCOUNT = 161` so `SetGpuReg` always writes immediately. Shutdown does
not join the game thread. The Win32 backend mirrors this with
`CreateThread` and an event.

## Interrupts and BIOS

REG_IE/IF/IME are plain bytes; desktop dispatch ignores IME and IE and
checks DISPSTAT enable bits; Android checks REG_IE for VBlank and VCount.
Handler indices hard-coded: `[0]` VCount, `[3]` HBlank, `[4]` VBlank.
Serial, timer, DMA, keypad, cartridge interrupts never raised. `IntrMain`
stubbed and still copied as 0x800 bytes of data. BIOS calls are C
("based on the VBA-M source code", ArcTan "taken from mGBA's source"):
CpuSet, CpuFastSet, LZ77UnCompVram/Wram, RLUnCompWram/Vram, BgAffineSet,
ObjAffineSet, ArcTan, ArcTan2, Sqrt, `MPlayJumpTableCopy` (replaces
`swi 0x2A`), RegisterRamReset (printing stub), MultiBoot (stub). Div
declared, not implemented. `SoftReset` is `puts(...); exit(0)`, so the
A+B+Start+Select combo and Ctrl+R terminate the program. `syscall.h`
disables the MODERN alignment-assert wrappers for CpuSet under PORTABLE.

## Video

Per-scanline renderer once per frame on the main thread into a BGR555
buffer, converted 5-to-8 bits to ARGB8888, `SDL_UpdateTexture`. Modes 0 and
1 only. Text BGs 4/8bpp with flips, 512-pixel maps, mosaic; in 8bpp colour
index 0 is drawn opaque (bug). Affine BG sign-extends the 28-bit reference,
computes `X + line*PB` from the register with no latched internal
reference, wrap and overflow handled, mosaic approximated. OBJ: all 128
entries scanned per line 127 to 0, affine and double-size, 4/8bpp, flips,
mosaic, semi-transparent, OBJ window; 2D mapping only warns; no cycle
limit. WIN0/WIN1/OBJWIN/WINOUT masks per line; WIN1 uses WIN0's registers
(bug, both renderers). Alpha, brighten, darken; second target searched in
lower layers. `gba_fast_draw.c` is an alternative with specialised paths.

Memory (`bios.c:7-11`): `REG_BASE[0x400]`, `PLTT[0x400]`, `VRAM_[0x18000]`,
`OAM[0x400]`, `FLASH_BASE[131072]`, all `unsigned char` arrays aligned 4.
`#define VRAM (u32)VRAM_`. EWRAM and IWRAM not modelled. `io_reg.h:4-8`:
`#ifndef PORTABLE #define REG_BASE 0x4000000 #else extern unsigned char
REG_BASE[]; #endif`, so `REG_DISPCNT` resolves to
`*(volatile u16*)&REG_BASE[0]`. The port added `DMA_DEST_MASK` and
`DMA_SRC_MASK`.

## Audio

`m4a_1.s` not linked; replaced by `music_player.c` (player),
`sound_mixer.c` (float mixer, BDPCM decode) and `cgb_audio.c` (PSG synth
with `cgb_tables.h`), added 2021 by Kurausukun, MCboy and froggestspirit
with no upstream project named. `m4a.c`: `m4aSoundMain` calls
`RunMixerFrame`; `SOUND_MODE_FREQ_42048` used unguarded; 701 samples per
vblank, `pcmFreq = 60*701`; `m4a_internal.h` float fields and
`PCM_DMA_BUF_SIZE 4907` unguarded; `SoundInfo` reinterpreted as
`SoundMixerState` by cast. Flow: VBlankIntr runs `m4aSoundMain` (mixer into
a 7-frame ring, then `cgb_audio_generate`); the VCount IRQ at line 150
calls `m4aSoundVSync`, which sums buffers, replaces NaN with 0, applies gain
0.125 and clamps, then `Platform_QueueAudio`. FIFO A/B, timers 0/1, DMA1/2
not emulated. SDL: `SDL_OpenAudioDevice` 42060 Hz `AUDIO_F32` stereo 1024
samples, `SDL_QueueAudio`, no queue-depth control, no mutexes anywhere.
Fast-forward pauses the device and clears the queue. Win32 backend has no
audio.

## Input, timers, RTC, DMA, save

Input: `ReadKeys` calls `Platform_GetKeyInput()`; hard-coded keyboard
(A=Z, B=X, Start=Return, Select=Backslash, L=A, R=S, arrows), Space
fast-forward, Ctrl+P pause, Ctrl+R soft reset (exits). Windows polls
`XInputGetState(0)` from the game thread. `keyboardKeys` is a plain `u16`
written on the main thread and read on the game thread (data race).

Timers: none. `SeedRngAndSetTrainerId` reads a never-written `REG_TM1CNT_L`
and seeds 0; `InitPlayerTrainerId` changed to `Random()<<16|Random()`.
RTC: `siirtc.c` replaced at API level with `Platform_*`; Get functions BCD
encode `localtime()` each call; setters effectively no-ops; `SiiRtcReset`
under PORTABLE is a TODO that never clears `sLocked`, so after it runs all
RTC calls return FALSE (reachable via `SiiRtcProbe` and `RtcReset`).

DMA (`dma.c`): four shadow channels; `DmaSet` stores SAD/DAD as `vu32`
(pointer truncation), runs `RunDMAs(DMA_NOW)`; `DmaCopy*`/`DmaFill*` macros
become CPU copies; `RunDMAs` honours `DmaStop`, copies element by element,
dest inc/dec/fixed, reload has a TODO; HBLANK runs per scanline and once
more at VBlank; VBLANK and SPECIAL never trigger.

Save: `pokeemerald.sav` in the working directory (Android: pref path);
`ReadFlashId` returns 0xCCCC selecting DUMMY (32 x 4096); `save.c` calls
`ProgramFlashSector_DUMMY` directly; reads go through a fresh `fopen` of the
`.sav` on disk while writes `fwrite` the whole 128 KiB through a long-lived
`r+b` handle with no fflush or fsync; `StoreSaveFile` at shutdown is
commented out. Save structs given `ALIGNED(4)` and `ObjectEvent.range`
bitfields changed to `u8` for layout compatibility, with no static assert
or test. `EraseFlashSector_DUMMY` erases to zero.

## Changes against pret

Guards: `PORTABLE`, `UBFIX`, `PLATFORM_SDL2`, `PLATFORM_WIN32`,
`RENDERER_EASY_DRAW`, `RENDERER_FAST_DRAW`, `NATIVE_LINUX`,
`NO_STD_LIB_ENABLED`, `__ANDROID__`, `_WIN32`. Diff vs pret base: 890 files,
+18004/-5149; 567 with content changes (asm 20, data 228, sound 189, src 78,
include 19), the asm/data/sound ones being x86 GAS syntax conversions.
Headers: `gba/defines.h`, `io_reg.h`, `macro.h`, `syscall.h`, `types.h`,
`flash_internal.h`, `m4a_internal.h`, `global.h`, `global.berry.h`,
`global.fieldmap.h`, `strings.h`. Largest C changes: `option_menu.c`
(+289, display page), `m4a.c`, `siirtc.c`, `battle_script_commands.c`,
`main.c`, `overworld.c`, `agb_flash.c`, `save.c`, `load_save.c`,
`new_game.c`. Unguarded changes that alter the GBA build: `math_util.c`
`s64` to `u32`, `pokemon_animation.c` `SAFE_DIV` order, the m4a frequency
and struct changes. Other PORTABLE patches: use-after-free reorder in
`battle_main.c`, `copyvar` uses `VarGet`, script HALT became `continue`,
null checks plus an `InitOverworldBgs` hack in `overworld.c`, title screen
no longer returns to intro, wireless adapter reports disconnected.

## Platforms and 64-bit

Windows i686 MinGW (SDL2 or GDI), Linux native 32-bit, Android armeabi-v7a.
No macOS, Emscripten, 3DS or Switch code. 64-bit unsupported (README):
4-byte data pointers, `VRAM` as `u32`, DMA `vu32`, `sound_mixer.c` pointer
arithmetic through `uintptr_t`, `bios.c` `(u32)dest`, task function
pointers split into two u16 in `battle_factory_screen.c`, a pointer/u32
union in `music_player.h`.

## License

`LICENSE` is MIT, "Copyright (c) 2026 pokeemerald-multiplatform
contributors", scoped to the "Port Modifications" only and explicitly
excluding the pret decompilation, Nintendo/Creatures/GAME FREAK IP and
third-party software. This is the precedent we follow for our own license.

## Code quality

Non-static generically named globals (`paused`, `isRunning`, `speedUp`,
`gb`, `sampleRate`); `static` function declared in a shared header;
`gIntrTable` redeclared const; magic numbers (border geometry, 42060, 701,
4907, `REG_VCOUNT = 161`, IRQ indices); strict-aliasing violations reading
char arrays as `u16*`, `OamData*`, `vBgCnt*` with no
`-fno-strict-aliasing`; pointer-to-u32 casts; out-of-object pointer
arithmetic; arithmetic on `void*`; non-void functions missing a return;
copying the code bytes of `IntrMain`; data races on input, `timeScale`,
settings and `gSaveBlock2Ptr`; SDL video calls from the game thread;
`localtime` not thread-safe; `SDL_CreateSemaphore`, `SDL_CreateThread`,
`SDL_UpdateTexture`, `fwrite` unchecked.
