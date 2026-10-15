# Building

One command builds the 64-bit game for an operating system, from a fresh
checkout, once its prerequisites are installed (below). It checks them,
naming what is missing, runs the build and prints where the executable is.
It installs nothing:

```
tools/build-native.sh --os linux      # build/native64/pokeemerald-native
tools/build-native.sh --os windows    # build/native64-windows/pokeemerald-native.exe
```

macOS: not yet (no port has been started). To play the result, see
[RUNNING.md](RUNNING.md); the rest of this page is every variant and the
gates.

## Linux (WSL2 or native), the primary target

Verified on Ubuntu 24.04 under WSL2. Everything is headless; a window
appears if a display is available (WSLg), and nothing depends on it.

### Prerequisites

```
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y build-essential gcc-multilib g++-multilib clang clang-format clang-tidy cppcheck pkg-config git
sudo apt-get install -y libsdl2-dev libsdl2-dev:i386 libc6-dev-i386 valgrind
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi libpng-dev
```

Line one is the toolchain and the linters; line two adds the SDL2 libraries at
both widths; line three is the ARM toolchain for the GBA regression target,
which follows expansion's `INSTALL.md`, and `libpng-dev`, which the asset tools
need in every build. `ccache` is optional (below); the Windows cross-build's
packages are in its section. The bundled `tools/mgba/mgba-rom-test` is used for
the GBA test suite; no mGBA build is needed.

### GBA target (regression reference)

```
make -j"$(nproc)"          # pokeemerald.gba
sha1sum pokeemerald.gba    # e2d4600d4e7b1269ac50c28bc96ece2b758ded9e for 1.17.1
make -j"$(nproc)" check    # expansion's test suite on mgba-rom-test
```

The sha1 is the reference for expansion 1.17.1: a change that moves it changes
GBA behaviour (`docs/UPGRADING.md` records a new one for each upgrade). Expected
test results for 1.17.1 are in `docs/research/gba-baseline-1.17.1.md`.

### Native target (32-bit and 64-bit)

```
make -f Makefile.native -j$(nproc) native BITS=32        # build/native32/pokeemerald-native
./build/native32/pokeemerald-native                       # windowed, scale 3
./build/native32/pokeemerald-native --help                # options: --headless --silent --unthrottled --frames N --screenshot F --data-dir DIR --save PATH --rtc-offset PATH --scale N --keys --version --replay SCRIPT --out DIR --rtc-fixed EPOCH --audio-dump PATH
```

