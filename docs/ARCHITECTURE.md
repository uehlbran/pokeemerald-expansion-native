# Architecture

## The boundary

Two bodies of code live in this repository.

Expansion's game code (`src/`, `include/`, `data/`, `graphics/`, `sound/`,
`test/`, the tools and the GBA build) is upstream's. It is pret-style GBA
C that talks to hardware through memory-mapped registers
(`include/gba/io_reg.h`), fixed addresses (`include/gba/defines.h`),
BIOS calls (`include/gba/syscall.h`) and a few lines of assembly. We do
not rewrite or re-lint it. We modify it only where a hardware assumption
cannot be isolated, under `PLATFORM_NATIVE`, and every such modification
is listed in `UPSTREAM_MODIFICATIONS.md`.

The platform layer (`platform/`) is ours. It is C that stands in for the
Game Boy Advance: it provides the memory the game addresses, performs
the work the hardware did at the moments the hardware did it, implements
the BIOS functions, and connects to SDL2 for the screen, speakers, keys
and files. It is compiled only by `Makefile.native`, never by expansion's
`Makefile`, and each of its modules builds and runs its unit tests
standalone with `make -C platform test`.

The layer reads GBATEK's names. Registers, timings, sizes and modes are
named constants with a comment citing the GBATEK section they come from.
The file header of each module says what hardware it stands in for, what
the real hardware did, and which reference it follows.

## The memory model

The platform maps the GBA regions at their GBA addresses
(`platform/gba_memmap.h`, GBATEK "GBA Memory Map"):

| Region | Address | Size | Native backing |
|---|---|---|---|
| EWRAM | 0x02000000 | 256 KiB | linker script places expansion's `.sbss` (`EWRAM_DATA`) here; the rest is mapped |
| IWRAM | 0x03000000 | 32 KiB | mapped; only fixed addresses are used (`INTR_CHECK`, `INTR_VECTOR`, the test runner's persistent state) |
| I/O | 0x04000000 | 1 KiB in a 4 KiB page | mapped; every `REG_*` macro reads and writes here |
| mGBA debug | 0x04FFF000 | 4 KiB page holding the registers at 0x04FFF600, 0x04FFF700, 0x04FFF780 | mapped; used by the test runner shim |
| Palette | 0x05000000 | 1 KiB | mapped |
| VRAM | 0x06000000 | 96 KiB | mapped |
| OAM | 0x07000000 | 1 KiB | mapped |
| Flash | 0x0E000000 | 128 KiB | mapped; the save container persists it |

Not GBA memory, placed below 2^27 so its addresses fit the battle test
runner's 27-bit fields: the stack `PlatformCallWithStack` runs the battle
tests' functions on, 1 MiB at 0x01800000 (`gba_regions.ld`).

On Windows (a cross-build from Linux with MinGW-w64, x86-64 only) the
regions are sections of the PE image, because a PE section's address is an
offset above the image base and the image cannot start at 0x40000000 with the
regions below it. The image is based at 0x01800000, below the lowest region,
with no address randomisation and no base relocations, so that the loader maps
it there or refuses it; the code is at 0x40001000, where the Linux build's is
(`platform/gba_regions.pe.ld`, `platform/gba_game.pe.ld`, `platform/target.mk`).
The loader refuses an image whose sections are not adjacent, so the stretches
between the regions are sections of their own, uninitialised and read-only
(`platform/gba_gaps.pe.s`): the image spans 1 GiB of address space, but the
gaps are not stored in the file and the probe measured no commit charge for
them. The battle runner's stack is the first section, at 0x01801000, because
the headers take 0x01800000. The probe that showed the loader accepts this
layout, and refuses the one with holes: `research/probes/README.md`, W1. `platform/build_glue/check_pe_layout.py` checks
every link's result against it, since the executable cannot be run where it is
built.

The executable is linked at 0x40000000 so it never overlaps a region. The
two places where expansion takes a pointer to be in the cartridge ROM are
guarded: the heap's allocation-site encoding (`src/malloc.c`, an offset
from the start of the executable) and `SetSpeaker`'s ROM range check
(`src/field_name_box.c`, `PlatformIsRomPointer`: the executable's
read-only segments).
Register writes are plain stores: the layer polls register state at
scanline and frame boundaries and the few writes that must act at once
(DMA enable, the sound engine's `swi 0x2A`, the test runner's debug
send) are routed through guarded calls.

