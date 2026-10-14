# Milestone 8 inventory: the Windows (MinGW-w64) and macOS ports

Measured 2026-10-09 on `work/m8-inventory` at c2f979940a (the same commit as main), read-only.
Sources: grep and reads of the tracked files (cited `file:line`); `readelf`/`size` over the main
checkout's `build/native*` of the same commit; two scratch probes in the session scratchpad
(the local GNU ld 2.42 carries the `i386pep`/`i386pe` emulations, and clang 18 emits COFF and
Mach-O objects), so a PE link and three object formats could be tried without installing
anything. Nothing was installed, built in the repository, committed or pushed.

Facts about Windows, macOS and GitHub's runners that cannot be measured here are marked
**(ext)**, with their source; each one that a recommendation rests on has a probe in section 5.

## 0. Verdicts

| | Windows x86_64 (MinGW-w64) | Windows i686 | macOS x86_64 | macOS arm64 |
|---|---|---|---|---|
| Memory model kept? | Probably: one PE image based at 0x01800000 holding the regions as sections and the code at 0x40000000. GNU ld places it (measured, probe B); the NT loader's acceptance of a 1000 MiB image with gaps is probe W1 | Not attempted (2.3) | Probably: non-PIE, `-pagezero_size`, the regions as Mach-O segments at `-segaddr` addresses (ext; probe M0) | No: arm64 executables are PIE (ext), and the model needs fixed code addresses (bit-25 tag) and an image below 4 GiB (41,946 4-byte address words). Needs a rebased model (2.5) |
| Division by zero | vectored exception handler, CONTEXT registers; decoder unchanged | - | sigaction as today; `uc_mcontext->__ss` field names | no trap at all; needs a code rewrite or a recorded divergence (3) |
| Crash reboot, restart | no exec: respawn the exe as a child, wait, exit with its status | - | execve as today, path from `_NSGetExecutablePath` | same |
| Data assembly | GNU as for COFF: 3 kinds of ELF-only text to filter, 8 16-bit words to resolve (1.3) | symbol underscores everywhere | system assembler is clang: 20,134 errors on event_scripts.s alone; assemble on Linux and convert (1.3) | same, plus 32-bit pointers impossible |
| Verify where | cross-build here; run under Wine here (optional install) and on `windows-latest` | - | `macos-15-intel` runner (10x minutes); the data conversion (M1) here with clang | `macos-latest` (probe only) |

## 1. Every Linux-, glibc- or ELF-specific thing, by module

### 1.1 Counts

- `platform/`: 59 modules (`platform/*.c`), 6 m4a engine files (`platform/m4a/m4a_*.c`), 55 unit-test files (`platform/test/*.c`).
- In platform C (`grep -rE` over `*.c`/`*.h`, build output excluded):
  `mmap(` 2 and `munmap(` 3, all in test support (`m4a/test_m4a_support.c:84`,
  `m4a/difftest/difftest_platform.c:45`), both with `MAP_FIXED_NOREPLACE`; none in the game: its
  regions come from the linker script. `sigaction(` 16 lines in 5 files (2 modules, `ptest.h`,
  2 tests); `sigaltstack(` 2 (`crash_reboot.c:217`, `:304`); `SA_ONSTACK` 3; `gregs[` 9 lines in 2
  modules (`arm_division.c`, `crash_reboot.c`); `getcontext/makecontext/swapcontext/setcontext` 10
  lines in `game_hooks.c`; `fork(` 3, `pipe(` 2, `waitpid` 4, all in tests; `execve`/`execv` 2
  (`crash_reboot.c:188`, `restart.c:65`); `/proc/self/exe` 3 run-time uses (`crash_reboot.h:62`,
  `restart.h:26`, `symbols.h:17`) and `/proc/$$/fd` in `test_runner/native-rom-test:39`; `"/dev/` 2
  (tests: `/dev/full`, `/dev/null`); `clock_gettime` 0 and `CLOCK_*` 0; `usleep` 1 (a test);
  `fsync(` 4 and `rename(` 2 in 2 modules; `<elf.h>` 2 files; `pthread_getattr_np` 1 module;
  `getauxval`/`<link.h>` 1 module; `setjmp`/`longjmp` 46 lines in 10 files; `alarm`/`SIGALRM` in
  `ptest.h`, which every unit test includes.