The executable reports the version it was built from (`--version`, `--keys`, the F1
overlay): `Makefile.native` passes `git describe --always --dirty --match 'v[0-9]*'`
(the commit, as no tag of this project's exists yet; `unknown` without git) to
`platform/Makefile`, which defines it for `platform/version.c` alone, so that ccache
still serves every other object after a commit.

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
widths, the 64-bit tests again under ThreadSanitizer (`tsan`), then `tools-test`
(in `platform/test_runner/`: `test_patchelf.sh`, tools/patchelf patches ELF32
and ELF64 executables at the right bytes and refuses a bad ELF class;
`test_hydra_end_of_output.sh`, expansion's hydra reports every result of a
runner that ends with its pipe full; and the Windows runner's scripts driven
with fake executables, `test_run_windows_tests.sh`,
`test_native_rom_test_windows.sh` and `test_compare_suite_results.sh`) and
`glue-test` (the self-test of every script in `platform/build_glue/`):

```
make -C platform gate              # format-check, lint and test at BITS=32 and 64, tsan, tools-test, glue-test
make -C platform format            # apply clang-format (../.clang-format) to platform/
make -C platform format-check      # fail, naming the files, if any would change
make -C platform lint BITS=64      # clang-tidy (../.clang-tidy) and cppcheck; any finding fails
```

`lint` needs clang-tidy and cppcheck (the prerequisites above install them) and
clang-format 18 for the style: `format-check` depends on that version, the one
`.clang-format` was derived with, and other major versions format differently.

Expansion's test suite against the native build, through expansion's own
hydra (as `make check` runs it against mgba-rom-test; about 40 seconds on
32 cores, about 75 with `SANITIZE=1`, which runs the sanitized build;
`TESTS=` filters as expansion's does):

```
make -f Makefile.native -j32 check BITS=32
```

How it works: `platform/README.md`, "The test runner"; the results against
the GBA baseline: `platform/test_runner/SUITE-LOG.md`.

The headless smoke boot used as a gate (`--data-dir`, or `--save` and `--rtc-offset`,
keeps a gate's files out of the per-user data directory, docs/RUNNING.md):

```
./build/native32/pokeemerald-native --headless --silent --unthrottled --frames 9000 --screenshot title.ppm --data-dir out
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

Save file: `pokeemerald.sav` in the data directory, the per-user one by default, the
executable's own in portable mode, or `--data-dir`'s (`docs/RUNNING.md`, "Where the
files live"; `platform/data_dir.h`), a 28-byte header plus the 128 KiB flash image (see
`platform/README.md`); RTC offset in `pokeemerald.rtc` beside it, and the player's
`settings.ini` (keys and scale, read only). Every replay and gate passes `--save` and
`--rtc-offset` (or `--data-dir`) and runs headless, which reads no settings, so none
of them reads or writes the per-user directory. A save is portable between the widths: a `pokeemerald.sav`
written by the 32-bit build is converted in memory when the 64-bit build loads it
and the file is rewritten with the 64-bit tag at the game's next save; a 64-bit
save loads in the 32-bit build the same way (`platform/README.md`;
`tests/smoke/run_cross_width.sh` plays it).

#### The 64-bit build

`BITS=64` is a first-class target, built the same way into `build/native64/`
(`native64-san/`, `native64-test/`, `native64-test-san/`):

```
make -f Makefile.native -j$(nproc) native BITS=64        # build/native64/pokeemerald-native
make -f Makefile.native -j$(nproc) native BITS=64 TEST=1 # build/native64-test/pokeemerald-native-test
make -f Makefile.native -j32 check BITS=64                # expansion's suite
```

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

#### The game's C and ccache

Each game and test source goes through `gcc -E`, `preproc` and
`platform/build_glue/c_filter.sed` as in expansion's build, and the result is written
to a file, `<object>.i` beside the object, which `gcc -c -x c` then compiles. It is a
file because ccache cannot cache a compile whose source is standard input. The file
exists only for the compile and is removed after it, whether the compile fails or not
(`COMPILE_C` in `Makefile.native`). The one trace in the object is its file symbol,
`<object>.i` where a pipe gave `<stdin>`. The compile is
therefore cacheable by ccache: with it installed and `/usr/lib/ccache` first on `PATH`
(the CI jobs do), a build after `make clean` or in a fresh checkout can be served from
the cache. (make itself does not recompile unchanged sources.)

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

## Gates

A change merges when the gates that apply to it pass. The letters are the ones
the rest of the documentation uses. `out/` is a scratch directory for a
replay's outputs; the sanitized replays are run with
`ASAN_OPTIONS=halt_on_error=0`.

| Gate | What it checks | Command |
|---|---|---|
| (a) | the native build from a clean clone with the prerequisites above (before a milestone is declared done) | `make -f Makefile.native -j"$(nproc)" native BITS=32`, then `BITS=64` |
| platform | style, lint, the sanitized unit tests at both widths, ThreadSanitizer, the tools and glue self-tests | `make -C platform gate` |
| (b) | expansion's test suite on the native build equals the GBA baseline, per test, but for the nine benchmarks (assumption failures by decision); the same at both widths | `make -f Makefile.native -j32 check BITS=32`, `BITS=64`, and each with `SANITIZE=1` |
| (c) | the GBA target builds, its ROM is unchanged (sha1 `e2d4600d4e7b1269ac50c28bc96ece2b758ded9e` for 1.17.1) and its suite equals the baseline | `make -j"$(nproc)"`, `sha1sum pokeemerald.gba`, `make -j"$(nproc)" check` |
| (d) | the opening, replayed: dumps and screenshots match, at both widths, plain and sanitized | `tests/smoke/run.sh build/native32/pokeemerald-native out/`, likewise `native32-san`, `native64`, `native64-san` |
| (e) | expansion's debug menu driven natively, the same four builds | `tests/smoke/run_debug_menu.sh build/native32/pokeemerald-native out/`, likewise the other three |
| (f) | the unit tests, both replays and the suite are clean under AddressSanitizer and UndefinedBehaviorSanitizer at both widths | (platform) `make -C platform test BITS=32`, `BITS=64`; the sanitized runs of (b), (d) and (e) |
| (g) | the status record, `docs/STATUS.md`, is current | none |
| (h) | a save written by one width loads in the other, both directions | `tests/smoke/run_cross_width.sh build/native32/pokeemerald-native build/native64/pokeemerald-native out/`, and the two builds swapped |

`tests/smoke/README.md` has the replays' and the cross-width gate's details,
and `platform/test_runner/SUITE-LOG.md` the suite's. `CONTRIBUTING.md` says
which gates a given change must pass.

## Windows (cross-build from Linux)

The Windows executable is x86-64 only and is built on Linux with MinGW-w64
(`TARGET_OS=windows`, `platform/target.mk`); the build machine needs nothing
Windows. i686 is not attempted (`research/m8-inventory.md` 2.3). Nothing built
this way can be run on the build machine without Wine, which is not part of
the project: what is checked here is the link and the file it writes. The unit
tests, a start-up smoke, both replays, expansion's suite and the cross-OS save
gate run on a Windows runner (below), and the owner verified the build by
hand on 2026-10-14: boot, the opening, the first battle, a save and its
reload.

It runs on Windows 10 version 1903 (the May 2019 Update, build 18362) or
later, 64-bit. Every executable carries an application manifest that makes
UTF-8 the process's code page, so that paths and arguments are UTF-8 as on
Linux, whatever the user's name and the system's language
(`platform/build_glue/windows_manifest.xml`; `docs/DECISIONS.md`, "Narrow
strings on Windows are UTF-8"); Microsoft's "Use UTF-8 code pages in Windows
apps" documents the setting from version 1903 on. On an older Windows the
setting is ignored, and a path with a character outside the system's code
page fails.

```
sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools   # windres is in binutils
make -f Makefile.native -j32 sdl2-mingw TARGET_OS=windows BITS=64   # SDL2 for MinGW, once
make -f Makefile.native -j32 native TARGET_OS=windows BITS=64       # build/native64-windows/pokeemerald-native.exe
```

Or, instead of the two makes, `tools/build-native.sh --os windows`, which
checks the prerequisites first and runs the same makes.

`native` fetches SDL2 itself when it is missing (`sdl2-mingw` is only for doing it
alone). The fetch is the newest 2.30 release's `SDL2-devel-2.30.12-mingw.tar.gz`
from github.com/libsdl-org/SDL, checked against the sha256 recorded in
`Makefile.native` before anything is unpacked (a mismatch removes the download
and stops), into `build/sdl2-mingw/` (not committed); SDL is under the zlib
licence, which allows this and shipping `SDL2.dll` beside the executable. The
build uses the package's headers and import library directly, not its
`sdl2.pc`, which carries another machine's prefix and asks for `-mwindows` and
SDL's `WinMain` shim; `SDL_MAIN_HANDLED` takes its place, and `main` tells
SDL with `SDL_SetMainReady` (on every OS). The game links with `-mwindows`
(the "Windows GUI" subsystem, so that Explorer opens no console window; it
keeps its own `main`, which MinGW-w64's start-up code for a GUI program
calls), the unit tests with `-mconsole`; `check_pe_layout.py --gui` and
`--console` check the subsystem of each, and `objdump -p` shows it
(`Subsystem 00000002 (Windows GUI)`). Where the messages go is
`platform/console.h`'s and `docs/RUNNING.md`'s.

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
stretch, as on Linux, and the resources before it); the application manifest
with the UTF-8 code page in the resources (`windres` compiles it,
`platform/target.mk`). The main thread's stack is the image's stack reserve,
2 MiB (MinGW's default), where Linux gives 8 MiB (`platform/host_image.h`). To
look at it yourself:

```
x86_64-w64-mingw32-objdump -h build/native64-windows/pokeemerald-native.exe   # sections
x86_64-w64-mingw32-objdump -p build/native64-windows/pokeemerald-native.exe | grep -E 'ImageBase|DllCharacteristics|Base Relocation'
x86_64-w64-mingw32-nm -u build/native64-windows/pokeemerald-native.exe        # no output: nothing undefined
```

The platform library alone: `make -C platform lib TARGET_OS=windows BITS=64`
(`platform/build/64-windows/`). The unit tests, cross-built here and run on Windows (below):
`make -C platform test TARGET_OS=windows BITS=64` writes every test that runs
on Windows to `platform/build/64-windows/test/` (the portable ones and
`platform/test/windows/`'s, 85 executables, with `SDL2.dll`); each is linked
with the PE scripts and checked by `check_pe_layout.py` like the game. The
Windows sources' static analysis: `make -C platform lint TARGET_OS=windows
BITS=64` (clang-tidy for the MinGW-w64 target, with its headers, and cppcheck;
not in the platform gate, which runs on Linux alone). patchelf on a PE file:
`make -C platform tools-test TARGET_OS=windows BITS=64`
(`platform/test_runner/test_patchelf_pe.sh`, the twin of the ELF test).

The test runner build, for the suite on Windows:

```
make -f Makefile.native -j32 native TARGET_OS=windows BITS=64 TEST=1   # build/native64-test-windows/pokeemerald-native-test.exe
```

It is made as the game is, from the Linux TEST=1 data (`build/native64-test/`)
converted to COFF, with two differences the tests' event scripts
(`include/test/overworld_script.h`) need: the COFF listings keep the data's
global absolute symbols global (`ITEM_POTION`, `SPECIAL_*`: the scripts name
them and the link resolves them, as on Linux; `elf_to_flat.py` says why Mach-O's
stay local), and each script is jumped over in its function's own section,
since COFF's assembler has no section stack to put it in `.rodata` and come
back (`platform/build_glue/c_filter_coff.sed`). `make ... check` with
`TARGET_OS=windows` is an error: the suite runs on Windows (below).
`SANITIZE=1` is an error with `TARGET_OS=windows` (MinGW's GCC has no
sanitizers), and so is `BITS=32`.

### Running it on Windows

Nothing built this way runs on the build machine. What runs it is the Windows
workflow, `.github/workflows/windows.yml` (nightly at 05:17 UTC, and by hand,
`gh workflow run windows.yml`): three Linux jobs side by side cross-build the
game and the unit tests (and play the Linux half of the cross-OS save gate),
cross-build the test runner, and run the suite on the Linux 64-bit build; a
Windows job (`windows-latest`, Git Bash and the image's MSYS2) runs everything
below on what they built; a last Linux job compares. The same by hand, on a
64-bit Windows 10 1903 or later with Git for Windows and, for the suite, MSYS2
at `C:\msys64`, from a checkout made with `core.autocrlf false` (the scripts
and the expectations are compared byte for byte) holding the build's outputs
where the build left them:

```
# Git Bash
platform/test_runner/run_windows_tests.sh platform/build/64-windows/test
platform/test_runner/run_windows_smoke.sh build/native64-windows/pokeemerald-native.exe
pwsh platform/test_runner/run_windows_no_console.ps1 build/native64-windows/pokeemerald-native.exe
tests/smoke/run.sh build/native64-windows/pokeemerald-native.exe out/open64
tests/smoke/run_debug_menu.sh build/native64-windows/pokeemerald-native.exe out/dbg64
tests/smoke/run_cross_os.sh load build/native64-windows/pokeemerald-native.exe out/cross-os/save/work/from out/cross-os/load

# MSYS2: C:\msys64\usr\bin\bash -l, with MSYSTEM=MSYS and CHERE_INVOKING=1
pacman -S --noconfirm --needed gcc make
make -C tools/patchelf && make -C tools/mgba-rom-test-hydra
MAKEFLAGS=-j$(nproc) platform/test_runner/run_windows_suite.sh build/native64-test-windows/pokeemerald-native-test.exe | tee suite-windows.log
```

and on Linux, for the comparisons (the same commit):

```
tests/smoke/run_cross_os.sh save build/native64/pokeemerald-native out/cross-os/save          # before `load`
tests/smoke/run_cross_os.sh back build/native64/pokeemerald-native out/cross-os/save/work/from out/cross-os/load/work/again out/cross-os/back
make -f Makefile.native -j32 check BITS=64 | tee suite-linux64.log
platform/test_runner/compare_suite_results.sh suite-linux64.log suite-windows.log
```

- `run_windows_tests.sh` runs every test executable as `make -C platform
  test` does on Linux (each to its end; a failure if it exits non-zero or its
  output has no "N passed, 0 failed"; its output and its standard error kept
  beside it) and ends with "all 87 Windows test executables passed". Each
  runs from a copy under its own name, deleted when it ends: an executable of
  this layout costs about 1 GiB of the system's commit charge from its first
  start until its file is deleted (the image's 1 GiB, gaps included, is
  charged to the image, which Windows keeps cached), and the runner's commit
  ran out after two dozen of them (`docs/DECISIONS.md`, "The Windows unit
  tests run from copies removed after them").
- `run_windows_smoke.sh` starts the game with `--headless --silent --frames
  60 --data-dir data` in an empty directory and passes when it exits 0,
  prints only `main: 60 frames` on standard output and nothing on standard
  error, and has written `data/pokeemerald.rtc` in binary mode beside the
  save's lock and nothing else in the directory; then again from a directory
  with a non-ASCII name, with non-ASCII save and clock paths on the command
  line, where the clock's file must appear under exactly its name and a file
  at the save path without the save header must be refused by its name and
  left as it was, and with a non-ASCII `--data-dir`, which must be made and
  hold the clock's file and the lock. Then `--keys` with `--data-dir` must
  print the version first and the four paths in it and the default keys, and,
  with a `settings.ini` there, its key and its bad line's number; `--version`
  the version alone, the one `--keys` printed;
  and a copy of the executable beside a `settings.ini` (portable mode) must
  name the copy's directory and the files in it. It also reads the PE
  header's subsystem (2, Windows GUI) and runs `--version`, `--keys` and a bad
  argument through a pipe. No run uses the per-user
  directory, so the machine's `%APPDATA%` is never written. The script runs
  against the Linux executable too (without the subsystem check).
- `run_windows_no_console.ps1` (PowerShell, `pwsh`) starts the game as Explorer
  does, through `Win32_Process.Create` with no handles and no console, with
  `--version --data-dir` and passes when `pokeemerald.log` there holds the
  version line and only it, again over a log left by an earlier start.
- The replays (gates d and e) compare with the Linux expectations unchanged:
  the Windows build draws the same frames and dumps the same facts as the
  Linux 64-bit one (the same code and data, bit-fields laid out as GCC lays
  them on Linux). Two things it does for that: it writes the dumps in binary
  mode, and its objects keep their local symbols whose names start with "L"
  (`-Wa,--keep-locals`, `platform/target.mk`), which GNU as for COFF would
  otherwise drop, so that the replays name the same callbacks. `lib.sh`
  computes checksums and sizes in forms Git Bash and Linux print alike.
- `run_cross_os.sh` is gate (h) between the two systems, a Linux save loaded
  by the Windows build and back (`tests/smoke/README.md`).
- The suite (gate b) runs through expansion's own hydra built by MSYS2's gcc
  (`docs/DECISIONS.md`, "The suite on Windows runs under MSYS2"):
  `run_windows_suite.sh` patches the copy as `make check` does (patchelf reads
  a PE file's COFF symbol table), and `native-rom-test` copies each runner's
  executable to a `.exe` name and is the crash reboot's supervising parent,
  starting a runner that exits with code 75 again with the state its report
  ends with. `compare_suite_results.sh` passes when the two runs have the
  same tests with the same results, line for line.

What the host layer does on Windows, and why each choice: `platform/README.md`,
"The host layer", and `docs/ARCHITECTURE.md`. The differences a player or a
test sees: the main thread's stack is 2 MiB, not 8; the replay's `restart`
starts a child process (another process id) that the first one waits for,
where Linux replaces the process in place; a crash does not reboot by itself:
the process reports the test runner's persistent state on standard error and
ends with exit code 75, and the program that started it (in the suite,
`platform/test_runner/native-rom-test`) starts it again with that state
(`platform/crash_reboot.h` has the protocol), so a run with no such parent
ends at its first crash with exit code 75; two crashes end the run with no report and no reboot, `abort()` (the
C runtime's, which raises no exception) and an overflow of the battle
runner's stack (no room for the exception's records); a crash report names
the fault as `exception 0xC0000005`, where Linux says `signal 11`.

What only Windows could show, shown on the runner (runs 38598389601 to
38621321132): the loader maps the executable at its base; an exception on the
battle runner's stack reaches the crash filter through the stack switch
(`test_host_stack_windows`), the suite's battle tests run on that stack, and
its two tests that crash on purpose are restarted by their parent as the
protocol says; msvcrt's time zones agree with the tests' TZ strings
(`test_rtc`, `test_host_file`); msvcrt hands `main` a non-ASCII argument as
its UTF-8 bytes under the manifest's code page (`test_utf8_windows`,
`test_host_process_windows`, the smoke's non-ASCII run), so the fallback
through `GetCommandLineW` is not needed. One assumption did not hold: the
page at 0x01000000 is not reliably free in a Windows process, whose own
allocations below the image end near it, so the tests' fixed page is
0x017F0000 (`test_host_memory.c`).

## The release workflow

`.github/workflows/release.yml` (by hand only) builds the Linux 64-bit tarball
and the Windows 64-bit zip, with the same make commands as above (the Windows
steps are `.github/actions/windows-cross-setup`, after
`.github/actions/native-setup`), writes `SHA256SUMS.txt` and attests both
archives (`gh attestation verify <file> --repo <owner>/<repo>`; GitHub offers
attestations to a user-owned repository only once it is public, so while this
one is private that step fails after the files are uploaded). Each archive
holds the executable, `LICENSE`, `RUNNING.md` and a short `README.txt`; the
Windows zip also holds `SDL2.dll` and SDL2's licence. The files stay on the run
as artifacts for 14 days. The run creates a GitHub release only with the
`publish` input and a `tag`, and no public release is made without the
owner's explicit instruction (`docs/DECISIONS.md`, "Why no public release").
