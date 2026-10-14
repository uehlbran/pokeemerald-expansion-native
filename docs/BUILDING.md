# Building

## Linux (WSL2 or native), the primary target

Verified on Ubuntu 24.04 under WSL2. Everything is headless; a window
appears if a display is available (WSLg), and nothing depends on it.

### Prerequisites

```
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y build-essential gcc-multilib g++-multilib clang clang-format clang-tidy cppcheck cmake ninja-build pkg-config git
sudo apt-get install -y libsdl2-dev libsdl2-dev:i386 libc6-dev-i386 valgrind
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi libpng-dev
```

The second line is the native build (32-bit and 64-bit); the third is
the GBA regression target, which follows expansion's `INSTALL.md`. The
bundled `tools/mgba/mgba-rom-test` is used for the GBA test suite; no
mGBA build is needed.

### GBA target (regression reference)

```
make -j$(nproc)            # pokeemerald.gba
make -j$(nproc) check      # expansion's test suite on mgba-rom-test
```

Expected results for 1.17.1 are in `research/gba-baseline-1.17.1.md`.

### Native target (32-bit and 64-bit)

```
make -f Makefile.native -j$(nproc) native BITS=32        # build/native32/pokeemerald-native
./build/native32/pokeemerald-native                       # windowed, scale 3
./build/native32/pokeemerald-native --help                # options: --headless --silent --unthrottled --frames N --screenshot F --save PATH --rtc-offset PATH --scale N --replay SCRIPT --out DIR --rtc-fixed EPOCH
```

`BITS=64` is a first-class target, built the same way into `build/native64/`
(`native64-san/`, `native64-test/`, `native64-test-san/`):

```
make -f Makefile.native -j$(nproc) native BITS=64        # build/native64/pokeemerald-native
make -f Makefile.native -j$(nproc) native BITS=64 TEST=1 # build/native64-test/pokeemerald-native-test
make -f Makefile.native -j32 check BITS=64                # expansion's suite
```

Variants: `TEST=1` builds `build/native32-test/pokeemerald-native-test`
with expansion's test suite linked in;
`SANITIZE=1` builds into `build/native32-san/` with AddressSanitizer and
UndefinedBehaviorSanitizer on everything (expansion's objects recover and
report every finding with `ASAN_OPTIONS=halt_on_error=0`; the platform
layer aborts at the first). The platform layer's own unit tests, always
sanitized:

```
make -C platform test BITS=32
make -C platform test BITS=64
```

The platform gate, which a change to anything under `platform/` passes before
it merges, runs the style check, the static analysis and those tests at both
widths, then `tools-test` (platform/test_runner/test_patchelf.sh: tools/patchelf patches
ELF32 and ELF64 executables at the right bytes and refuses a bad ELF class), about a minute on 32 cores:

```
make -C platform gate              # format-check, lint (BITS=32, 64), test (BITS=32, 64), tools-test
make -C platform format            # apply clang-format (../.clang-format) to platform/
make -C platform format-check      # fail, naming the files, if any would change
make -C platform lint BITS=64      # clang-tidy (../.clang-tidy) and cppcheck; any finding fails
```

`lint` needs clang-tidy and cppcheck (the prerequisites above install them) and
clang-format 18 for the style: `format-check` depends on that version, the one `.clang-format` was derived with, and other major versions format differently.

