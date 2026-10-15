# Getting started

This page takes you from nothing to the game running on your computer. You
need a computer, an internet connection and 5 GB of free disk space: the code
and one build take about 1 GB, and Ubuntu and its tools take the rest. The
first build is the slow part; every later one is quicker. You build the game
yourself because its assets are not this project's to distribute
([why](../README.md#faq)). You do not need to know how to program. Each step
below is one small action.

Three words you will meet. A *terminal* is a window where you type commands.
A *command* is one line of text that tells the computer to do something. The
*prompt* is the line in the terminal, ending in `$` or `>`, where you type;
when it comes back, the last command has finished. You copy each command from
a grey box on this page, paste it into the terminal and press Enter.

## Pick your computer

- [Windows 10 or 11](#windows-10-or-11)
- [Linux: Ubuntu 24.04 and systems based on it](#linux-ubuntu-2404)
- [Steam Deck](#steam-deck)
- macOS: not available yet. The port is in progress on the branch
  `work/macos`, and this guide will cover it when it is ready.

## Windows 10 or 11

The game is built inside Ubuntu, a Linux system that Windows can run for you
through WSL (Windows Subsystem for Linux). You then copy the finished game
back to Windows.

1. Open the Start menu and type `PowerShell`. Right-click "Windows PowerShell"
   and choose "Run as administrator". Answer "Yes" if Windows asks.

2. Install Ubuntu. Paste this into the PowerShell window and press Enter:

   ```
   wsl --install -d Ubuntu-24.04
   ```

   If WSL is already installed, Ubuntu may start at once inside this same
   window and ask the questions of step 5 there: answer them there, and skip
   steps 3 and 4. If the command cannot find the name "Ubuntu-24.04", run
   `wsl --list --online` to see the names your Windows offers, and use the one
   for 24.04.

3. If the text ends with a line saying the system must be restarted ("Changes
   will not be effective until the system is rebooted"), restart the computer
   yourself: Start menu, the power button, "Restart". The command does not ask.

4. After the restart, a window titled Ubuntu may open by itself and ask for a
   user name. If no Ubuntu window opens within a minute, open the Start menu,
   type `Ubuntu` and open the entry whose name starts with Ubuntu. Wait while
   it sets itself up.

5. It asks for a user name. Type letters only, no spaces (your first name will
   do) and press Enter. Then it asks for a password: type one and press Enter,
   then type it again. The password does not show while you type, not even as
   dots. That is normal. You will need it for `sudo`, a prefix that runs a
   command with administrator rights.

6. This window is your terminal. To paste into it, right-click inside it, or
   press Ctrl+Shift+V. Paste this and press Enter. It updates the list of
   software Ubuntu can install:

   ```
   sudo apt update
   ```

   If it asks for your password, type it.

7. Install the tools the build needs. A *package* is a piece of software
   Ubuntu installs for you. This installs all of them in one go:

   ```
   sudo apt install -y build-essential git python3 pkg-config curl clang gcc-multilib libpng-dev libsdl2-dev gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
   ```

   A lot of text scrolls past while it downloads and installs, and it can take
   a while. Do not close the window; wait for the prompt to come back.

   Stay in this window, in the folder it opened in. Do not move to a folder
   whose path starts with `/mnt/c`: that is your Windows drive, and building
   there is slow and can fail.

8. Download the code. A *repository* is a project's folder of code, and
   `git clone` copies one from the internet. `--depth 1` skips the project's
   long history, which you do not need:

   ```
   git clone --depth 1 https://github.com/uehlbran/pokeemerald-expansion-native.git
   ```

9. Go into the new folder:

   ```
   cd pokeemerald-expansion-native
   ```

10. Build the Windows game:

    ```
    tools/build-native.sh --os windows
    ```

    Lots of text scrolls past, and some of it says "warning". That is normal.
    This is the slow step, and the window can look still for a while. Do not
    close it; wait for the prompt to come back. Success looks like this, as
    the last line:

    ```
    build-native: done: /home/YOU/pokeemerald-expansion-native/build/native64-windows/pokeemerald-native.exe
    ```

    where YOU is your user name. If the last lines say "missing
    prerequisites", step 7 did not finish: run step 7 again (the guide's
    install command, not the lines the message mentions), then run this step
    again.

11. Open the folder with the finished game in Windows Explorer:

    ```
    explorer.exe build/native64-windows
    ```

12. In the Explorer window that opens, click `pokeemerald-native.exe`, hold
    Ctrl and click `SDL2.dll`, then press Ctrl+C. Nothing else in the folder
    is needed. Minimise the window, right-click an empty part of the Desktop,
    choose "New", then "Folder", and name it `Pokemon`. Open the new folder
    and press Ctrl+V. Keep the two files together.

13. Double-click `pokeemerald-native.exe` (the game file) in the new folder.
    The game opens in a window.

    The first time, Windows may show a blue box, "Windows protected your PC".
    It appears because the executable is not signed: signing costs money and
    the project does not do it. Click "More info", then "Run anyway".

Now play. The keys are [below](#keys). To quit, close the window. Save inside
the game first (Start, then SAVE), as on the original.

Your save is in `C:\Users\YOU\AppData\Roaming\pokeemerald-expansion-native\pokeemerald-native\`.
To open it, press Windows+R, type `%APPDATA%` and press Enter, then open
`pokeemerald-expansion-native` and `pokeemerald-native`.

### Updating later

Close the game. Open Ubuntu from the Start menu, then paste these one at a time:

```
cd pokeemerald-expansion-native
```

```
git pull
```

```
tools/build-native.sh --os windows
```

```
explorer.exe build/native64-windows
```

Copy the two files to your Desktop folder again, as in step 12, and replace
the old ones. Your save stays where it is.

## Linux (Ubuntu 24.04)

For Ubuntu 24.04 and systems based on it. Other distributions: the package
names differ, and [BUILDING.md](BUILDING.md#prerequisites) lists what the build
needs.

1. Open a terminal: press Ctrl+Alt+T. To paste into it, press Ctrl+Shift+V.

2. Update the software list. If it asks for your password, type it (it does
   not show while you type):

   ```
   sudo apt update
   ```

3. Install the tools:

   ```
   sudo apt install -y build-essential git python3 pkg-config curl clang gcc-multilib libpng-dev libsdl2-dev gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64
   ```

   A lot of text scrolls past, and it can take a while. Wait for the prompt to
   come back.

4. Download the code:

   ```
   git clone --depth 1 https://github.com/uehlbran/pokeemerald-expansion-native.git
   ```

5. Go into the folder:

   ```
   cd pokeemerald-expansion-native
   ```

6. Build. This is the slow step; text scrolls past, some of it "warning", and
   it can look still for a while. Wait for the prompt to come back. The last
   line should read `build-native: done:` and the path of the game:

   ```
   tools/build-native.sh --os linux
   ```

7. Run the game:

   ```
   ./build/native64/pokeemerald-native
   ```

Your save is in `~/.local/share/pokeemerald-expansion-native/pokeemerald-native/`.

To update later, run these in the folder, one at a time:

```
git pull
```

```
tools/build-native.sh --os linux
```

```
./build/native64/pokeemerald-native
```

## Steam Deck

Build the Linux game on a Windows PC (follow the Windows steps up to step 9,
then build with `tools/build-native.sh --os linux`) or on a Linux PC (the
Linux steps above). The finished game is `build/native64/pokeemerald-native`.

Then get that file to the Deck:

- From a Windows PC, run `explorer.exe build/native64` in Ubuntu to open the
  folder in Explorer.
- Copy the file to a USB stick and plug the stick into the Deck, or share a
  folder over your home network, or send it with any file-sharing tool you
  already use.
- A stick formatted FAT may drop the file's right to run. Run
  `chmod +x pokeemerald-native` on the Deck, as RUNNING.md says.

The Steam Deck section of [RUNNING.md](RUNNING.md#steam-deck) has the rest:
where to put the file and how to add it to Steam. If the game does not start
and Konsole names a missing `GLIBC` version, the Deck's system is older than
the one you built on; build on an older Linux. This has not been tried on a
Deck yet.

## Keys

| Keyboard | Does |
|---|---|
| Arrow keys | the D-pad |
| Z | A |
| X | B |
| A | L |
| S | R |
| Enter | Start (the help screen and the settings file call it Return) |
| Backspace | Select |
| F1 | the help screen |
| F11 | fullscreen on and off |
| Tab, held | fast-forward |

A game controller works as well. [RUNNING.md](RUNNING.md) has the controller,
the settings file and the rest.

## If something goes wrong

**`wsl --install` says virtualization is not enabled.** WSL needs a feature of
your computer's processor, switched on in its firmware settings (the BIOS).
Microsoft explains how in
[Troubleshooting Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/troubleshooting).
Look for "virtualization".

**"Permission denied" when you run `tools/build-native.sh`.** Check that you
are in the folder `pokeemerald-expansion-native` (run `pwd`; it should end
with that name) and that it is not under `/mnt/c`. Then run
`bash tools/build-native.sh --os windows` instead.

**The build says "missing prerequisites".** It names what is missing. Run the
install command again (step 7 on Windows, step 3 on Linux), check that it
ended without an error, then run the build again.

**The build stops with an error.** Copy the last twenty lines of the output
and open an issue with the
[bug report form](https://github.com/uehlbran/pokeemerald-expansion-native/issues/new?template=bug_report.yml).
Paste the lines in. Do not attach any game file.

**The build says there is no space left.** Free some disk space (5 GB is
enough), then run the build again; it carries on. On Windows, Ubuntu's files
live on the C: drive.

**On Windows, the game does not open.** If a box says `SDL2.dll` was not
found, the two files are not in the same folder: copy both again. If nothing
happens, your antivirus program may have removed the unsigned game file:
look in its quarantine list. Otherwise open `pokeemerald.log` in the save
folder (above): the game writes its messages there, and a box also names the
error. Paste its contents into a bug report.

**On Linux, the game window does not open and the terminal mentions SDL2.**
Install the SDL2 library: `sudo apt install -y libsdl2-2.0-0`. Then run the
game again.

**The save is not where the guide says.** On Windows, the game keeps its
save on Windows, not in Ubuntu. On Linux, run
`./build/native64/pokeemerald-native --keys` to print the real folder. If a
file named `settings.ini` sits beside the game file, the save is kept in that
folder instead.
