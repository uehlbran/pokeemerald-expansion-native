# Research: how to run pokeemerald-expansion natively

This is the shared reading for everyone working on the platform layer. It
records what the reference ports do, what the GBA hardware does, what
expansion's test suite needs, and which approach this project takes for
each subsystem and why. The evidence behind each claim is in
`docs/research/`. Decisions with alternatives considered are in
`DECISIONS.md`.

Date of the research: 2026-10-08, against expansion/1.17.1.

## Required reading

Read these before touching the platform layer. Cite them in code comments
where a constant, timing or behaviour comes from them.

| Topic | Source |
|---|---|
| GBA hardware, every register and timing | GBATEK, https://problemkaputt.de/gbatek.htm |
| GBA hardware in plain language | Tonc, https://www.coranac.com/tonc/text/ |
| Behavioural oracle when GBATEK is ambiguous | mGBA source, https://github.com/mgba-emu/mgba (the bundled `tools/mgba/mgba-rom-test` is built from commit `dbffb46c4e7d2e7a2cbed7c3488cece4c2176d4c`) |
| The test runner protocol expansion uses | `docs/research/test-runner-protocol.md`, mGBA `src/platform/test/rom-test-main.c`, `src/gba/io.c` |
| Reference port 1: pret vanilla, single-threaded, 64-bit, headless tests | https://github.com/fuddlesworth/pokeemerald-native/tree/vanilla and `docs/research/port-native-vanilla.md` |
| Reference port 2: the platform code the community reuses | https://github.com/gradenGnostic/pokeemerald-multiplatform at `2f35335eff69ea1a08ebf19e64ea4d97ff6c0a05` and `docs/research/port-multiplatform.md` |
| Reference port 3: the only expansion-integrated port, with a technical report | https://github.com/hirata-producoes/pokemon-regionalidades and `docs/research/port-regionalidades.md` |
| pret decompilation and build | https://github.com/pret/pokeemerald, its `INSTALL.md` and wiki |
| expansion docs, test runner, debug menu | https://github.com/rh-hideout/pokeemerald-expansion (`README.md`, `docs/`, `test/`, `docs/tutorials/how_to_testing_system.md`, `include/config/debug.h`) |
| SDL2 | https://wiki.libsdl.org/SDL2 |
| C practice | SEI CERT C, https://wiki.sei.cmu.edu/confluence/display/c |
| Sanitizers | https://clang.llvm.org/docs/AddressSanitizer.html and https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html |

## What the reference ports have in common

All three ports descend from the same 2021 lineage (NTx86 and Kurausukun's
`pc_port` branch) and share these choices:

- The GBA assembly (`crt0.s`, `m4a_1.s`, `libgcnmultiboot.s`,
  `rom_header.s`) is not built. The m4a sound engine is replaced by a C
  sequencer and mixer. `IntrMain` is a stub.
- Hardware memory is plain C arrays: `REG_BASE[0x400]`, `PLTT`, `VRAM_`,
  `OAM`, `FLASH_BASE`. `include/gba/io_reg.h` and `include/gba/defines.h`
  are modified so `REG_BASE`, `VRAM`, `PLTT`, `OAM` name those arrays, and
  `EWRAM_DATA`/`IWRAM_DATA` are emptied. Register writes have no side
  effects, so nothing that depends on a write side effect (DMA start,
  timer start, sound FIFO, mGBA debug output) works unless the game is
  patched to call a function instead.
- BIOS calls are C functions linked in place of `libagbsyscall`. The code
  is "based on the VBA-M source code" with ArcTan "taken from mGBA's
  source", and the renderer is "heavily borrowed from NanoboyAdvance".
  VBA-M is GPL-2.0+, NanoBoyAdvance is GPL-3.0, mGBA is MPL-2.0. None of
  the ports documents this as a licensing matter.