Expansion's test suite against the native build, through expansion's own
hydra (as `make check` runs it against mgba-rom-test; about 40 seconds on
32 cores, about 75 with `SANITIZE=1`, which runs the sanitized build;
`TESTS=` filters as expansion's does):

```
make -f Makefile.native -j32 check BITS=32
```

#### What the 64-bit link adds

Before it links, the 64-bit build runs three checks that the 32-bit one has no
use for (`docs/ARCHITECTURE.md`, "The 64-bit build", says what each catches):

- `platform/build_glue/check_relocations.py` classifies every address word of the
  assembled data with `platform/build_glue/data_address_words.txt` (the classified
  list) and compares it with the same objects assembled with 4-byte pointers, which
  a second make builds into `build/native64/pointer4` for this purpose only (about
  2 s);
- `platform/build_glue/check_data_layout.py` compares the bytes of the structs the
  data holds with that reference and with the C layout at each width, which
  `data_layout_probe.c` prints compiled with the game's flags;
- `platform/build_glue/check_battle_script_operands.py` compares the battle script
  macros' operand widths with the C that reads them.

Each runs its `--self-test` first. Together they add well under half a minute
to a 64-bit build (the self-tests take under a second and the reference build
a few seconds); a 64-bit build from nothing takes about 1.5 minutes with `-j32`,
asset conversion included, and about 30 s once the assets exist. A table that
upstream adds to the data without a rule, or a macro whose operands change, stops
the link with the symbol or macro named; `docs/UPGRADING.md` says what to do.

#### The data for macOS and Windows

The macOS and Windows ports link the data this 64-bit build assembles and checks,
converted (`docs/DECISIONS.md`, "The data for Windows and macOS is assembled on Linux
and converted"). `data-flat` writes each 64-bit data object (`data/*.o` and the 530
songs) as a listing for the target's object format, at the object's path under
`build/native64/flat/<target>/`, with `platform/build_glue/elf_to_flat.py`;
`check-flat` assembles each listing with clang (the prerequisites above install it;
`FLAT_CLANG=` names another) and compares the object it writes, kept beside the
listing, with the ELF one (`check_flat.py`): the bytes, each relocation's offset,
width, symbol and addend, the symbols and the alignment. Neither changes the Linux
build; both make the 64-bit data objects and their three checks first, and `BITS`
is 64 for them. A conversion and check of everything takes about 6 s per target with
`-j32`. Their self-tests, in `make -C platform glue-test` and so in the gate, assemble
small objects with GNU as and with clang, so the gate needs both.

```
make -f Makefile.native -j32 data-flat TARGET=macho-x86_64    # or TARGET=coff-x86_64
make -f Makefile.native -j32 check-flat TARGET=macho-x86_64
```

The 64-bit replays are the 32-bit ones on the 64-bit executable (the same
expectations, about 30 s each):

```
tests/smoke/run.sh build/native64/pokeemerald-native out/
tests/smoke/run_debug_menu.sh build/native64/pokeemerald-native out/
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run.sh build/native64-san/pokeemerald-native out-san/
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run_debug_menu.sh build/native64-san/pokeemerald-native out-san/
```

(`native BITS=64 SANITIZE=1` builds the sanitized executable.) The suite's
sanitized run compares widths with `ASAN_OPTIONS=suppress_equal_pcs=0`
(`platform/test_runner/SUITE-LOG.md`).

#### Assets of the native build

The native build keeps the graphics it converts (`.4bpp`, `.smol`, `.gbapal`, ... from
INCGFX) in `build/assets-native/`, not in `build/assets/` where the GBA build puts
its own, and `make -f Makefile.native clean` (which removes `build/native*/`) leaves
the directory alone (that is why it is not called `native-assets`). The one difference
is palettes: many species have 11 to 15 colours, a `.gbapal` that is not a whole
number of 32-byte banks, and `LoadSpritePalette` and `LoadPaletteFast` copy a whole
bank, so the GBA read the next bytes of ROM, which is harmless there, and the native
build read past the object (AddressSanitizer). The native build therefore runs
`gbagfx` through `platform/build_glue/gbagfx_pad.sh`, which pads each `.gbapal` it
writes under `build/assets-native/` with zero colours to a whole bank. Nothing is
padded elsewhere, so the GBA ROM is unaffected (sha1 `e2d4600d...` before and after).

Five palettes must stay as they are, because the game takes the array's size
(`sizeof`, `ARRAY_COUNT`, an explicit size, or a `[MODE_COUNT][2]` initializer):
`platform/build_glue/unpadded_palettes.txt` lists them with the reason for each.
`make -f Makefile.native check-palette-padding` (a prerequisite of linking, about a
second) runs `platform/build_glue/check_palette_padding.py`, which fails with the
file and symbol if a short palette whose size is taken is not in the list, or a
listed file is not short or its size is not taken: a new palette that the game sizes
cannot be padded by accident.

First build, measured on 2026-10-08 with `-j32`: about 34 s wall (10.6 minutes of CPU)
for `native BITS=32` from nothing, of which converting about 21,700 asset files is
most. `make -f Makefile.native seed-assets` first copies `build/assets/` (build the
GBA target before) into `build/assets-native/` and deletes the copied palettes that
need padding, so that only about 150 conversions remain: the same build then takes
about 15 s. The copy is not a hard link on purpose: `gbagfx` rewrites a stale output
in place, which would reach through a link into the GBA build's file.

How it works: `platform/README.md`, "The test runner"; the results against
the GBA baseline: `platform/test_runner/SUITE-LOG.md`.

The headless smoke boot used as a gate:

```
./build/native32/pokeemerald-native --headless --silent --unthrottled --frames 9000 --screenshot title.ppm
```

The input-replay smoke test (gate (d): the opening through a save, a cold
restart and Continue), on the plain and the sanitized build:

```
tests/smoke/run.sh build/native32/pokeemerald-native out/
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run.sh build/native32-san/pokeemerald-native out-san/
```

`tests/smoke/README.md` has how it works and how to update its expectations.

Gate (e), expansion's own debug menu driven natively (gives of every generation and
of expansion-only forms and items, the party menu, summary screens, the bag, trainer
battles, flags and vars, a save and a restart):

