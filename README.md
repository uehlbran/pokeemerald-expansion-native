# pokeemerald-expansion-native

A native PC build of [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion),
the rh-hideout fork of the pret Emerald decompilation. The game
logic is expansion's, pinned at `expansion/1.17.1`. This project adds a
platform layer under `platform/` that stands in for the Game Boy
Advance hardware (video, audio, input, timers and clock, DMA, BIOS calls,
save flash) using SDL2, so the game compiles and runs as an ordinary
desktop executable. No emulator, no ROM: everything builds from the
source and assets in this repository, and nothing extracted from a
cartridge is distributed.

The aim is to run expansion's engine natively, checked by the same test
suite expansion runs on the GBA.

## Status

Verified by an automated gate (details, dates and commands in
[docs/STATUS.md](docs/STATUS.md)):

- The 32-bit build boots through the intro to the title screen, and
  plays the opening from New Game through the first battle and a save, a
  process restart and Continue (a scripted input replay, headless).
- The same replay drives expansion's own debug menu: gives of Pokémon
  and items, the party, summary and bag screens, trainer battles to the
  action prompt, a save and Continue.
- Both replays are clean under AddressSanitizer and UndefinedBehaviorSanitizer.
- Expansion's test suite (5862 tests) runs on the native build and gives,
  per test, the results it gives on the GBA, except nine benchmark tests,
  which cannot run natively and are reported as assumption failures.
- The GBA build is unchanged and still passes its own suite.

Not working or not verified:

- 64-bit: the platform's unit tests pass, but the game does not link yet.
- Windows and macOS: not attempted; Linux (built under WSL2 Ubuntu 24.04)
  is the only platform tried.
- GBA save interchange: native saves use the host's struct layout and are
  not GBA `.sav` files.
- Nothing past Birch's lab in play, the windowed path with a real display
  and audio device, and most menus that neither replay opens have no gate.
  The renderer and the sound mixer are covered by unit tests and, beyond
  those, only by the replays' screenshots and the intro's audio.

## Building

Prerequisites are in [docs/BUILDING.md](docs/BUILDING.md) (Ubuntu
packages, including the 32-bit SDL2 libraries). Then:

```
make -f Makefile.native -j"$(nproc)" native BITS=32
./build/native32/pokeemerald-native                 # a window, scale 3
./build/native32/pokeemerald-native --headless --silent --unthrottled --frames 9000 --screenshot title.ppm
make -C platform test BITS=32                       # the platform's unit tests, sanitized
make -f Makefile.native -j"$(nproc)" check BITS=32  # expansion's test suite on the native build
```

Keys: arrows, Z (A), X (B), A (L), S (R), Return (Start), Backspace
(Select). The first build converts every graphic (34 s on 32 cores). Expansion's
own `make` and `make check` still build and test the GBA ROM, which is the
regression reference.

## Documentation

- [docs/BUILDING.md](docs/BUILDING.md): prerequisites, every build variant
  and every gate.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the platform layer, its
  boundary with expansion, the memory model and the frame.
- [docs/STATUS.md](docs/STATUS.md): what is verified, by which gate.
- [docs/DECISIONS.md](docs/DECISIONS.md): decisions with their alternatives.
- [docs/UPSTREAM_MODIFICATIONS.md](docs/UPSTREAM_MODIFICATIONS.md): every
  change to an expansion file.
- [docs/UPGRADING.md](docs/UPGRADING.md): moving to a newer expansion tag.
- [docs/RESEARCH.md](docs/RESEARCH.md): what we learned from GBATEK and from
  the three earlier native ports.
- [platform/README.md](platform/README.md): the platform modules one by one.
- [CONTRIBUTING.md](CONTRIBUTING.md): the rules, the gates, how to add a
  module.

## Credits

The three earlier ports were read for structure and pitfalls (no code was
copied):

- [pokeemerald-native](https://github.com/fuddlesworth/pokeemerald-native)
  by NTx86 and fuddlesworth.
- [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform)
  by gradenGnostic and contributors, descending from Kurausukun's port.
- [pokemon-regionalidades](https://github.com/hirata-producoes/pokemon-regionalidades)
  by hirata-producoes, the first to merge a platform layer into
  expansion, with a technical report that shaped our 64-bit plan.

pret and RH-Hideout wrote the game code this project builds
([pret](https://github.com/pret/pokeemerald),
[RH-Hideout](https://github.com/rh-hideout/pokeemerald-expansion)). Full credits in [CREDITS.md](CREDITS.md).

## License

The platform layer, build glue, tests and documentation this project adds
are under the scoped MIT license in [LICENSE](LICENSE). It covers only
those files. The decompiled game code and all Nintendo, Creatures and
GAME FREAK material are not ours to license and are not covered.