- A per-scanline software renderer runs once per frame for lines 0-159,
  firing HBlank and VCount interrupts between lines; lines 160-227 are
  never simulated; `REG_VCOUNT` is left at 161 so `SetGpuReg` writes
  through immediately. Modes 0 and 1 only (the game uses only those).
- Frame pacing is 60.000 Hz, not 59.7275 Hz, and audio runs at
  `60 * 701 = 42060` Hz to match. Audio is pushed with `SDL_QueueAudio`.
- Timers are not emulated, so the trainer ID seed and anything else read
  from `REG_TMxCNT_L` is wrong. Sound DMA and FIFOs are not emulated.
- `data/*.s` and the song data are assembled by the host assembler after
  a sed that strips `@` comments and rewrites `.4byte` to `.int`.
- Flash is a 128 KiB RAM image selected by making `ReadFlashId` return a
  fake ID, persisted as a raw file with the GBA sector layout. Two of the
  three erase to 0x00 instead of 0xFF because of
  `u8 clearBuffer[0x1000] = { 0xFF }`. Writes are not flushed atomically.
- The RTC is replaced at the `SiiRtc*` API level by host `localtime()`.
- Input: `ReadKeys` calls a platform function instead of reading
  `REG_KEYINPUT`.
- Shared bugs: WIN1 reads WIN0's registers; `RLUnComp*` advances the
  source four bytes per byte; `gIntrTable` is redeclared `const`; strict
  aliasing violations through `unsigned char` arrays; pointer-to-u32 casts
  silenced with `-Wno-pointer-to-int-cast`.

Where they differ:

| | native-vanilla | multiplatform | regionalidades |
|---|---|---|---|
| Base | pret master 2025-08 | pret master 2025-08 | expansion 1.16.3 |
| Threads | one; `AgbMain` returns after init, host calls `MainLoop()` per frame | two; game thread blocks in `VBlankIntrWait` on a semaphore, main thread renders and runs all interrupt handlers | same as multiplatform |
| Bit width | 64-bit default, 32-bit supported | 32-bit only | 32-bit only |
| Pointers in data asm | `ptrvalue`/`ptr_align` macros, `.quad` on 64-bit | `.int` | `.int` |
| Interrupt gating | DISPSTAT bits only | DISPSTAT bits (Android: IE) | IE for VBlank, DISPSTAT for others |
| Soft reset | zero a `gba_ram` section and `longjmp` | `exit(0)` | `exit(0)` |
| Save | raw image, reads from a second file handle | raw image | `PGRSAVE` container wrapping the raw image, atomic rename, recovery copies |
| 64-bit save | `SaveBlock1` conversion layer keeps the GBA layout | none | none |
| Test suite | its own Python headless harness with per-frame hashes | none | native harnesses for its own save code; expansion's `make check` copied but non-functional |
| GBA build | kept, CI checks the ROM is unchanged | probably broken (unguarded `src/platform/bios.c` is picked up) | kept, `src/platform` filtered out |
| Upstream modification policy | everything under `PORTABLE`, CI enforces | `PORTABLE` plus unguarded drift (`math_util.c` lost its 64-bit intermediate) | `PORTABLE` doubles as a gameplay feature flag |

The lesson from the three ports is that the approach works: expansion's C
compiles on x86 with few changes, a scanline renderer plus a C mixer plus
SDL2 is enough, and a raw flash image keeps saves compatible. The lesson
from their defects is where plain-memory registers and a frame-granular
world break down: timers, DMA timing, sound FIFO, the test runner's
debug channel, and every side effect of a register write.

## What expansion's test suite needs

Summarised from `docs/research/test-runner-protocol.md`. The GBA suite is
5862 tests (5414 pass, 18 known failing, 9 expected failing, 421 to-do; see
`docs/research/gba-baseline-1.17.1.md`). `make check` builds a test ELF,
patches `gTestRunnerHeadless` into it, and runs
`tools/mgba-rom-test-hydra`, which forks N copies of `mgba-rom-test`, each
with `gTestRunnerN`/`gTestRunnerI` patched into its ELF copy, and parses
their stdout. The hydra's protocol is lines of `GBA Debug: :<letter>...`
and the process exit code, which the ROM sets with `swi 0x3` and r0.