## The frame

The game runs on the main thread, unmodified: `AgbMain()` loops over
`MainLoop()` and calls `VBlankIntrWait()` once per frame. The platform's
`VBlankIntrWait` (the `bios` module, `bios_control.c`) is the frame boundary: until
`INTR_CHECK` shows the VBlank flag the handler sets, as the BIOS waits, it
calls the frame hook, `frame_run` (`platform/frame.h`), which does, in this
order:

1. Lines 0 to 159. Each line starts (`VCOUNT` = line, the H-Blank flag in
   `DISPSTAT` cleared, the V-Counter flag set and VCount raised when the
   line matches `DISPSTAT`'s setting), dispatch; the PPU renders the line
   into the frame buffer (not in `--test-runner` mode without a replay or a
   screenshot, where nobody sees it); H-Blank starts (flag set),
   HBLANK-timed DMA runs, HBlank is raised, dispatch.
2. Advance the four timers by one frame of cycles; raise their overflow
   interrupts; dispatch.
3. Enter VBlank: line 160 starts (`VCOUNT` = 160, the V-Blank flag set),
   the PPU latches its affine reference points, VBLANK-timed DMA runs,
   VBlank is raised, dispatch (this runs expansion's `VBlankIntr`, which
   runs the sound engine and the DMA3 queue and flushes the buffered GPU
   registers).
4. Line 161 starts, with its V-Counter match, dispatch.
5. Mix one frame of audio (the m4a ring frame the FIFO DMAs play, the PSG,
   `SOUNDCNT_H` routing, DC removal) and queue it.
6. Present the frame; poll SDL events; run the step hook if one is set
   (`frame_set_step_hook`; the replay driver is its one user); write
   `REG_KEYINPUT`.
7. Store the save file if the game wrote the flash chip.
8. Pace to the GBA frame period.
9. Count the frame; a frame limit or a quit ends the game after it (main
   unwinds to its exit target), and the screenshot, if asked for, is
   written at the end of the loop.

A handler may leave the frame by a jump (the test runner's timeout calls
`PlatformJumpToAgbMainLoop`): the rest of the frame is not run and the next
`frame_run` starts at line 0.

Interrupts are raised only when their `DISPSTAT` enable is set, as the LCD
requests them. Lines 162 to 227 are not run one by one: their HBlank
interrupts and a VCount setting in that range never fire (expansion uses
150, and its HBlank effects act on lines 0 to 159 only).

`REG_VCOUNT` reads 161 while the game runs, between frames and from
power-on (main sets it, and `KEYINPUT`, before every entry into `AgbMain`,
as the BIOS leaves the cartridge started in VBlank with the buttons
readable). Expansion's `SetGpuReg` (`src/gpu_regs.c`) writes a register
through only when `VCOUNT` is 161 to 225, as during VBlank on hardware, and
buffers it until the next VBlank handler otherwise; 160, the first VBlank
line, is outside that window and would delay every write by a frame.

## Modules

One module per hardware subsystem. Each has one header that is its only
public interface, `static` for everything else, no global mutable state
outside the module that owns it, paired `init` and `shutdown` with
`shutdown` safe on a partially initialised module, and error codes
returned to the caller (`main` decides what to do; modules never call
`exit`).

| Module | Stands in for | GBATEK section | Unit-testable |
|---|---|---|---|
| `gba_memmap` | the address decoder | GBA Memory Map | yes (region bounds, containment, mapping) |
| `transfer_bounds` | the bus decoder as DMA and the BIOS copy calls meet it: may this range be transferred | GBA Memory Map | yes |
| `interrupt` | `IntrMain` and the IE/IF/IME logic | GBA Interrupt Control | yes |
| `timer` | Timers 0 to 3 | GBA Timers | yes |
| `rtc` | the S-3511A clock chip's registers and offset | GBA Cart Real-Time Clock | yes |
| `rtc_offset_store` | the battery-backed RTC setting, as a one-integer text file (internal to `rtc`) | GBA Cart Real-Time Clock | yes (through `rtc`) |
| `fixed_clock` | nothing: a deterministic clock for the replay, `--rtc-fixed` | n/a | yes |
| `rtc_calendar` | the chip's calendar arithmetic (BCD, leap years, the 2000 to 2099 cycle) | GBA Cart Real-Time Clock | yes |
| `dma` | DMA channels 0 to 3 | GBA DMA Transfers | yes |
| `bios_arith`, `bios_affine`, `bios_control`, `bios_decompress`, `bios_memory`, `bios_sound`, `bios_stream`, `bios_unfilter`, `bios_internal` | the BIOS SWI functions, one file per family (arithmetic, affine, control, decompressors, memory copies, sound, the unfilters), with helpers shared through `bios_internal.h` and the decompressors' shared parts in `bios_stream`; `bios.h` is the interface (there is no `bios.c`), and `VBlankIntrWait`, the frame boundary, is in `bios_control.c` | BIOS Functions | yes (one test file per family) |
| `input` | the keypad | GBA Keypad Input | yes (mapping, replay source) |
| `ppu`, `ppu_bg`, `ppu_obj`, `ppu_window`, `ppu_blend` | the LCD controller, drawing: backgrounds, objects, windows, blending and brightness (shared through `ppu_internal.h`) | LCD Video Controller | yes (synthetic scenes in the regions) |
| `video` | the LCD panel, via SDL | n/a | yes (SDL dummy drivers; the pixel format and sizes, not the picture) |
| `psg`, `psg_channel`, `psg_square`, `psg_wave`, `psg_noise` | the four Game Boy sound channels (shared through `psg_internal.h`) | GBA Sound Controller, Sound Channels 1 to 4 | yes |
| `audio` | the speaker, via SDL | n/a | yes (SDL dummy drivers; the queue sizes, not the sound) |
| `frame_pacer` | the LCD's frame period: holds the loop to 59.7275 Hz | LCD Dimensions and Timings | yes (`test_frame_pacer.c`) |
| `m4a/` | the ARM sound engine `src/m4a_1.s`, in C | n/a (game code) | yes, and a differential test against the ARM original under qemu-arm, run by hand (`platform/m4a/README.md`) |
| `flash` | the 1 Mbit flash chip's driver entry | GBA Cart Backup Flash ROM | yes |
| `save_file` | the cartridge's persistence | n/a | yes |
| `save_convert` | the save moving between the 32-bit and the 64-bit build (`SaveBlock1`'s layouts, `save_layout32.h`, `save_layout64.h`) | n/a (expansion's `src/save.c` format) | yes, and gate (h) |
| `debug_channel` | mGBA's debug registers | mGBA `src/gba/io.c` | yes |
| `crash_reboot` | the reboot after a crash, keeping IWRAM's last 0x200 bytes (test runner mode) | BIOS Reset Functions | yes (real crashes and re-execs) |
| `arm_division` | the GBA's software division (libgcc), which returns instead of trapping on a zero divisor | (libgcc, measured under qemu-arm by `arm_division_libgcc/`; the Intel SDM for decoding) | yes |
| `lcd_status` | the LCD controller's timing registers, `VCOUNT` and `DISPSTAT` | LCD I/O Interrupts and Status | yes (through `test_frame`) |
| `m4a_output` | the sound FIFO DMAs, reading the m4a engine's PCM ring | GBA Sound Channel A and B - DMA Sound | yes |
| `sound_mix` | the sound mixer and output capacitor | GBA Sound Control Registers | yes |
| `frame` | a frame of hardware time, in order (above) | LCD Dimensions and Timings | yes (headless, with the real modules) |
| `game_hooks` | the game's control-flow assembly (test runner jumps, the battle runner's stack, delay loops) | n/a (game code) | yes |
| `crt0`, `rom_header`, `gcn_multiboot`, `decompress_asm` | the `src/*.s` the native build does not assemble | n/a (game code) | yes |
| `options` | the command line | n/a | yes |
| `replay`, `replay_parse`, `replay_include`, `replay_session` | nothing: the input-replay script, its parser, its `include` expansion and the glue to the running game | n/a | yes (against a fake host), except `replay_session`, covered only by the replays |
| `inspect`, `inspect_menu`, `inspect_args`, `inspect_decode`, `symbols` | nothing: the facts a replay reads from expansion's variables, the ranges of their arguments, and function names from the map of the executable's symbols the link writes beside it | n/a | `inspect_args`, `inspect_decode`, `symbols` yes; `inspect`, `inspect_menu` only through the replays |
| `restart` | a switch-off and switch-on: re-exec of the process for the replay's `restart` | n/a | yes |
| `host_memory`, `host_fault`, `host_process`, `host_image`, `host_stack`, `host_file` (sources `host_posix.c`, `host_linux.c`) | nothing on the GBA: what the modules above need from the operating system, one header per concern (below, "The host layer") | n/a | yes (one test per header) |
| `main` | power-on | n/a | no |

