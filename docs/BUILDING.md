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
to a 64-bit build; a 64-bit build from nothing takes about as long as the
32-bit one (about 2 minutes with `-j32` including asset conversion). A table that
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