The runner needs from the hardware layer: the three mGBA debug registers
at 0x4FFF600/0x4FFF700/0x4FFF780 with the 0xC0DE/0x1DEA handshake and
send-on-flag-write; a Timer 2 interrupt at about 1 Hz for timeouts; a way
to jump back to the main loop from any depth (`JumpToAgbMainLoop`, naked
ARM asm that restores `sp`); the Timer 2 handler patching the BIOS IRQ
return address at 0x3007F9C; a `.tests` section with
`__start_tests`/`__stop_tests` and `test_test_runner.o` first; the
ELF-patched constants `gTestRunnerN`, `gTestRunnerI`, `gTestRunnerArgv`,
`gTestRunnerHeadless`, `gTestRunnerSkipIsFail`; a heap whose pointers lie
in `[EWRAM_START, EWRAM_END)` for the leak check; a persistent 0x100-byte
region at 0x3007F00 that survives a reboot; the battle runner's private
1 KiB stack switched by naked Thumb asm; `VBlankIntrWait`; `CpuSet`,
`CpuFastSet`, `LZ77UnComp*`, `RegisterRamReset`; Timer 3 for eight
`BENCHMARK` tests; inline asm in `OVERWORLD_SCRIPT` and `Random32`.

## Decisions per subsystem

Each decision names the reference it follows and the GBATEK section the
implementation must cite. "Clean room" means written from GBATEK and
tested against GBATEK, with the ports consulted for structure and
pitfalls only, because of their licensing.

### Memory model: the GBA address space, mirrored

We map the GBA memory regions at their real addresses in the native
process: EWRAM at 0x02000000 (256 KiB), IWRAM at 0x03000000 (32 KiB), I/O
at 0x04000000 (4 KiB) plus the mGBA debug page at 0x04FFF000, palette at
0x05000000, VRAM at 0x06000000 (96 KiB), OAM at 0x07000000, and the save
flash at 0x0E000000 (128 KiB). The executable is linked at 0x40000000 so
it sits above every GBA region on both bit widths. Verified on 32-bit and
64-bit, with and without ASan and UBSan: `docs/research/memory-map-experiment.md`.

Why, instead of the ports' C arrays: `include/gba/io_reg.h`,
`include/gba/defines.h`, `include/gba/flash_internal.h` and every
`REG_*` macro compile unmodified, which removes the largest class of
upstream modifications; `EWRAM_START`/`EWRAM_END`, `INTR_CHECK`,
`INTR_VECTOR`, the test runner's persistent state at 0x3007F00 and its
leak check all work as written; `(u32)pointer` casts on EWRAM, VRAM and
I/O addresses remain correct on 64-bit because those pointers fit in 32
bits. `EWRAM_DATA` objects (section `.sbss` under MODERN) are placed at
0x02000000 by the native linker script so the heap really is in EWRAM.
IWRAM is mapped for the few fixed addresses the game uses; ordinary
globals stay in the native `.bss`.

What this does not give us: write side effects. A register write is a
store. The platform layer therefore polls register state at the points
where hardware would act (frame boundary, scanline boundary) and the few
writes that must act immediately (DMA start, mGBA debug send, sound
engine `swi`) are routed through guarded, recorded modifications of the
game source. GBATEK: "GBA Memory Map".

### Entry point and loop: single thread, VBlankIntrWait is the frame boundary

