# Building

## Linux (WSL2 or native), the primary target

Verified on Ubuntu 24.04 under WSL2. Everything is headless; a window
appears if a display is available (WSLg), and nothing depends on it.

### Prerequisites

```
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y build-essential gcc-multilib g++-multilib clang clang-tidy cppcheck cmake ninja-build pkg-config git
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

### Native target (32-bit)

```
make -f Makefile.native -j$(nproc) native BITS=32        # build/native32/pokeemerald-native
./build/native32/pokeemerald-native                       # windowed, scale 3
./build/native32/pokeemerald-native --help                # options: --headless --silent --unthrottled --frames N --screenshot F --save PATH --rtc-offset PATH --scale N --replay SCRIPT --out DIR --rtc-fixed EPOCH
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

Expansion's test suite against the native build, through expansion's own
hydra (as `make check` runs it against mgba-rom-test; about 40 seconds on
32 cores, about 75 with `SANITIZE=1`, which runs the sanitized build;
`TESTS=` filters as expansion's does):

```
make -f Makefile.native -j32 check BITS=32
```

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
`pokeemerald.rtc`. 64-bit (`BITS=64`) compiles but does not link yet
(milestone 7).
