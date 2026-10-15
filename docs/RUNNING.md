# Running the game

This page is for people who want to play. To build it, see
[BUILDING.md](BUILDING.md).

## Getting a build

There is no public release: an executable contains the game's complete
assets, so none is published (the repository's README says why this project
distributes none). You build it yourself, with one command per system, from
[BUILDING.md](BUILDING.md):

```
tools/build-native.sh --os linux      # build/native64/pokeemerald-native
tools/build-native.sh --os windows    # build/native64-windows/pokeemerald-native.exe, built on Linux
```

The Windows executable is cross-built on Linux (WSL2 is fine). The owner can
also get archives for testing from a run of the repository's "release"
workflow, which publishes nothing unless told to; their README.txt and this
page are inside. macOS is not started: there is no build for it.

## Linux

64-bit x86. The build needs SDL2 at run time (`libsdl2-2.0-0` on Debian and
Ubuntu); nothing else. Run it from the directory where you want the save:

```
./pokeemerald-native
```

## Windows

64-bit Windows 10 version 1903 (the May 2019 Update) or later. Keep
`pokeemerald-native.exe` and `SDL2.dll` in the same folder and run the
executable from the folder where you want the save. A console window opens
beside the game; it shows the game's messages and is not needed to play.

The executable is not signed, so Windows SmartScreen may say "Windows
protected your PC". Click "More info", then "Run anyway". Paths and names
with characters outside your system's language (accents, other scripts) work.

There is no 32-bit Windows build and no sanitizer build for Windows.

## Keys

| Keyboard | GBA button |
|---|---|
| Z | A |
| X | B |
| Return | Start |
| Backspace | Select |
| Arrow keys | D-pad |
| A | L |
| S | R |

A game controller that SDL2 recognises works as well: its A, B, Start, Back,
shoulder buttons, D-pad and left stick are the GBA's A, B, Start, Select, L,
R and D-pad. The window is 3 times the GBA's screen; `--scale` changes it.

## Options

`pokeemerald-native --help` prints this (copied by hand, so the program's
output wins if they differ):

```
usage: pokeemerald-native [options]
  --headless         no window (input reads no keys); the frames still run
  --silent           no audio device
  --save PATH        the save file (default pokeemerald.sav)
  --rtc-offset PATH  the real-time clock's offset file (default pokeemerald.rtc)
  --scale N          window scale, 1 to 16 (default 3)
  --unthrottled      do not hold the GBA frame rate
  --frames N         exit after N frames (exit status 0)
  --screenshot PATH  write the last frame to PATH as a PPM when the game ends
  --test-runner      run expansion's test runner as mgba-rom-test does for its hydra:
                     debug output on stdout, the runner's exit status, reboot on a
                     crash; no save or clock file is read or written
  --persistent-state HEX
                     (added by the reboot) the runner's state to start from
  --replay SCRIPT    play a replay script (tests/smoke/README.md): the keypad reads the script, not the
                     keyboard; exit 0 when the script ends, 1 on the first failed command
  --out DIR          where the replay's screenshots and dumps go (default .)
  --rtc-fixed EPOCH  the clock starts at EPOCH (Unix seconds) and advances with the frame count,
                     so a replay sees the same time every run
  --resume-at LINE   (a restart adds it) continue the replay after script line LINE
  --audio-dump PATH  also write the sound output to PATH as a WAV file (16-bit stereo at the
                     output's rate), headless and --silent too; a replay's restart ends it; its
                     sizes are written at exit (after a crash: sox --ignore-length reads it)
  --help             this text
```

A player needs `--scale`, `--save`, `--rtc-offset` and `--silent`; the rest
are for the project's tests (`--audio-dump` records what you hear, at the
GBA's 32768 Hz, for comparing builds).

## The files the game writes

All of them go where the options say, and by default into the directory you
started the game from:

- `pokeemerald.sav`: the save (`--save PATH`). A 28-byte header of this
  project's, then the 128 KiB flash image the game saves to. It is written
  whenever the game saves, by writing a temporary file and renaming it, so a
  crash leaves the old save or the new one. A save moves between the 32-bit
  and the 64-bit build (it is converted when loaded).
- `pokeemerald.sav.lock`: held while a game runs on that save. It is empty;
  leave it alone.
- `pokeemerald.rtc`: the real-time clock's offset (`--rtc-offset PATH`). The
  game's clock follows your computer's; this file holds the difference when
  you set the time in the game.

## What the game refuses, and why

- A second copy on the same save: it prints that another instance holds the
  save and does not start, so that two games cannot write over each other's
  saves. Close the first, or give the second its own `--save`.
- A save file without this project's header, which is what an emulator or a
  flash cartridge writes: it prints that it looks like an emulator or
  cartridge save and leaves the file untouched. Importing those saves is not
  supported; this game's saves are not GBA `.sav` files either.

## Planned, not built

A settings file for the keys, saves and settings in a per-user folder (with a
portable mode beside the executable), an in-game overlay on F1, and no
console window on Windows.