Follows native-vanilla's single-threaded design, not multiplatform's two
threads. `main()` initialises the platform modules, then calls expansion's
`AgbMain()`, which runs the game's own `for(;;) MainLoop()` unchanged.
The frame boundary is the BIOS call `VBlankIntrWait()`, which the game
calls once per frame from `WaitForVBlank`. Our implementation of it:
render lines 0-159 (firing HBlank DMA, HBlank and VCount interrupts
between lines, exactly as the ports do), run the timers for one frame,
raise the VBlank interrupt through the interrupt controller, mix one
frame of audio, present, poll input into `REG_KEYINPUT`, pace to
59.7275 Hz (280896 cycles at 16.78 MHz; GBATEK "LCD Dimensions and
Timings"), then return when the handler has set the VBlank flag in
`INTR_CHECK`. `REG_VCOUNT` reads 160 during game logic so `SetGpuReg`
writes through, as on hardware during VBlank.

Why single-threaded: all three ports show the game never needs
concurrency; the two-thread ports busy-spin and have data races on input
and time scale; one thread makes the sanitizers and the headless replay
deterministic. Audio uses SDL's queue, so no callback thread touches
game state.

### Interrupts: a real interrupt controller

Clean room from GBATEK "GBA Interrupt Control". A module owns dispatch:
an interrupt is raised by setting its bit in `REG_IF`; dispatch happens at
the points named above and only when `REG_IME` is 1 and the bit is set in
`REG_IE`; it calls the handler from expansion's `gIntrTable` by the index
`IntrMain` would use, after which the handler's own write to `REG_IF` and
`INTR_CHECK` acknowledges it. This is what `src/crt0.s` `IntrMain` does;
the ports skip IE/IF/IME and hard-code three indices. Timer, VBlank,
HBlank and VCount are the sources we raise; serial, DMA, keypad and
gamepak sources exist in the controller but are never raised (link
hardware is stubbed).

### Video: scanline renderer, clean room

Follows the ports' structure (per-line compositing of up to four text
backgrounds, one affine background in mode 1, 128 sprites, windows,
blending, mosaic) but written from GBATEK "LCD Video Controller" with
tests for the pure functions (tile and map address decoding, affine
reference-point stepping, window masks, blend arithmetic with the
coefficients clamped to 16). Modes 0 and 1, which are the only modes the
game sets; the others return a documented error. Fixes the ports' shared
WIN1 and clamping bugs by construction. Output is a 240x160 BGR555
buffer converted to an SDL streaming texture. Forced blank is honoured.

### Audio: the m4a engine translated to C, PSG synthesised, SDL queue

The GBA engine is `src/m4a_1.s` (ARM: `SoundMainRAM`, `MPlayMain`,
`ply_*`) plus `src/m4a.c`. The ports replaced the assembly with a C
sequencer and mixer of undocumented provenance; we do not copy it. Our
mixer is a C translation of expansion's own `m4a_1.s`, which is part of
the decompilation and carries the same status as the rest of the game
code, with its struct layouts pinned by static assertions against
`include/gba/m4a_internal.h`. It is a game module, not a platform module,
and lives with the game code it replaces; `m4a.c`'s one inline `swi 0x2A`
becomes a guarded call. The PSG (the four Game Boy channels) is a
platform module written from GBATEK "GBA Sound Controller", driven by the
`REG_NR*` and `REG_SOUNDCNT*` registers the game writes, with the
frame sequencer, envelopes, sweep, length counters and the noise LFSR
unit-tested. Direct Sound FIFO A/B, DMA1/2 and Timer 0/1 are not
emulated as hardware; the mixer's output buffer is consumed directly, as
in every port. Output: `SDL_QueueAudio` at the engine's native rate with
a bounded queue depth, single-threaded, no callback.

### DMA: a channel model, immediate for NOW, polled for HBLANK and VBLANK

