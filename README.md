# pokeemerald-expansion-native

A native PC build of [pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion),
the community engine that adds every Pokémon, move, ability, item, form
and battle gimmick to the pret decompilation of Pokémon Emerald. The game
logic is expansion's, pinned at `expansion/1.17.1`. This project adds a
platform layer under `src/platform/` that stands in for the Game Boy
Advance hardware (video, audio, input, timers and clock, DMA, BIOS calls,
save flash) using SDL2, so the game compiles and runs as an ordinary
desktop executable. No emulator, no ROM: everything builds from the
source and assets in this repository, and nothing extracted from a
cartridge is distributed.

The point is not to play Emerald on a PC. It is to have the complete
expansion engine, with every piece of content expansion contains, running
natively and proven by the same test suite expansion runs on the GBA, so
that the next phase can be adding new maps and story without an
emulator in the loop.

Status: see [docs/STATUS.md](docs/STATUS.md). It lists what is verified by
which automated gate, what is untested, and what is known broken. Nothing
is called working unless a gate proves it.

## Building

Linux first, 32-bit first, 64-bit next; Windows and macOS later. The
prerequisites and commands are in [docs/BUILDING.md](docs/BUILDING.md).
The GBA build still works and is the regression reference: expansion's
own `make` and `make check` are unchanged.

## How it is put together

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains the platform layer,
its boundary with expansion, and the memory model.
[docs/RESEARCH.md](docs/RESEARCH.md) records what we learned from the
GBA documentation and from the three earlier native ports, and which
approach we took for each subsystem.
[docs/DECISIONS.md](docs/DECISIONS.md) records the decisions with their
alternatives. Every change to an upstream file is listed in
[docs/UPSTREAM_MODIFICATIONS.md](docs/UPSTREAM_MODIFICATIONS.md), and
[docs/UPGRADING.md](docs/UPGRADING.md) is the procedure for moving to a
newer expansion tag.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) has the C rules for the platform
layer and how to run every gate locally.

## Credits

This project would not exist without the three earlier ports, which
proved the approach and showed where the hard parts are:

- [pokeemerald-native](https://github.com/fuddlesworth/pokeemerald-native)
  by NTx86 and fuddlesworth.
- [pokeemerald-multiplatform](https://github.com/gradenGnostic/pokeemerald-multiplatform)
  by gradenGnostic and contributors, descending from Kurausukun's port.
- [pokemon-regionalidades](https://github.com/hirata-producoes/pokemon-regionalidades)
  by hirata-producoes, the first to merge a platform layer into
  expansion, with a technical report that shaped our 64-bit plan.

And of course [pret](https://github.com/pret/pokeemerald) and
[RH-Hideout](https://github.com/rh-hideout/pokeemerald-expansion), whose
work this is. Full credits in [CREDITS.md](CREDITS.md).

## License

The platform layer, build glue, tests and documentation this project adds
are under the scoped MIT license in [LICENSE](LICENSE). It covers only
those files. The decompiled game code and all Nintendo, Creatures and
GAME FREAK material are not ours to license and are not covered.
