# platform/

The platform layer: C that stands in for the Game Boy Advance hardware so
expansion's game code runs natively. See `docs/ARCHITECTURE.md` for the
design and `docs/C-RULES.md` for the rules every file here follows.

## Building and testing

Run from the repository root; nothing here needs expansion's build.

```sh
make -C platform test            # build and run every unit test, 32-bit (the default)
make -C platform test BITS=64    # the same, 64-bit
make -C platform lib             # build/<bits>/libplatform.a from every platform/*.c
make -C platform clean           # remove platform/build/
```

Every object is compiled with `-std=gnu17 -Wall -Wextra -Wshadow
-Wstrict-prototypes -Wconversion -Werror -O1 -g` and AddressSanitizer,
LeakSanitizer and UndefinedBehaviorSanitizer. `make test` runs every test
binary, prints its output, and fails if any binary exits non-zero or prints
a sanitizer report. The 32-bit build needs the i386 C library and sanitizer
runtimes (`gcc-multilib`). The tests run on Linux only for now: the region
layout needs GNU ld and a non-PIE ELF executable (see below).

Every test binary is linked with `gba_regions.ld` and `-no-pie
-Wl,-Ttext-segment=0x40000000`, so the GBA memory regions exist in it (see
below). A native executable that uses the platform layer must be linked the
same way.

## Adding a module and its tests

- A module is `platform/<name>.c` with its header `platform/<name>.h`. It is
  picked up by `make lib` with no Makefile edit.
- Its tests are `platform/test/test_<name>.c`, one translation unit per test
  binary. The binary `build/<bits>/test_<name>` is linked with `<name>.c`
  (if it exists) and with every file named on a deps line near the top of the
  test, paths relative to `platform/`:

  ```c
  // deps: gba_memmap.c interrupt.c
  ```

  `// # deps:` is accepted too. A deps entry that does not exist stops the
  build with an error naming the test.
- To test a module's `static` functions without widening its header, a test
  can `#include "<name>.c"` as long as its own name matches no module, so the
  module is not linked a second time (`test/test_gba_memmap_layout.c`).

## ptest.h

