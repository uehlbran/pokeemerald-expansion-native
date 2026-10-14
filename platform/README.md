# platform/

The platform layer: C that stands in for the Game Boy Advance hardware so
expansion's game code runs natively. See `docs/ARCHITECTURE.md` for the
design and `docs/C-RULES.md` for the rules every file here follows.

## Building and testing

Run from the repository root; nothing here needs expansion's build.

```sh
make -C platform test            # build and run every unit test, 32-bit (the default)
make -C platform test BITS=64    # the same, 64-bit
make -C platform tsan            # the same under ThreadSanitizer (64-bit, build/64-tsan)
make -C platform lib             # build/<bits>/libplatform.a from every platform/*.c
make -C platform lib NOSAN=1     # the same without sanitizers, build/<bits>-nosan/ (lib only)
make -C platform clean           # remove platform/build/
```

Every object is compiled with `-std=gnu17 -Wall -Wextra -Wshadow
-Wstrict-prototypes -Wconversion -Werror -O1 -g` and AddressSanitizer,
LeakSanitizer and UndefinedBehaviorSanitizer. `make test` runs every test
binary, prints its output, and fails if any binary exits non-zero, prints
a sanitizer report, or ends without ptest's `N passed, 0 failed` line (a
binary that exits early passes no test). The 32-bit build needs the i386 C library and sanitizer
runtimes (`gcc-multilib`). The tests run on Linux only for now: the region
layout needs GNU ld and a non-PIE ELF executable (see below), and `HOST_OS`
(`host.mk`, default `uname -s`) accepts `Linux` only (the host layer,
below). `make lib TARGET_OS=windows BITS=64` cross-builds the library for
Windows with MinGW-w64 (`target.mk`; `docs/BUILDING.md`, "Windows (cross-build
from Linux)"); it is the only goal that accepts `TARGET_OS=windows`.

Every test binary is linked with `gba_regions.ld`, `gba_game.ld` and
`-no-pie -Wl,-Ttext-segment=0x40000000`, so the GBA memory regions exist in
it (see below). A native executable that uses the platform layer must be
linked the same way; `Makefile.native` does (next section).

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
  build with an error naming the test. Every test is also linked with the
  host layer's sources for `HOST_OS` (below), which any module may reach.
- A test that needs something only some operating systems have goes in the
  directory of that OS, never behind an `#ifdef` (next section).
- To test a module's `static` functions without widening its header, a test
  can `#include "<name>.c"` as long as its own name matches no module, so the
  module is not linked a second time (`test/test_gba_memmap_layout.c`).

## Tests that need an operating system

A test file has a portable part and, where a test needs `fork`, a signal, a
pipe, a file permission or a Linux-only fact, a per-OS part in another file.
`platform/test/test_<name>.c` holds the tests every target runs and calls the
OS only through `test/test_support.h` (scratch directories and files under the
temporary directory, file queries, the environment, standard error sent
elsewhere: `tsup_*`). The tests that need more are in a directory named for
the OS family, as `test/<dir>/test_<name>_<dir>.c`, a binary of its own
(`build/<bits>/test_<name>_<dir>`) with the same `// deps:` rules, built only
when `HOST_OS` (`host.mk`) lists the directory in `HOST_TEST_DIRS`:

| Directory | Built for | Holds |
|---|---|---|
| `test/posix/` | Linux, macOS | `fork` and `waitpid`, signals, pipes, `setrlimit`, permissions, the descriptor syncs; `posix/support.c` implements `test_support.h`, and `posix/posix_support.h` adds `psup_run_child` (a forked child with no core file and an alarm), `psup_exit_code`, `psup_terminating_signal` and `psup_read_within` (a pipe read that gives up) |
| `test/linux/` | Linux | what only Linux and glibc say of the process: `pthread_getattr_np`, the linker's `__executable_start` |
| `test/windows/` | Windows (cross-built on Linux, run on Windows) | where faults go (the vectored handler, the crash filter, a stack overflow), the crash reboot's report and exit code for the parent that starts the program again, the restart through a waited child, the stack switch's limits, registers and dispatch, the binary and uninherited files, the UTF-8 code page the manifest gives (argv, a save through a non-ASCII path); `windows/support.c` implements `test_support.h`, and `windows/windows_support.h` adds `wsup_run_child` (this executable again as a child that runs one named body, with a time limit), `wsup_child_name` and `wsup_end` |