Clean room from GBATEK "GBA DMA Transfers". Four channels read from
their registers; a count of 0 means the maximum (0x4000, or 0x10000 for
channel 3); destination increment, decrement, fixed and reload; source
increment, decrement, fixed; repeat; 16 and 32-bit units; source,
destination and length validated against the mapped regions and rejected
with an error otherwise. Immediate transfers run when the game sets the
enable bit, which requires routing `DmaSet` through a function (a guarded
modification of `include/gba/macro.h`, as in every port; `DmaCopy*` and
`DmaFill*` keep their semantics through the same path). HBLANK transfers
run between scanlines and VBLANK transfers at the start of VBlank, by
polling the channel registers. Special (FIFO) timing is not emulated.

### Timers and RTC: frame-granular timers, S-3511A semantics

Clean room from GBATEK "GBA Timers". The four timers advance by one
frame of cycles (280896) at each frame boundary according to their
prescaler, cascade and enable bits, with reload on overflow and the
overflow interrupt raised through the controller. This is what makes the
test runner's Timer 2 timeout and Timer 3 benchmarks work, and gives the
trainer ID seed real entropy. The RTC follows GBATEK "GBA Cart Real-Time
Clock (RTC)" at the level of the S-3511A's registers (BCD date and time,
24-hour status, offset persisted next to the save) rather than the GPIO
bit-banging in `src/siirtc.c`, which is replaced at its API level under a
guard, as in every port. Both are unit-tested for rollover, prescaler
boundaries and invalid input.

### Input: write `REG_KEYINPUT`

The platform writes the active-low key state into `REG_KEYINPUT` once per
frame before `ReadKeys` runs (GBATEK "GBA Keypad Input"). No
modification of `src/main.c`. Keyboard and SDL game controllers, with a
documented default mapping; the headless replay feeds the same register
from a script.

### BIOS calls: clean room, link-time substitution

`include/gba/syscall.h` declares the BIOS functions; the GBA build gets
them from `libagbsyscall`. The native build links our C implementations
of the same symbols, written from GBATEK "BIOS Functions": `CpuSet`,
`CpuFastSet`, `LZ77UnCompWram`, `LZ77UnCompVram`, `RLUnCompWram`,
`RLUnCompVram`, `HuffUnComp`, `BitUnPack`, `BgAffineSet`, `ObjAffineSet`,
`ArcTan`, `ArcTan2`, `Sqrt`, `Div`, `DivArm`, `Mod`, `RegisterRamReset`,
`SoftReset`, `VBlankIntrWait`, `IntrWait`, `Halt`, `MidiKey2Freq`, and
the `MultiBoot` family as documented stubs. Each is unit-tested at the
boundaries (zero and maximum lengths, alignment, overflow). `SoftReset`
clears the mapped regions and `longjmp`s to the loop (native-vanilla's
approach), never `exit()`.

### Save: flash image in the mapped region, atomic file

Expansion's `src/agb_flash*.c` drive the chip through command writes to
0x0E005555 and polls, which plain memory cannot emulate, so the flash
driver is replaced at the driver-table level as in every port: a native
driver entry whose program and erase operate on the mapped 128 KiB
region (erase to 0xFF, sector bounds checked). The container module
persists that image as a raw `.sav` with the GBA sector layout, written
to a temporary file, fsynced, then renamed, with a size and sector-footer
check on load, and corrupted or missing files reported rather than
silently zeroed. Every path that writes flash persists, including
`TryWriteSpecialSaveSector` and the link-save paths the ports forgot.
GBATEK "GBA Cart Backup Flash ROM".

### The test runner on the native build

The hydra and its line protocol are reused unchanged. The native test
binary is run by the hydra in place of `mgba-rom-test` through a thin
launcher that accepts the hydra's arguments, sets `gTestRunnerN` and
`gTestRunnerI` from them, and prints `GBA Debug: ` lines. Inside the
binary, the mGBA debug registers become a platform module that the
runner's three static functions call through a guarded modification;
`swi 0x3` becomes a call that exits with r0's value; `JumpToAgbMainLoop`
becomes `longjmp`; the battle runner's stack switch becomes a
`ucontext` coroutine with sanitizer fiber annotations; the `.tests`
section is emitted by the native linker script with the same ordering
rules. Crash recovery across a reboot is implemented by re-executing the
process with the persistent state passed through the environment, and
the two `KNOWN_CRASHING` tests are reported honestly if that proves
impossible.

