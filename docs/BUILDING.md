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
hydra (as `make check` runs it against mgba-rom-test; about 35 seconds on
32 cores; `TESTS=` filters as expansion's does; `SANITIZE=1` runs the
sanitized build):

```
make -f Makefile.native -j32 check BITS=32
```

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

Save file: `pokeemerald.sav` in the working directory (a 28-byte header
plus the 128 KiB flash image; see `platform/README.md`); RTC offset in
`pokeemerald.rtc`. 64-bit (`BITS=64`) compiles but does not link yet
(milestone 7).
