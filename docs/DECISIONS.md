# Decisions

Each entry records a decision that was not obvious, the alternatives, and
why. Newest at the bottom. Dates are when the decision was made.

## 2026-10-08: Fork point and remote

Clone of rh-hideout/pokeemerald-expansion at tag `expansion/1.17.1`,
branch `main` from it, upstream kept as remote `rhh`. No `origin` exists
yet; `STATUS.md` says what to add. Never force-push.

## 2026-10-08: Coding workers are Sonnet, not DeepSeek

The brief asks for DeepSeek 4.1 Flash workers. The agent runtime in this
session offers Sonnet, Opus, Haiku and Fable only, so the brief's
fallback applies: coding workers run on Sonnet, complex modules on Opus,
and the orchestrator checks at each milestone whether a DeepSeek option
has appeared. Recorded in `STATUS.md`.

## 2026-10-08: License

An MIT license scoped to this project's own files (the platform layer
under `src/platform`, the native build files, the native tests, and the
documentation), following the one reference port that has a license
(pokeemerald-multiplatform). It explicitly disclaims the decompiled game
code, Nintendo's property and third-party software. Alternatives: no
license (what pret, expansion, native-vanilla and regionalidades do),
rejected because contributors to the platform layer need to know what
they are granting; GPL, rejected because it would be inconsistent with
the decompilation projects and would be meaningless for the game code we
cannot license. We do not copy code from the reference ports: two of them
credit GPL and MPL emulators as sources for their BIOS and renderer, so
their code cannot be relicensed MIT by us.

## 2026-10-08: Memory model mirrors the GBA address space

The platform maps EWRAM, IWRAM, I/O, palette, VRAM, OAM and flash at their
GBA addresses and links the executable at 0x40000000. Alternative: the
ports' C arrays with `REG_BASE` and friends redefined in expansion's
headers. Chosen because it leaves `include/gba/io_reg.h`,
`include/gba/defines.h` and `include/gba/flash_internal.h` unmodified,
keeps `EWRAM_START` and the test runner's fixed addresses meaningful,
and was verified to work under both bit widths with the sanitizers
(`research/memory-map-experiment.md`). Cost: a native linker script,
`MAP_FIXED_NOREPLACE` on Linux, and later an equivalent on Windows and
macOS (both have fixed-address reservation; macOS needs the page-zero
size reduced for the low addresses). Flagged for the 64-bit milestone:
nothing here depends on pointer size.

## 2026-10-08: Single thread; VBlankIntrWait is the frame boundary

The game runs on the main thread and the platform does all per-frame
work inside `VBlankIntrWait`. Alternative: multiplatform's game thread
plus render thread. Rejected: the ports prove no concurrency is needed,
the two-thread ports have data races and busy-spins, and one thread keeps
the sanitizers and the input replay deterministic. Audio is queued, so
SDL's audio thread never touches game state.

## 2026-10-08: Clean-room platform modules, m4a translated from the decomp's own asm

BIOS, renderer, PSG, DMA, timers, RTC, interrupt controller and save
container are written from GBATEK with tests written first. The m4a
mixer is a C translation of expansion's `src/m4a_1.s` rather than the
ports' C engine of undocumented provenance. Reason: licensing (above) and
the brief's rule that tests precede adaptation. The ports are consulted
for structure and pitfalls.

## 2026-10-08: Frame rate is the GBA's

Frames are paced at 280896 cycles of 16.777216 MHz, 59.7275 Hz, and audio
at the engine's native sample rate, not the ports' 60 Hz and 42060 Hz.
Reason: the test suite, the replay and the RTC all count emulated time;
matching the hardware removes a class of divergence from the GBA
reference. Vsync is not used for pacing.

## 2026-10-08: One guard macro, PLATFORM_NATIVE

Every modification of an expansion file is under `PLATFORM_NATIVE`,
defined only by the native makefile. Alternatives: reuse the ports'
`PORTABLE` (rejected: it carries their semantics and regionalidades uses
it as a gameplay flag) or several macros per backend (rejected: backends
are selected inside the platform layer, not in game code).

## 2026-10-08: Native build entry point is Makefile.native

A separate makefile at the root, reusing expansion's rule fragments,
rather than extending expansion's `Makefile` (every port that did so
either broke the GBA build or made upgrades a merge exercise) or a
different build system (expansion's asset pipeline is make-based and we
reuse it).