Header-only: `gba_irq.h` (the fourteen interrupt sources), `gba_timing.h`
(the clock and frame length), `dma_hooks.h` (the two functions expansion's
`include/gba/macro.h` calls under `PLATFORM_NATIVE`), `script_tag.h` (see
"Known divergences"), and `inspect_internal.h` (what `inspect.c` and
`inspect_menu.c` share).

Dependencies point one way: `main` depends on everything; `frame` on the
modules a frame drives; `ppu`, `dma`, `timer`, `interrupt`, `psg`, `flash`
depend on `gba_memmap` for addresses and bounds; `bios` depends on
`gba_memmap` and `interrupt`; nothing but `frame`, `main` and `replay_session` (which writes the
screenshot) depends on `video` or `audio`, and nothing on `main`. The modules do not include
expansion's headers; the exceptions are `gba_memmap.h`, which may include
`gba/defines.h` and `gba/io_reg.h` as system headers to reuse their names
and assert ours match, `m4a_output.c`, which reads the m4a engine's
`struct SoundInfo` (`gba/m4a_internal.h`) as the DMA did, the `m4a/`
engine, which is game code, and `inspect.c`, `inspect_menu.c` and
`inspect_args.c`, which read the game's variables and are compiled with the
game's defines (`platform/game_flags.mk`). The host layer depends on no
module (`host_memory.h` takes `gba_memmap.h`'s region numbers, nothing
more), and only the modules of the next section depend on it.