```
tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/
ASAN_OPTIONS=halt_on_error=0 tests/smoke/run_debug_menu.sh build/native32-san/pokeemerald-native out-san/
```

Save file: `pokeemerald.sav` in the working directory (a 28-byte header
plus the 128 KiB flash image; see `platform/README.md`); RTC offset in
`pokeemerald.rtc`. A save is portable between the widths: a `pokeemerald.sav`
written by the 32-bit build is converted in memory when the 64-bit build loads it
and the file is rewritten with the 64-bit tag at the game's next save; a 64-bit
save loads in the 32-bit build the same way (`platform/README.md`;
`tests/smoke/run_cross_width.sh` plays it).

## Windows (cross-build from Linux)

The Windows executable is x86-64 only and is built on Linux with MinGW-w64
(`TARGET_OS=windows`, `platform/target.mk`); the build machine needs nothing
Windows. i686 is not attempted (`research/m8-inventory.md` 2.3). Nothing built
this way can be run on the build machine without Wine, which is not part of
the project: what is checked here is the link and the file it writes.

```
sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools
make -f Makefile.native -j32 sdl2-mingw TARGET_OS=windows BITS=64   # SDL2 for MinGW, once
make -f Makefile.native -j32 native TARGET_OS=windows BITS=64       # build/native64-windows/pokeemerald-native.exe
```

`native` fetches SDL2 itself when it is missing (`sdl2-mingw` is only for doing it
alone). The fetch is the newest 2.30 release's `SDL2-devel-2.30.12-mingw.tar.gz`
from github.com/libsdl-org/SDL, checked against the sha256 recorded in
`Makefile.native` before anything is unpacked (a mismatch removes the download
and stops), into `build/sdl2-mingw/` (not committed); SDL is under the zlib
licence, which allows this and shipping `SDL2.dll` beside the executable. The
build uses the package's headers and import library directly, not its
`sdl2.pc`, which carries another machine's prefix and asks for `-mwindows` and
SDL's `WinMain` shim; `SDL_MAIN_HANDLED` and `-mconsole` take their place,
and `main` tells SDL with `SDL_SetMainReady` (on every OS).