- 13 of the 59 modules touch the OS beyond stdio and SDL: `arm_division` (464 lines),
  `crash_reboot` (306), `restart` (68), `game_hooks` (275), `transfer_bounds` (200), `symbols`
  (203), `save_file` (449), `rtc_offset_store` (199), `rtc` (373; `localtime_r` only),
  `replay_session` (196; `mkdir` only), `gba_memmap` (172; linker symbols), `crt0` (197; linker
  symbols and weak references), `main` (493; `<SDL.h>`'s main and `setvbuf`). The other 46 modules
  and the m4a engine are portable C over SDL.
- Shell: 7 bash scripts, 574 lines (1.4). Python: 10 glue scripts, 3,418 lines, all run as
  `python3 -I` (1.5).
- ELF-format code: `platform/symbols.c` (203 lines, an ELF symbol-table reader),
  `platform/build_glue/elf_object.py` (240 lines, "one ELF64 reader", `docs/STATUS.md:202`) used by
  `check_relocations.py:59` and `check_data_layout.py:42`, `tools/patchelf/patchelf.c` (225 lines),
  `tools/mgba-rom-test-hydra/main.c:429-514` (`build_symbol_table`), `game_hooks.c:254-275`
  (program headers through `getauxval`), `platform/test_runner/test_patchelf.sh` (52 lines),
  `platform/test/test_symbols.c` (371 lines, builds ELF images), and the two GNU ld scripts
  (`gba_regions.ld` 150 lines, `gba_game.ld` 123 lines).

### 1.2 By module: what, where, and the counterparts

| # | What (Linux) | Where | Windows (MinGW-w64, PE/COFF) | macOS (Mach-O) |
|---|---|---|---|---|
| 1 | The regions are NOLOAD sections at GBA addresses; the ELF loader maps them (file-less `PT_LOAD`s) before any code runs; init only checks them | `gba_regions.ld:21-143` (`INSERT AFTER .bss` :143); `gba_memmap.c:13-24`; `readelf -lW build/native64/pokeemerald-native`: 8 RW `LOAD`s from 0x017ff000 to 0x0e000000 | GNU ld PE refuses sections below the image base: "section below image base", and wraps them to 0x1_0200_0000 (probe A, measured). With `--image-base 0x01800000` they land at their GBA addresses and `.text` at 0x40001000; `SizeOfImage` 0x3E804000 (1000 MiB) with gaps between sections (probe B, measured). Loader acceptance: probe W1 | No GNU scripts in ld64. Counterpart: extra segments holding zerofill sections, placed with `-segaddr` in a non-PIE image (ext), or `mmap(MAP_FIXED)` after `-pagezero_size` shrinks the 4 GiB page zero (ext). arm64: 2.5 |
| 2 | Non-PIE link at 0x40000000 | `Makefile.native:202` (`-fno-pie`), `:425` (`-no-pie -Wl,-Ttext-segment=0x40000000`); `platform/Makefile:110`, `:143` | `--image-base` and `--disable-dynamicbase`: binutils 2.42's PE default sets DYNAMIC_BASE, HIGH_ENTROPY_VA and NX (DllCharacteristics 0x160, measured in probe B). `-Ttext-segment` is ELF-only | x86_64: `-no_pie` (deprecated, ext), `-image_base`/`-segaddr __TEXT` (ext). arm64: PIE only (ext) |
| 3 | `gba_game.ld`: absolute symbols; the `.tests` table with `__start_tests`/`__stop_tests` (`INSERT AFTER .rodata`); `game_data`/`game_bss` brackets chosen by object path (`*native*/src/*.o(...)`) | `gba_game.ld:18-19`, `:24`, `:41-51`, `:94-123`; `crt0.c:44`, `:117` | Measured with `ld -m i386pep -T platform/gba_game.ld`: "ld: .rodata not found for insert" (the PE default script calls it `.rdata`); `.data` and `.bss` exist, so the object-path brackets work as written. The PE default script provides `etext`, `__data_start__`, `__bss_end__`, `_end` (`ld -m i386pep --verbose`) | `.tests` becomes `section("__DATA,__tests")` with ld64's `section$start$__DATA$__tests` (ext); 7 sites in expansion (`include/test/test.h:119`, `:131`, `include/test/battle.h:889-961`). ld64 cannot select sections by input object: the soft-reset brackets need the game's objects compiled into renamed sections |
| 4 | EWRAM_DATA is `section(".sbss")`, placed by the linker at the bottom of EWRAM | `include/gba/defines.h:9-13` (5 section macros); 762 `EWRAM_DATA` uses; `.sbss` totals 227.7 KiB at 64 bits and 221.5 KiB at 32 (`size -A` over build/native64, native32) of EWRAM's 256 KiB; `gHeap` is EWRAM_DATA (`src/malloc.c:19-27`) and `test/test_runner.c:182` requires heap blocks inside `EWRAM_START..EWRAM_END` | Section names are fine in COFF; placement: as row 1 | `section(".sbss")` is not a Mach-O specifier ("segment,section"): 18 literal `section(...)` sites in `src/`, `include/`, `test/` need a Mach-O spelling: 6 in `include/gba/defines.h:9-16`, 7 `.tests`, and `test/test_runner.c:34` (`.persistent`, the crash-surviving state at 0x03007F00), `src/m4a.c:11`, `src/decompress.c:353`, `src/rom_header_rhh.c:30`, `src/rom_header_gf.c:99` |
| 5 | Image bounds from GNU ld/glibc symbols | `transfer_bounds.c:67` (`__executable_start`, `__data_start`, `_end`), `test_transfer_bounds.c:20`; `src/malloc.c:6` (`__executable_start`, the base of the heap's allocation-site offsets) | `__ImageBase`, `__data_start__`, `_end` (provided, measured above) | `_mh_execute_header`, `getsegbyname`/`get_end` (ext) |
| 6 | `PlatformIsRomPointer`: the read-only `PT_LOAD`s from `getauxval(AT_PHDR)` | `game_hooks.c:21` (`<link.h>`), `:26` (`<sys/auxv.h>`), `:254-275` | walk the section headers from `__ImageBase`, non-writable sections | walk `_mh_execute_header`'s `LC_SEGMENT_64`s without `VM_PROT_WRITE` |
| 7 | Main-thread stack bounds | `transfer_bounds.c:41`, `:145-160` (`pthread_getattr_np`, glibc) | `GetCurrentThreadStackLimits` (Windows 8+, ext) | `pthread_get_stackaddr_np`, `pthread_get_stacksize_np` (ext) |
| 8 | SIGFPE completion of a division | `arm_division.c:14` (`_GNU_SOURCE` for `REG_*`), `:23`, `:31-44` (register slots as `gregs` indices), `:47-48` (`#error` for anything but i386 and x86-64), `:369-385` (edits `gregs`), `:438` (`si_code == FPE_INTDIV`), `:449` (`SA_SIGINFO | SA_ONSTACK`) | Section 3 | Section 3 |
| 9 | Crash reboot: handler on an alternate stack, PC from `gregs`, `execve("/proc/self/exe")` from the handler | `crash_reboot.c:28` (SEGV, BUS, ILL, FPE, ABRT), `:113-123`, `:188`, `:217` (64 KiB stack, `crash_reboot.h:59`), `:231`, `:236` (`sigprocmask`); `crash_reboot.h:36` ("Linux only") | No exec: an unhandled-exception filter respawns `GetModuleFileNameW`'s path as a child with the same handles and arguments plus `--persistent-state`, waits, exits with its status (the hydra waits on the PID it started); `EXCEPTION_STACK_OVERFLOW` needs `SetThreadStackGuarantee`; WER dialogs off (ext) | Same POSIX code; the path from `_NSGetExecutablePath`, taken at init (it is not async-signal-safe); PC from `uc_mcontext->__ss.__rip` (x86_64) or `__pc` (arm64) (ext) |
| 10 | Restart: `execv("/proc/self/exe")` | `restart.c:65`, `restart.h:26`, called at `main.c:461` | spawn, wait, exit with the child's status; the replays count processes by the children's `main: ` lines on the shared stdout (`tests/smoke/lib.sh` `play`) | `execv` of the `_NSGetExecutablePath` path |
| 11 | Stack switch for the battle runner: POSIX ucontext, ASan fiber annotations | `game_hooks.c:11-15`, `:27`, `:224-238` | no ucontext; fibers allocate their own stack and cannot use the fixed one at 0x01800000 → a small assembly switch for the Win64 ABI that also sets the TEB's `StackBase`/`StackLimit` (ext) | x86_64: ucontext deprecated but present (ext); arm64: reported unimplemented (ext, probe M0) → the same assembly switch per ABI |
| 12 | Durable replace: temp file, `fsync`, `rename`, `fsync` of the directory | `save_file.c:389`, `:407` (`O_DIRECTORY|O_CLOEXEC`), `:413`, `:435`; `rtc_offset_store.c:137`, `:142`, `:182`, `:193` | CRT `rename` fails onto an existing file → `MoveFileExW(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)`; `_commit`/`FlushFileBuffers`; no directory sync; no `O_DIRECTORY`/`O_CLOEXEC` (ext) | as Linux (`F_FULLFSYNC` if a disk-cache flush is wanted, ext) |
| 13 | `mkdir(path, 0777)` | `replay_session.c:24`, `:103` | MinGW's `mkdir` takes one argument (ext) | as Linux |
| 14 | `time(NULL)`, `localtime_r` | `rtc.c:87`, `:115` | MinGW-w64 declares `localtime_r` only under `_POSIX_C_SOURCE`/`_POSIX_THREAD_SAFE_FUNCTIONS` (ext) | as Linux |
| 15 | Clocks | `clock_gettime` 0 uses; `frame_pacer.c:55-83` uses `SDL_GetPerformanceCounter`/`SDL_Delay`; `fixed_clock` counts frames (`docs/ARCHITECTURE.md:128`) | portable, no work | portable, no work |
| 16 | SDL2 from apt through pkg-config; `<SDL.h>` in `main.c` without `SDL_MAIN_HANDLED` | `platform/Makefile:91-95`; `Makefile.native:429`; `<SDL.h>` in 5 modules and 4 tests | `SDL.h` renames `main` to `SDL_main` on Windows (needs `-lSDL2main`), and the MinGW `sdl2.pc` adds `-mwindows`, a GUI-subsystem exe without a console, while the gates read stdout (ext) → define `SDL_MAIN_HANDLED`, link `-mconsole`; `SDL2.dll` beside the exe | Homebrew `sdl2` through pkg-config; SDL does not take `main` on macOS (ext) |
| 17 | Line-buffered stdout for the hydra | `main.c:133` (`setvbuf(_IOLBF)`) | the Windows CRT treats `_IOLBF` as full buffering (ext); `debug_channel` flushes each line itself (`debug_channel.c:113`), so the protocol holds | as Linux |
| 18 | `setjmp`/`longjmp` across the frame hook and interrupt dispatch | `main.c:411`, `:423`; 46 lines in 10 files | MinGW x64's `longjmp` unwinds through SEH frames (ext): fine for C frames; `game_hooks.c:245-250` jumps only after switching back to the caller's stack, which row 11's TEB update keeps valid | as Linux |
| 19 | Weak undefined references | `crt0.c:45-46` | undefined weak externals are unreliable in MinGW ld (ext) → a defined default | ld64 needs `-U` or a definition (ext) |
| 20 | Function names from the executable's ELF symbol table | `symbols.c:13`, `:20-32`, `symbols.h:17` | a PE keeps a COFF symbol table unless stripped (ext) | `LC_SYMTAB` `nlist_64` (ext) |
| 21 | `tools/patchelf`: patches `.rodata` constants at their ELF file offsets | `tools/patchelf/patchelf.c:9`, `:41` (`mmap MAP_SHARED`), `:47` (ELF magic); called by `Makefile.native:512` and by the hydra at `main.c:686` (path `tools/patchelf/patchelf`) | needs a PE path (COFF symbol, section raw offset) | needs a Mach-O path |
| 22 | Expansion's hydra: fork/pipe/dup2/poll, `regex.h`, `mmap`, `sys/prctl.h`, `ioctl(TIOCGWINSZ)`, `/tmp/mgba-rom-test-hydra-<pid>`, `execlp("stdbuf")` | `tools/mgba-rom-test-hydra/main.c:18-33`, `:561`, `:586`, `:625-631`, `:636-637`, `:663`, `:737`, `:772`, `:788`; ELF symbol table `:429-514` is cosmetic (a non-ELF file leaves it empty, `:431`, `:510`) | not buildable with MinGW; under MSYS2/Cygwin's POSIX layer only after the `sys/prctl.h` guard (`#ifndef __APPLE__`, `:28`) becomes `__linux__`. Upstream itself says MSYS2 and Cygwin builds do not work (`docs/install/windows/MSYS2.md:1-4`, `CYGWIN.md:1-4`) | builds (upstream's `__APPLE__` guards); on macOS it runs `objcopy -O binary` on the copy (`:706-733`), which would destroy the Mach-O: `Makefile.native` passes `$(OBJCOPY)` (`:486`, `:513`), so `OBJCOPY=true` makes the step a no-op without touching the hydra; `stdbuf` comes from Homebrew coreutils (`docs/install/mac/MAC_OS.md:17`), on PATH only through `libexec/gnubin` (ext) |
| 23 | `native-rom-test`: closes the hydra's descriptors on the copy by `/proc/$$/fd` (Linux's ETXTBSY), `readlink -f`, scrubs stdbuf's `LD_PRELOAD` | `platform/test_runner/native-rom-test:38-44`, `:46-57` | Git Bash: the copy has no `.exe` suffix (hydra `:663`), so the script copies it to `<copy>.exe` before exec | the `/proc` glob matches nothing and the loop is a no-op; stdbuf's preload is `DYLD_INSERT_LIBRARIES`, which SIP strips at `/bin/bash` (ext) |
| 24 | Build-time ELF readers and a probe run at build time | `elf_object.py` (and its self-test runs `as --64`, `elf_object.py:179`); the three 64-bit checks; `data_layout_probe.c` compiled and run (`Makefile.native:765-768`) | the checks read the Linux build's ELF objects of the same data text; a cross build cannot run the probe without Wine | a native macOS build has Mach-O objects: run the checks on Linux only, or teach the reader Mach-O |
| 25 | Toolchain flags | `-m$(BITS)` (`Makefile.native:180`, `:425`, `:767`; `platform/Makefile:97`, `:111`, `:229`); `as --$(BITS)` (`Makefile.native:258`, `:639`); `-malign-data=abi` (GCC x86 only, `:657`); GCC-only `--param` sanitizer options (`:225`, `:237`); `ASAN_OPTIONS=detect_leaks=1` (`platform/Makefile:115`) | MinGW GCC has no sanitizers (ext): `SANITIZE=1` and the platform unit tests' sanitizers stay on Linux. No multilib: Ubuntu packages one cross compiler per target (`gcc-mingw-w64-x86-64`, `gcc-mingw-w64-i686`), so `BITS` selects the compiler, not `-m32`/`-m64` | Apple clang: ASan and UBSan yes, LeakSanitizer not supported (ext) → `detect_leaks=1` must go; the `--param` spellings are GCC's (clang: `-mllvm`), or use Homebrew GCC (ext). No 32-bit at all: macOS dropped 32-bit executables in 10.15 (ext), so `BITS=64` only; `as --64` is GNU syntax (Apple's `as` takes `-arch`) |
| 26 | Struct layout | 1,247 bitfield members in `include/*.h` (grep); no bare `long` in a struct there (grep: 0) | MinGW GCC defaults to `-mms-bitfields` (ext: GCC manual, x86 options) → expansion's objects and the `GAME_FLAG_OBJS` (`platform/Makefile:149-151`) must be compiled with `-mno-ms-bitfields`, or the save layout (`save_layout64.h`) and the data layout checks differ from Linux x86-64. `%z` in 40 format strings, `%ll` in 5 → `-D__USE_MINGW_ANSI_STDIO=1` (msvcrt's printf lacks `%zu`, ext) | LP64 like Linux; arm64 Darwin's layout rules for these types are the same (ext) |
| 27 | GNU userland in the build | GNU make >= 4.2 (`Makefile.native:335`); `stat -c` (`Makefile.native:444`, `gbagfx_pad.sh:27`, `run_cross_width.sh:139`); `cp -a --no-clobber` (`Makefile.native:442`); `nproc` (`platform/Makefile:230`) | the cross build runs on Linux: no change | macOS ships make 3.81 (ext) → Homebrew `gmake`; BSD `stat -f %z`, `cp -n`; no `nproc` (`INSTALL.md:98`). `gbagfx_pad.sh:27` is on the path of every padded palette, so it breaks the asset build on macOS (`wc -c <` is portable on both) |

### 1.3 The data the host `as` assembles

How it was measured: the 64-bit data pipeline of `Makefile.native:683` (`preproc -s | cpp |
preproc -ie | asm_filter.py --pointer-size 8`) run for 7 of the 14 `data/*.s` (event_scripts,
battle_scripts_1, maps, map_events, sound_data, battle_anim_scripts, mystery_gift) into the
scratchpad (GNU as assembles all of them for ELF in the build; maps.s was re-assembled here as a control), then by clang 18 for COFF x86-64 and Mach-O x86-64
and arm64; relocations counted with `readelf -r` over `build/native64` and `build/native32`.

- **ELF-only directives in the filtered text.** `.size`: 28,982 lines in those 7 files (26,123 in
  event_scripts.s), written by expansion's preproc for every `::` label
  (`tools/preproc/preproc.cpp:69`, `:132`). `.section script_data, "aw", %progbits`: 7 files
  (`data/event_scripts.s:82`, `battle_scripts_1.s:24`, `battle_scripts_2.s:15`,
  `battle_anim_scripts.s:15`, `contest_ai_scripts.s:8`, `field_effect_scripts.s:5`,
  `mystery_event_script_cmd_table.s:1`). `.type` 3 per file. `sound/music_player_table.inc:11-32`
  (`.bss`, four `.size`). GNU as for COFF ignores `.size` outside `.def` with a warning (ext:
  binutils `obj-coff.c`); `%progbits` under COFF is unknown here: both to confirm with
  `x86_64-w64-mingw32-as` (task W4). Mach-O: clang rejects `.size` ("unknown directive" 17,200
  times in event_scripts.s) and the section line ("mach-o section specifier uses an unknown
  section type").
- **Relocation widths.** 64-bit data objects (14 data + 530 songs): 19,208 `R_X86_64_64`,
  41,946 `R_X86_64_32` (the 4-byte bytecode address words, `docs/ARCHITECTURE.md:255-257`),
  8 `R_X86_64_16` (`data/battle_anim_scripts.o` x3 `B_MSG_MON_*`, `data/mystery_gift.o` x5
  `SPECIAL_*`: 16-bit values of symbols defined in another object). 64-bit C objects (384 in
  `src/`): 15,488 `R_X86_64_32` and 34,879 `R_X86_64_32S` (absolute addressing, `-fno-pie`).
  32-bit data: 50,163 `R_386_32`, 8 `R_386_16`.
  - COFF x86-64 has no 16-bit relocation: clang refuses `.2byte ext16` with "unsupported
    relocation type" (measured); the 8 words need their values known at assembly time. 32-bit
    absolute words (`ADDR32`) are accepted (measured) and hold while the image is below 4 GiB.
  - Mach-O: the assembler accepts `.4byte sym` and `.2byte sym` (measured, both archs); whether
    ld64 links 32-bit absolute pointers is the linker's question: plausible in a non-PIE x86_64
    image below 4 GiB (probe M0), impossible in a slid arm64 image (2.5).
- **Symbol names on Mach-O.** C symbols carry a leading underscore there, and a name beginning
  with `L` is assembler-local on Darwin: clang reports "assembler local symbol ... not defined" and
  "non-local symbol required" for map labels such as `LittlerootTown_ProfessorBirchsLab_MapScripts`
  (maps.s: 76 + 91 of its 168 errors; map_events.s: 268 + 46 of 315). Every global symbol the data defines or
  references would need an underscore prefix on Mach-O, as on i686 COFF, whose C symbols are
  underscored too.
- **Dialect.** The macOS system assembler is clang's, which also differs from GNU as in macro
  argument parsing ("missing value for required parameter", 497 in event_scripts.s, e.g.
  `script_cmd_table_entry SCR_OP_NOP ScrCmd_nop, requests_effects=1`,
  `data/script_cmd_table.inc:37`) and in `.if` over symbol expressions ("expected absolute
  expression", 1,228). GNU as cannot emit arm64 Mach-O (ext). Totals on event_scripts.s: COFF
  (clang) 18,634 errors, Mach-O 20,134; GNU as for ELF none.
- **Consequence.** MinGW's assembler is GNU as with the same dialect as Linux, so Windows needs
  only object-format rules in `asm_filter.py` (drop `.size`/`.type`, rewrite the 7 section lines,
  give the 8 16-bit words their values). macOS cannot assemble expansion's data text with its
  own assembler without a second dialect translation. The route with the least new surface:
  keep assembling on Linux with GNU as (as today, checked by the three 64-bit checks) and
  convert the ELF objects, which `elf_object.py` already reads, into a flat format-neutral listing
  (labels, `.byte`/`.quad`/`.long` with symbol plus addend, `.p2align`, section switches, the
  target's symbol prefix) that clang, ld64 and MinGW's as all accept.

### 1.4 The gates' shell scripts (7 bash scripts, 574 lines)

| Script | Lines | Not portable as written |
|---|---|---|
| `tests/smoke/lib.sh` | 196 | `md5sum` (`:148`, `:176`; macOS has `md5`, coreutils gives `md5sum`), `diff <(...)` (bash 3.2 has it); no bash 4 feature (grep for `declare -A`, `mapfile`, `${x,,}`: none) |
| `tests/smoke/run.sh`, `run_debug_menu.sh` | 19, 17 | none (they source lib.sh) |
| `tests/smoke/run_cross_width.sh` | 198 | `stat -c %s` (`:139`), `md5sum` (`:92`, `:180`); a cross-OS variant (a Linux save into the Windows build) is new work |
| `platform/build_glue/gbagfx_pad.sh` | 33 | `stat -c %s` (`:27`), `head -c ... /dev/zero` (`:30`, fine on macOS and Git Bash) |
| `platform/test_runner/native-rom-test` | 59 | `/proc/$$/fd` (`:39`), `readlink -f` (`:38`), `LD_PRELOAD` (1.2 row 23) |
| `platform/test_runner/test_patchelf.sh` | 52 | builds ELF test executables with `gcc -m32/-m64 -no-pie` and reads them with `nm`/`readelf -SW`: ELF by purpose; a PE/Mach-O twin is new work with patchelf's new paths |

On `windows-latest` the replays run under Git Bash, which has bash, `md5sum`, `stat -c`, `cmp`
and `diff` (ext); MSYS converts POSIX path arguments for native executables (ext), which
`play()`'s `--save "$dir/pokeemerald.sav"` relies on. On macOS, `md5sum` and `stat -c` need
Homebrew coreutils on PATH, or the two-line portable spellings.

### 1.5 Python

The 10 glue scripts import only the standard library (`sys`, `os`, `re`, `collections`,
`tempfile`, `subprocess`, `struct`, `bisect`, `shlex`, `glob`, `fnmatch`) and run as `python3 -I`
(`Makefile.native:246`, `:450-451`, `:638-639`, `:715`, `:750`, `:754`, `:775`, `:785`;
`platform/Makefile:280`): portable. Three spawn tools: `elf_object.py:179` runs `as --64` in its
self-test, `overworld_macros.py:85` runs `$(HOST_AS) --$(BITS)`, `check_relocations.py:53`
imports subprocess; all are GNU-as and ELF specific, and all run on the Linux build host in the
cross-build plan. Expansion's own generators are run as plain `python3` (`Makefile.native:543-585`).

### 1.6 The unit tests

`platform/test/ptest.h` gives every test a timeout with `alarm`/`SIGALRM`/`sigsetjmp`
(`ptest.h:203`, `:224`, `:232`) and registers tests with a constructor (`:275`; GCC's constructor
works on PE and Mach-O). Windows has no `SIGALRM`: all 55 test binaries need another timeout (a
watchdog thread) before any of them runs there. Fifteen test files plus `save_file_fixture.h` (so 16 tests) use
`fork`, `pipe`, `mkdtemp`/`mkstemp`, `dup2`, `sigaction`, `/dev/*`, `pthread_getattr_np`, `<elf.h>`
or `unlink`: test_crash_reboot, test_arm_division, test_restart, test_debug_channel (`/dev/full`,
`pipe`), test_options (`/dev/null`), test_symbols, test_transfer_bounds, test_video, test_replay,
test_replay_include, test_rtc, test_frame, test_flash, test_interrupt, test_save_file_store
(and test_save_file through the fixture). `setenv` (absent from MinGW-w64, which has
`_putenv_s`, ext): `test_input.c:50`, `:558`, `test_audio.c:22`. The m4a tests map a page at
0x01000000 with `MAP_FIXED_NOREPLACE` (`m4a/test_m4a_support.c:84`; macOS has no
`MAP_FIXED_NOREPLACE`, ext).

## 2. The memory model on each platform

### 2.1 What the model needs (measured)

1. Eight regions and the call stack at fixed addresses, 1,552 KiB in whole 4 KiB pages
   (`gba_regions.ld:39-137`): EWRAM 0x02000000 (256 KiB), IWRAM 0x03000000 (32 KiB), I/O
   0x04000000, mGBA debug 0x04FFF000, palette 0x05000000, OAM 0x07000000 (one 4 KiB page each),
   VRAM 0x06000000 (96 KiB), SRAM 0x0E000000 (128 KiB), call stack 0x01800000 (1 MiB).
2. EWRAM_DATA (227.7 KiB at 64 bits) inside EWRAM at link time (1.2 row 4): the heap is there and
   the test runner checks it is (`test/test_runner.c:182`). A model that maps EWRAM at run time
   must still have the linker give EWRAM addresses to the variables behind the 762 `EWRAM_DATA` uses.
3. Every code address below 0x42000000, so that bit 25 is the script tag
   (`platform/script_tag.h:16-27`; `gba_regions.ld:150` asserts `etext < 0x42000000`). Measured
   ends of code: 0x4031C2B8 plain, 0x4078E498 TEST=1, 0x40EF3518 SANITIZE=1, 0x41BB1358 TEST=1
   SANITIZE=1 (`script_tag.h:25-27`).
4. At 64 bits, the whole image below 4 GiB: 41,946 4-byte address words in the data (1.3),
   50,367 32-bit absolute relocations in the C objects, and the readers that turn a 4-byte value
   into a pointer (`T1_READ_PTR`/`T2_READ_PTR`, `include/global.h:115-122`, 103 and 10 uses;
   `ScriptReadWord`, 77 uses).
5. The call stack below 2^27 for the battle runner's 27-bit fields (`gba_regions.ld:124-131`).
6. Nothing at the first page (`transfer_bounds.c:61-64`; `ARCHITECTURE.md:326`).
7. ROM: nothing is mapped at 0x08000000. `gba_regions.ld` has no ROM section; `gba_game.ld:24`
   defines `__rom_end = 0x08000000` for the debug menu's size line only; tagged script pointers
   are 0x42xxxxxx and never dereferenced. The 32 MiB ROM reservation in the task's list is not
   part of this model and needs no counterpart.

Alignment of each reservation (computed from the bases; Windows reserves at the 64 KiB allocation
granularity and rounds a reservation's base down to it, macOS arm64 pages are 16 KiB, ext):

| Region | Base | Size | 64 KiB granule (Windows) | 16 KiB page (arm64) |
|---|---|---|---|---|
| call stack | 0x01800000 | 1 MiB | aligned | aligned |
| EWRAM | 0x02000000 | 256 KiB | aligned | aligned |
| IWRAM | 0x03000000 | 32 KiB | aligned; the reservation holds a 64 KiB granule | aligned, 2 pages |
| I/O | 0x04000000 | 1 KiB | aligned, 64 KiB granule | aligned, 1 page |
| mGBA debug | 0x04FFF000 | 4 KiB | **not aligned**: reserved as 0x04FF0000-0x05000000 | **not aligned**: page 0x04FFC000-0x05000000 |
| palette | 0x05000000 | 1 KiB | aligned | aligned |
| VRAM | 0x06000000 | 96 KiB | aligned, granules to 0x06020000 | aligned, 6 pages |
| OAM | 0x07000000 | 1 KiB | aligned | aligned |
| SRAM | 0x0E000000 | 128 KiB | aligned | aligned |

No two rounded reservations overlap (the debug page's granule ends where the palette's begins),
so neither granularity breaks the layout. The check that each region is "exactly its size in
whole 4 KiB pages" (`gba_memmap.c:101`, `:111`) would use the host's page size.

### 2.2 Windows x86_64: keep the model as one PE image (probe W1 decides)

- Measured (probe A): with `--image-base 0x40000000`, as on Linux, GNU ld for PE prints
  "section below image base" for each region and moves them to 0x1_0200_0000: the Linux layout
  (image at 0x40000000, regions below it as part of the image) cannot be expressed in PE, whose
  section addresses are offsets above the image base.
- Measured (probe B): with `--image-base 0x01800000` and the code at 0x40001000, ld puts EWRAM at
  0x02000000, IWRAM at 0x03000000 and `.text` at 0x40001000; `SizeOfImage` is 0x3E804000
  (1000 MiB), with gaps between the sections; long section names are cut to 8 characters in the
  image (`.gba_ewr`), which only matters to anything that looks sections up by name.
- (ext) The PE format asks for section addresses "in ascending order and adjacent" (Microsoft PE
  format, section table). Whether the NT loader enforces adjacency is not knowable here: probe W1
  runs layout B on `windows-latest` as linked, and again with the gaps filled by uninitialised,
  non-writable sections, and records whether it loads, at which address, and the commit charge
  (1,552 KiB of writable regions plus the image's own data is what should be charged).
- ASLR: link with `--disable-dynamicbase --disable-high-entropy-va` and no `.reloc`, so the loader
  maps the exe at its base or refuses it; never silently elsewhere. A PE32+ is large-address-aware
  by default (ext); the whole layout sits below 0x42000000 anyway. The exe is the first thing the
  kernel maps into a new process (ext), so nothing can take its range first; that is the property
  `gba_regions.ld` gives on Linux (`DECISIONS.md:113-133`, reason 2).
- The fallback, regions reserved with `VirtualAlloc` at run time, is weaker on two counts:
  EWRAM_DATA could not be given EWRAM addresses (2.1, item 2) unless it moved to the image's
  `.bss`, a recorded divergence touching `test/test_runner.c:182`; and before `main` the loader
  has already placed the main stack, heaps, the PEB/TEB and `.nls` mappings with bottom-up
  randomisation in the low 4 GiB (ext), so a fixed reservation at `main` can fail. Guaranteeing
  it takes a launcher that creates the game suspended and reserves the ranges with
  `VirtualAllocEx` before resuming it (ext; the technique Wine's preloader applies on Linux).
- Verdict: keep the model if W1 shows layout B loads; the smallest change then is the base moving
  from 0x40000000 to 0x01800000 on Windows only, with the code still at 0x40000000, so items 3
  to 5 of 2.1 hold unchanged.

### 2.3 Windows i686: do not attempt

- i686 COFF prefixes every C symbol with `_` (ext): the two linker scripts' symbols and every
  global the assembled data defines or references (1.3) would need a second spelling.
- The 32-bit pointer path is already proven on Linux (gates a to h green at 32 bits,
  `docs/STATUS.md:300-312`).
- Windows 11 has no 32-bit edition and Windows 10 support ended 2025-10-14 (ext).
- Layout B is a 1000 MiB image in a 2 GiB user address space.

### 2.4 macOS x86_64: probably kept (probe M0 decides)

- (ext) A 64-bit Mach-O has a 4 GiB `__PAGEZERO` by default; `-pagezero_size 0x1000` shrinks it
  and frees the GBA addresses. `-no_pie` with `-image_base 0x40000000` (or `-segaddr __TEXT`)
  makes a fixed-address image; `-no_pie` is deprecated for x86_64 and may need an old deployment
  target or `-ld_classic` with current Xcode. Unverified: probe M0 links and runs exactly that.
- The regions as Mach-O segments: one zerofill section per region in segments placed with
  `-segaddr <SEG> <addr>` gives the "exists before any code runs" property of `gba_regions.ld`;
  EWRAM_DATA goes into the EWRAM segment through `defines.h:10`'s Mach-O spelling. 4 KiB pages:
  every base aligns.
- Code signing: x86_64 executables need no signature to run; arm64 ones need at least the
  ad-hoc signature the linker adds (ext). The hardened runtime only matters for notarised
  distribution; nothing here needs `MAP_JIT` (ext).
- Rosetta 2 runs an x86_64 build on Apple silicon; Apple said Rosetta stays through macOS 27
  (WWDC 2025, ext). An x86_64 port therefore reaches arm64 Macs for its lifetime, with the GBA
  division semantics of x86 intact.
- Verdict: the model holds if M0 passes; changes: the Mach-O counterparts of the two linker
  scripts (rows 1, 3, 4 of 1.2), and the data path of 1.3.

### 2.5 macOS arm64: the model cannot hold as it is

- arm64 executables are always PIE and slid at load; ld64 ignores `-no_pie` for arm64 (ext). Code
  addresses change from run to run, so item 3 of 2.1 (bit 25 clear in every code address) cannot
  be guaranteed, and nothing in the image can be at a GBA address.
- Item 4 fails: an arm64 image cannot carry 32-bit absolute pointers that the loader would have
  to slide (ext: arm64 chained fixups have no 32-bit pointer format for arm64 images), and with
  the default 4 GiB page zero the image is above 4 GiB anyway.
- Whether an arm64 process can map anything below 4 GiB at all with a reduced page zero is
  reported as not allowed (a hard 4 GiB page zero for arm64 processes, ext, unverified): probe M0
  maps 0x02000000 with `MAP_FIXED` there. If it cannot, expansion's fixed addresses (`REG_BASE`
  0x04000000 in `include/gba/io_reg.h`, `INTR_CHECK` 0x03007FF8 in `defines.h`, the test runner's
  0x03007F00) are unreachable.
- 16 KiB pages: harmless (table in 2.1).
- No divide trap (3).
- Smallest change that would work: a rebased model, the GBA's 256 MiB address space mapped at a
  run-time base with expansion's fixed addresses expressed relative to it, the bytecode words as
  32-bit offsets with the base added in `T1_READ_PTR`/`T2_READ_PTR`/`ScriptReadWord` (190 uses),
  and a tag that does not depend on the link address (arm64 code addresses are 4-byte aligned, so
  bit 0 or 1 is free). It touches `include/gba/io_reg.h`, `defines.h`, `global.h` and the script
  readers: a milestone of its own with its own decision, not a port task.

## 3. The signal model

What exists (measured): `arm_division` decodes the faulting `div`/`idiv` and writes libgcc's
results (`arm_division.c:369-385`); only three things in it are OS-bound: register access by
`gregs` index (`:31-44`, `:125`, `:371-384`, `:401`), the `si_code == FPE_INTDIV` test (`:438`) and
`sigaction` install, chain and restore (`:418-463`). `crash_reboot` catches five signals on a
64 KiB alternate stack and execs (1.2 row 9). The decoder, the libgcc rules
(`arm_division.h:13-19`) and their tests are OS-independent.

**Windows.**
- `AddVectoredExceptionHandler(1, ...)` for `EXCEPTION_INT_DIVIDE_BY_ZERO` (0xC0000094) and
  `EXCEPTION_INT_OVERFLOW` (0xC0000095, INT_MIN / -1); x86 raises #DE for both, as Linux delivers
  both as `FPE_INTDIV` (ext). The `CONTEXT` record names the registers: `Rax, Rcx, Rdx, Rbx, Rsp,
  Rbp, Rsi, Rdi, R8..R15, Rip` (x64; `Eax..Edi, Eip` on x86) (ext). The ModRM order of
  `arm_division.c:40-41` maps onto a 16-entry table of `offsetof(CONTEXT, ...)`; after the edit the
  handler returns `EXCEPTION_CONTINUE_EXECUTION`. Only `complete()` and init/shutdown change.
- The handler runs on the faulting thread's own stack: there is no `SA_ONSTACK`; for the crash
  reboot `SetThreadStackGuarantee` reserves room for `EXCEPTION_STACK_OVERFLOW` (ext).
- The crash classes: `EXCEPTION_ACCESS_VIOLATION` (SIGSEGV, SIGBUS), `EXCEPTION_ILLEGAL_INSTRUCTION`
  and `PRIV_INSTRUCTION` (SIGILL), a division the decoder refuses (SIGFPE), and `abort()`, for
  which the CRT raises `SIGABRT` through `signal()` (ext; `_set_abort_behavior` and
  `SetErrorMode(SEM_NOGPFAULTERRORBOX)` keep dialogs off a headless runner, ext).
- Order: `arm_division` as the first vectored handler, `crash_reboot` as the unhandled-exception
  filter, which keeps today's chain (arm_division passes what it cannot complete on,
  `arm_division.c:418-434`).
- Under Wine, `SIGFPE` reaches the program as `EXCEPTION_INT_DIVIDE_BY_ZERO` with a CONTEXT (ext),
  so this path can be developed here once Wine is installed.

**macOS.**
- `sigaction`, `SA_SIGINFO`, `SA_ONSTACK`, `sigaltstack`: the same calls. The context differs:
  `uc_mcontext` is a pointer to `struct __darwin_mcontext64`, and the registers are named fields,
  `->__ss.__rax ... __r15, __rip` on x86_64 and `->__ss.__x[0..28], __fp, __lr, __sp, __pc` on
  arm64 (ext, `<mach/i386/_structs.h>`, `<mach/arm/_structs.h>`); there are no `REG_*` indices, so
  the same 16-entry accessor as on Windows. 
- `si_code`: XNU turns the x86 #DE (`EXC_I386_DIV`) into `SIGFPE`/`FPE_INTDIV` (ext), which covers
  INT_MIN / -1 as on Linux; probe M0 checks it. Mach exception ports are not needed.
- `execve` stays async-signal-safe; the executable's path is read at init.

**arm64 (macOS, and Windows on ARM if it ever matters).** `SDIV`/`UDIV` never trap: n / 0 is 0,
and the remainder, computed with `MSUB`, is n; INT_MIN / -1 is INT_MIN with remainder 0 (ext,
Arm ARM). The GBA's libgcc gives 0xFFFFFFFF, INT32_MAX or INT32_MIN for both n / 0 and n % 0
(n != 0), and INT_MIN for INT_MIN / -1 (`arm_division.h:13-19`): every division by zero with
n != 0 differs, INT_MIN / -1 matches. Options:
- (a) Guard each division site in expansion: rejected by `DECISIONS.md:388-401` ("nine today,
  unknown tomorrow").
- (b) `-fsanitize=integer-divide-by-zero` with a runtime handler: the handler sees the operands
  but cannot change the result; execution continues with the hardware's 0 (ext). Does not work.
- (c) `-fsanitize=integer-divide-by-zero -fsanitize-trap`: a `BRK` before each division whose
  divisor is zero; a `SIGTRAP` handler would find the `SDIV`/`UDIV` (and any `MSUB`) after it,
  write libgcc's results and resume past them. The arm64 counterpart of today's decoder, but it
  depends on the code shape the compiler emits after the check: to be measured before chosen.
- (d) Rewrite the compiler's assembly: compile expansion's C with `-S`, replace each
  `sdiv`/`udiv` by a call to a helper that implements libgcc's rule, then assemble (a filter like
  `asm_filter.py`, for code). Every site, no run-time trap; the helper is tested against
  `platform/arm_division_libgcc/libgcc_division.txt`.
- (e) Accept the divergence on arm64 and record it; the nine tests that divide by zero would
  then differ from the GBA baseline.
Recommendation: (d) if arm64 is ever ported; nothing in milestone 8 (2.5).

## 4. The toolchains, and what can be verified where

**MinGW-w64 on this Ubuntu 24.04** (candidates measured with `apt-cache policy`): for the owner,
one line:

    sudo apt-get install -y gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools

(gcc-mingw-w64-x86-64 13.2.0-6ubuntu1+26.1, binutils-mingw-w64-x86-64 2.41.90.20240122,
mingw-w64 11.0.1-3build1.) Optional, to run the cross-built executables here:

    sudo apt-get install -y wine64

(wine 9.0~repack-4build3.) i686 would be `gcc-mingw-w64-i686` and `wine32:i386`; not recommended
(2.3). The default `win32` thread model needs no extra DLL; nothing here uses threads.

**SDL2 for MinGW.** Ubuntu has none (`apt-cache search --names-only sdl2 | grep -i mingw`: empty).
SDL publishes `SDL2-devel-<version>-mingw.tar.gz` on github.com/libsdl-org/SDL/releases, with
`x86_64-w64-mingw32/` (and `i686-w64-mingw32/`) trees: headers, `libSDL2.dll.a`, `sdl2.pc`,
`bin/SDL2.dll` (ext). Licence: zlib (ext), compatible with shipping `SDL2.dll` beside the exe.
Vendoring: not committed; a make target fetches a pinned 2.30.x (this machine's SDL is 2.30.0,
`pkg-config --modversion sdl2`) into `build/deps/` and checks a recorded sha256; `PKG_CONFIG_PATH`
points at its `lib/pkgconfig` with the prefix redefined (`pkg-config --define-prefix`), since the
tarball's `.pc` carries the prefix it was built under (ext). CI caches the tarball by version.

**What runs where.**
- Here, with only the apt line: the cross build (assets, tools and the three 64-bit checks stay
  host Linux; only compile, assemble and link use `x86_64-w64-mingw32-*`), and inspecting the
  result (`x86_64-w64-mingw32-objdump -h -p`), as probe B already did with the local ld. Nothing
  executes.
- Here, with Wine 9: the unit tests that do not fork, the replays, the SIGFPE path. Wine's 64-bit
  preloader keeps 0x00110000-0x68000000 for the Windows process (ext), so layout B fits. Wine is
  not Windows: the loader question of W1 is answered on `windows-latest` only.
- `windows-latest` (Windows Server 2025, ext): runs the cross-built artifacts; `shell: bash` is Git
  Bash with coreutils; MSYS2 is installed at `C:\msys64` but not on PATH (ext), for a POSIX build
  of the hydra if it is used. Building the game natively on Windows is not a goal: upstream builds
  only under WSL (`INSTALL.md:14-20`; `docs/install/windows/MSYS2.md:1-4`: "Don't, just don't").
- macOS: only on GitHub runners. `macos-latest` is macOS 15 on arm64 (3 vCPU M1 for private
  repositories, ext); `macos-15-intel` is the last x86_64 image (ext). Xcode's clang is the system
  assembler and ld64 the linker; Homebrew is preinstalled: `brew install sdl2 libpng pkg-config
  make coreutils` (`gmake` for `Makefile.native:335`). Cross-building for macOS from Linux needs
  Apple's SDK, whose licence limits it to Apple hardware (ext): not an option.
- Minutes: on a private repository Windows minutes count double and macOS minutes ten times
  against the included minutes (ext; `docs/PLAN.md:198-200` already counts macOS tenfold).
- The GBA target: gate (c) stays on Linux. Upstream documents macOS (devkitARM or
  arm-none-eabi, `docs/install/mac/MAC_OS.md`) and Windows only through WSL (`INSTALL.md:20`);
  neither port needs the ROM or any ARM tool (`Makefile.native:24-28`).

## 5. Worker tasks

### 5.1 Shared between the ports: a thin OS layer, not `#ifdef` per module

With the module table in hand (`docs/ARCHITECTURE.md:120-155`): 13 of 59 modules touch the OS
(1.1), and each does one of six things:

| Concern | Modules (and expansion) | Linux today |
|---|---|---|
| fixed-address memory | `gba_memmap` (check or reserve), m4a test page | linker script; `mmap MAP_FIXED_NOREPLACE` in tests |
| hardware faults with the faulting context | `arm_division` (register accessor, install), `crash_reboot` (install) | `sigaction`, `gregs` |
| starting the executable again | `crash_reboot`, `restart` | `execve`/`execv` of `/proc/self/exe` |
| image and stack bounds | `transfer_bounds`, `game_hooks` (`PlatformIsRomPointer`), `symbols`, `src/malloc.c:6` | GNU ld symbols, `getauxval`, `pthread_getattr_np`, ELF |
| stack switching | `game_hooks` | ucontext |
| durable file replace, small libc gaps | `save_file`, `rtc_offset_store`, `replay_session`, `rtc` | `fsync`, `rename`, `mkdir(path, mode)`, `localtime_r` |

`#ifdef` blocks per module would put up to three OS branches into 13 modules and their tests (the
decoder's and the save file's tests among them), against the rule that a module's header is its
only interface and its logic is tested once. A thin `host` layer, one header per concern
(`host_memory.h`, `host_fault.h`, `host_process.h`, `host_image.h`, `host_stack.h`,
`host_file.h`) and one source per OS chosen by the Makefiles (`host_posix.c` shared by Linux and
macOS for `sigaction`, `execve`, `fsync`/`rename`; `host_linux.c`, `host_macos.c`,
`host_windows.c` for the rest), keeps the 13 modules single-sourced, leaves the 46 others
untouched, and gives each concern one test file that runs on every OS. It adds one row to the
module table. `src/malloc.c:6` needs no edit: each link can define `__executable_start` as an
alias (`--defsym=__executable_start=__ImageBase` on PE; ld64 `-alias`, ext).

The symbol names for the replay (`symbols.c`) are best made OS-neutral rather than ported twice:
the link step writes a sorted address/name map with the target's `nm`, and `symbols` loads it
(`test_symbols.c`'s ELF fixtures become map fixtures).

### 5.2 Windows (x86_64 only), in order

| # | Task (one module group each) | Gate | Runs where |
|---|---|---|---|
| W0 | Owner: the apt line of section 4 (`wine64` optional) | - | here |
| W1 | Probe: layout B (base 0x01800000, regions as sections, code at 0x40000000, `--disable-dynamicbase`), as linked and with filled gaps; 100 starts; each region writable; commit charge; the `VirtualAlloc` fallback measured beside it | the probe's report in STATUS.md | link here (the local ld already links PE), run on `windows-latest`, one `workflow_dispatch` job of a few minutes |
| W2 | The host layer on Linux, a refactor only (5.1), and the `nm` symbol map | platform gate, then (a), (b), (d), (e), (f), (h) at both widths: same results, replay outputs identical | here |
| W3 | `ptest.h` timeout without `SIGALRM` (a watchdog thread); the 16 POSIX-bound tests split into portable and per-OS parts | platform gate | here |
| W4 | Cross-build glue: `Makefile.native` and `platform/Makefile` with a target OS (cross prefix; `-mno-ms-bitfields`; `-D__USE_MINGW_ANSI_STDIO=1`; `SDL_MAIN_HANDLED`, `-mconsole`; `--image-base 0x01800000 --disable-dynamicbase`); PE variants of the two linker scripts (`.rdata`); `asm_filter.py` rules for COFF (drop `.size`/`.type`, the 7 section lines, the 8 16-bit words); the SDL2 fetch target; the layout probe run under Wine or its results taken from the Linux 64-bit build | (a) the cross link here; `objdump -h` shows every region at its GBA address; the three 64-bit checks pass | here (needs W0) |
| W5 | `host_windows.c`: memory check, image and stack bounds, exe path, `MoveFileExW` replace, `mkdir`, `localtime` | the unit tests cross-built and run | Wine here, then `windows-latest` |
| W6 | Faults and processes: `arm_division` behind a vectored handler with the CONTEXT accessor; `crash_reboot` as the unhandled-exception filter with respawn; `restart` as spawn, wait, exit | `test_arm_division`, `test_crash_reboot`, `test_restart` (Windows variants); replays (d) and (e), which include a restart | Wine here, then `windows-latest` |
| W7 | `game_hooks` stack switch for the Win64 ABI with the TEB limits | `test_game_hooks`; the suite's battle tests | `windows-latest` |
| W8 | The test runner: patchelf's PE path (a `tools/` row in `UPSTREAM_MODIFICATIONS.md`), `native-rom-test` copying to `.exe`, and either the hydra under MSYS2 (its `sys/prctl.h` guard made `__linux__`, a second row) or, here, the Linux hydra driving `wine` through `native-rom-test` | (b): per test equal to the Linux 64-bit results | Wine here; `windows-latest` with MSYS2 |
| W9 | CI: the Linux job cross-builds and uploads; a Windows job runs the unit tests, (d), (e), (b) and a cross-OS (h): a Linux 64-bit save loaded by the Windows build and back (same layout with `-mno-ms-bitfields`) | the job green on `workflow_dispatch` | GitHub (Windows minutes x2) |

Not attempted on Windows: i686 (2.3); sanitizers (MinGW GCC has none, ext); building the game
on Windows (upstream: WSL only); the GBA ROM there.

### 5.3 macOS (x86_64), in order

| # | Task | Gate | Runs where |
|---|---|---|---|
| M0 | Probes, one job per image. `macos-15-intel`: `-no_pie -pagezero_size 0x1000 -image_base 0x40000000`, segments at the GBA addresses with `-segaddr`, `.4byte` absolute words linked, `SIGFPE` `si_code` for /0 and INT_MIN/-1, the mcontext fields. `macos-latest`: `MAP_FIXED` at 0x02000000 with and without `-pagezero_size`, ucontext on arm64, Rosetta present (`arch -x86_64 /usr/bin/true`) | the report in STATUS.md | two short jobs (x10 minutes) |
| M1 | The data for Mach-O: an ELF-to-flat converter on Linux (1.3), with the `_` prefix | every data object assembles with `clang -target x86_64-apple-macos11` and its section bytes and relocations equal the ELF object's | here (clang 18 is installed) |
| M2 | The linker-script counterparts: region segments, Mach-O spellings of the 18 `section(...)` sites (upstream rows), the tests table through `section$start$`, the soft-reset brackets through renamed sections | links on `macos-15-intel` | GitHub |
| M3 | `host_macos.c`: exe path, image and stack bounds, mcontext accessor, stack switch | unit tests under clang ASan and UBSan without `detect_leaks` | `macos-15-intel` |
| M4 | The build on macOS: `gmake`, Homebrew prerequisites, `gbagfx_pad.sh`'s `stat`, `OBJCOPY=true` for the hydra, coreutils on PATH for `md5sum` and `stdbuf` | (a), (d), (e), (b), cross-OS (h) | `macos-15-intel` |
| M5 | CI job, `workflow_dispatch` only | green | GitHub |

Not attempted on macOS: arm64 natively (2.5): it needs the rebased model, a decision for the
owner; until then an x86_64 build runs on Apple silicon under Rosetta 2 (ext). No macOS work
before M0's report: a failing build on a x10 runner teaches nothing the probes do not
(`docs/DECISIONS.md:798-816`).

### 5.4 Which gate can run where

| Gate | Linux here | here with Wine | `windows-latest` | `macos-15-intel` | `macos-latest` (arm64) |
|---|---|---|---|---|---|
| platform: format, lint | yes (clang-format 18 pinned, `BUILDING.md:74-75`) | - | - | - | - |
| unit tests | yes, sanitized | plain (after W3, W5) | plain | ASan+UBSan, no LSan | - |
| (a) build | yes; the Windows cross build | - | runs the artifacts | native build | probe only |
| (b) suite | yes | yes, Linux hydra driving wine (W8) | yes, hydra under MSYS2 (W8) | yes (M4) | - |
| (c) GBA ROM | yes | - | - | - | - |
| (d), (e) replays | yes | yes (W5, W6) | yes | yes | - |
| (f) sanitized gates | yes | - | - | partly (no LSan) | - |
| (h) save across widths, across OSes | yes (widths) | - | cross-OS variant | cross-OS variant | - |

## 6. What a CI job needs on each platform

- **ubuntu-latest** (Ubuntu 24.04, ext): the Linux job of `docs/PLAN.md:173-185` plus the MinGW line
  of section 4 and the pinned SDL2 tarball (cache key: version and sha256). Packaging differs
  from this machine: the Linux CI worker found `libsdl2-dev:i386` uninstallable on the runner and
  unpacks it by hand (`pen-worktrees/ci-native`, commits ed4d84e5b8 and fd780e0658). The cross
  job uploads the exe, `SDL2.dll` and the unit-test binaries with `actions/upload-artifact`.
- **windows-latest** (Windows Server 2025, ext; private standard runner 2 vCPU, ext): downloads
  the artifacts; `shell: bash` for the replays (Git Bash coreutils); for (b) `C:\msys64` with
  `pacman -S --noconfirm gcc make` to build the hydra (ext). Two traps: the runner's git checks
  out with `core.autocrlf=true` (ext), which would turn the replays, the expectations and the
  bash scripts to CRLF, so the job sets `git config --global core.autocrlf false` before
  `actions/checkout` (or the repository gets a `.gitattributes`); and a crash can open a Windows
  Error Reporting dialog that hangs until the job's timeout, so the exe sets
  `SetErrorMode(SEM_NOGPFAULTERRORBOX)` and every job has `timeout-minutes`. A loader refusal of
  layout B shows as a non-zero exit with an NTSTATUS such as 0xC000007B
  (`STATUS_INVALID_IMAGE_FORMAT`) or 0xC0000018 (`STATUS_CONFLICTING_ADDRESSES`) (ext); a crash the
  filter catches shows `crash_reboot:` lines as on Linux.
- **macos-15-intel / macos-latest** (ext): `HOMEBREW_NO_AUTO_UPDATE=1 brew install sdl2 libpng
  pkg-config make coreutils`, coreutils' `libexec/gnubin` added to `GITHUB_PATH`; tools and
  converted assets cached as on Linux (`actions/cache`, 10 GB per repository, evicted after 7 days
  unused, ext); `workflow_dispatch` only (x10). Failures: ld64 prints `ld: ...` with the symbol;
  an x86_64 exe on an arm64 image without Rosetta fails with "Bad CPU type in executable"; a
  kernel refusal of a Mach-O shows "Killed: 9" (ext).
- All jobs keep `out/` of a failed replay as an artifact, as the Linux draft does
  (`pen-worktrees/ci-native/.github/workflows/native.yml:171-176`).

## 7. M1 measurements: the data converted for Mach-O and COFF

Measured 2026-10-09 on `work/m8-dataconv` (from main at 2c170bed5b) with clang 18 and Python
3.12 on 32 cores (WSL2): `make -f Makefile.native data-flat|check-flat TARGET=...`
(`platform/build_glue/elf_to_flat.py` and `check_flat.py`; `docs/BUILDING.md`, "The data for
macOS and Windows"). Counts and the `-j32` times again on 2026-10-14 after the review's
fixes (the game's `.data` and `.bss` under their own section names, every absolute symbol
local); the serial times are the first version's.

| | macho-x86_64 | coff-x86_64 |
|---|---|---|
| Objects converted, and checked equal | 544, 544 | 544, 544 |
| Listings (MB) | 544 (55.3) | 544 (54.9) |
| Objects clang writes (MB; the ELF ones are 26.0) | 22.7 | 22.2 |
| Section bytes, and zero-fill (`game_bss`) | 13,752,400, 2,576 | the same |
| 8-byte words | 19,208 `X86_64_RELOC_UNSIGNED`, `r_length` 3 | 19,208 `IMAGE_REL_AMD64_ADDR64` |
| 4-byte words | 41,946 `X86_64_RELOC_UNSIGNED`, `r_length` 2 | 41,946 `IMAGE_REL_AMD64_ADDR32` |
| 16-bit words given their values | 8 | 8 |
| Symbols (the ELF objects: 298,581), all absolute ones local | 298,550 | 298,550 |
| `data-flat`, then `check-flat`, `-j32` | 4.3 s, 2.2 s | 1.5 s, 2.2 s |
| the same without `-j` (first version) | 22 s, 23 s | 24 s, 71 s |

- **Failures: none.** Every object converts, and the object clang writes from its listing equals
  the ELF object in bytes, relocations (offset, width, symbol, addend), 16-bit values, symbols
  (name, section, value, binding) and alignment, for both targets.
- **Relocations.** 25,753 of the 61,154 address words name a section symbol in the ELF object
  (GNU as's form for a local label; 21,241 of them 4 bytes, 4,512 8 bytes): the listing names the
  label at or before the target with the difference as the addend, and the checker compares the
  place each word reaches. No other relocation type occurs.
- **The 8 16-bit words** name 6 symbols, all absolute constants (`.set` with `.global`) of other
  data objects, taken from one table over the 544 objects (0.5 s): `B_MSG_MON_WATCHING` 0,
  `B_MSG_MON_ANGRY` 1, `B_MSG_MON_EATING` 2 (each defined, with one value, in
  battle_scripts_1.o and battle_scripts_2.o; one word each in battle_anim_scripts.o),
  `SPECIAL_CalculatePlayerPartyCount` 0x84, `SPECIAL_ValidateEReaderTrainer` 0xf7,
  `SPECIAL_GetMysteryGiftCardStat` 0x187 (event_scripts.o; 5 words in mystery_gift.o).
- **Symbols.** 31 fewer than in the ELF objects: the 25 undefined symbols no relocation names (C
  enum constants the preprocessor leaves as names, met in `.equiv` aliases and `.if`
  comparisons: `CONNECTION_*` in three objects, `ANIM_BATTLER`, `TYPE_ROCK`, `TYPE_STEEL`,
  `STR_VAR_1..3`), and the 6 that only 16-bit words named. The 3,463 names beginning with `L`
  become `_L...` on Mach-O with the prefix every name gets, which is also the name clang gives
  the C declaration: no rename. One label lies inside a relocated word, `keysplit_piano`
  (sound_data.o `.rodata+0x863dc`, 4 bytes into the wav pointer before it, where
  `sound/keysplit_tables.inc` sets it 36 bytes back from its table); the listing sets it back
  from the word's end as well (`.set _keysplit_piano, . - 4`), which clang writes as an
  `N_ALT_ENTRY` symbol on Mach-O.
- **Absolute symbols.** 257,908, most of them local constants (`MPlayDef.s`, the script
  macros); all values fit COFF's 32-bit symbol value (8 are -1, which COFF holds as
  0xffffffff). 7,930 global absolute names are defined in more than one object, each always
  with one value. GNU ld accepts that for ELF and PE (measured for ELF; bfd ignores "a
  redefinition of an absolute symbol to the same value", ext: `bfd/linker.c`); a Mach-O linker
  may refuse them as duplicates. Since no 8- or 4-byte word names an absolute symbol (the
  `--symbols` pass now checks it) and the 16-bit words take their values from the table, the
  listings write every absolute symbol local.
- **Sections of the game's variables.** The data objects' `.data` (empty at 1.17.1) and `.bss`
  (2,576 bytes, sound_data.o's music players) are what `platform/gba_game.ld` brackets by
  object path as `game_data` and `game_bss`, the bss that crt0 zeroes on a soft reset. ld64
  cannot select by path, so the listings name them `__DATA,__game_data` and zero-fill
  `__DATA,__game_bss` on Mach-O, `game_data` and `game_bss` on COFF.
- **COFF's relocation count.** The most relocations in one section are 18,123
  (event_scripts.o's script_data; then 15,401 in battle_anim_scripts.o's), under the 65,535 a
  COFF section header counts, so no object needs `IMAGE_SCN_LNK_NRELOC_OVFL`; `coff_object.py`
  reads that form anyway (its self-test makes one). An earlier draft of this section said
  111,111 from a misread `readelf` column.
- **Time.** The converter takes 0.22 s for event_scripts.o and 0.5 s for sound_data.o (10.7 MB
  of `.rodata`), clang under 0.3 s for any listing. The serial COFF check is slow on this
  machine only: clang's MinGW driver searches `PATH` for a GCC, and under WSL2 `PATH` has 15
  `/mnt/c` directories, about 80 ms a run (`--sysroot` avoids the search; not used).