## 2026-10-08: Platform unit tests use an in-house header, not a vendored framework

See `RESEARCH.md`, "Test framework for the platform layer".

## 2026-10-08: Sanitizers on 32-bit instead of Valgrind

The i386 ASan and UBSan runtimes work in this WSL environment (verified
with SDL2 linked), so the 32-bit smoke boot runs under the sanitizers.
Valgrind fails at startup here for 32-bit binaries without the i386 libc
debug package, and is not required.

## 2026-10-08: The platform layer lives at `platform/`, not `src/platform/`

Expansion's `Makefile` collects `src/*.c`, `src/*/*.c` and `src/*/*/*.c`,
so anything under `src/platform/` would be compiled into the GBA ROM
unless the Makefile were modified (regionalidades had to add a
`filter-out`). A top-level `platform/` directory needs no upstream change.
The C translation of `src/m4a_1.s` lives there too, under
`platform/m4a/`, for the same reason, even though it is game code rather
than hardware.

## 2026-10-08: GBA regions come from a linker script, not runtime mmap

`platform/gba_regions.ld` (a GNU ld fragment, `INSERT AFTER .bss`) declares
each GBA region a NOLOAD section at its GBA address, so the kernel maps it
zero-filled and writable when it loads the executable; `gba_memmap_init`
only checks the link and `gba_memmap_shutdown` has nothing to release.
Alternative: `mmap` with `MAP_FIXED_NOREPLACE` at init, as in
`research/memory-map-experiment.md`. Both were tried with 32-bit and 64-bit
binaries under ASan and UBSan, and both map every region. The linker script
was chosen because EWRAM also has to hold expansion's `EWRAM_DATA` (section
`.sbss`), which the linker places: with mmap the loader has already mapped
those pages and mapping EWRAM fails with `EEXIST` (observed), so EWRAM would
be split between two mechanisms, while one output section holds both here.
The regions also exist before any code runs, so nothing (sanitizer runtime,
loader, malloc) can take an address first and there is no runtime failure
path, and LeakSanitizer scans them as globals, where an mmap'd region holding
the only pointer to a heap block produced a false leak report (observed).
Costs: GNU ld and a non-PIE link (`-no-pie -Wl,-Ttext-segment=0x40000000
-Wl,-T,platform/gba_regions.ld`, which `Makefile.native` must also use); ld
puts the ELF headers in the page below EWRAM (0x01FFF000, unused on the GBA);
Windows and macOS will need init to reserve the regions at run time instead.
Details in the header comment of `platform/gba_memmap.c`.

## 2026-10-08: Platform builds include expansion's headers with -idirafter

`docs/C-RULES.md` asks for expansion's headers as system headers via
`-isystem`. `platform/Makefile` uses `-idirafter ../include`, which also
marks them as system headers but searches them after the C library's:
expansion's `include/` has `strings.h`, `malloc.h`, `link.h` and `menu.h`,
and with `-isystem` glibc's `<string.h>` includes expansion's `strings.h`
and nothing compiles.

## 2026-10-08: Native structs follow the host ABI; GBA save interchange is out of scope for now

Expansion compiles the GBA with `-mabi=apcs-gnu`, which rounds every
struct to 4-byte alignment and size. x86 has no equivalent flag, so
natively `SaveBlock1` is 15496 bytes against 15568 on the GBA, and
structs without 4-byte members are 2-byte aligned. Alternatives: hand
`ALIGNED(4)` on the save structs (what pokeemerald-multiplatform did;
incomplete because nested structs keep their native stride), or a
conversion layer between native and GBA layouts (what native-vanilla did
for 64-bit). Decision: the native build uses the host ABI as its own
format, its save file is the native layout, and interchange with GBA
`.sav` files is listed in `STATUS.md` as not supported until a
conversion layer exists. Consequence for the platform layer: `CpuSet`,
`CpuFastSet` and DMA must accept 2-byte-aligned addresses (the game
passes them legitimately now) and must copy with `memcpy` rather than
reject or mask them; the `CpuFastSet` alignment static assert in
`include/gba/syscall.h` is disabled under `PLATFORM_NATIVE`. Flagged
64-bit: the same reasoning applies with 8-byte pointers.

## 2026-10-08: `-funsigned-char` for expansion's sources on x86

