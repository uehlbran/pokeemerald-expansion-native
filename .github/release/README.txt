pokeemerald-native: a native PC build of pokeemerald-expansion (expansion/1.17.1).

Start it from anywhere (on Linux, pokeemerald-native.sh runs it with the SDL2 in
this folder): the save, the clock's file and an optional
settings.ini (to change the keys) live in your per-user data directory,
which `pokeemerald-native --keys` prints; put a settings.ini beside the
executable to keep them all in its folder instead (portable mode). F1 in the
game shows the keys and the paths, and Q there quits.

  Linux    ./pokeemerald-native
           Needs SDL2 2.0 installed (libsdl2-2.0-0 on Debian and Ubuntu), unless
           it is started through pokeemerald-native.sh, which uses the copy
           in this folder.
  Steam Deck  Copy this folder to your home directory in Desktop Mode, then
           in Steam: Games > Add a Non-Steam Game, Browse, pick
           pokeemerald-native.sh (set the file filter to All files). The
           game starts fullscreen on a Deck; RUNNING.md has the controller
           buttons. The script runs the game with the SDL2 in this folder
           (libSDL2-2.0.so.0, licence in SDL2-LICENSE.txt: it needs glibc
           2.38 and the system's X11, Wayland, audio and graphics libraries,
           which SteamOS has).
  Windows  pokeemerald-native.exe, with SDL2.dll in the same folder.
           Windows 10 version 1903 or later, 64-bit. The executable is not
           signed: if SmartScreen says "Windows protected your PC", click
           "More info", then "Run anyway".

  macOS    pokeemerald-native, with libSDL2-2.0.0.dylib and libSDL3.dylib in
           the same folder. An Intel Mac, or an Apple-silicon Mac with
           Rosetta 2 (macOS offers to install it). It is not signed or
           notarized, so macOS blocks it the first time: on macOS 14 and
           earlier Control-click the file, choose Open, then Open; on macOS 15
           and later try to open it, then System Settings > Privacy &
           Security > Open Anyway; or in Terminal run
           xattr -dr com.apple.quarantine <this folder>. RUNNING.md has the
           details and Apple's pages. This build has not yet been run on a Mac
           by this project's CI.

RUNNING.md has the keys, the settings file, the options and where the files
are. LICENSE
covers this project's platform layer only; the game's code and assets are not
ours to license. The Windows zip also holds SDL2's licence (SDL2-LICENSE.txt); the macOS
tarball holds the licences of the SDL libraries it bundles (sdl2-compat's, as
libSDL2-2.0.0-LICENSE.txt, and SDL3's, as libSDL3-LICENSE.txt); both are zlib.