A test has one place: its name is in one file, the portable part when it can
run everywhere. A fixture two parts share is a header beside them
(`save_file_fixture.h`, `host_file_fixture.h`, `debug_channel_fixture.h`,
`crash_reboot_fixture.h`, `host_fault_fixture.h`). A port adds a directory
(`test/windows/`, `test/darwin/`), its `support.c`, and its values in
`host.mk`; it edits no existing test. A port that lacks a test's OS part runs
the portable part only, so the binary count is lower there, not a skipped
test. A child is forked by `psup_run_child` (`posix/posix_child.h`), which stops the
time limit's watchdog thread (`ptest_pause_watchdog`, a join) before the fork
and starts it again after, in the parent: the child inherits no thread and no
handle to one.

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

Each test has a wall-clock limit, 60 seconds or `PTEST_TIMEOUT_SECONDS`, kept
by `host_thread.h`'s watchdog: a thread of its own that only sets an atomic
flag, which the test's thread reads in every assertion and when the test
returns. A test past its limit fails with `timed out after N seconds` and the
next test runs. A test that reaches no assertion after the limit is not
interrupted (nothing but its own thread may touch its state), so a loop in a
test needs an assertion and a blocking call a timeout of its own;
`ptest_set_timeout(seconds)` gives a legitimately long test more time. Why a
thread, and who owns what: `docs/DECISIONS.md`, "The test time limit is kept
by a flag-setting thread". A test may use `alarm(2)` itself: the runner takes
no signal.