### Build system: a separate native makefile, reusing expansion's rules

`Makefile.native` at the repository root, invoked as
`make -f Makefile.native`, so expansion's `Makefile` is untouched. It
reuses expansion's tool builds and the `graphics_file_rules.mk`,
`json_data_rules.mk`, `map_data_rules.mk`, `audio_rules.mk` and
`spritesheet_rules.mk` fragments, compiles `src/**/*.c` and `test/**/*.c`
with the host compiler, excludes `src/*.s`, and assembles `data/*.s` and
the song data with the host assembler after a build-time filter that
strips `@` comments and converts `.align` to `.p2align` (ARM `.align` is
a power of two, x86 `.align` is a byte count). Nine of the fifteen
`data/*.s` files assemble with only that filter; the other six need the
macro-level fixes the ports made (`.if` on symbols, the undefined-map
check), which become recorded modifications of `asm/macros/*.inc` only
where a filter cannot do it. Pointers in data stay `.4byte` on 32-bit; the
64-bit milestone introduces `ptrvalue` and `ptr_align` as native-vanilla
did. The GBA `Makefile` must never see `src/platform`, which it does not
because it wildcards `src/*.c` and `src/*/*.c`: regionalidades's
`filter-out src/platform/%` is the precedent if that changes.

### Guard macro

One macro, `PLATFORM_NATIVE`, defined only by `Makefile.native`. Every
modification of an expansion file is `#if PLATFORM_NATIVE` or
`#if !PLATFORM_NATIVE` and is listed in `UPSTREAM_MODIFICATIONS.md`.
Never a feature flag.

## Test framework for the platform layer

The platform modules are tested standalone, without expansion's build,
under ASan and UBSan. Expansion's own runner is designed around the GBA
memory map and the mGBA debug channel and runs inside the game image, so
it cannot test a module in isolation. We use a small header-only
assertion framework of our own, `src/platform/test/ptest.h`: test
registration by a macro, assertions that print file, line, expression
and values, a runner that returns a non-zero exit code on any failure or
any sanitizer report, no skipped-test concept. About a hundred lines,
no dependency, no third-party license, and nothing a reader needs to
learn. Alternatives considered: greatest.h and Unity (single-header, but
a vendored file with its own license and more features than we need);
CMocka and Check (shared-library dependencies the 32-bit build would
need in i386 form).

## Licensing

pret/pokeemerald and pokeemerald-expansion ship no license file. Nintendo
owns the game code and assets; the decompilation projects claim no
rights over them. pokeemerald-multiplatform adds an MIT license scoped
to "the original multiplatform-port modifications" and explicitly
excludes the decompilation and Nintendo's property; regionalidades
carries that notice as a third-party notice and adds no grant of its
own; native-vanilla has no license and credits GPL and MPL emulators as
sources. Our choice: an MIT license scoped exactly as multiplatform's,
covering only this project's platform layer, build glue, tests and
documentation; the game code remains as upstream has it; and we do not
copy code from the ports, so no GPL or MPL obligation attaches to our
files. See `../LICENSE` and `DECISIONS.md`.

## Open questions carried into milestone 2

- Whether placing `.rodata` at 0x08000000 (the executable's read-only
  data is about 25 MiB on GBA and the ROM window is 32 MiB) is worth it
  to keep `field_name_box.c`'s `ROM_START..ROM_END` check and
  `malloc.c`'s location encoding working, or whether those two sites get
  guarded modifications. The worker decides on evidence and records it.
- Whether the native `.sbss` (EWRAM data) exceeds 256 KiB on x86; if it
  does, the leak check in the test runner needs a guarded bound.
- Which `data/*.s` macro files need source changes versus filter rules.
