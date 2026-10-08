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

The executable is linked at 0x40000000 so it never overlaps a region.
Register writes are plain stores: the layer polls register state at
scanline and frame boundaries and the few writes that must act at once
(DMA enable, the sound engine's `swi 0x2A`, the test runner's debug
send) are routed through guarded calls.

## The frame

The game runs on the main thread, unmodified: `AgbMain()` loops over
`MainLoop()` and calls `VBlankIntrWait()` once per frame. The platform's
`VBlankIntrWait` is the frame boundary and does, in this order:

1. Render lines 0 to 159 into the frame buffer. Between lines: run
   HBLANK-timed DMA, raise HBlank, raise VCount when the line matches
   `DISPSTAT`, dispatch.
2. Advance the four timers by one frame of cycles; raise their overflow
   interrupts; dispatch.
3. Enter VBlank: set the flag in `DISPSTAT`, run VBLANK-timed DMA, raise
   VBlank, dispatch (this runs expansion's `VBlankIntr`, which runs the
   sound engine and the DMA3 queue).
4. Mix one frame of audio and queue it.
5. Present the frame; poll SDL events; write `REG_KEYINPUT`.
6. Pace to the GBA frame period.
7. Return when `INTR_CHECK` shows the VBlank flag the handler set, as
   the BIOS does.

`REG_VCOUNT` reads 160 while the game runs, so expansion's `SetGpuReg`
writes through, as during VBlank on hardware.

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
| `ppu` | the LCD controller, drawing | LCD Video Controller | partly (pure helpers) |
| `video` | the LCD panel, via SDL | n/a | no |
| `psg` | the four Game Boy sound channels | GBA Sound Controller, Sound Channels 1 to 4 | yes |
| `audio` | the speaker, via SDL | n/a | no |
| `m4a/` | the ARM sound engine `src/m4a_1.s`, in C | n/a (game code) | partly |
| `flash` | the 1 Mbit flash chip's driver entry | GBA Cart Backup Flash ROM | yes |
| `save_file` | the cartridge's persistence | n/a | yes |
| `debug_channel` | mGBA's debug registers | mGBA `src/gba/io.c` | yes |
| `main` | power-on | n/a | no |

Dependencies point one way: `main` depends on everything; `ppu`, `dma`,
`timer`, `interrupt`, `psg`, `flash` depend on `gba_memmap` for addresses
and bounds; `bios` depends on `gba_memmap` and `interrupt`; nothing
depends on `video`, `audio` or `main`. The modules do not include
expansion's headers; the one exception is `gba_memmap.h`, which may
include `gba/defines.h` and `gba/io_reg.h` as system headers to reuse
their names and assert ours match.

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
plus `platform/`. `platform/Makefile` builds the modules and their tests
standalone under AddressSanitizer and UndefinedBehaviorSanitizer with
`-std=gnu17 -Wall -Wextra -Wshadow -Wstrict-prototypes -Wconversion
-Werror`; expansion's headers are included with `-isystem` so their
warnings are not ours. See `BUILDING.md`.
