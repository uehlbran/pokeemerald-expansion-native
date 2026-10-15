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
tools/build-native.sh --os macos      # build/native64/pokeemerald-native, built on a Mac
```

The Windows executable is cross-built on Linux (WSL2 is fine); the macOS one
is built on a Mac. Archives for testing can also be had from a run of the
repository's "release" workflow, which publishes nothing unless told to; their
README.txt and this page are inside.

## Linux

64-bit x86. The build needs SDL2 at run time (`libsdl2-2.0-0` on Debian and
Ubuntu); nothing else. Start it from anywhere: the save and the settings live
in your per-user data directory ([below](#where-the-files-live)).

```
./pokeemerald-native
```

## Windows

64-bit Windows 10 version 1903 (the May 2019 Update) or later. Keep
`pokeemerald-native.exe` and `SDL2.dll` in the same folder and start the
executable from anywhere: the save and the settings live in your per-user
data directory ([below](#where-the-files-live)). No console window opens
beside the game; where its messages go instead is
[below](#no-console-window-on-windows).

The executable is not signed, so Windows SmartScreen may say "Windows
protected your PC". Click "More info", then "Run anyway". Paths and names
with characters outside your system's language (accents, other scripts) work.

There is no 32-bit Windows build and no sanitizer build for Windows.

## macOS

An Intel Mac, or an Apple-silicon Mac through Rosetta 2: the build is x86-64
only. The first time you open an x86-64 program on Apple silicon, macOS offers
to install Rosetta; accept ([Apple: About Rosetta](https://support.apple.com/en-us/102527)).
Rosetta is available through macOS 27; from macOS 28 it runs only certain
older games, so this build has a horizon (`docs/DECISIONS.md`, "macOS ships as
x86_64 under Rosetta").

**This build has not yet passed this project's macOS CI** (`macos.yml` on
GitHub's `macos-15-intel` runner); until it has, treat everything on this
page about macOS as untried.

**A build you make yourself** (`tools/build-native.sh --os macos` on the Mac)
is not quarantined and needs none of the steps below: macOS only blocks
programs that came from a download.

**The release archive** (`pokeemerald-native-...-macos-x86_64.tar.gz`) holds
the executable and the SDL libraries it loads (`libSDL2-2.0.0.dylib` and
`libSDL3.dylib`, with their licences), which it finds in its own folder. It is
not signed with a developer identity and not notarized, so Gatekeeper blocks it
the first time. Unpack it (double-click in Finder), then allow it, whichever
way fits your macOS:

- **macOS 14 Sonoma and earlier.** In Finder, Control-click (right-click) the
  file `pokeemerald-native` and choose Open, then Open in the dialog. macOS
  keeps the exception, and you can start it normally after that
  ([Apple, macOS 14 guide](https://support.apple.com/guide/mac-help/open-a-mac-app-from-an-unidentified-developer-mh40616/14.0/mac/14.0)).
- **macOS 15 Sequoia and later.** Apple removed the Control-click override.
  Try to open the file once and let macOS refuse, then open System Settings >
  Privacy & Security, scroll to the Security section and click "Open Anyway"
  (offered for about an hour after the refusal), enter your password, and
  click Open when the warning appears again
  ([Apple, Open a Mac app from an unidentified developer](https://support.apple.com/guide/mac-help/mh40616/mac);
  [Apple, Safely open apps on your Mac](https://support.apple.com/en-us/102445)).
- **From Terminal, any version.** Remove the quarantine mark from every file in
  the unpacked folder, then start the game from it:

  ```
  xattr -dr com.apple.quarantine pokeemerald-native-...-macos-x86_64
  cd pokeemerald-native-...-macos-x86_64 && ./pokeemerald-native
  ```

  `-d` deletes the attribute and `-r` does so for every file under the
  folder, the two libraries included (`man xattr`); Apple's support pages
  describe only the Open and Open Anyway steps. It is also the safest way
  here, because those steps apply to one file, and macOS may ask again for
  each library in the folder (not tried). `xattr -l pokeemerald-native`
  shows whether a file carries the mark: no `com.apple.quarantine` line, and
  there is nothing to remove.

Apple's pages describe apps; this is a plain program and the same Gatekeeper
rules should apply, but that is untried here too. Overriding Gatekeeper is
your decision: do it only for a build you trust. Apple's own advice is to
prefer the App Store.

Your save and settings are in
`~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/`
([below](#where-the-files-live)).

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
| F1 | the help overlay (not a GBA button) |
| F11 | fullscreen on and off ([below](#fullscreen)) |
| Tab, held | fast-forward while held ([below](#fast-forward)) |
| Shift+Tab | fast-forward on, and off again |

Those are the defaults; the settings file ([below](#the-settings-file))
moves any GBA button, and either fast-forward key, to another key. The keys are physical positions: on a
keyboard that is not QWERTY, "Z" is the key where QWERTY has its Z.

A game controller that SDL2 recognises works as well, and does everything the
keyboard does:

| Controller | Does |
|---|---|
| A, B | the GBA's A and B |
| Start, Back | the GBA's Start and Select |
| Left and right shoulder | the GBA's L and R |
| D-pad, left stick | the GBA's D-pad |
| Right trigger, held | fast-forward while held |
| Right stick, clicked | fast-forward on, and off again |
| Y | the help overlay, shown and hidden |
| B, while the overlay is shown | closes it (the game does not see that B) |
| X, while the overlay is shown | quits |
| Left trigger | fullscreen on and off |

On a Steam Deck these are the Deck's own buttons, as Steam Input presents
them to the game ([Steam Deck](#steam-deck)). The GBA buttons are fixed (Steam
Input can remap them); the last six rows are settings that move to other
buttons ([below](#the-settings-file)). A trigger counts as pressed past about
half its travel. A button given to one of those functions still does its GBA
job too, so give them buttons the GBA layout leaves free (X, Y, the stick
clicks, the triggers, the guide button).

## The help overlay: F1, and quitting

F1 shows the help overlay over the game: the version, the data directory and
the paths of the save, the clock's file and the settings file, the keys in
force, the controller's buttons and fast-forward's keys, speed and sound,
under the line `Q: quit  F1: close`; once the last thing you pressed was a
controller button, stick or trigger, the line names the controller's instead (`X: quit  B: close`).
The overlay shows those lines from the top as far as the window has room: in
a small window (`--scale 1` or 2) the last ones may not fit, and
`pokeemerald-native --keys` prints them all.
F1 again closes it (the controller's Y shows and hides it, B closes it). While
it is shown the game keeps running but receives no key and no controller
button, so nothing you press reaches the game; a button still down when it closes
(the B that closed it, say) stays released until you let go of it.

Q, or the controller's X, while the overlay is shown, quits the game, as
closing the window does.
Quitting does not save your game: save in the game first (Start, then SAVE),
as on the GBA. The game's Start menu has no quit; its EXIT closes the menu,
as in the original.

## Fast-forward

Hold Tab and the game runs at twice its speed until you let go. Press
Shift+Tab (Tab with either Shift key) to switch fast-forward on, and again
to switch it off. While it is on, `FF 2x` shows in the top-right corner of
the window (`FF 3x`, `FF 4x` or `FF max` at the other speeds).

- The speed is a setting, `fast_forward_speed` ([below](#the-settings-file)):
  2, 3 or 4 times the GBA's speed, or `unlimited`, as fast as your computer
  runs the game. The game itself is unchanged: it runs the same frames, more
  of them a second. Everything it counts in frames hurries along (walking,
  text, animations, and the play time on your trainer card); the clock
  (the time of day, Berries, tides) keeps your computer's time.
- The sound plays sped up, faster and higher, as on an emulator: at 2x an
  octave higher. At `unlimited` it plays in fragments, and when fast-forward
  ends it starts again cleanly after a moment's silence.
  `fast_forward_audio = mute` silences the game while fast-forward is on
  instead.
- Both keys can be changed in the settings file (`fast_forward` for the one
  you hold, `fast_forward_toggle` for the switch). A fast-forward key may
  have `shift+`, `ctrl+` or `alt+`, or several, in front of it, as
  `shift+tab` has; it then works only with exactly those held, so Tab and
  Shift+Tab are two keys. A GBA button and fast-forward may share a key:
  pressing it does both.
- Letting go of the hold key ends fast-forward, and so does switching to
  another window while you hold it. Fast-forward switched on with Shift+Tab
  stays on until you press it again, switch to another window or quit; after
  you come back, Shift+Tab switches it on again.
- While the F1 overlay is shown, the fast-forward keys do nothing (letting
  go of a held Tab still ends it). Fast-forward switched on stays on under
  the overlay, which covers the indicator.
- A replay, a game started with `--headless`, and the test runner never
  fast-forward.

Fast-forward also works from a controller: hold the right trigger, or click
the right stick to switch it on and off. The hold ends when the trigger is
let go, when the controller is unplugged and when the window loses focus; the
switch ends when the window loses focus too (unplugging the controller leaves
it on). While the overlay is shown the controller's fast-forward buttons do
nothing either.

## Fullscreen

F11, or the controller's left trigger, switches the window between a window and
fullscreen. Fullscreen takes the whole screen at its own resolution and keeps
the picture's shape: the game is drawn as large as a whole multiple of 240x160
allows, centred, with black bars (a 1280x800 screen shows it five times as
large, 1200x800). On a Steam Deck the game starts fullscreen, because
SteamOS sets `SteamDeck=1` for what it runs; elsewhere it starts in a window.
`fullscreen = yes` or `no` in the settings file decides for yourself, on any
machine (`auto`, the default, is the Deck rule). A replay and a
`--headless` run are never fullscreen.

## Steam Deck

The Linux 64-bit build is the Deck's build: SteamOS is a Linux x86-64 system.
No build is distributed, so you make the executable yourself, and none of this
has been tried on a Deck.

1. Build it as [docs/BUILDING.md](BUILDING.md) says for Linux, either on a Linux
   PC or on the Deck in Desktop Mode. The result is
   `build/native64/pokeemerald-native`. The Deck needs SDL2 installed and a
   glibc at least as new as the one the executable was built against; a build
   on the Deck meets both.
2. Put the executable in its own folder on the Deck, for example
   `/home/deck/Games/pokeemerald-native/`, and make sure it is executable
   (`chmod +x pokeemerald-native`).
3. In Desktop Mode, open Steam, then Games > Add a Non-Steam Game to My
   Library > Browse, set the file filter to All files, pick `pokeemerald-native`
   from that folder and add it. No launch options are needed.
4. Return to Game Mode: the game is in the library under Non-Steam.

You do not need a launcher script. `.github/release/pokeemerald-native.sh`
exists only to run a release archive's executable with the SDL2 library
bundled beside it, and a build you made links the system's SDL2.

The game starts fullscreen. The Steam button and the quick-access button
belong to Steam and the game never sees them; the controller table
[above](#keys) lists the rest: A, B, Start, Back, the shoulders and the D-pad
play, the right trigger and a right-stick click fast-forward, Y opens the
overlay (B closes it, X quits), the left trigger toggles fullscreen. If Steam
Input's default layout for non-Steam games maps those buttons differently,
choose "Gamepad" in the controller settings for the game.

Your save is in the per-user data directory like anywhere on Linux:
`/home/deck/.local/share/pokeemerald-expansion-native/pokeemerald-native/`.
Put a `settings.ini` beside the executable instead if you want the folder to
carry everything ([portable mode](#where-the-files-live)).

If the game does not start from Steam, run `./pokeemerald-native` in Konsole,
from its folder, to see the loader's message.

## The settings file

`settings.ini` in the data directory ([below](#where-the-files-live)). The
game reads it when it starts and never writes it, so you make it yourself
with any text editor; without it the game uses the defaults. One setting a
line, `setting = value`; a line starting with `#` is a comment. The whole
file with the defaults:

```
# pokeemerald-native settings: one "setting = value" a line.
# A key is named as SDL names it: Z, X, Return, Backspace, Space,
# Left Shift, Right Ctrl, Tab, Keypad 8, F2, Up, Down, Left, Right ...
a = z
b = x
select = backspace
start = return
up = up
down = down
left = left
right = right
l = a
r = s
# The window: 1 to 16 times the GBA's 240x160 screen.
scale = 3
# Fast-forward: hold the first key, or press the second to switch it on
# and off; shift+, ctrl+ and alt+ go in front of a key.
fast_forward = tab
fast_forward_toggle = shift+tab
# 2, 3 or 4 times the GBA's speed, or unlimited.
fast_forward_speed = 2
# While fast-forward is on, the sound: play (sped up) or mute.
fast_forward_audio = play
# Fullscreen: yes, no, or auto (fullscreen on a Steam Deck only), and its key.
fullscreen = auto
fullscreen_toggle = f11
# The controller's functions (SDL's button names, or lefttrigger and
# righttrigger); the GBA buttons on a controller are fixed.
pad_fast_forward = righttrigger
pad_fast_forward_toggle = rightstick
pad_overlay = y
pad_overlay_close = b
pad_quit = x
pad_fullscreen = lefttrigger
```

The settings are the ten GBA buttons (`a`, `b`, `select`, `start`, `up`,
`down`, `left`, `right`, `l`, `r`), `scale`, and fast-forward's four
(`fast_forward`, `fast_forward_toggle`, `fast_forward_speed`,
`fast_forward_audio`; [above](#fast-forward)), `fullscreen` and
`fullscreen_toggle` ([above](#fullscreen)), and the controller's six
(`pad_fast_forward`, `pad_fast_forward_toggle`, `pad_overlay`,
`pad_overlay_close`, `pad_quit`, `pad_fullscreen`), each a button as SDL names
it (`a`, `b`, `x`, `y`, `back`, `start`, `guide`, `leftstick`, `rightstick`,
`leftshoulder`, `rightshoulder`, `dpup`, `dpdown`, `dpleft`, `dpright`, ...) or
`lefttrigger` or `righttrigger`. One button serves one controller function: if
two lines name the same button, the later is reported and keeps its default. A comment is a whole line:
`a = z  # my key` reads the key name `z  # my key`, which is not one. Names and keys can be
written in any case, with or without spaces around the `=`; a setting you
leave out keeps its default, and if a setting appears twice the later line
wins. Two buttons may share a key. F1 cannot be a button's, a
fast-forward or the fullscreen key: it opens the overlay. The two fast-forward keys must
differ: if the file makes them one key, the later of the two lines is
reported and that key keeps its default (to swap them, write both lines).
`pokeemerald-native --keys` prints the keys in force with SDL's spelling of
each name, so it also shows how to write one.

A line the game cannot use (a misspelt setting, a key name SDL does not know,
a scale outside 1 to 16, a fast-forward speed other than 2, 3, 4 or
`unlimited`, a controller button name SDL does not know, a `fullscreen` other than yes, no or auto, a line without `=`) is reported with its line number
on standard error (on Windows, [see below](#no-console-window-on-windows)), for example

```
settings: /home/you/.local/share/pokeemerald-expansion-native/pokeemerald-native/settings.ini:4: 'jump' is not a setting (a, b, select, start, right, left, up, down, r, l, scale, fast_forward, fast_forward_toggle, fast_forward_speed, fast_forward_audio, fullscreen, fullscreen_toggle, pad_fast_forward, pad_fast_forward_toggle, pad_overlay, pad_overlay_close, pad_quit, pad_fullscreen); the line is ignored
```

and the game starts with the rest of the file. `--scale` on the command
line wins over the file's `scale`. A game started with `--headless` has no
window and no keyboard, and reads no settings file.

## Where the files live

The save, the clock's file and the settings file live together in one data
directory, whatever folder you start the game from:

| System | The per-user data directory |
|---|---|
| Linux | `~/.local/share/pokeemerald-expansion-native/pokeemerald-native/` (`$XDG_DATA_HOME/...` if you set that variable) |
| Windows | `%APPDATA%\pokeemerald-expansion-native\pokeemerald-native\`, that is `C:\Users\YOU\AppData\Roaming\pokeemerald-expansion-native\pokeemerald-native\` |
| macOS | `~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/` |

The game makes the directory the first time it starts.
`pokeemerald-native --keys` prints which directory is in use and the path of
each file in it.

**Portable mode.** Put a file named `settings.ini` beside the executable
(an empty one will do, or one with your keys). From then on that folder is
the data directory: the save, the clock's file and the settings are kept
beside the executable, for a game on a USB stick or in a folder of its own.
The folder must be one you can write to (not under `Program Files`).

**Choosing it yourself.** `--data-dir DIR` makes `DIR` the data directory
(the game creates it if it is missing; its parent must exist; a file of that
name is refused), whatever the two rules above say. `--save PATH` and `--rtc-offset PATH` choose one file
each and win over the data directory for that file; a relative path is taken
from the folder you start the game in.

**Saves from before.** Builds before this one kept `pokeemerald.sav` and
`pokeemerald.rtc` in the folder the game was started from. To keep playing
that save, either move both files into the per-user directory
(`pokeemerald-native --keys` prints it, and makes it if it is not there
yet), or, if they are beside the executable, put a `settings.ini` there to
keep them where they are (portable mode). The old `pokeemerald.sav.lock` can
be deleted.

## Options

`pokeemerald-native --help` prints this (copied by hand, so the program's
output wins if they differ):

```
usage: pokeemerald-native [options]
  --headless         no window (input reads no keys, no settings file is read); the frames still run
  --silent           no audio device
  --data-dir DIR     where the save, the clock's file and settings.ini are (made if missing); default:
                     the executable's directory if settings.ini is there (portable), else the
                     per-user directory (--keys prints it)
  --save PATH        the save file (default pokeemerald.sav in the data directory)
  --rtc-offset PATH  the real-time clock's offset file (default pokeemerald.rtc in the data directory)
  --scale N          window scale, 1 to 16 (default: the settings file's, else 3)
  --keys             print the keys, the data directory, the files and the version, and exit
  --version          print the version and exit
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

A player needs `--keys`, `--version`, `--scale`, `--silent` and perhaps
`--data-dir`, `--save` and `--rtc-offset`; the rest are for the project's
tests (`--audio-dump` records what you hear, at the GBA's 65536 Hz, for
comparing builds). `--keys` prints this (here run with
`--data-dir /home/you/pokeemerald-data` and no settings file), and the game then exits:

```
pokeemerald-native a554c4080f
Data directory (--data-dir): /home/you/pokeemerald-data
Save: /home/you/pokeemerald-data/pokeemerald.sav
Clock: /home/you/pokeemerald-data/pokeemerald.rtc
Settings: /home/you/pokeemerald-data/settings.ini (not there: the defaults)
Keyboard:
  A          Z
  B          X
  Select     Backspace
  Start      Return
  Right      Right
  Left       Left
  Up         Up
  Down       Down
  R          S
  L          A
  Fullscreen F11
Controller:
  GBA buttons: A, B, Back (Select), Start, the shoulders (L, R), the D-pad or the left stick
  Fast-forward hold    Right trigger
  Fast-forward toggle  Right stick click
  Help overlay         Y
  Overlay close        B
  Overlay quit         X
  Fullscreen           Left trigger
Fast-forward:
  Hold       Tab
  Toggle     Shift+Tab
  Speed      2x
  Sound      sped up
```

The mode in brackets is `per-user`, `portable` or `--data-dir`. The version
is the commit the game was built from, `-dirty` if the build had changes not
committed, or `unknown` for a build made without git; `--version` prints it
alone (`a554c4080f`).

## The files the game writes

All of them go into the data directory, unless `--save` or `--rtc-offset`
says otherwise:

- `pokeemerald.sav`: the save. A 28-byte header of this project's, then the
  128 KiB flash image the game saves to. It is written whenever the game
  saves, by writing a temporary file and renaming it, so a crash leaves the
  old save or the new one. A save moves between the 32-bit and the 64-bit
  build (it is converted when loaded).
- `pokeemerald.sav.lock`: held while a game runs on that save. It is empty;
  leave it alone.
- `pokeemerald.rtc`: the real-time clock's offset. The game's clock follows
  your computer's; this file holds the difference when you set the time in
  the game.

`settings.ini` is yours: the game only reads it.

## What the game refuses, and why

- A second copy on the same save: it prints that another instance holds the
  save and does not start, so that two games cannot write over each other's
  saves. Close the first, or give the second its own `--save` or
  `--data-dir`.
- A save file without this project's header, which is what an emulator or a
  flash cartridge writes: it prints that it looks like an emulator or
  cartridge save and leaves the file untouched. Importing those saves is not
  supported; this game's saves are not GBA `.sav` files either.
- A settings file it cannot read (no permission to, or larger than 16 KiB),
  or a data directory it cannot make or that is a file: it says which and
  does not start.

## No console window on Windows

The Windows executable is a graphical program, so starting it from Explorer,
a shortcut or Steam opens the game window and nothing else. The game still
prints (the refusals above, `--keys`, `--version`), and where that goes
depends on how it was started:

| Started from | The messages go to |
|---|---|
| Explorer, a shortcut, Steam | `pokeemerald.log` in the data directory ([above](#where-the-files-live): the per-user directory, the executable's own folder in portable mode, or `--data-dir`), emptied at each start, so it never grows. The messages that stop the game from starting are also shown in a message box that names the error and the log file. |
| A command prompt (`cmd.exe`, PowerShell) with nothing redirected | the prompt. The prompt does not wait for a graphical program, so it comes back at once and the messages appear after it; run `start /wait pokeemerald-native.exe --keys` in `cmd.exe`, or `Start-Process -Wait -NoNewWindow` in PowerShell, to have it wait. |
| A pipe or a file (`> out.txt`, `\| more`, a script) | there, as with any program. |
| A launcher that gives the game one stream (say, output to a file) and no console | that stream; the other, if it is left out, goes to `pokeemerald.log` as above (no message box then, since someone reads the stream). |

If no data directory can be found at all, the log is written beside the
executable and says so. Linux and macOS programs print to the terminal as
they always have; nothing changes there.