What the build does, in two makes (`Makefile.native`, "The Windows
cross-build"): the first is the Linux 64-bit data (`build/native64/`) with its
three checks, converted to COFF listings and compared with the ELF objects
(`check-flat TARGET=coff-x86_64`, above); the second compiles the game and the
platform layer with `x86_64-w64-mingw32-gcc` into `build/native64-windows/`,
assembles the listings there with MinGW's `as`, and links with
`platform/gba_regions.pe.ld` and `platform/gba_game.pe.ld`. The flags (the
bit-field layout `-mno-ms-bitfields`, so that saves stay interchangeable with
Linux's, and the image layout) are in `platform/target.mk`. The link
writes `pokeemerald-native.exe`, its function map `pokeemerald-native.exe.symbols`
and a copy of `SDL2.dll` beside it, and checks the result with
`platform/build_glue/check_pe_layout.py`: image base `0x01800000`; no dynamic
base, no high-entropy VA, no relocations; every GBA region a section at its GBA
address and size; the sections adjacent; `.text` at `0x40001000`; above it the
read-only sections first and the writable ones last (`gba_game.pe.ld` moves the
initialised data after `.xdata`, so that the program's mutable data is one
stretch, as on Linux). The main thread's stack is the image's stack reserve,
2 MiB (MinGW's default), where Linux gives 8 MiB (`platform/host_image.h`). To
look at it yourself:

```
x86_64-w64-mingw32-objdump -h build/native64-windows/pokeemerald-native.exe   # sections
x86_64-w64-mingw32-objdump -p build/native64-windows/pokeemerald-native.exe | grep -E 'ImageBase|DllCharacteristics|Base Relocation'
x86_64-w64-mingw32-nm -u build/native64-windows/pokeemerald-native.exe        # no output: nothing undefined
```

The platform library alone: `make -C platform lib TARGET_OS=windows BITS=64`
(`platform/build/64-windows/`). The unit tests, cross-built and not run:
`make -C platform test TARGET_OS=windows BITS=64` writes every test that runs
on Windows to `platform/build/64-windows/test/` (the portable ones and
`platform/test/windows/`'s, 68 executables, with `SDL2.dll`); each is linked
with the PE scripts and checked by `check_pe_layout.py` like the game. The
Windows sources' static analysis: `make -C platform lint TARGET_OS=windows
BITS=64` (clang-tidy for the MinGW-w64 target, with its headers, and cppcheck;
not in the platform gate, which runs on Linux alone). `SANITIZE=1` and
`TEST=1` are errors with `TARGET_OS=windows` (MinGW's GCC has no sanitizers; the
test runner is not ported). `BITS=32` is an error.

### Running it on Windows

Nothing here runs on the build machine; on a 64-bit Windows with Git Bash
(the CI runner's job, task W9), from a checkout with the build's outputs:

```
platform/test_runner/run_windows_tests.sh platform/build/64-windows/test
platform/test_runner/run_windows_smoke.sh build/native64-windows/pokeemerald-native.exe
```

The first runs every test executable as `make -C platform test` does on
Linux (each to its end, a failure if it exits non-zero or ends without
"N passed, 0 failed", a log beside each) and ends with "all 68 Windows test
executables passed". The second starts the game with `--headless --silent
--frames 60` in an empty directory and passes when it exits 0, prints only
`main: 60 frames` on standard output and nothing on standard error, and has
written `pokeemerald.rtc` in binary mode. The game itself needs
`SDL2.dll` beside it (the Makefile copies it), as each test directory has.

What the host layer does on Windows, and why each choice: `platform/README.md`,
"The host layer", and `docs/ARCHITECTURE.md`. Two differences a player or a
test sees: the main thread's stack is 2 MiB, not 8; a crash reboot and the
replay's `restart` start a child process (another process id) that the first
one waits for, where Linux replaces the process in place.

What cannot be verified here: that the loader maps this executable (the probe
on `windows-latest` showed it for the layout, `research/probes/README.md`, W1);
that the exception dispatch walks through the battle runner's stack switch to
the crash filter; that the C runtime's time zones match the tests' POSIX TZ
strings (msvcrt applies its own daylight-saving rules: `test_rtc` and
`test_host_file`'s zone tests are the first to watch); that a fixed page can be
mapped at 0x01000000 in a Windows process (`test_host_memory`, the m4a tests).
The first run on Windows is the evidence for all of them.
