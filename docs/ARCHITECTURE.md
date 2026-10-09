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
`VBlankIntrWait` (the `bios` module) is the frame boundary: until
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
6. Present the frame; poll SDL events; write `REG_KEYINPUT`.
7. Store the save file if the game wrote the flash chip.
8. Pace to the GBA frame period.
9. Count the frame; a frame limit or a quit ends the game after it (main
   unwinds to its exit target).

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
| `interrupt` | `IntrMain` and the IE/IF/IME logic | GBA Interrupt Control | yes |
| `timer` | Timers 0 to 3 | GBA Timers | yes |
| `rtc` | the S-3511A clock chip | GBA Cart Real-Time Clock | yes |
| `dma` | DMA channels 0 to 3 | GBA DMA Transfers | yes |
| `bios` | the BIOS SWI functions | BIOS Functions | yes |
| `input` | the keypad | GBA Keypad Input | yes (mapping, replay source) |
| `ppu` | the LCD controller, drawing | LCD Video Controller | yes (synthetic scenes in the regions) |
| `video` | the LCD panel, via SDL | n/a | no |
| `psg` | the four Game Boy sound channels | GBA Sound Controller, Sound Channels 1 to 4 | yes |
| `audio` | the speaker, via SDL | n/a | no |
| `m4a/` | the ARM sound engine `src/m4a_1.s`, in C | n/a (game code) | partly |
| `flash` | the 1 Mbit flash chip's driver entry | GBA Cart Backup Flash ROM | yes |
| `save_file` | the cartridge's persistence | n/a | yes |
| `debug_channel` | mGBA's debug registers | mGBA `src/gba/io.c` | yes |
| `crash_reboot` | the reboot after a crash, keeping IWRAM's last 0x200 bytes (test runner mode) | BIOS Reset Functions | yes (real crashes and re-execs) |
| `arm_division` | the GBA's software division (libgcc), which returns instead of trapping on a zero divisor | (libgcc, the Intel SDM for decoding) | yes |
| `lcd_status` | the LCD controller's timing registers, `VCOUNT` and `DISPSTAT` | LCD I/O Interrupts and Status | yes |
| `m4a_output` | the sound FIFO DMAs, reading the m4a engine's PCM ring | GBA Sound Channel A and B - DMA Sound | yes |
| `sound_mix` | the sound mixer and output capacitor | GBA Sound Control Registers | yes |
| `frame` | a frame of hardware time, in order (above) | LCD Dimensions and Timings | yes (headless, with the real modules) |
| `game_hooks` | the game's control-flow assembly (test runner jumps, the battle runner's stack, delay loops) | n/a (game code) | yes |
| `crt0`, `rom_header`, `gcn_multiboot`, `decompress_asm` | the `src/*.s` the native build does not assemble | n/a (game code) | yes |
| `main` | power-on | n/a | no |

Dependencies point one way: `main` depends on everything; `frame` on the
modules a frame drives; `ppu`, `dma`, `timer`, `interrupt`, `psg`, `flash`
depend on `gba_memmap` for addresses and bounds; `bios` depends on
`gba_memmap` and `interrupt`; nothing but `frame` and `main` depends on
`video` or `audio`, and nothing on `main`. The modules do not include
expansion's headers; the exceptions are `gba_memmap.h`, which may include
`gba/defines.h` and `gba/io_reg.h` as system headers to reuse their names
and assert ours match, and `m4a_output.c`, which reads the m4a engine's
`struct SoundInfo` (`gba/m4a_internal.h`) as the DMA did.

## Conventions that the tests enforce

- Fixed-width types for anything that models hardware.
- Every buffer access bounds-checked against the named constant for its
  region; DMA and save reject out-of-range source, destination and length.
- Static assertions for every struct whose layout must match a hardware
  or save format, with the endianness and alignment assumptions written
  next to them.
- No pointer stored in an integer; `uintptr_t` where unavoidable, with a
  comment flagging it for the 64-bit milestone.
- Every SDL and C library return value checked; failure messages name
  the module and the call.

## Build

`Makefile.native` builds the native executable from expansion's sources
plus `platform/` (`make -f Makefile.native native`, with `TEST=1` for the
test runner build and `SANITIZE=1` for the sanitized one), linked non-PIE
at 0x40000000 with `platform/gba_regions.ld` and `platform/gba_game.ld`. `platform/Makefile` builds the modules and their tests
standalone under AddressSanitizer and UndefinedBehaviorSanitizer with
`-std=gnu17 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion
-Werror`; expansion's headers are included with `-isystem` so their
warnings are not ours. See `BUILDING.md`.
