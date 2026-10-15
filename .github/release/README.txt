pokeemerald-native: a native PC build of pokeemerald-expansion (expansion/1.17.1).

Start it from anywhere: the save, the clock's file and an optional
settings.ini (to change the keys) live in your per-user data directory,
which `pokeemerald-native --keys` prints; put a settings.ini beside the
executable to keep them all in its folder instead (portable mode). F1 in the
game shows the keys and the paths, and Q there quits.

  Linux    ./pokeemerald-native
           Needs SDL2 2.0 installed (libsdl2-2.0-0 on Debian and Ubuntu); it is
           not bundled.
  Windows  pokeemerald-native.exe, with SDL2.dll in the same folder.
           Windows 10 version 1903 or later, 64-bit. The executable is not
           signed: if SmartScreen says "Windows protected your PC", click
           "More info", then "Run anyway".

RUNNING.md has the keys, the settings file, the options and where the files
are. LICENSE
covers this project's platform layer only; the game's code and assets are not
ours to license. The Windows zip also holds SDL2's licence (SDL2-LICENSE.txt).