Plain `char` is unsigned on ARM and signed on x86. Expansion's code was
written against the ARM behaviour, so the native build compiles
expansion's sources with `-funsigned-char`. The platform layer uses
explicit `uint8_t`/`int8_t` and does not depend on it.

## 2026-10-08: Interrupt controller keeps the request bits itself

`REG_IF` clears on write-1 on hardware; natively a write is a store. The
controller holds the pending bits in its own state, stores them to
`REG_IF` after every change together with a marker in bit 15 (unused by
the hardware, never written by the game), and treats a differing
`REG_IF` as a game write whose 1 bits clear requests. Because of the
marker, every game write differs from the stored value, so expansion's
stale-request idiom (`REG_IF = INTR_FLAG_x` before enabling a source) is
seen even when that bit is the only one pending. Residual blind spots,
documented in `platform/interrupt.h`: several game writes between two
platform calls (only the last counts), a raw read of `REG_IF` shows bit
15 (expansion never reads `REG_IF`), and a `REG_IF |= x` read-modify-write
(expansion has none). The alternative, routing every `REG_IF` write
through a guarded call, would touch nine game files. Also decided: no interrupt nesting
(`crt0.s` lines 99-108 narrow `IE` while a handler runs; a request
raised inside a handler runs after it in the same dispatch); a Game Pak
request halts as `IntrMain` does; a dispatch cap of 256 requests per
call guards against a handler that re-raises forever.

## 2026-10-08: RTC runs on UTC plus a persisted offset

A cartridge clock is set once and never knows time zones or DST. The
model's seconds are UTC plus an offset persisted next to the save; on
first run the offset is initialised to the host's local-minus-UTC
difference so the in-game clock reads wall time, after which host zone
or DST changes do not move it. The alternative, host local time (what
every reference port does), jumps an hour at DST changes.

## 2026-10-08: Keypad details

`KEYINPUT` bits 10-15 read as 0 (GBATEK says "not used"; mGBA does the
same; expansion masks with `KEYS_MASK`). Opposite directions are not
filtered, as the hardware does not. Up to four controllers are held open.
A failure to start SDL's controller subsystem is reported and the
keyboard continues; a single controller that fails to open is reported
and skipped.

## 2026-10-08: Audio queue bound of four frames; no vsync pacing

Audio is pushed with `SDL_QueueAudio`; a block that would push the queue
past four GBA frames of samples is dropped and counted rather than
queued, so latency cannot grow without bound (the reference ports let it).
The device starts paused and `main` unpauses it after the first frames
are queued. The frame pacer, not vsync, sets the rate, using an
accumulator of the exact GBA period (280896 cycles at 16777216 Hz) so
there is no long-run drift, and it resynchronises after a stall longer
than 250 ms instead of running frames back to back.

## 2026-10-08: The native save file is versioned, and 32-bit saves must load in the 64-bit build

Requested by the project owner after the host-ABI decision. The native
save file carries a small header (magic, format version, a layout tag
naming the pointer width and struct-layout identifier the image was
written with, image size, CRC32) ahead of the 128 KiB flash image. A
bare 131072-byte file is accepted as legacy with an unknown layout and
is rewritten with a header on the next store. Gate (h), run in milestone
7: a save written by the 32-bit native build loads correctly in the
64-bit build, which will need a layout conversion keyed on the tag. The
header makes any future layout change detectable instead of silent.

## 2026-10-08: PSG trigger, wave-bank and output-scale rules

