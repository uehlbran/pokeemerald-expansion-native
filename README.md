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

Verified by an automated gate (the gates and their commands are in
[docs/BUILDING.md](docs/BUILDING.md#gates)):

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
- The 64-bit build (`BITS=64`) links and plays both replays to the same
  dumps and screenshots, plain and sanitized, and runs expansion's suite with
  the 32-bit results; a save moves between the two builds (converted on load).
- The GBA build is unchanged and still passes its own suite.
- Windows (x86-64 only, cross-built from Linux) runs the platform's unit
  tests, a start-up smoke, both replays, expansion's suite (equal to
  Linux 64-bit, test by test) and the cross-OS save gate on a Windows
  runner, and the owner verified the build by hand on 2026-10-14: boot,
  the opening, the first battle, a save and its reload
  (docs/BUILDING.md, "Windows").
- The player's files: the save, the clock's offset and a settings file
  (`settings.ini`, which moves the GBA buttons to other keys and sets the
  window's scale and fast-forward) live in a per-user data directory, or
  beside the executable in portable mode; `--keys` prints the keys and the
  paths, and F1 shows them in an overlay over the game that also quits it
  ([docs/RUNNING.md](docs/RUNNING.md)). Unit tests cover the rules, the
  settings file and the overlay's picture; the Windows start-up smoke checks
  the paths `--keys` prints, in a given and in a portable directory.
- Steam Deck: the Linux 64-bit build with a controller binding for everything
  the keyboard does (fast-forward, the overlay and its quit, fullscreen),
  fullscreen by default on a Deck (`SteamDeck=1`), F11 for everyone, and a
  tarball that runs as a non-Steam game from the home directory
  ([docs/RUNNING.md](docs/RUNNING.md), "Steam Deck"). Unit-tested, not tried
  on a Deck: the owner's hand-check list is in docs/STATUS.md, "Needs the owner" (2b).
- Fast-forward: Tab held, or Shift+Tab to switch it on and off, at 2x (3x,
  4x or unlimited in the settings file), with the sound sped up or muted
  and an indicator in the corner; the frame pacer alone changes, so the
  replays and the suite are untouched. Unit-tested, the sound's queue on
  SDL's dummy device; not yet seen or heard on a screen.

Not working or not verified:

- macOS (x86_64, built on a Mac with Homebrew's tools; an Apple-silicon Mac
  runs it under Rosetta 2; a release-workflow archive and a one-command build
  script exist, equally unrun): the port is written, with its own layout, host
  layer, clang build and CI workflow (docs/BUILDING.md, "macOS"), and its
  clang build passes both replays and the suite on Linux, plain and
  sanitized, but nothing of it has run on a Mac beyond two probes: the macOS
  workflow's first runs were refused before any job started (the account's
  billing). The branch (`work/macos`) is parked, unmerged, until a run of
  the full macOS gate on `macos-15-intel` is green; merging it is the
  owner's call once GitHub Actions' billing is restored.
- GBA save interchange: native saves use the host's struct layout and are
  not GBA `.sav` files.
- Nothing past Birch's lab in play, the windowed path with a real display
  and audio device (the F1 overlay included: its picture is unit-tested, not
  seen on a screen), and most menus that neither replay opens have no gate.
  The renderer and the sound mixer are covered by unit tests and, beyond
  those, only by the replays' screenshots and the intro's audio.

## Playing and building

There is no release: nothing built is published, so you build the game. To
play, [docs/RUNNING.md](docs/RUNNING.md) has the keys, the options and
the files the game writes, for Linux, Windows and macOS. To build,
[docs/BUILDING.md](docs/BUILDING.md) has the prerequisites (Ubuntu packages,
including the 32-bit SDL2 libraries) and every variant. One command builds
the 64-bit game for an operating system, and prints where the executable is:

```
tools/build-native.sh --os linux      # build/native64/pokeemerald-native
tools/build-native.sh --os windows    # build/native64-windows/pokeemerald-native.exe
tools/build-native.sh --os macos      # build/native64/pokeemerald-native, on a Mac
```

The commands of the 32-bit build, the unit tests and the suite:

```
make -f Makefile.native -j"$(nproc)" native BITS=32
./build/native32/pokeemerald-native                 # a window, scale 3
./build/native32/pokeemerald-native --headless --silent --unthrottled --frames 9000 --screenshot title.ppm --data-dir out
make -C platform test BITS=32                       # the platform's unit tests, sanitized
make -f Makefile.native -j"$(nproc)" check BITS=32  # expansion's test suite on the native build
```

Keys: arrows, Z (A), X (B), A (L), S (R), Return (Start), Backspace
(Select), F1 for the help overlay and its quit, F11 for fullscreen, Tab held or Shift+Tab for
fast-forward; `settings.ini` changes them and `--keys` prints them with the
save's path. The first build converts every graphic (34 s on 32 cores). Expansion's
own `make` and `make check` still build and test the GBA ROM, which is the
regression reference.

## Documentation

- [docs/RUNNING.md](docs/RUNNING.md): for players: running the game, the
  keys and the settings file, the F1 overlay and quitting, fast-forward,
  where the files live (per-user or portable), the options.
- [docs/BUILDING.md](docs/BUILDING.md): prerequisites, every build variant
  and the gates.
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
