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

### Native target

Not available yet; milestone 2. It will be:

```
make -f Makefile.native -j$(nproc) BITS=32
make -C platform test
```