The PSG module consumes a trigger by clearing NRx4 bit 7 in the I/O
region after acting on it (the bit is write-only and reads 0 on
hardware), because m4a rewrites NRx4 with bit 7 on every volume change
and every note and expects a retrigger each time; remembering the bit
would silence those. Wave RAM bytes changed since the last call go into
the bank selected for playback at that call, which reproduces the result
of m4a's load sequence (select the other bank, write, select back within
one frame); GBATEK's literal rule (writes go to the unselected bank at
the moment of each write) cannot be applied to state sampled once per
frame. Length is loaded from NRx1 on each trigger for the same reason.
Output is 16 int16 units per GBA output unit (GBATEK "Max Output
Levels"), maximum 15360 for four channels at full volume, leaving room
for the two Direct Sound FIFOs in the same int16 range; the audio mix
must remove DC after summing, as the GBA's output capacitor did. The PSG
is generated at the output rate with point sampling and an exact cycle
remainder, never at m4a's 13379 Hz mixer rate, which would alias badly.

## 2026-10-08: UBSan shift-base is disabled for expansion's objects only

Expansion's DMA macros (`include/gba/macro.h`) and several source files
compute `(DMA_ENABLE | ...) << 16`, which shifts a signed `int` into
its sign bit. GNU C defines that result and expansion relies on it, but
UndefinedBehaviorSanitizer's `shift-base` check aborts on it even with
constant operands, so a sanitized native game would die at its first
DMA. The sanitized build therefore compiles expansion's objects with
`-fno-sanitize=shift-base` and keeps every other check; the platform
layer keeps all checks, including `shift-base`. Alternatives: a guarded
change to every site (dozens of files, pure noise), or dropping UBSan
from the game build (loses every other check). Recorded next to the flag
in `Makefile.native`.

## 2026-10-08: DMA module state is authoritative

The DMA channels act on the state recorded by `dma_set` and `dma_stop`
(reached from expansion's macros through the guarded `DmaSetUnchecked`),
never on direct stores to the DMA registers (m4a's `SoundInit` writes
DMA1/2 for the sound FIFOs, which are not emulated). Refused transfers
print the reason and are discarded by the macros rather than asserted,
because the game never checks DMA results and an abort would kill it
where hardware would have transferred. The 27/28-bit address masks are
not applied because native addresses are not GBA bus addresses; the
transfer-bounds policy replaces them.

## 2026-10-08: PPU interpretation points

Windows with X1 > X2 or Y1 > Y2 wrap around (hardware and mGBA
behaviour; GBATEK's literal "X2 = 240" wording is not followed). OBJ tile
reads wrap inside the 32 KiB OBJ area; BG map and tile reads beyond the
64 KiB BG area read 0. Affine reference points are copied in when
BGxX/BGxY differ from the last value copied (plain memory cannot show a
write of the same value; the game writes them through SetGpuReg at
VBlank, picked up at line 0) and step by PB/PD after every line. The OBJ
cycle budget (1210, or 954 with the H-Blank Interval Free bit) is
charged per OBJ in OAM order including horizontally off-screen ones,
and the OBJ that exhausts it is drawn partially. Modes 2 to 7 are not
supported and render a black line with an error. Not modelled: the
"garbage" quirk for out-of-order OBJ priorities and the one-frame delay
before a newly enabled BG appears.

## 2026-10-08: The m4a engine is a C translation of src/m4a_1.s, proven by a differential test

The ARM sound engine (`SoundMain`, `MPlayMain`, the `ply_*` commands) is
translated to C under `platform/m4a/`, with every struct offset the
assembly hard-codes asserted against `include/gba/m4a_internal.h` and
the pointer-dependent ones flagged for the 64-bit milestone. Where the
assembly would read outside its data for state the game never produces
(compressed samples past either end, a data byte before any running
status, more than 12 channels, PSG voice types 5 to 7, a ring frame out
of range, `SOUND_INFO_PTR` still 0), the C guards and does nothing, with
a test per case. Evidence: a differential test builds one driver twice,
for ARM with the original `src/m4a_1.s` under `qemu-arm` and natively
with the C, and compares the whole engine state after every frame of 300
mixer and 60 sequencer scenarios: identical. It is not part of the
standard gate because it needs the ARM toolchain and QEMU; it is run by
hand before releases. `ptest.h`'s equality macros do not cast their
arguments, which is how the one sign-conversion the author's private
stub had hidden was found.

## 2026-10-08: BIOS interpretation points

The sine table is 1.14 fixed point (65 quarter entries, round(sin x
4000h)), products shifted right by 14, matching ArcTan's 14 fractional
bits; GBATEK gives neither the table nor its precision. The ArcTan
polynomial coefficients are the BIOS's own eight constants, supplied
from knowledge of the BIOS rather than from GBATEK or tonc, and checked
by ArcTan(4000h) == 2000h and the error bound. Div by zero returns zeros
with a one-time message. A wrong decompression type nibble is refused.
SoftReset longjmps to a target the frame loop registers; without one it
prints and returns. Bounded decompressor variants exist for the tests
only. CpuSet, CpuFastSet and the Vram writers do not enforce alignment
(host-ABI decision above).

## 2026-10-09: The link base stays at 0x40000000; no ROM-window mirror

Expansion tags script-command, special and `callnative` pointers with
`ROM_SIZE` (0x02000000) to mark "effect-instrumented" entries; on the
GBA the tagged address is a valid ROM mirror. The test-runner worker
tried linking the executable at 0x08000000 and mapping its read-only
segments again at +0x02000000 (`platform/rom_mirror`), which also makes
`ROM_START..ROM_END` checks and the 28-bit location encodings work
unchanged. Rejected: it caps the native image at 32 MiB (the TEST=1
image had 5.7 MiB left and the sanitized one only fitted with outline
instrumentation and a relocated rodata), it cannot exist on 64-bit
(data access is RIP-relative), and this project's next phase is adding
content. Chosen: the base stays at 0x40000000, tagged pointers are
untagged at their four call sites under guards (`platform/script_tag.h`;
bit 25 is clear for every code address below 0x42000000, asserted at
link time), and the two remaining ROM-address assumptions
(`src/malloc.c`'s location encoding, `src/field_name_box.c`'s pointer
test) get small guards. One mechanism for both bit widths and no size
cliff. A 1 MiB `.platform_call_stack` region at 0x01800000 holds the
battle runner's stack so that a 27-bit address field in the battle
tests' queued events can hold a stack address.

## 2026-10-09: Native fixes do what the GBA ends up doing

Every site where the GBA reads or writes memory that does not exist
natively (NULL, past a table, a wrapped counter) gets the smallest
guarded change that produces the GBA's outcome, worked out from what the
GBA actually serves there (the BIOS at address 0, open bus past the ROM,
EWRAM mirrors). Where that outcome is unknowable (an open-bus word used
as an address) the change does what a correct program would, and the row
in UPSTREAM_MODIFICATIONS.md says "deliberate divergence". No native
build ever emulates the bus: the platform does not map page zero
(`vm.mmap_min_addr` forbids it without privileges, checked) and will
not, because the faults are how these bugs are found.
`platform/test_runner/SUITE-LOG.md` has every site with its test count.

## 2026-10-09: Benchmarks are assumption failures natively

`BENCHMARK`/`EXPECT_FASTER` compare GBA cycle counts from Timer 3. The
native build advances time at frame boundaries, so both counts are 0;
wall-clock timings on x86 would say nothing about the GBA. The 9
benchmark tests end in `BenchmarkStart` as assumption failures with the
reason, which the hydra lists under ASSUMPTIONS_FAILED and does not fail
on (`gTestRunnerSkipIsFail` is 0 outside rh-hideout's CI). Gate (b)
counts "per test the GBA baseline but for the 9 benchmarks" as green.

## 2026-10-09: The test runner's persistent state crosses a crash by re-exec

mgba-rom-test restarts the ROM after a crash with the last 0x200 bytes of
IWRAM intact, which is how the runner resumes after a `CRASH`. Natively
`platform/crash_reboot` catches the fatal signal, prints `crash_reboot:
signal N at <pc>, fault address <addr>` on stderr, and re-executes the
executable with the persistent state passed as `--persistent-state
<hex>`; `crt0` restores it before `AgbMain`. The alternative, a
longjmp-style in-process reboot, was rejected: the crashed process's
heap, sanitizer state and signal context are not trustworthy. The 28-bit
`address` field holds the test's index in `.tests` plus one rather than
a native address. The crash line goes to the shared stderr and can
interleave with the hydra's stdout line for another runner (seen once as
a crash line inside a PASS line); cosmetic, and the hydra's own result
lines are unaffected.

## 2026-10-09: Division by zero gives libgcc's ARM results, through a SIGFPE handler

Nine tests divide by zero (`ComputeBallShakeOdds`, `GetHealthPercentage`,
`AnimSparkElectricityFlashing_Step`, two animation tables); on the GBA
libgcc's `__udivsi3`/`__divsi3`/`__modsi3` return (0xFFFFFFFF, INT_MAX or
INT_MIN, the dividend) and the game goes on. `platform/arm_division`
installs a SIGFPE handler that decodes the faulting `div`/`idiv`, writes
libgcc's results into the registers and resumes after the instruction,
reporting each one with its program counter. 32-bit only: the decoder
refuses 66h/67h-prefixed forms and 64-bit is a hard stop until it is
redone (milestone 7). Rejected: guarding every division site in
expansion (nine today, unknown tomorrow) and `-fsanitize=integer-divide-
by-zero` recovery (which gives x86's result, not ARM's).

## 2026-10-09: Native save-block sizes are checked, not the GBA's

`test/save.c` compares `sizeof` of the save blocks with constants so that
a hack notices a layout shift. The host ABI gives different sizes
(DECISIONS.md, "Native structs follow the host ABI"), so under the guard
the test checks the 32-bit native sizes. The test keeps its purpose, a
layout shift in the native save still fails it, and the GBA values stay
in the `#else`. 64-bit will get its own values, and the cross-width
round trip (gate h) is what makes the two layouts interchangeable.

## 2026-10-09: The sanitized TEST=1 build uses outline AddressSanitizer checks

Inline instrumentation makes the TEST=1 code about 40 MB, past
0x42000000, where bit 25 of a code address would collide with
`platform/script_tag.h`'s tag (gba_regions.ld asserts `etext` stays
below it). `--param=asan-instrumentation-with-call-threshold=0` makes
every check a call: about 28 MB, the same checks, slower. Applies to
Makefile.native's SANITIZE=1 build; the platform unit tests are small
and stay inline.

## 2026-10-09: `check` runs the sanitized suite with halt_on_error=0 and abort_on_error=1

Expansion's objects are built with `-fsanitize-recover=address`, so the
game's reports are printed and the runner carries on, which is what
makes the sanitized run useful (one run lists every finding). The
platform library never recovers, and a report there exited the process,
losing the rest of that runner's partition (107 tests in one run);
`abort_on_error=1` turns the exit into SIGABRT, `crash_reboot` reboots,
and the hydra counts one `CRASH`. The caller's `ASAN_OPTIONS` come last
and win.

## 2026-10-09: Headless test runs do not draw

The first full suite runs took 15 minutes, almost all in the PPU drawing
frames nobody sees. In `--test-runner` mode `frame` skips rendering
unless a replay or a screenshot is active (both need every frame); the
suite takes about 40 seconds on 32 cores. The game-visible state (VCOUNT,
DISPSTAT, HBlank DMA, interrupts) is unchanged, which the per-test
comparison with the GBA baseline confirms. An earlier version skipped
drawing in every headless run and blacked out the replay's screenshots;
a test now checks that a replay still draws.

## 2026-10-09: No wall-clock watchdog in the native runner

Expansion's runner times a test out after 60 emulated seconds (Timer 2);
the native runner advances the timers per frame, so the same path fires
after the same number of frames, and the hydra has no watchdog of its own.
A wall-clock limit would add a second, machine-dependent reason for a
`TIMEOUT` and make the result depend on the host's load. The emulated
timeout was exercised end to end with a throwaway probe (SUITE-LOG.md,
"The timeout, end to end").

## 2026-10-09: Short palettes are padded at build time, with an exclusion list

152 generated `.gbapal` files have fewer than 16 colours, and the game
loads most of them as whole 32-byte banks (`LoadSpritePalette`,
`LoadPalette(..., PLTT_SIZE_4BPP)`): on the GBA the copy reads the bytes
after the palette in ROM into colours the sprite never uses; natively it
reads past the object and the sanitized gate stops. Milestones 3 and 4
padded ten of them by hand with `[16]` declarations and rows; the debug
menu reached the species palettes (about 80 files, declared in generated
headers), where rows do not scale. Chosen: Makefile.native converts
INCGFX assets into its own directory, `build/assets-native`, through a
wrapper that zero-pads every `.gbapal` it writes there to a whole number
of banks; the GBA build's `build/assets` and the ROM are untouched, and
the wrapper never pads a file outside that directory (a short in-tree
palette would otherwise change the ROM). Padding is wrong for a palette
the game loads by `sizeof` or into a sized initialiser (the GBA loads
exactly its colours; a padded one would black out the rest of the bank):
those five are listed in `platform/build_glue/unpadded_palettes.txt` with
the reason, and `check_palette_padding.py` verifies on every link that
the list is exactly the set of short palettes used that way, so a new
upstream palette cannot slip into either mistake. The hand-padded guards
and their rows were removed. Cost: the first native build converts every
graphic (34 s wall, 10.6 CPU-minutes on 32 cores); `seed-assets` copies a
GBA build's conversion and regenerates only the palettes (15 s). A copy,
not hard links: gbagfx truncates its output in place and would rewrite
the GBA build's file through a link.

## 2026-10-09: Setup loops that spin on the DMA3 queue yield a frame

`CB2_InitPartyMenu`, `CB2_ReloadPartyMenu`, `CB2_InitSummaryScreen`,
`CB2_Bag`, `CB2_LoadPyramidBagMenu` and `DexNav_RunSetup` run their
setup state machines in a `while (TRUE)` loop whose steps wait on
`IsDma3ManagerBusyWithBgCopy`; on the GBA the V-Blank interrupt drains the
queue under the loop, natively a frame (and the DMA3 manager) runs only
in `VBlankIntrWait`, so the loop never ended. Rejected: draining the DMA3
queue inside the query (an expansion-wide behaviour change for one
caller class) and a conditional `if (busy) VBlankIntrWait()` (a step that
waits on anything else frame-driven would spin). Chosen: an unconditional
`VBlankIntrWait()` in each loop body under the guard, as `DoMapLoadLoop`
already does; a setup step per frame costs a few frames in a replay and
nothing else. 60 `while (TRUE)` loops exist in expansion; the six above
are the ones that spin on DMA3, the others are not on this pattern.

## 2026-10-09: The debug-menu gate is a replay with facts, not a menu walker

Gate (e) is `tests/smoke/debug_menu.replay` through the same engine as
the opening (`lib.sh`): the opening's first steps are `include`d, then the
debug menu gives items, Pokémon of every generation and form (complex
gives with shiny, Dynamax, Gigantamax, Tera and a chosen move), fills the
party into the PC, opens the party, summary and bag screens with a
screenshot each, fights two trainer battles to the action prompt
(`battle_main` fact), saves, restarts the process and continues, and
checks species, extras, moves, bag counts, a flag and a var after
Continue. Every numeric entry is checked as it is made (`debug_input`).
Argument facts (`party_extras:N`, `bag_item_count:ID`, `flag:ID`,
`var:ID`, `party_move:N:M`) are bounds-checked against expansion's
constants in `platform/inspect_args.c` and are not dumped. Not covered:
the debug menu's other paths (warps, encounters, sound, cheats, eggs,
decorations) and movement inside the summary screen.

