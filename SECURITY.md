# Security

## What the platform layer does with a network

Nothing. A search of `platform/` for `socket`, `getaddrinfo`, `gethostbyname`,
`curl`, `SDL_net` and the socket headers finds no match, and the only `http`
strings in the C sources are reference links in comments. The game links SDL2
and the C maths library and no other library (`NATIVE_LDLIBS` in
`Makefile.native`). It reads and writes files in its data directory (the save
file, the settings file, the log) and the files you name on its command line.
It makes no network connection, and it does not look for updates.

This describes the platform layer and the game's link line. It is not a
guarantee about SDL2, your operating system or the game's own C code, and the
build and release tooling (`tools/`, the workflows) is separate from the game.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting: open this repository's Security
tab and choose "Report a vulnerability". Do not open a public issue for it.

In scope: the files this project added, as `LICENSE` lists them (`platform/`,
`tests/`, the native build files and scripts, the workflows and actions, the
documents). A save file or replay script that makes the native build read or
write out of bounds, or write outside its data directory, is such a bug.

Not in scope: pokeemerald-expansion's game code (`src/`, `include/`, `data/`
and the rest). Report those to
[rh-hideout/pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion/issues).
If you are not sure which side a bug is on, report it here and say so.

Only the latest commit on `main` is supported.
