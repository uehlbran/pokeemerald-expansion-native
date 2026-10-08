# Reference port: hirata-producoes/pokemon-regionalidades (expansion 1.16.3 + SDL2)

Research notes gathered 2026-10-08 from the squashed commit `aa120be1`
(2026-10-07). This is the only project that merged a PC platform layer into
pokeemerald-expansion. Its platform layer was copied from
gradenGnostic/pokeemerald-multiplatform at commit 2f35335e (`bios.c`,
`dma.c`, `nostd.c`, `gba_easy_draw.c` near-verbatim; `cgb_audio.c` with
volume scaling; `sdl2.c` extended from 1200 to 2721 lines, most of it
project UI; `gba_fast_draw.c` dropped). The Windows-only `Makefile_pc`
hard-codes a Git-for-Windows shell. Line numbers refer to that commit.
Decisions drawn from this are in `../RESEARCH.md`.

Link: https://github.com/hirata-producoes/pokemon-regionalidades
Technical report (Portuguese): `docs/pokemon_go_world/RELATORIO_TECNICO_PORTE_PC.md`

## Layout and build

`src/platform/` (7,917 lines): `sdl2.c` 2721, `gba_easy_draw.c` 941,
`developer_map_camera.c` 816 (project), `win32.c` 803 (not built),
`pc_save_container.c` 587, `bios.c` 565, `resource_pack.c` 415,
`cgb_audio.c` 278, `save_file.c` 244, `dma.c` 110, `stubs.c` 69,
`nostd.c` 41, three `pc_*_state.c` project files. Audio engine outside
it: `src/music_player.c` 879 and `src/sound_mixer.c` 503 (whole-file
`#ifdef PORTABLE`), `src/agb_flash_dummy.c` 60.

`Makefile_pc` (782 lines): `PORTABLE := 1`; the main Makefile forwards
`make pc` and filters out `src/platform/%` for the GBA build (the one
precedent for keeping the GBA target green). Host gcc with `-m32
-mno-ms-bitfields -fno-dce -fno-builtin -fleading-underscore
-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -std=gnu17 -Wall
-Wno-strict-aliasing`, no `-Werror`. `C_ASM_SRCS` empty (excludes crt0.s,
m4a_1.s, libgcnmultiboot.s, decompress_asm.s, rom_header.s). `data/*.s`,
songs and map data go through `preproc | cpp | preproc -ie |
ASM_PSEUDO_OP_CONV | as --32` then `objcopy --prefix-symbol _`;
`ASM_PSEUDO_OP_CONV` is a sed that strips `@` comments, drops `.size`,
rewrites `.4byte`/`.word` to `.int` and `.2byte` to `.short`, and rewrites
the `script_data` section flags. Pointers in data stay 4 bytes. Per-file
flags: `librfu_intr.o` and `agb_flash.o` `-fno-toplevel-reorder`,
`pokedex_plus_hgss.o` `-Wno-enum-conversion`, `graphics.o`
`-Wno-missing-braces`. C pipeline `cpp | preproc | cc1 | as` as on GBA;
`graphics_file_rules.mk` and `json_data_rules.mk` byte-identical to
upstream. No linker script; linker-script symbols replaced in C:
`gNumMusicPlayers`/`gMaxLines` constants, `gInitialMainCB2` set in
`main.c`, `__iwram_end` and `__rom_end` code excluded. MIDI objects built
but not linked (songs come from their resource pack).

## Entry point and loop

Two threads like multiplatform: `DoMain` thread runs `AgbMain`; SDL main
thread renders and runs every interrupt handler while the game thread is
parked in `SDL_SemWait` inside `VBlankIntrWait`. VBlank gated on `REG_IE`
(multiplatform used DISPSTAT); VCount and HBlank gated on DISPSTAT; timer,
serial, DMA, keypad never raised. Fixed 60 Hz timestep, stall clamp, inner
loop busy-spins with no yield. No `SDL_WaitThread`, no semaphore destroy,
no `SDL_CloseAudioDevice`, no renderer destroy at shutdown. `SoftReset`
exits the process. Busy-waits on interrupt-driven state never progress, so
they added `ProcessDma3Requests()` inside `IsDma3ManagerBusyWithBgCopy`
(`bg.c:515-520`) and `menu.c`, removed `while (REG_VCOUNT == 159)` spins in
`SampleFreqSet`, and leave `REG_VCOUNT = 161` so `SetGpuReg` writes
directly. `IntrMain` is a dummy `u32 IntrMain[1]`.