`platform/test/ptest.h` is a header-only test framework, about two hundred
lines with no dependency beyond the C library (why in-house: `docs/RESEARCH.md`, "Test framework
for the platform layer").

```c
#include "ptest.h"
#include "gba_memmap.h"

PTEST(vram_is_a_region)
{
    PT_ASSERT_EQ_INT(gba_memmap_region_of(GBA_VRAM_BASE, 2), GBA_REGION_VRAM);
}

PTEST_MAIN()
```

| Macro | Fails when | Failure line shows |
|---|---|---|
| `PTEST(name) { ... }` | (defines and registers a test) | |
| `PT_ASSERT(cond)` | `cond` is false | the expression |
| `PT_ASSERT_EQ_U32(actual, expected)` | values differ as `uint32_t` | both, in hex |
| `PT_ASSERT_EQ_INT(actual, expected)` | values differ as `intmax_t` | both, in decimal |
| `PT_ASSERT_EQ_PTR(actual, expected)` | pointers differ | both, as `%p` |
| `PT_ASSERT_EQ_STR(actual, expected)` | strings differ (two NULLs are equal) | both strings |
| `PT_ASSERT_MEM_EQ(actual, expected, nbytes)` | bytes differ | the first differing offset and both bytes |
| `PT_FAIL(message)` | always | the message |
| `PTEST_MAIN()` | (defines `main`) | |

A failing assertion records `file:line: test: expression: values`, cut to
`PTEST_MESSAGE_SIZE` (512) bytes, marks the test failed and returns from the
enclosing function. A test reports its first failure: an assertion in a
`void` helper returns from the helper, and later failures in the same test
do not replace the first. Each argument is evaluated once. Restore any shared state (such as bytes written into a GBA region)
before asserting, so an early return leaves nothing behind. The runner prints
one `FAIL` line per failed test and `N passed, M failed`, and exits 0 only if
every test passed; a binary with no tests fails, and so does one that
registers more than `PTEST_MAX_TESTS` (256), with a message naming the limit.
There is no skip and no filter. Arguments must already have the assertion's type: `-Wconversion`
rejects, for example, a `size_t` passed to `PT_ASSERT_EQ_U32` on 64-bit, so
compare sizes and addresses with `PT_ASSERT(a == b)`.

`ptest_capture(fn, context)` runs `fn(context)` and returns its failure
(`struct ptest_outcome`) instead of failing the running test, and
`ptest_register_in(registry, name, fn)` and `ptest_main_for(registry, out)`
do what `PTEST` and `PTEST_MAIN` do on a registry the caller owns. They are
for testing the framework itself (`test/test_ptest.c`).

## The GBA memory map (`gba_memmap`)

The regions exist at their GBA addresses in every executable linked with
`gba_regions.ld`: the linker makes each a zero-filled, writable section and
the kernel maps it at load time. `gba_memmap_init()` checks the link put
each at its base, exactly its size in whole 4 KiB pages long, and does not
touch the memory; `gba_memmap_contains()` and
`gba_memmap_region_of()` are the bounds checks other modules use before
touching a range a game-supplied address names. Why a linker script rather
than `mmap` at run time: the header comment of `gba_memmap.c` and
`docs/DECISIONS.md`.

| Region | `enum gba_region` | Base | Size | Mapped as | Source |
|---|---|---|---|---|---|
| EWRAM | `GBA_REGION_EWRAM` | 0x02000000 | 256 KiB | 256 KiB; expansion's `EWRAM_DATA` (`.sbss`) at its bottom | GBATEK "GBA Memory Map" |
| IWRAM | `GBA_REGION_IWRAM` | 0x03000000 | 32 KiB | 32 KiB | GBATEK "GBA Memory Map" |
| I/O registers | `GBA_REGION_IO` | 0x04000000 | 1 KiB | one 4 KiB page | GBATEK "GBA Memory Map" |
| mGBA debug registers | `GBA_REGION_MGBA_DEBUG` | 0x04FFF000 | 4 KiB | one 4 KiB page | mGBA `src/gba/io.c`, `docs/research/test-runner-protocol.md` |
| BG/OBJ palette RAM | `GBA_REGION_PALETTE_RAM` | 0x05000000 | 1 KiB | one 4 KiB page | GBATEK "GBA Memory Map" |
| VRAM | `GBA_REGION_VRAM` | 0x06000000 | 96 KiB | 96 KiB | GBATEK "GBA Memory Map" |
| OAM | `GBA_REGION_OAM` | 0x07000000 | 1 KiB | one 4 KiB page | GBATEK "GBA Memory Map" |
| Game Pak SRAM / flash | `GBA_REGION_SRAM` | 0x0E000000 | 128 KiB | 128 KiB | GBATEK "GBA Memory Map" (64 KiB window), "GBA Cart Backup Flash ROM" (128 KiB chip) |

Sizes are what the hardware decodes, and are what `contains()` checks
against; the 4 KiB pages are only the native mapping granularity. The
executable itself starts at 0x40000000, above every region.

## The LCD controller's drawing (`ppu`)

`ppu.h` draws one visible line at a time from the mapped regions, as the
GBA's LCD controller does (GBATEK "LCD Video Controller"): BG modes 0 and 1
(the only ones the game sets; modes 2-7 return `PPU_ERR_UNSUPPORTED_MODE`
and a black line), text and rotation/scaling backgrounds, the 128 OBJs
within the per-line OBJ cycle budget, WIN0, WIN1, the OBJ window, alpha
blending, brightness and mosaic. Forced blank draws white.

| Call | When the frame loop makes it |
|---|---|
| `ppu_init()` / `ppu_shutdown()` | at start-up and exit |
| `ppu_render_line(line, out)` | for lines 0 to 159, in order, once each; HBlank DMA and the HBlank and VCount handlers run between calls |
| `ppu_latch_vblank()` | at the start of VBlank: copies BG2X/BG2Y/BG3X/BG3Y to the internal reference points |

`ppu_render_line` writes `out` (BGR555, bit 15 clear) and nothing else: VCOUNT
and the DISPSTAT flags belong to the frame loop. A mid-frame write to a
reference point register is noticed by its value changing (register writes
are plain stores), so a mid-frame write of the value already there is not.

The module is split by hardware unit: `ppu.c` (registers, composition,
reference points), `ppu_bg.c`, `ppu_obj.c`, `ppu_window.c`, `ppu_blend.c`;
`ppu_internal.h` is what they share, including the bounds-checked readers of
VRAM, palette RAM, OAM and I/O. Each file's header comment lists where it
interprets GBATEK. The tests (`test/test_ppu*.c`) build tiny scenes in the
regions with `test/ppu_scene.h`; an all-zero OAM entry is a displayed 8x8 OBJ
at (0,0), as on hardware, so scenes hide every OBJ first.
## DMA (`dma`)

`dma_set()` and `dma_stop()`, declared in `dma_hooks.h`, are what expansion's
`DmaSetUnchecked` and `DmaStop` become under `PLATFORM_NATIVE`
(`include/gba/macro.h`), so every `DmaSet`, `DmaCopy*`, `DmaFill*` and
`DmaClear*` reaches them; macro.h includes the header as
`"platform/dma_hooks.h"`, so a build compiling the game puts the repository
root on its quote include path (`platform/Makefile` uses `-iquote ..`).
`main` calls `dma_init(interrupt_raise)`; the frame loop calls
`dma_run_timing(DMA_TIMING_HBLANK)` between scanlines and
`dma_run_timing(DMA_TIMING_VBLANK)` on entering VBlank (GBATEK "GBA DMA
Transfers"). Every block is checked whole with `transfer_bounds_ok()` before
a byte moves; a refused one prints to stderr, returns a negative
`enum dma_error` (the macros discard it) and touches no memory. What is
relaxed (alignment, address masks), what is not emulated ("special" start
timing, Game Pak DRQ), and why: `dma.h`.

`test_dma_macro.c` compiles expansion's `macro.h` as the native build does
and checks that the game's own macros reach the module.
## The interrupt controller (`interrupt`)

`interrupt.h` stands in for the CPU's IRQ line, IE, IF and IME (GBATEK "GBA
Interrupt Control") and expansion's IRQ handler `IntrMain` (`src/crt0.s`).
The sources are `enum gba_irq` in `gba_irq.h`, valued by their IE/IF bit.
`interrupt_init(gIntrTable, 14)` keeps a pointer to the game's table (the game
fills it later); `interrupt_raise()` latches a request in IF;
`interrupt_dispatch()` runs the game's handlers synchronously, as `IntrMain`
would, until nothing enabled is pending. The header comment of `interrupt.h`
walks through `IntrMain` line by line.

| `IntrMain` (src/crt0.s) | Here |
|---|---|
| sources searched VCount, Serial, Timer 3, HBlank, VBlank, Timer 0-2, DMA 0-3, Keypad (53-93); the one at position i runs `gIntrTable[i]` | same order and slots |
| IME 0 during the VCount handler, 1 during the others (49-57) | same |
| the bit written to IF before the handler (98); `main.c`'s handlers never write IF | same |
| IE and IME restored after the handler, undoing its writes to them (126-127) | same |
| Game Pak: zero byte to SOUNDCNT_X, spin forever (94-96) | same, then `interrupt_halted()` is true and nothing more runs |
| IE narrowed to the nestable sources while a handler runs (99-108) | not done: there is no nesting; a request raised by a handler runs after it returns |

IF is write-1-to-clear on the GBA but a plain store here, so the module keeps
the requests and stores them at IF with bit 15 (`INTERRUPT_IF_MARKER`, unused
by the hardware) set; a value at IF that differs from what it stored is a game
write, and its 1 bits are acknowledged. The game only ever writes
`INTR_FLAG_*` bits, so every write is seen. A raw read of IF shows bit 15; of
several game writes between two calls into the module only the last is seen
(the header lists the call sites). `INTERRUPT_DISPATCH_CAP` (256) stops a call
whose handler re-raises itself forever, with a report on stderr.

## The keypad (`input`)

`input_update_keyinput()` writes `REG_KEYINPUT` (0x04000130, GBATEK "GBA
Keypad Input") once per frame from the host (SDL keyboard and game
controllers) or, for the headless replay, from `input_script_set()`.
`main` initialises SDL, pumps events, passes each event to
`input_handle_event()` (controller hot-plug), then calls
`input_update_keyinput()`. Bits 10-15 read 0; opposite directions are
reported as pressed, unfiltered.

| GBA key | Keyboard (SDL scancode) | Controller |
|---|---|---|
| A | Z | A |
| B | X | B |
| Select | Backspace | Back |
| Start | Return | Start |
| L | A | left shoulder |
| R | S | right shoulder |
| Up/Down/Left/Right | arrow keys | D-pad, or left stick beyond `INPUT_STICK_DEADZONE` (16000) |

`test_input.c` sets `SDL_VIDEODRIVER=dummy` itself. SDL's dummy driver cannot
put a key into `SDL_GetKeyboardState` (a pushed `SDL_KEYDOWN` is only queued),
so key translation is tested through the pure `input_keyboard_to_keys` and
`input_pad_to_keys`; controllers are tested with SDL virtual joysticks.