## The host layer

Thirteen modules need something of the operating system beyond the C library
and SDL, and each need is one of six concerns. Each concern is one header,
`platform/host_<concern>.h`, which says what it stands in for and which OS
facility it wraps; the modules call it and contain no OS call of their own
(no `sigaction`, `ucontext`, `/proc`, `getauxval`, `pthread`, `fsync`).
`research/m8-inventory.md`, sections 1.2, 3 and 5.1, has the counterparts on
Windows and macOS; `DECISIONS.md`, "The ports share a thin host layer", why
it is one layer and not `#ifdef`s in the modules.

| Header | Concern | Its users | Linux |
|---|---|---|---|
| `host_memory.h` | memory at fixed addresses: where the link put each GBA region and the battle runner's stack; pages mapped at run time | `gba_memmap`, `game_hooks`, `transfer_bounds`, the m4a tests | `gba_regions.ld`'s symbols; `mmap` with `MAP_FIXED_NOREPLACE` |
| `host_fault.h` | hardware faults with the faulting context: the divide-error and crash handlers' install and uninstall, the registers by name and by ModRM number, the alternate stack | `arm_division`, `crash_reboot` | `sigaction`, `sigaltstack`, `sigprocmask`, `uc_mcontext.gregs` |
| `host_process.h` | the executable's own file, and starting it again | `crash_reboot`, `restart`, `symbols` | `/proc/self/exe`, `readlink`, `execve` |
| `host_image.h` | the bounds of the executable's image and of the main thread's stack; its read-only segments | `transfer_bounds`, `game_hooks` (`PlatformIsRomPointer`) | `__executable_start`, `__data_start`, `_end`, `getauxval(AT_PHDR)`, `pthread_getattr_np` |
| `host_stack.h` | running a function on a stack of the caller's choosing | `game_hooks` (`PlatformCallWithStack`) | `ucontext`, with AddressSanitizer's fiber annotations |
| `host_file.h` | durable file replacement, `mkdir` with a mode, the local time | `save_file`, `rtc_offset_store`, `replay_session`, `rtc` | `fsync`, `rename`, a directory's `fsync`, `mkdir`, `localtime_r` |