`ptest_capture(fn, context)` runs `fn(context)` and returns its failure
(`struct ptest_outcome`) instead of failing the running test, and
`ptest_register_in(registry, name, fn)` and `ptest_main_for(registry, out)`
do what `PTEST` and `PTEST_MAIN` do on a registry the caller owns. They are
for testing the framework itself (`test/test_ptest.c`, and
`test/test_ptest_time_limit.c` for the limit).

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
executable itself starts at 0x40000000, above every region. At 0x01800000
(GBATEK's unused area), `gba_regions.ld` also places the 1 MiB stack
`game_hooks` runs the battle test runner's functions on, low enough for the
runner's 27-bit address fields; on Windows `gba_regions.pe.ld` puts it at
0x01801000, since the image's headers take 0x01800000, and the regions are
sections of the image (`docs/ARCHITECTURE.md`, "The memory model").

## The host layer (`host_*`)

What the modules need from the operating system, one header per concern, so
that no module makes an OS call of its own (`docs/ARCHITECTURE.md`, "The
host layer", has the design and the users). The sources are chosen by
`TARGET_OS` (`target.mk`, through `host.mk`, included by this Makefile,
`m4a/Makefile` and `Makefile.native`). Linux: `host_posix.c`, what Linux and
macOS share, and `host_linux.c`, the rest; `host_internal.h` is what they
share. Windows: `host_windows.c` (memory, image, files, process),
`host_windows_fault.c`, `host_windows_stack.c` and `host_windows_thread.c`.
Both: `host_linked.c`, where the linker scripts put the regions. Each header
says what it stands in for and what it wraps, and has its test; the Windows
sources are linted with `make -C platform lint TARGET_OS=windows BITS=64` and
their tests are cross-built (`make -C platform test TARGET_OS=windows
BITS=64`) and run on Windows (`test_runner/run_windows_tests.sh`).

| Header | Gives | Linux (source) | Windows (source) | Test |
|---|---|---|---|---|
| `host_memory.h` | `host_memory_region` and `host_memory_call_stack`: where the link put each GBA region and the battle runner's stack; `host_memory_map_fixed`/`unmap_fixed`: a page at an exact address, never over a mapping | `gba_regions.ld`'s symbols (`host_linked.c`), `mmap` with `MAP_FIXED_NOREPLACE` (`host_linux.c`) | `gba_regions.pe.ld`'s symbols (`host_linked.c`), `VirtualAlloc` at a multiple of 64 KiB, `VirtualFree` of the whole mapping | `test_host_memory.c` |
| `host_fault.h` | the divide-error handler (passes what it does not complete to the one installed before) and the crash handler (on a 64 KiB alternate stack; the fault ends the process when it returns), each with install and uninstall; the fault's number, address, and registers: PC, accumulator and data by name, the general ones by ModRM number | `sigaction`, `sigaltstack`, `sigprocmask`, `raise` (`host_posix.c`); `uc_mcontext.gregs` (`host_linux.c`) | a vectored exception handler called first, for `EXCEPTION_INT_DIVIDE_BY_ZERO` and `_OVERFLOW` only; the unhandled-exception filter; `SetThreadStackGuarantee` for a stack overflow; `SetErrorMode` (no dialogs); the `CONTEXT` by offset (`host_windows_fault.c`) | `test_host_fault.c`, `posix/`, `windows/test_host_fault_windows.c` |
| `host_process.h` | `host_process_executable_path`; `host_process_restart`: this executable again with the same standard handles, and whether a crash handler may call it; `host_process_end`: the process ended at once with a status | `readlink` of `/proc/self/exe` (`host_linux.c`), `execve`, async-signal-safe, so a handler may; `_exit` (`host_posix.c`) | `GetModuleFileNameW` converted to UTF-8; `CreateProcessW` of a child, a wait, `TerminateProcess` with its status, which no handler may call (it allocates); `TerminateProcess` | `test_host_process.c`, `posix/`, `windows/test_host_process_windows.c` |
| `host_image.h` | the executable's image and its writable part, whether an address is in a read-only segment, the main thread's whole stack | `__executable_start`, `__data_start`, `_end`, `getauxval(AT_PHDR)`, `pthread_getattr_np` (`host_linux.c`) | `__text_start__`, `__data_start__`, `__bss_end__` (the writable sections linked last, `gba_game.pe.ld`), `VirtualQuery`, `GetCurrentThreadStackLimits` (2 MiB) | `test_host_image.c`, `linux/` |
| `host_stack.h` | `host_stack_run`: a function on a given stack, at the same depth each call; `host_stack_leave`: back to the caller early | `ucontext`, AddressSanitizer's fiber annotations (`host_linux.c`) | a Win64 switch in assembly with unwind information, the `NT_TIB` stack limits widened over both stacks (`host_windows_stack.c`) | `test_host_stack.c`, `windows/test_host_stack_windows.c` |
| `host_file.h` | `host_file_open` (not inherited; binary on Windows), `host_file_write_all` (every byte) and `host_file_write_from_handler` (the same for a fault handler, errno left alone), `host_file_sync`, `host_file_replace` (atomic; on Windows, on NTFS), `host_file_sync_directory_of`, `host_file_make_directory`, `host_file_local_time`; failures are -1 with `errno`, reported by the caller | `open` with `O_CLOEXEC`, `write`, `fsync`, `rename`, `open` of the directory and `fsync`, `mkdir(path, 0777)`, `localtime_r` with a 64-bit `time_t` (`host_posix.c`) | `_open` with `_O_NOINHERIT` and `_O_BINARY`, `WriteFile` and `FlushFileBuffers` on the descriptor's handle, `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` and `MOVEFILE_WRITE_THROUGH`, no directory sync (the length only), `CreateDirectoryW`, `_localtime64_s` | `test_host_file.c`, `posix/`, `windows/test_host_file_windows.c` |
| `host_thread.h` | the test time limit's watchdog: a thread that stores `true` in an atomic flag once its deadline passes | a `pthread` on a condition variable (`host_posix.c`) | a thread on an event with a timeout, joined by a wait on its handle (`host_windows_thread.c`) | `test_host_thread.c`, `linux/` |

## The flash chip (`flash`) and the save file (`save_file`)

`flash` is the cartridge's 1 Mbit flash chip as an entry of expansion's own
driver table: `NativeFlash`, a `struct FlashSetupInfo`
(`include/gba/flash_internal.h`), whose program and erase functions work on
the 128 KiB SRAM region. The region holds both 64 KiB banks back to back, so
bank switching is a no-op and sector n is at `0x0E000000 + n * 4096`. Under
`PLATFORM_NATIVE`, `src/agb_flash_1m.c` lists `NativeFlash` first and
`src/agb_flash.c` leaves `ReadFlashId`, `ReadFlash` and `VerifyFlashSector`
to this module, so `IdentifyFlash` always selects it. Erase fills with 0xFF;
a byte program that would turn a 0 bit into a 1 is rejected as on hardware;
every sector index, offset and length is bounds-checked. Each program and
erase marks its sector dirty.

`save_file` persists the chip in a versioned `.sav`. Stores go to
`<path>.tmp`, are fsynced, renamed over `<path>`, and the directory is
fsynced (`host_file.h`). A NULL path keeps everything in memory (tests, the test runner).
Format version 1 is a 28-byte header followed by the chip image, every
header integer an unsigned 32-bit little-endian value:

| Offset | Field | Value |
|---|---|---|
| 0 | magic | the 8 bytes `PENSAVE\0` |
| 8 | format version | 1 (`SAVE_FILE_FORMAT_VERSION`) |
| 12 | layout tag: pointer width | `sizeof(void *) * 8` of the build that wrote it, 32 or 64 |
| 16 | layout tag: layout id | that build's `SAVE_FILE_LAYOUT_ID` (1) |
| 20 | image size | 131072 |
| 24 | image CRC | CRC-32/ISO-HDLC of the image (`save_file_crc32`; check value 0xCBF43926) |
| 28 | image | 131072 bytes, byte n of the chip, sector s at 28 + s * 4096 |

A file with the magic loads only if its version is 1, its sizes are exact,
its pointer width is 32 or 64 and its CRC matches; with this build's layout
tag the load returns `SAVE_FILE_OK`, with another (say a 32-bit save in the
64-bit build) `SAVE_FILE_FOREIGN_LAYOUT`, and `save_file_loaded_layout`
reports the tag so main can refuse or convert. A bare 131072-byte image
without the magic, what GBA emulators write, loads as `SAVE_FILE_LEGACY`
(layout unknown) and gets a header on the next store; for an emulator, our
image is the file's last 131072 bytes. Anything else is `SAVE_FILE_CORRUPT`
and leaves an erased chip.

`save_convert` moves a save between the 32-bit and the 64-bit layout, which
differ in `SaveBlock1` only: its 64 object event templates and its Enigma
Berry record hold pointers, so at 64 bits the block is 15768 bytes instead of
15496 and its fourth sector holds 3864 bytes instead of 3592. For each save
slot whose four `SaveBlock1` sectors are intact (signature, and checksums at
the source's chunk sizes), it reassembles the block, moves it member by member
through a segment table (the scripts zeroed, since the game refills them from
the map on Continue; the Enigma Berry's descriptions zero-extended or
truncated; padding zero; the Enigma Berry's checksum recomputed if it held),
and writes it back with `CalculateChecksum`'s sums; any other slot, and every
other sector, is left as it is, so the game's own slot fallback sees what it
would have seen. The offsets are `save_layout32.h` and `save_layout64.h`, each
pinned by static assertions in the build of its own width. Main converts a
`SAVE_FILE_FOREIGN_LAYOUT` file with layout id 1, and, in the 64-bit build, a
`SAVE_FILE_LEGACY` one, which it takes to have the 32-bit layout (its
producers are emulators and the native builds from before the header). The
image is not marked dirty: the file keeps its layout until the game saves.
Gate (h), `tests/smoke/run_cross_width.sh`, plays a save across the widths.

The frame loop must call `save_file_store()` whenever `flash_take_dirty()`
returns true; that is what persists every path that writes flash, including
the link saves and `TryWriteSpecialSaveSector`. Full contract: `flash.h`,
`save_file.h`.

| Constant | Value | Source |
|---|---|---|
| `FLASH_SECTOR_SIZE` | 4096 | GBATEK "GBA Cart Backup Flash ROM", "Erase 4Kbyte Sector" |
| `FLASH_SECTOR_COUNT` | 32 | 128 KiB / 4 KiB; expansion's `MX29L010` entry |
| `FLASH_CHIP_SIZE` | 131072 | GBATEK, "128 KBytes - 1Mbit Flash ROM"; expansion's `FLASH_ROM_SIZE_1M` |
| `FLASH_ERASED_BYTE` | 0xFF | GBATEK, "erased memory is FFh-filled" |
| `FLASH_NATIVE_ID` | 0x314E | ours: no real chip's ID, maker byte non-zero |
| `FLASH_STATUS_BAD_SECTOR` | 0x80FF | `EraseFlashSector_MX`, `ProgramFlashSector_MX` |
| `FLASH_STATUS_BAD_ARGUMENT` | 0x8000 | `ProgramFlashByte_MX` |
| `FLASH_STATUS_TIME_LIMIT`, `FLASH_STATUS_TIMEOUT` | 0xA000, 0xC000, ORed with the phase | `WaitForFlashWrite_Common` |
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

## The BIOS calls (`bios`)

`bios.h` provides every function `include/gba/syscall.h` declares, with its
exact signature, plus the libagbsyscall extras expansion could call (`Mod`,
`DivArm`, `ModArm`, `ArcTan`, `HuffUnComp`, `BitUnPack`, the `Diff*UnFilter*`
functions, `IntrWait`, `Halt`, `Stop`, `SoftResetRom`, `SoftResetExram`,
`SoundBiasChange/Reset/Set`, `MidiKey2Freq`), so the native link uses them in
place of libagbsyscall. `test/test_bios.c` includes `bios.h` and expansion's
`gba/gba.h` together, so a signature that drifts is a compile error. The
BIOS's own sound driver (SWI 1Ah-1Eh, 20h-24h, 28h-2Ah) is not provided.

| File | GBATEK group | Functions |
|---|---|---|
| `bios_arith.c` | BIOS Arithmetic Functions | Div, Mod, DivArm, ModArm, Sqrt, ArcTan, ArcTan2 |
| `bios_affine.c` | BIOS Rotation/Scaling Functions | BgAffineSet, ObjAffineSet |
| `bios_memory.c` | BIOS Memory Copy | CpuSet, CpuFastSet |
| `bios_decompress.c` | BIOS Decompression Functions | LZ77UnComp*, RLUnComp*, HuffUnComp |
| `bios_unfilter.c` | BIOS Decompression Functions | BitUnPack, Diff8bitUnFilter*, Diff16bitUnFilter |
| `bios_control.c` | BIOS Reset/Halt Functions, Multi Boot | RegisterRamReset, SoftReset*, VBlankIntrWait, IntrWait, Halt, Stop, MultiBoot |
| `bios_sound.c` | BIOS Sound Functions | SoundBias*, MidiKey2Freq |
| `bios_stream.c`, `bios_internal.c` | (internal) | stream header, source bound, 8/16-bit writers; range checks, 32-bit fixed point |

For the frame loop: `bios_init(run_frame)` registers the function
`VBlankIntrWait`, `IntrWait` and `Halt` call to let a frame pass (they then
check `INTR_CHECK` at 3007FF8h as the BIOS does; with `NULL` they return at
once), and `bios_set_reset_target(&jmp_buf)` registers where `SoftReset`
longjmps after clearing what the BIOS clears. Without a target `SoftReset`
prints and returns; `bios_soft_reset()` returns `BIOS_ERR_NO_RESET_TARGET`.

Every range a call touches whose length comes from the game is checked with
`transfer_bounds_ok()` first (so `transfer_bounds_init()` must have run); a
refused call prints one line and does nothing. Alignment is deliberately not
enforced (expansion's GBA build pads structs to 4 bytes, the native ABI does
not). The "Vram" decompressors write 16-bit units only. The tests' expected
values are worked by hand from GBATEK; where GBATEK leaves a behaviour open,
`bios.h` says what was chosen.

## The game (`main`, `options`, `frame`)

```sh
make -f Makefile.native -j$(nproc) native               # build/native32/pokeemerald-native
make -f Makefile.native -j$(nproc) native TEST=1        # build/native32-test/pokeemerald-native-test
make -f Makefile.native -j$(nproc) native SANITIZE=1    # build/native32-san/, ASan and UBSan throughout
./build/native32/pokeemerald-native [--headless] [--silent] [--save PATH] [--rtc-offset PATH]
        [--scale N] [--unthrottled] [--frames N] [--screenshot PATH]
        [--replay SCRIPT [--out DIR]] [--rtc-fixed EPOCH] [--test-runner] [--help]
make -f Makefile.native -j32 check [SANITIZE=1] [TESTS=filter]   # expansion's suite, below
```

The defaults are `pokeemerald.sav` and `pokeemerald.rtc` in the current
directory, scale 3, paced to 59.7275 Hz, no frame limit. `--frames N` ends
the game after N frames with status 0 (the gates use it with `--headless
--silent --unthrottled`); `--screenshot` writes the last frame as a PPM.
Headless, the keypad reads no keys. A save file this build cannot use
(corrupt, or written with another struct layout) stops `main` before the
game starts, so it is never overwritten.

`options.c` (`options.h`, `test/test_options.c`) reads and validates the
command line and prints the usage text; it starts nothing. `main.c` starts
the modules in dependency order (`module_steps`), checks
each, and stops the ones started, in reverse, on a failure or at the end. It
then calls expansion's `AgbMain` with two `setjmp` targets: the exit target,
where the frame hook (a frame limit, the window closed, a failure) and
`PlatformTestRunnerExit` unwind to, and the reset target, where `SoftReset`
lands; after a soft reset `main` does what `src/crt0.s` does
(`RegisterRamReset(RESET_ALL)`, then `crt0_restart`, which zeroes the
game's variables and restores its initialised ones and the EWRAM_INIT data)
and restarts the modules that mirror registers (interrupt, timer, dma, ppu,
psg), but never flash, the RTC or the host side. Both jumps, and the BIOS's
`SoftReset`, go through `game_hooks_unwind`, which first leaves the battle
runner's stack if a test body is running on it. `main` is the only place
the process ends.

`frame_run` (`frame.h`, which lists the order) is the frame: lines 0-159
with their H-Blank DMA and interrupts, the timers, V-Blank, sound, present,
input, the save, pacing. `lcd_status` owns `VCOUNT` and `DISPSTAT`'s status
bits; `VCOUNT` reads 161 between frames (`docs/ARCHITECTURE.md`, "The
frame"). `m4a_output` finds the frame of the m4a engine's PCM ring the FIFO
DMAs would play; `sound_mix` routes the two FIFOs and the PSG as
`SOUNDCNT_H` says, at `psg.c`'s scale, and removes the DC offset the GBA's
output capacitor removes.

## What the game links from its assembly files

| File | Stands in for | Notes |
|---|---|---|
| `game_hooks.c` | the guarded game code's control flow: `PlatformWaitCycles`, `PlatformJumpToAgbMainLoop`, `PlatformTestRunnerExit`, `PlatformCallWithStack` | `game_hooks.h` says why each behaves as it does; the battle runner's functions run on a 1 MiB platform stack (ucontext, with AddressSanitizer fiber annotations) so their locals stay put between calls |
| `crt0.c` | `src/crt0.s`: `IntrMain` (data: only its address is stored), `ReInitializeEWRAM`, and the soft-reset restart | the copies are taken at `crt0_init` |
| `decompress_asm.c` | `src/decompress_asm.s`: `FastUnsafeCopy32`, `LZ77UnCompWRAMOptimized` | writes exactly the LZ77 header's size, unlike the assembly |
| `gcn_multiboot.c` | `src/libgcnmultiboot.s` | no GameCube: the copyright screen takes its "no multiboot" path |
| `rom_header.c` | `src/rom_header.s`, as gbafix fills it | Emerald's `BPEE`, version 0 |
| `gba_game.ld` | what `ld_script_modern.ld` and `ld_script_test.ld` give the game | symbols, the tests table, `game_data`/`game_bss` around the game's own variables |

`build_glue/LINK-NEEDS.md` maps every symbol and section the link needed to
its provider; `build_glue/BOOT-LOG.md` records what failed on the way to
the first boot, and the sanitizer findings in expansion's code.

## The test runner (`debug_channel`, `crash_reboot`, `test_runner/`)

`make -f Makefile.native -j32 check` runs expansion's test suite against
the TEST=1 executable through expansion's own hydra
(`tools/mgba-rom-test-hydra`), unchanged, as expansion's `make check` runs
it against mgba-rom-test: the executable is copied to
`build/native32-test/pokeemerald-native-test-headless`, `tools/patchelf`
patches `gTestRunnerArgv` (from `TESTS`, a trailing `*` removed, as
expansion does), `gTestRunnerHeadless` and `gTestRunnerSkipIsFail` into the
copy, and the hydra forks one runner per `-jN` (at most 32), patching
`gTestRunnerN`/`gTestRunnerI` into its own copy (they are `.rodata`
constants with `.symtab` entries, which is all patchelf needs).

The hydra runs `stdbuf -oL <mgba-rom-test> -l15 -ClogLevel.gba.dma=16 -Rr0
<copy>`; `test_runner/native-rom-test` takes mgba-rom-test's place. It
ignores mGBA's options, makes the copy executable, closes the descriptor the
hydra leaves open on it (Linux will not exec a file open for writing), drops
stdbuf's 64-bit preload, and execs the copy with `--headless --silent
--unthrottled --test-runner`.

`--test-runner` makes `main`:

- start `debug_channel` on stdout: mGBA's debug registers (STRING
  0x04FFF600, FLAGS 0x04FFF700, ENABLE 0x04FFF780). The runner's
  `MgbaOpen_` (and `src/libisagbprn.c`'s) call `PlatformDebugOpen`, its
  sends `PlatformDebugSend`; each send prints `GBA Debug: <buffer>\n` if
  its level passes the hydra's `-l15`, flushes, and zeroes the buffer, as
  mGBA's `GBADebug` does. stdout is line-buffered and carries nothing else.
- return the runner's exit code (`PlatformTestRunnerExit`: 0 all as
  expected, 1 a test failed, 2 no debug channel).
- keep the save and the clock's offset in memory: parallel runners share no
  file.
- start `crash_reboot`: a SIGSEGV, SIGBUS, SIGILL, SIGFPE (one
  `arm_division` cannot complete) or SIGABRT runs a handler on an alternate
  stack that writes `crash_reboot: signal N at <pc>, fault address <addr>`
  to stderr and execs `/proc/self/exe` (`host_fault.h`, `host_process.h`)
  with the same arguments plus
  `--persistent-state <512 hex digits>`, the 0x100 bytes at 0x3007F00. The
  new process restores them before `AgbMain`, so the runner reports the test
  that crashed as CRASH and carries on, as after mGBA's reboot (IWRAM's last
  0x200 bytes survive `RegisterRamReset`). A crash with those bytes as the
  process started with them would loop and ends the process instead. The
  runner stores the crashed test as its index in `.tests` plus one, not its
  address (the 28-bit field cannot hold a native address). On Windows,
  which has no exec and where the handler may not create a process, the
  report ends with the state and the process with exit code 75, and the
  driver that started it starts it again with that state
  (`crash_reboot.h`).

The timeout works as on the GBA: Timer 2 counts emulated seconds and the
runner's handler calls `PlatformJumpToAgbMainLoop` from inside the
interrupt dispatch (the GBA patched the BIOS's IRQ return address); a test
stuck in a loop that never waits for a frame is not timed out (no emulated
time passes), as there is no wall-clock watchdog.

`make -f Makefile.native -j32 check SANITIZE=1` runs the suite on the
sanitized build: the recipe sets `ASAN_OPTIONS=halt_on_error=0:abort_on_error=1`
(reports carry on; one that stops aborts, so `crash_reboot` turns it into one
CRASH instead of a lost runner) and `UBSAN_OPTIONS=print_stacktrace=1`.

What the suite needed natively, test by test, how it compares with the GBA
baseline, and every sanitizer finding: `test_runner/SUITE-LOG.md`.

## The input replay (`replay`, `inspect`, `restart`, ...)

The gate that plays the game, `tests/smoke/` (its README has the script's
key sequence and how to run and update it). `--replay SCRIPT --out DIR`
makes the keypad read the script, not the keyboard (window or `--headless`),
and `--rtc-fixed EPOCH` makes the clock deterministic.

| Module | What it is |
|---|---|
| `replay.h`, `replay_parse.c`, `replay.c` | the script's grammar (`replay.h` documents every command) and the state machine that plays it, stepped once per frame against a `struct replay_host`, so `test/test_replay.c` plays scripts against a fake |
| `replay_include.h`, `replay_include.c` | the `include FILE` command: textual expansion before parsing, with a map from each expanded line to its (file, line) so messages name the file a problem came from; tested on real files (`test/test_replay_include.c`) |
| `replay_session.c` | glue: reads the script (through `replay_include`, writing `expanded.replay` when it had includes), connects the driver to `inspect`, `input_script_set`, `video_write_frame_ppm` and the frame loop's step hook, rewrites a failure's line number to `file:line`, and saves the screen at a failure (`failed.ppm`) |
| `inspect.h`, `inspect.c`, `inspect_menu.c`, `inspect_args.c`, `inspect_decode.c`, `symbols.c` | the facts (`callback2`, `map_name`, `party0_species`, ... `inspect.h` lists them, including the facts with arguments, `flag:ID`, `var:ID`, `party_species:N`, ...) read from expansion's variables; `inspect.c` has the game-state facts and the dispatch, `inspect_menu.c` the facts of the menus (debug menu, party, summary, bag, PC), `inspect_args.c` the table of argument ranges (unit tested against expansion's constants, `test/test_inspect_args.c`); `inspect*.c` are documented exceptions to the rule that platform files do not include expansion's headers (like `m4a_output.c`), compiled with the game's defines (see `platform/Makefile`); `symbols.c` names the game's function pointers from the function map the link writes beside the executable (`pokeemerald-native.symbols`, from `nm`: `host.mk`'s `write_symbol_map`), since most callbacks are `static`; `inspect_decode.c` has the charmap decoding and callback naming that need no game variables, and is unit tested |
| `restart.c` | `restart`: build the arguments for, and `execv`, a new process |
| `fixed_clock.c` | `--rtc-fixed`: base epoch plus the frame count divided by 60 |
| `frame.c`'s `frame_set_step_hook` | a function called once per frame before KEYINPUT is written; the replay driver is the one user |
| `script_tag.h` | see below |

`replay_step` is called at the end of each frame, so a key state it sets is
what the game reads in the next frame; `press` is therefore down for one frame
and up for one, and two presses in a row are two presses. `wait-until` and
`expect` see the state the game left at the end of the frame.

A `restart` is a process re-exec because it is what a player's switch-off and
switch-on does: the in-game soft reset only reinitialises RAM and cannot show
that the game starts from its save file. `main` shuts every module down (the
save is flushed), `restart_exec` runs `/proc/self/exe` (`host_process_restart`) with the same arguments
plus `--resume-at LINE`, and `--rtc-fixed` advanced by the time that passed.

### Event script function tags

Expansion lists a script command, native or special that can be run for effect
analysis as its function's address plus `ROM_SIZE` (0x02000000), and on the GBA
that is also a valid address, the ROM's wait-state mirror. `script_tag.h`
explains how the native build keeps the tag (bit 25, clear in the code's link
addresses) and where it is removed (`SCRIPT_UNTAG`, in the guarded changes to
`src/script.c`, `src/scrcmd.c` and `include/script.h`). 64-bit needs more: the
tables are assembled with `.4byte`, which cannot hold a 64-bit pointer.