## 2026-10-09: The trainer party seed does not hash the party table's address

`GeneratePartySeed` and `MakePartnerGenerator` (src/trainer_util.c) seed
a trainer's party with a CRC over the whole `struct Trainer`, whose
`party` field is a pointer. On the GBA that pointer is a ROM address
fixed by the ROM's link map, so the seed, and with it every trainer
Pokémon's personality and gender, is stable for a given ROM. Natively
the pointer is an address in the executable and moves with any relink;
three refactoring branches that changed nothing in the game flipped the
debug-menu gate's opponent between male and female, and a 32-byte shift
of the table was enough. Chosen: under the guard the CRC runs over a
copy with `party` cleared, so the seed depends on the trainer's data and
the player's ID only and is the same in every build of a width (the two
widths hash their own ABI's struct bytes and differ from each other,
which no gate compares). A deliberate divergence from the GBA, whose
seed is a property of its link map and cannot be reproduced. Rejected:
pinning the executable's layout (every change to the build would have to
preserve the table's address) and accepting layout-dependent gate
expectations (the gate would fail on every relink). `trainer_pools.c`'s
`GetPoolSeed` has the same pattern behind a disabled config and empty
data and is left alone. The glyph was first suspected to be an
uninitialised read; the proof that it was not (a Python reimplementation
of CRC32 and SFC32 predicting all six observed outcomes from the party
address alone) is in the worker's report, summarised in the row.

## 2026-10-09: The refactoring pass is seven reviewed module groups

The owner's standing instruction (PLAN.md) asked for a dedicated
refactoring pass over everything this project wrote before 32-bit is
declared done, as module-sized worker tasks reviewed as usual. Chosen:
seven groups on disjoint files (core, video, audio, bios and debug,
replay and inspection, storage and build glue, and an audit of the
modifications to expansion's files), each with the C-RULES checklist, a
cppcheck baseline, its own worktree and a mutation-testing review before
merge; behaviour is frozen by the existing tests, the replays and the
suite, which every branch runs before and the orchestrator runs again at
merge. Formatting (clang-format) and clang-tidy come after the pass, so
that the one-time reformat does not conflict with seven branches and the
refactors stay readable in the history. Follow-ups the reviews found
outside their groups are collected in STATUS.md for the end-to-end read.