## BIOS, video, audio, DMA

BIOS in `bios.c` ("based on the VBA-M source code"; ArcTan "taken from
mGBA's source"): Div (0 on divide by zero), CpuSet (records last transfer
for the crash log; returns on NULL dst), CpuFastSet, LZ77UnCompVram/Wram,
RLUnCompWram/Vram (4-byte source stride bug, latent), BgAffineSet,
ObjAffineSet, ArcTan, ArcTan2, Sqrt; `RegisterRamReset` no-op;
`BitUnPack` empty stub in `assertf.c`. Memory: `REG_BASE[0x400]`,
`PLTT[0x400]`, `VRAM_[0x18000]`, `OAM[0x400]`, `FLASH_BASE[131072]` as
globals; `defines.h` turns PLTT/VRAM/OAM/`SOUND_INFO_PTR`/`INTR_*` into
externs and empties `IWRAM_DATA`/`EWRAM_DATA`/`COMMON_DATA`/`ARM_FUNC`;
`io_reg.h` makes `REG_BASE` an extern array. Renderer verbatim from
multiplatform (modes 0 and 1, WIN1 bug, missing returns, "heavily
borrowed from NanoboyAdvance"). Audio: C MP2K sequencer "adapted from
gradenGnostic/pokeemerald-multiplatform", `STATIC_ASSERT`s pin `ToneData`
0xC, `MusicPlayerTrack` 0x50, `SoundChannel` 0x40; `m4a_internal.h`
heavily edited with unguarded alias unions; VCount IRQ at line 150 runs
`m4aSoundVSync` which queues 701 stereo frames; VBlank runs
`m4aSoundMain`; `SDL_QueueAudio` 42060 Hz F32 with no queue cap. DMA
verbatim from multiplatform (`DmaSet` function, fill/copy to CPU, HBLANK
per scanline plus once at VBlank, VBLANK and SPECIAL never run).

## Input, timers, RTC, save

Input: `ReadKeys` calls `Platform_GetKeyInput()`; configurable keyboard
table; XInput polled directly from the game thread; data races on
`keyboardKeys` and `timeScale`. Timers not emulated: `SeedRngAndSetTrainerId`
reads `REG_TM2CNT_L<<16 | REG_TM1CNT_L`, always 0, so every new game gets
the same trainer ID. RTC: `siirtc.c` replaced at API level; setters
compute an offset with `mktime` persisted in the `.cfg`; the project runs
with `OW_USE_FAKE_RTC TRUE` anyway.

Save: `ReadFlashId` returns 0xCCCC selecting `DUMMY_SAVE`; `save.c` calls
`ProgramFlashSector_DUMMY` directly; `TrySavingData` and
`save_failed_screen.c` call `Platform_StoreSaveFile()`; the four
`STATIC_ASSERT`s that SaveBlock1/2/3 and PokemonStorage fit their sectors
are disabled on PORTABLE with no reason given. Saves that bypass
`TrySavingData` (`TryWriteSpecialSaveSector`, link paths) are not
persisted. Their own addition: a `PGRSAVE` container (CRC32 header and
directory, a mandatory `LEGACY` chunk holding the 131,072-byte flash
image) and `save_file.c` with write-to-`.pending`, size verification,
rotation of `.recovery-N` copies, and `MoveFileExA` with write-through on
Windows but plain `rename()` without fsync elsewhere. A bare 131,072-byte
file is accepted as a legacy `.sav`; any other size is rejected.
`RecordedBattleSave` is made `packed, aligned(4)` on PORTABLE only, which
changes its layout wherever the GBA layout has interior padding.

## Modifications to expansion files

Guard `PORTABLE` (sometimes `PLATFORM_SDL2`), also used as a gameplay
feature flag, so platform and content changes are interleaved. Hardware
headers: `gba/defines.h`, `gba/io_reg.h`, `gba/macro.h`,
`gba/syscall.h`, `gba/flash_internal.h`, `gba/m4a_internal.h`,
`global.h` (`#include <stdio.h>`), `recorded_battle.h`. Core: `main.c`
(platform include, `gInitialMainCB2` removed, `InitRFU` skipped, flash
detection screen skipped, AGBPrint init skipped, keys from platform),
`m4a.c`, `agb_flash.c`, `agb_flash_1m.c`, `save.c`,
`save_failed_screen.c`, `siirtc.c`, `decompress.c` (IWRAM code copies made
direct calls for DEP/NX; `FastLZ77UnCompWram` to BIOS LZ77),
`random.c` (`Random32` C implementation instead of naked Thumb asm),
`librfu_intr.c` (asm bodies compiled out leaving empty naked functions),
`link.c`, `multiboot.c`, `script.c` and `asm/macros/event.inc` (ROM-mirror
bit for `callnative` removed on PC), `assertf.c`, `debug.c`
(`CheckROMSize`). Workarounds: DMA3 queue drained in `bg.c` and `menu.c`;
`battle_transition.c` clears callbacks before freeing; `naming_screen.c`
NULL guards; summary-screen text margins. Not modified: `crt0.s`,
`m4a_1.s`, `test/test_runner.c`, the ld scripts.

Unguarded GBA assumptions still compiled in: `bg.c` `IsTileMapOutsideWram`
compares against `IWRAM_END` (works only because the heap happens to sit
below 0x03008000); `field_name_box.c:190` drops pointer speaker names
outside `ROM_START..ROM_END`; `malloc.c` `MemBlockLocation` builds
`ROM_START | ...`; `libisagbprn.c` `MgbaPrintf` writes to 0x4FFF600 and
crashes in non-release builds whenever a `DebugPrintf` is reached;
pointer-to-u32 casts in `sprite.c`, `field_effect.c`, `menu.c`,
`save_failed_screen.c`, `chooseboxmon.c`, `m4a.c`, `script.c` silenced by
`-Wno-pointer-to-int-cast`.

## The technical report, summarised

Purpose: recompile the decompiled source for x86 and swap GBA services for
PC ones; no ROM conversion; move large assets into a `.pak` to escape the
32 MiB ROM cap. Mapping: ARM to 32-bit x86; PPU to a renderer of simulated
state; PSG/MP2K to a software mixer; flash to a file; RTC to the OS clock;
BIOS and DMA to C. "32 bits" (process) is distinct from "32 MiB" (ROM
window). Timeline: all milestones on 2 August 2026, first native build at
02:01, first map at 03:05, save and RTC at 03:21, mixer at 03:39, battle
at 03:53. Validated by hand: boot to new game, truck, Littleroot, save and
continue, audio, party screens, one wild battle; "not a substitute for a
full automated suite".

What bit them: drumset size inferred from "the next symbol" caused an
invalid read in `MidiKeyToFreq`; code copied to IWRAM and executed had to
become direct calls (DEP/NX); the script ROM mirror; the DMA3 queue never
draining during synchronous loads (party screen hung); RFU polling;
screen teardown with callbacks still pointing at freed frames
(use-after-free between field and battle); two summary-screen text writes
reaching the buffer edge; a recurring crash class from invalid CpuSet
transfers (hence the "last CpuSet" crash instrumentation).

Why 64-bit is not a flag flip, their words: "serialized structures,
pointer casts, assembly, tables and ABI interfaces must be audited";
"game scripts and tables store 32-bit pointers"; "a large part of the base
assumes ROM addresses are constant". Additional blockers found in the
code: all data asm emits 4-byte pointers that C structs read directly;
script bytecode embeds 4-byte native pointers and `script.c` compares a
rebuilt `u32` to `(u32)funcPtr`; the MP2K GOTO/PATT reader copies
`sizeof(u8 *)` bytes from track data with 4-byte pointers;
`PokemonCrySong.gotoTarget` is `u32`; m4a `STATIC_ASSERT`s only hold for
4-byte pointers; DMA registers hold pointers in `vu32`; pointers split
into s16 task and sprite data; address-window checks in `bg.c` and
`field_name_box.c`; `RLUnCompVram`'s `(u32)dest & 1`.

## Tests, GBA build, debug menu, license

Expansion's test suite does not run on PC: `Makefile_pc check` is the GBA
rule copied verbatim and cannot produce a runnable artifact; `test/` has
no PC guards. Their tests are separate native harnesses for the save
container and env-var smoke runs. The GBA build is kept green by the
`src/platform` filter and CI runs `make check` on GBA. Debug menu untouched
except `CheckROMSize`. No LICENSE file; `THIRD_PARTY_NOTICES.md` carries
multiplatform's scoped MIT; inherited code cites VBA-M, mGBA and
NanoBoyAdvance.