The sources are one per OS family: `host_posix.c` holds what Linux and macOS
share (the signal handlers' installation and chaining, `execve`, the files),
and `host_linux.c` the rest (the linker-script symbols, `MAP_FIXED_NOREPLACE`,
the `gregs` index table, `/proc/self/exe`, `getauxval`, `pthread_getattr_np`,
the `ucontext` switch); `host_internal.h` is what they share. `TARGET_OS`
(`platform/target.mk`, included by `host.mk`, so by every Makefile) selects them:
Linux, or `windows` for the cross-build, whose `host_windows.c` is a stub that
fails every call until the port is written. A port adds its source and its value. `crt0` and `main`
also use the toolchain (the linker scripts' own symbols, weak references,
SDL's `main`) but call nothing of the OS's, so they are not users.

The replay's function names are OS-neutral the same way: the link writes
`<executable>.symbols`, the executable's function symbols as `nm` lists
them (`host.mk`'s `write_symbol_map`, sorted by address), and `symbols`
reads that map rather than the executable's own format.

## The test runner

`make -f Makefile.native check` runs expansion's own hydra
(`tools/mgba-rom-test-hydra`), unchanged, over the `TEST=1` executable;
`platform/test_runner/native-rom-test` stands in for `mgba-rom-test`. The
executable started with `--test-runner` talks the runner's protocol through
`debug_channel` (mGBA's debug registers, plain memory in the 0x04FFF000
page, whose send function prints to stdout), survives the runner's crashes
through `crash_reboot` (a re-exec that carries IWRAM's persistent bytes,
as the GBA's reboot kept them) and does not draw unless a replay or a
screenshot needs the frames. The battle tests' functions run on the
platform stack at 0x01800000 (`game_hooks`). The details are in
`platform/README.md`, "The test runner", and the per-test comparison with
the GBA baseline in `platform/test_runner/SUITE-LOG.md`.

## The replay and its facts

The gates that play the game are scripts (`tests/smoke/*.replay`) fed
through `input`'s script source. `replay` parses and plays a script one
step per frame (`frame_set_step_hook`); `inspect` reads named facts
(`callback2`, `map_name`, `party_species:N`, ...) from expansion's
variables, so a script can `expect` and `wait-until` on game state;
`symbols` names the game's static callbacks from the executable's symbol
map (`pokeemerald-native.symbols`, written by the link); `restart`
re-executes the process, which is what a switch-off and
switch-on does, so the save file is shown to carry the state. `--rtc-fixed`
(`fixed_clock`) makes the clock deterministic. The facts with arguments are
range-checked against expansion's constants (`inspect_args`). Scripts,
expectations and how to update them: `tests/smoke/README.md`; the grammar:
`platform/replay.h`; the facts: `platform/inspect.h`.

## The asset pipeline and palette padding

`Makefile.native` reuses expansion's rule fragments and tools (`gbagfx`,
`preproc`, `scaninc`, `mapjson`) rather than calling its `Makefile`; the
header comment of `Makefile.native` says what is mirrored and what must be
kept in sync. The graphics converted from `INCGFX` go to
`build/assets-native/`, not to the GBA build's `build/assets/`, because the
palettes differ: `platform/build_glue/gbagfx_pad.sh` zero-pads every
`.gbapal` it writes there to whole 32-byte banks, since the game copies
whole banks (`LoadSpritePalette`) and the GBA read the next bytes of ROM.
Palettes whose size the game takes (`sizeof`, `ARRAY_COUNT`, a sized
initialiser) must not be padded: `platform/build_glue/unpadded_palettes.txt`
lists them, and `check_palette_padding.py` verifies the list on every link.
The GBA build and its ROM are untouched. `DECISIONS.md`, "Short palettes are
padded at build time", has the reasoning.

## The save container

The cartridge's flash is the 128 KiB `SRAM` region; `flash` is the chip's
driver entry in expansion's own table, and `save_file` persists the region
as `pokeemerald.sav`: a 28-byte header (`PENSAVE\0`, format version, the
writing build's pointer width and layout id, image size, CRC-32) and the
image, written to a temporary file, synced and renamed. A file written by a
build with another layout is refused, not overwritten. A bare 128 KiB image
loads as legacy. The frame loop stores the file whenever the flash was
written. The field table is in `platform/README.md`, "The flash chip and the
save file"; the decisions are "Native structs follow the host ABI" and "The
native save file is versioned" in `DECISIONS.md`.

## The 64-bit build

`make -f Makefile.native native BITS=64` builds the same game with 8-byte
pointers (`docs/BUILDING.md`). What differs from 32 bits, and where it lives:

- **The memory model is the same.** The regions sit at their GBA addresses
  below 4 GiB, the executable is linked at 0x40000000 and script pointers carry
  the tag bit (`script_tag.h`), so a GBA address is still the native address of
  the same byte and every `uintptr_t` conversion in `platform/` holds at both
  widths. Only 4-byte values that name addresses (bytecode, below) rely on the
  data being below 4 GiB.
- **Assembled data is emitted in the x86-64 layout.** C reads maps, events,
  voices, songs, cries and the script and special tables through structs whose
  pointers are 8 bytes, so the macros that emit those words write `.8byte` and
  the structs' padding under `.ifdef NATIVE_POINTER_SIZE_8` (`asm/macros/m4a.inc`,
  `music_voice.inc`, `map.inc`, `data/specials.inc`, `data/script_cmd_table.inc`,
  `sound/*.inc`), with upstream's text in the `.else`. `Makefile.native` defines
  the symbol with `--defsym` for `BITS=64` only. Words no macro emits (mapjson's
  output, mid2agb's song headers, seven hand-written tables) are widened by
  `platform/build_glue/asm_filter.py`'s `--pointer-size 8` rules, which only the
  64-bit recipes pass. Address words inside bytecode (event scripts, song tracks, animation
  scripts) stay 4 bytes: they are read byte by byte, and every target is below
  4 GiB.
- **Three checks run before the 64-bit link** (`Makefile.native`, the targets
  `data_relocations.checked` and `battle_script_operands.checked`), each with a
  `--self-test` that kills its mutations:
  `check_relocations.py` classifies each symbol that holds an address word
  with `data_address_words.txt` (pointer, table, packed, operands, tone,
  bytecode) and fails on an unlisted symbol, a word of the wrong width or
  alignment, or a count that differs from a reference assembled with 4-byte
  pointers (`build/native64/pointer4`), so a data table upstream adds cannot be
  left at 4 bytes unnoticed; `check_data_layout.py` compares every byte
  and padding byte of the structs the data holds with that reference and with
  the layout of the C struct at each width, which `data_layout_probe.c` prints
  compiled with the game's flags; `check_battle_script_operands.py` compares
  each battle script macro's operand widths with the `CMD_ARGS` struct that
  `src/battle_script_commands.c` reads them through, at both pointer sizes,
  and the `setzeffect` length of `src/battle_z_move.c`.
- **Battle scripts' pointer operands** (jump targets, data pointers, a
  `callnative`'s function) are read through packed structs laid over the
  bytecode, so they are 8 bytes at 64 bits: `asm/macros/battle_script.inc`.
- **Expansion is guarded for width in a few places** (the rows marked 64-bit
  in `UPSTREAM_MODIFICATIONS.md`): `INTR_VECTOR` as a 32-bit store, alignment
  of the heap, the save blocks, task data and the RFU buffers to 8 bytes, the
  structs that grow (`ListMenu` in a task's data, `RecordedBattleSave`), the
  Union Room task data, `test/save.c`'s size. The convention: the width test
  is `#if __SIZEOF_POINTER__ == 8` nested inside `#if PLATFORM_NATIVE` (in
  assembly, `.ifdef NATIVE_POINTER_SIZE_8`); the 32-bit text keeps its lines
  (it comes first, with its `#line`) and the 64-bit text follows in the `#else`,
  so the 32-bit executables do not change.
- **SIGFPE.** `arm_division` decodes the faulting division at both widths (i386
  and x86-64, REX.W included) and gives a division by zero the result the GBA's
  libgcc gives. `platform/arm_division_libgcc/` is the record of those results,
  rebuilt under qemu-arm and compared by hand.
- **The test table.** At 64 bits GCC would space the `.tests` entries 64 bytes
  apart; the test objects are built with `-malign-data=abi` so they are 40 bytes,
  and `crt0_init` refuses a table that is not a whole number of
  `gPlatformTestEntrySize` entries (`test/test_runner.c` defines the size; the
  start-up line says how many tests of what size). `tools/patchelf` and the
  hydra read ELF64.
- **Saves.** Only `SaveBlock1`'s layout differs between widths (its object event
  templates and Enigma Berry record hold pointers). `save_convert` reads the
  other width's save when it loads, through the layout tables
  `save_layout32.h` and `save_layout64.h`, each pinned by static assertions in
  its own build. Gate (h), `tests/smoke/run_cross_width.sh`, plays a save from
  one width to the other and back (`tests/smoke/README.md`).
- **Sanitizers.** The suite under AddressSanitizer and UndefinedBehaviorSanitizer
  reports the same accesses test by test at both widths (compare with
  `ASAN_OPTIONS=suppress_equal_pcs=0`); no report is new at 64 bits.
  `platform/test_runner/SUITE-LOG.md`, "The sanitized run at 64 bits".

## Known divergences from the GBA

Each is a decision in `DECISIONS.md`; none changes the GBA build.

- Structs follow the host ABI, not `-mabi=apcs-gnu`: native save blocks have
  other sizes, `test/save.c` checks the native ones, and GBA `.sav`
  interchange is not supported ("Native structs follow the host ABI").
- The executable is linked at 0x40000000, not in a ROM window; the two
  places that assume ROM addresses are guarded, and tagged script pointers
  use bit 25 (`script_tag.h`; "The link base stays at 0x40000000").
- `VCOUNT` reads 161 between frames, and lines 162 to 227 are not run one by
  one (see "The frame").
- Game code takes no emulated time: busy waits on timers or `VCOUNT` are
  replaced by `PlatformWaitCycles`, loops that wait for the V-Blank handler
  yield a frame, and the nine `BENCHMARK` tests are assumption failures
  ("Benchmarks are assumption failures natively", "Setup loops that spin on
  the DMA3 queue yield a frame").
- Where the GBA reads or writes memory that does not exist natively
  (NULL, past a table), the guarded change does what the GBA ends up doing,
  or what a correct program would where that is unknowable ("Native fixes do
  what the GBA ends up doing"). The platform does not map page zero.
- The trainer party seed does not hash the party table's address ("The
  trainer party seed does not hash the party table's address").
- Short palettes are padded in the native asset directory (above).
- Expansion's sources are compiled with `-funsigned-char`; the sanitized
  build disables UBSan's `shift-base` for expansion's objects only, and uses
  outline AddressSanitizer checks for the `TEST=1` build.
- Not simulated: time within a line, lines 162 to 227 one by one, and the
  GameCube multiboot (the copyright screen takes its "no multiboot" path).

## Conventions that the tests enforce

- Fixed-width types for anything that models hardware.
- Every buffer access bounds-checked against the named constant for its
  region; DMA and save reject out-of-range source, destination and length.
- Static assertions for every struct whose layout must match a hardware
  or save format, with the endianness and alignment assumptions written
  next to them.
- No pointer stored in an integer; `uintptr_t` where unavoidable, with a
  comment saying why (GBA addresses are native addresses).
- Every SDL and C library return value checked; failure messages name
  the module and the call.

## Build

`Makefile.native` builds the native executable from expansion's sources
plus `platform/` (`make -f Makefile.native native`, with `TEST=1` for the
test runner build and `SANITIZE=1` for the sanitized one; `check` runs
expansion's suite on it), linked non-PIE
at 0x40000000 with `platform/gba_regions.ld` and `platform/gba_game.ld`. `platform/Makefile` builds the modules and their tests
standalone under AddressSanitizer and UndefinedBehaviorSanitizer with
`-std=gnu17 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion
-Werror`; expansion's headers are included with `-isystem` (`-idirafter` where
`-isystem` would shadow libc headers) so their warnings are not ours. See `BUILDING.md`.
