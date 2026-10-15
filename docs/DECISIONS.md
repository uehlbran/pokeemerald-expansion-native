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

Amendment 2026-10-14: audio no longer runs at the engine's rate but at the
GBA's 32768 Hz PWM grid, the engine's frame read on it (the owner's ruling
(1); the amendment to "PSG trigger, wave-bank and output-scale rules").

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

Amendment, 2026-10-14: the bare-file clause no longer holds. A file without
the header is refused, never accepted as legacy and never rewritten (the
owner's ruling 2 in "The owner's five rulings on the independent review's
open questions").

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

Amendment 2026-10-14: until the owner's ruling (1) of the same day the
shipped build ran the PSG at m4a's 13379 Hz (`FRAME_AUDIO_SAMPLE_RATE_HZ`),
so that the 224-sample FIFO frame mixed sample for sample with the PSG
output. The corrected reasoning: at 13379 Hz the square and noise channels
alias into the audible band. A square wave's odd harmonics fall off only as
1/k and the noise channel clocks at up to 524288 Hz, so every component
above 6689.5 Hz folds back below it as a tone the GBA never plays.
What ships now, by the ruling: the device and the PSG run at 32768 Hz, the
grid GBATEK's PWM stage samples on by default (SOUNDBIAS, "Resampling to
32.768kHz"), one sample every 512 cycles. One accountant counts each frame's
samples on that grid, 548 or 549 with the remaining cycles carried
(`platform/sound_grid.h`), and the PSG is asked for exactly that many, so it
is point-sampled where the PWM stage samples it and folds only what the
hardware folds at that setting: the square and wave channels' frequencies
are 131072 / (2048 - n) and 65536 / (2048 - n) Hz, so their periods are
whole numbers of quarter samples on this grid and their folded harmonics
land on harmonics (or quarter-harmonics), not between them as at 13379 Hz.
The FIFO frame is read on the same grid by
zero-order hold on cycle time: each engine sample held for its timer
interval (1254 cycles for 224 a frame) and taken at the last cycle of each
output sample's span, which is what the FIFO's DAC and the PWM stage do. So
the engine's other rates (SampleFreqSet's table) play too, where the old
path left them silent. The DC filter's pole moves with the rate (32701/32768,
the same 11 Hz corner). The A/B recordings of the opening replay, the old
path against this one, are the ruling's evidence; they hold the game's music
and are kept outside the repository. Not followed: Emerald itself selects
the next SOUNDBIAS mode, 8 bits at 65536 Hz (`src/m4a.c`: m4aSoundInit asks
for SOUND_MODE_DA_BIT_8 and SoundInit writes SOUNDBIAS_H bits 6-7 = 1), so
the console this game runs on samples every 256 cycles; the grid is one
constant (`SOUND_GRID_RATE_HZ`), and moving it to 65536 Hz is the owner's
call.

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

Amended 2026-10-14 (independent review, finding 01-11): the handler does not
report every division. Completed divisions by zero are silent, as they were
on the GBA; only a divide error the decoder cannot complete is reported, with
its program counter, before it is passed on. 64-bit operands are decoded now
(see "The 64-bit division rule is compiled at both widths").

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

## 2026-10-09: Milestone 7 starts from a measured inventory, not from the flags

An Opus worker measured the 64-bit build before any code was written
(the inventory is summarised in PLAN.md "Milestone 7 plan"; the full
document lived in the session's scratchpad and its findings are carried
into the task briefs). What it found that a read of the "flagged 64-bit"
comments would not have: the link fails on exactly two static assertions,
one of which (RecordedBattleSave) has no pointer in it at all (a u64 that
x86-64 aligns to 8); the 8-byte INTR_VECTOR store runs off the end of
IWRAM; the assembled data holds 61,154 address words of which 17,033 must
widen and 44,121 must not (bytecode) (the count at the inventory; after the
battle scripts were found to be read through packed structs with pointer
members it is 19,208 widened and 41,946 left); GCC's x86-64 data alignment spaces
the test runner's `.tests` entries 64 bytes apart; tools/patchelf is
ELF32-only; and in the save only SaveBlock1 changes layout (two members).
Each milestone 7 task is one section of that design, gated on the 32-bit
executables staying byte-identical ("gate 32=": sha256 of the four 32-bit
executables recorded on main at 174d3f5d9c) and on the GBA ROM's sha1.

## 2026-10-09: Gate (h) is a platform-side save converter, not a shared packed layout

At 64 bits only SaveBlock1's layout differs from the 32-bit build's: the
64 object event templates grow from 24 to 28 bytes (a script pointer) and
the Enigma Berry record from 52 to 64 (two description pointers), which
shifts everything after them and changes the fourth sector's chunk size.
Every other saved struct is byte-identical once RecordedBattleSave's u64
is given 4-byte alignment at 64. Two ways were evaluated. A: a platform
module converts a 32-bit image to the 64-bit layout when save_file_load
reports a foreign 32-bit layout, slot by slot, through two offset tables
each pinned by static assertions in its own width's build, recomputing
the sector and Enigma Berry checksums; expansion is untouched; about 300
lines plus a test. B: one packed layout for both widths, which means a
u32 handle behind an accessor at about 20 sites in 9 expansion files and
a departure from the host-ABI decision. A is chosen: it fits the rule
that expansion changes only where a hardware assumption cannot be
isolated, the difference is two members and fully tabled, and the static
assertions make upstream drift loud. B stays the fallback if upstream
adds more pointers to saved structs. The stored pointers themselves carry
no information worth converting: object event scripts are refilled from
the map header on Continue (as on the GBA) and the Enigma Berry
descriptions are meaningless at any native width. The container's
SAVE_FILE_LAYOUT_ID stays 1: the layout is "the host ABI" and the
pointer width in the header tells the two apart. Open with it: how a
64-bit build treats a bare image with no header (an emulator's or an old
native build's): decided when the converter is written, and recorded here.

## 2026-10-09: The 64-bit data is emitted in the x86-64 struct layout; the bytecode is not

The assembled data (maps, map events, layouts, voices, songs, cries, the
script and special tables) is read by C through struct and array types
whose pointers are 8 bytes at 64, so those address words (19,208 once the battle scripts' 2,174 pointer
operands were counted in) must be 8 bytes with the C structs' padding
and alignment; the 41,946 words inside bytecode (event scripts, battle scripts, song tracks, animation
scripts) are read byte by byte and must stay 4 bytes, which is lossless
because every target is below 4 GiB and the ROM_SIZE tag bit survives
zero-extension. Mechanism, in order of preference: guarded `.ifdef`
variants of the macros that emit the words (asm/macros/m4a.inc,
music_voice.inc, map.inc, def_special, the script command table,
TRACK_SIZE), each a row in UPSTREAM_MODIFICATIONS.md; native-only rules
in asm_filter.py for the raw words no macro emits (mapjson's output, the
seven hand-written tables, mid2agb's song headers), selected by an option
only the 64-bit recipes pass; and a relocation check run by the 64-bit
link that fails unless every must-widen symbol has only 8-byte
relocations and every 4-byte relocation left is in bytecode, so an
upstream addition cannot slip through. Changing mapjson's or mid2agb's
output was rejected: their outputs are shared, gitignored files the GBA
build uses too, so the text cannot differ between builds, and a tool
change would touch upstream for something the native build can do alone.

## 2026-10-09: Gate "32=" compares like with like, and platform code may change the executables

The 32-bit executables embed the worktree path, so a sha256 recorded on
main cannot be compared with a build in a worker's worktree; the gate is
a build of the base commit and of the branch in the same worktree. A
change to an expansion file under the guard must leave the four 32-bit
executables byte-identical to that base, which also means keeping the
32-bit code on its original lines (AddressSanitizer embeds the line
numbers of instrumented locals in the sanitized executable; a 64-bit
branch goes below the 32-bit one). A change under platform/ or to the
test runner legitimately changes every executable that links it; for
those the gate is the nm diff (only what was added) and the behavioural
gates (the suite at the baseline counts, the replays). Byte identity was
first held for platform code too and produced a duplicated decoder in
arm_division.c; that was reversed before the merge.

## 2026-10-09: The C width test in expansion's files is __SIZEOF_POINTER__ == 8

Not UINTPTR_MAX > UINT32_MAX: in a header included before <stdint.h>
UINTPTR_MAX is undefined and the test is silently false (the first
INTR_VECTOR guard kept the 8-byte store for that reason). The predefined
macro is true in every translation unit. In assembly the test is
`.ifdef NATIVE_POINTER_SIZE_8`, defined by Makefile.native for BITS=64
only; the GBA build and BITS=32 never define it.

## 2026-10-09: A 64-bit division by zero natively gives what the GBA's __udivdi3 gives

Expansion compiles the ROM with -mabi=apcs-gnu, so a u64 or s64
division calls libgcc's __udivdi3/__divdi3, never the __aeabi_* routines
(checked with nm on the ROM ELF). Their results for a zero divisor were
measured under qemu-arm against the GBA toolchain's own libgcc
(platform/arm_division_libgcc, a record rebuilt and compared by hand, as
the m4a difftest is): with k the bit length of n, n / 0 = 2^(k+1) - 1 and
n % 0 = n mod 2^(64-k), signed by negation, INT64_MIN / -1 = INT64_MIN
rem 0. The x86-64 SIGFPE handler gives those results for a REX.W
division. On the 32-bit build a 64-bit division by zero goes through the
host's i386 __udivdi3 and the handler completes its internal 32-bit DIV,
so the result differs from the GBA's; every 64-bit division site in the
game was listed and none can divide by zero (platform/build_glue/LINK-NEEDS.md),
so that divergence is recorded as unreachable rather than fixed by
wrapping the routines, which would change the 32-bit executables for no
reachable case.

## 2026-10-09: The sanitized 64-bit suite is compared with the suppression off; nothing is fixed for it

The 64-bit sanitized suite run lists more AddressSanitizer reports than
the 32-bit one (56 at 9 sites against 25 at 5), which looked like new
64-bit bugs. It is an artefact: the 32-bit build's outlined checks
report libasan's own address as the program counter, and ASan's default
suppress_equal_pcs then keeps only the first report of each access size
per runner. With that suppression off both widths report the same
accesses test by test (56,930 and 56,921; the nine extra at 32 bits are
a 7-byte copy that stays in bounds behind an 8-byte pointer). So the
comparison of widths is made with ASAN_OPTIONS=suppress_equal_pcs=0 and
the default `make check` keeps the suppression (57,000 lines per run
otherwise). No report is new at 64 bits and no expansion code changes for
this. Three upstream sites the 32-bit table had missed (Doom Desire's
coordinate table, Extrasensory's and Zap Cannon's gSineTable[320], a
non-affine animation loop) stay unfixed under SUITE-LOG's policy that a
game-code read which changes no test result is documented, not patched;
two of them (Doom Desire, Extrasensory) read a different value natively
than the GBA for one animation frame; those two were then given the
GBA's value at every width (the entry "Out-of-table reads that change a
frame get the GBA's value" below).

## 2026-10-09: A bare save image is read as the 32-bit layout, and conversion runs both ways

The native save file carries a header with the layout width, but a file
written before the header existed (the LEGACY form, a bare image) names
no width. It is taken as the 32-bit layout (SAVE_CONVERT_BARE_IMAGE_POINTER_BITS
in platform/save_convert.h): every such file was written by a 32-bit
build, the only build that existed then. The converter runs in both
directions (a 32-bit build reads a 64-bit save as well as the reverse),
so the two builds can share one save directory without a one-way trap.
A slot is converted only when all four SaveBlock1 sectors are intact
with each id in exactly one sector; a slot that fails its own checksum
is left for the game's own recovery, since converting a corrupt sector
would hide the corruption from it. The cross-width script's fourth step
(continue, save, restart, continue) checks the 64-bit header round trip.

Amendment, 2026-10-14: the first half of this entry is withdrawn. A bare
image is no longer read as the 32-bit layout or any other: a file without
the header is refused as an emulator or cartridge save and the converter
never runs on it (the owner's ruling 2 in "The owner's five rulings on the
independent review's open questions"). SAVE_FILE_LEGACY,
SAVE_CONVERT_BARE_IMAGE_POINTER_BITS and main's legacy branch are gone.
The two-way converter and the slot rule stand. The refusal is
SAVE_FILE_REFUSED: save_file_load prints one message and leaves the file
alone, and save_file_store also writes nothing while the refusal stands;
main stops, as it does for any file the build cannot use, because the
player named that file and a new game over it would be the next store's
overwrite in a different guise. The cross-width script's bare/ stage became
headerless/, which expects the refusal.

## 2026-10-09: At 64 bits the save blocks keep 32 positions by doubling the range, and the RFU carve uses fixed offsets

The save blocks are moved to one of 32 positions in 4-byte steps on the
GBA (SAVEBLOCK_MOVE_RANGE 128). At 64 bits an 8-byte step keeps the
pointers aligned, and keeping 32 positions means doubling the range
(256, the three aslr[] tails grow by 128 bytes each, 384 bytes of
EWRAM); the alternative, 16 positions in the same range, would change
how often the same trainer id and seed give the same position and is a
behaviour change for no gain. The RFU library carves its work buffers
from one array at hand-written offsets; at 64 bits those offsets are
written out again as fixed numbers in upstream's style, with
_Static_asserts pinning each one and RFU_API_BUFF_SIZE_RAM/ROM, rather
than computed from sizeof, so a reader sees the same kind of table at
both widths and the GBA text does not change.

## 2026-10-09: The battle script operand check parses the macros and the readers, and skips commands that have no reader

check_battle_script_operands.py expands every macro in
asm/macros/battle_script.inc and compares each operand's width with the
CMD_ARGS or NATIVE_ARGS struct member that src/battle_script_commands.c
reads it through, at both pointer sizes, instead of keeping a checked-in
table of widths: a table would have to be maintained by hand when
upstream adds a command, and the check's purpose is to catch exactly
that. Three upstream macros with no reader (tryKO, trymemento,
tryupperhand) are skipped by name; a macro that writes operands and
names no command is rejected. The relocation list gained the classes
`operands` (battle script pointer operands, 8 bytes at 64) and `table`
(hand-written pointer arrays, whose words must fill the symbol). The
struct layouts the data checks compare against come from a probe
compiled with the game's own flags during the build (data_layout_probe.c),
not from numbers written into the scripts, so a flag change moves the
reference with it.

## 2026-10-09: The 64-bit division rule is compiled at both widths

The x86-64 part of arm_division (REX.W, the 64-bit registers, the
__udivdi3 rule) is compiled into the 32-bit build too, where it cannot
run, rather than guarded out: one decoder with one set of tests at both
widths, and the 32-bit executables stay byte-identical because the code
is placed after the existing text (the reviewer and author kept the
32-bit line numbers for the sanitized build). The duplicated plumbing
that placement first required was merged in the review round (c10ba57e0a,
one decoder) before the branch merged.

## 2026-10-09: The test table check uses weak symbols and succeeds silently

crt0_init checks that the .tests section holds a whole number of entries
of the size test/test_runner.c declares (gPlatformTestEntrySize). The
size and the sentinel are weak references in crt0.c, so a build without
the test runner has no check and no dependency on test code; the check
walks the entries rather than dividing the byte count, because a
padded table can still be a multiple of the entry size. On success it
prints one line to stderr (the count and entry size) rather than nothing,
so a run's log shows the table was checked. The hydra ignores ELF64
symbols above 4 GiB: its Symbol.address is 32 bits, and every game symbol
is below 4 GiB by the memory model, so widening it would be dead code.

## 2026-10-09: Out-of-table reads that change a frame get the GBA's value, at every width

Two animation reads past a table (Doom Desire's coordinate table at
index 4, gSineTable[320] from Extrasensory and from Cos with an unmasked
angle) read a different value natively than on the GBA for one frame,
and a different value again at 64 bits. They are given the GBA's value
(0 and 0x041E, with the ROM address as evidence) under the guard at
every width, not only at 64, so the two native builds draw the same
frame as the GBA; the fix is small, has ROM evidence and is worth
reporting upstream, so SUITE-LOG's "documented, not patched" policy
does not apply. Cos is fixed only at index 320: the other out-of-range
indices (336 and on, -64) have no evidence gathered and stay as upstream
wrote them. The constants live in trig.h so the psychic step and Cos
share one definition.

## 2026-10-09: The milestone 7 refactoring pass keeps expansion's tools in their smallest form

The pass over the build glue had rewritten the milestone 7 changes to
tools/patchelf and the hydra (ELF32 or ELF64 chosen by class at every
use) into a helper layer of structs and three readers, about 75 more
lines of difference from upstream per tool for the same behaviour. The
reviewer recommended reverting it and the orchestrator agreed: expansion's
files, the tools included, take the smallest clear change
(UPSTREAM_MODIFICATIONS.md's policy), because every line of difference is
a line to carry across an upstream upgrade (UPGRADING.md), and the
per-use form was already readable. The same rule kept two other things
the pass raised: the unreachable `CRITICAL_HIT_ALWAYS` branch in
`GetCriticalHitOdds` (two lines with ROM evidence; removing it would
change the 32-bit executables for nothing) and the 174 repeated
`.ifdef NATIVE_POINTER_SIZE_8` blocks in asm/macros/battle_script.inc (a
helper macro would rewrite upstream's lines in every operand macro;
check_battle_script_operands.py guards them). The pass's eight glue
self-tests now run in the platform gate (`glue-test`); before it they ran
only as the 64-bit build's stamps, so a 32-bit-only change could not have
broken them unnoticed, but a change to a module would have failed the
next 64-bit build rather than the gate.

## 2026-10-09: CI starts on Linux only, in this project's own workflow, on pushes to main

Milestone 8 asks for CI for both targets and for Windows and macOS.
Upstream's workflows push-trigger on branches this repository does not
have (`master`, `upcoming`), so no push has run them here; they were left
as they were rather than edited. (Amended 2026-10-14: `build.yml` and
`labels.yml` also triggered on every pull request, which would have started
upstream's ROM jobs on the first one. They now filter pull requests to the
same two branches, a row in UPSTREAM_MODIFICATIONS.md; deleting them was
rejected because every upgrade would conflict on the deletion.) This project's workflow,
`.github/workflows/native.yml`, runs the gate STATUS.md records, on
`ubuntu-latest`, on pushes to `main` and by hand only: a run is about
an hour of a 4-core runner (the suite alone is four runs of 5862 tests)
and the private repository's Actions minutes are the owner's budget, so
a run per branch push would spend them on work the orchestrator's local
gates already do. Windows and macOS get no CI job before the port
exists: a `macos-latest` minute counts ten times, and a job that fails
to build teaches nothing the inventory does not. The orchestrator
proceeds with the Linux workflow without waiting (the standing orders:
nothing short of an irreversible choice waits for the owner; a workflow
file is reverted by deleting it, and the owner can disable Actions in
the repository settings at any time).

## 2026-10-09: The ports share a thin host layer; Windows is x86_64 only; macOS arm64 waits for a decision

The milestone 8 inventory (`research/m8-inventory.md`) measured that 13
of the 59 platform modules touch the OS, in six concerns. The port is
built as one `host` layer with a header per concern and a source per
OS, not as `#ifdef` blocks: three OS branches in 13 modules and their
tests would break the rule that a module's header is its only interface
and its logic is tested once. The layer is introduced on Linux first as
a pure refactor under every Linux gate, so its correctness is proven
before a second OS exists. Windows is built for x86_64 only: i686 would
rename every C symbol (the underscore prefix) through the linker scripts
and the data, for a target Windows 11 no longer ships. macOS arm64 is
not attempted until the owner decides on a rebased memory model: the
measured facts (every arm64 executable slid at load; 41,946 4-byte
absolute address words in the data; the bit-25 script tag; no divide
trap) mean the fixed-address model cannot hold there, and a rebased
model is a milestone of its own that could reasonably be declined
(x86_64 builds run on Apple silicon through Rosetta). Probes on the
runners (W1, M0) settle the loader questions before any port code is
written, because those facts cannot be measured here.

## 2026-10-09: The data for Windows and macOS is assembled on Linux and converted, not re-assembled

Expansion's data is assembler text in GNU as's dialect (macros, `.if`
over symbols, `::` labels with `.size` from preproc) that only GNU as
accepts: clang's assembler, the only one on macOS, gives 20,134 errors
on event_scripts.s alone and COFF has no 16-bit relocation for the 8
words that need one (`research/m8-inventory.md` 1.3). The inventory's
two routes were per-format rules in asm_filter.py for Windows (MinGW's
as is GNU as) plus a converter for macOS, or one converter for both.
Decision: one converter for both, `platform/build_glue/`, reading the
ELF objects the Linux 64-bit build already assembles and checks (the
three link-time checks, `elf_object.py`) and writing a flat listing
(labels, `.byte`/`.long`/`.quad` with symbol plus addend, alignment,
section switches, the target's symbol prefix) that clang and MinGW's
as accept, with the 16-bit words given their values. One path means
the data every port links is byte-for-byte the data the Linux checks
passed, and a second format costs a table of spellings, not a second
dialect. The Linux build keeps assembling directly; the converter is an
additional output, gated by a checker that compares each converted
object's section bytes and relocations with the ELF object's, for both
targets, on Linux (clang 18 here cross-assembles both).

## 2026-10-14: macOS ships as x86_64 under Rosetta; arm64 is not started; Rosetta's wind-down is a known revisit

The owner decided on the milestone 8 question STATUS.md had raised: the
macOS build is x86_64 only, run on Apple silicon through Rosetta 2, and
no arm64 build is started. The reason is the memory model: the loader
probes (`research/probes/README.md`) and the linker itself ("-no_pie
ignored for arm64") show that an arm64 executable is always slid at
load, so the fixed-address model (the GBA regions and the image below
4 GiB, the 41,946 four-byte absolute address words in the data, the
bit-25 script tag, the division trap the arm64 ISA lacks) cannot hold
there; an arm64 build needs a rebased model, a milestone of its own.
Known future revisit: Apple announced (WWDC 2025) that Rosetta 2 stays
available through macOS 27 and is then kept only for a subset of older
software, so an x86_64-only macOS build has a horizon. When that horizon
matters, the rebased memory model is the work to plan, with this entry
and the inventory's section 2.5 as the starting point; nothing else in
the port depends on the choice.

## 2026-10-14: CI runs in two tiers; the local pre-merge gate stays the full check

The owner ruled that an hour of runner time on every push to main,
docs-only pushes included, is not acceptable. The workflow is split:
a per-push tier (the platform gate, the 64-bit build, both 64-bit
replays, the plain 64-bit suite; docs-only pushes do not trigger it; a
newer push cancels an older run; the compiler cache is kept between
runs; the target is under 15 minutes warm) and a nightly tier
(`schedule` plus `workflow_dispatch`: the GBA ROM and its sha1, the
32-bit build and replays, both sanitized builds with their replays, all
four suite runs, the cross-width save gate, as parallel jobs). The
reasoning: the orchestrator's local gate before every merge is the full
check at both widths, plain and sanitized, and runs in about 20
minutes on 32 cores; CI is the backstop that proves a clean clone on a
documented image builds and passes, and a backstop that costs an hour a
push is paid for in minutes the owner budgets. The measured runtimes of
each tier are recorded in STATUS.md when the first runs complete.

## 2026-10-14: The coding-worker default is checked again: no DeepSeek model is offered, so Sonnet stays

The owner asked for DeepSeek 4.1 Flash as the default coding worker, as
the original brief did, with Sonnet for anything needing screenshots or
visual judgment and Opus for hard pieces only. The check the milestone 1
decision prescribed ("the orchestrator checks at each milestone whether
a DeepSeek option has appeared") was made on 2026-10-14: the Agent tool
of this session offers Sonnet, Opus, Haiku and Fable and no other
provider, so the brief's fallback applies as before: plain code, build
glue, tests and docs drafts go to Sonnet, hard pieces to Opus, with the
owner's escalation rule kept (a worker that fails the gate twice on the
same task is escalated to Opus or the orchestrator). The model used for
each branch stays recorded in STATUS.md's table so the split is
visible, and this check is repeated at the next milestone.

## 2026-10-14: The owner's standing rules on concurrency and public-repository quality are in C-RULES.md

The owner added two sections to docs/C-RULES.md, applying to all
current and future work: concurrency (the platform stays
single-threaded; a new thread needs an entry here with the reason a
single-threaded design cannot work and a written ownership model;
signal and exception handlers follow signal-safety(7) and share state
only through `volatile sig_atomic_t` or lock-free atomics; `volatile`
is not synchronization; threads share state through C11 atomics with
justified memory orders or an SDL mutex with a documented lock order;
the SIGALRM replacement in the test harness must not let a second
thread touch game state; a thread brings a ThreadSanitizer build into
the gate; save writes are temporary file, flush, rename; reviewers
check races, signal safety, lock order, fork/exec with threads and
file races explicitly) and code quality for a public repository
(names without the spec, comments that say why, no filler, small
cohesive modules, no speculative abstraction, everything we own left
better on every touched call chain, expansion's files untouched but
for hardware assumptions). Every coding worker receives the file
verbatim in its task and the first-pass reviewer checks each diff
against it line by line. The task W3 in flight (the test timeout) was
re-briefed under these rules before it chose a design.

## 2026-10-14: The per-push CI tier runs no suite; the plain 64-bit suite is nightly

Measured on the runner (`.github/workflows/native.yml`, runs 38563489510
cold and 38567630409 warm): the per-push tier as the owner specified it
(the platform gate, the 64-bit build, both 64-bit replays, the plain
64-bit suite) takes 31 minutes cold and 20 warm, against the 15-minute
target; the suite alone is 887 s cold and 595 s warm, and the compiler
cache barely helps because the game's C is compiled from a pipe that
ccache cannot hash (3,682 of 4,003 calls uncacheable; only the platform
library's 321 calls hit). The two parts of the specification cannot both
hold, so the suite moves to the nightly tier as its own job and the
per-push tier keeps the platform gate, the build and the replays (about
10 minutes warm). The reasoning is the one already recorded: the
orchestrator's local gate runs the suite at both widths before every
merge, so the per-push backstop loses nothing the project relies on, and
the nightly still runs every suite. Two follow-ups are recorded in
PLAN.md: compiling the game's C through a file so ccache can cache it
(the 64-bit build is 328 s warm), after which the suite could return to
the per-push tier if the owner wants it there; and caching the converted
assets. The owner can flip the suite back with one line in the workflow.
## 2026-10-14: The test time limit is kept by a flag-setting thread; the per-OS tests live in one directory per OS family

`ptest.h` kept each test's limit with `alarm(2)` and SIGALRM, which Windows
lacks and which interrupted a test at any instruction. Its replacement is
`host_thread.h`: a watchdog thread that sleeps until the deadline and then
stores `true` in one C11 `atomic_bool`. The test's own thread reads the flag in
every assertion and when a test returns, and abandons the test (`longjmp`,
as the signal handler's `siglongjmp` did) with the same report text and exit
status as before. The thread is a new thread in a single-threaded platform
(docs/C-RULES.md, "Concurrency"), so this is the justification the rule asks
for.

Why a single-threaded design cannot work: a limit has to be measured while the
test runs, and the test thread is the one thing that cannot measure it when
it is stuck. A signal is the single-threaded form, and it is POSIX only
(Windows has neither SIGALRM nor `alarm`; its timer-queue and multimedia
timers call back on a thread of their own). A signal handler that does more
than set a flag is a handler that touches test state at any instruction, which
the rule forbids as much as a thread would. So the one portable clock is a
thread; the rule's second half then holds it to doing nothing but setting a
flag.

Ownership model. The test thread owns all test and game state, the
`ptest_expired` flag's meaning, the watchdog handle, and the jump target. The
watchdog thread owns the watchdog's deadline and stop request, behind that
watchdog's mutex, and writes one thing outside it: the flag, once, with a relaxed
store (the flag publishes no data; the join in `host_watchdog_stop` orders the
rest). It takes no callback, prints nothing, allocates nothing and reads no
test state. It is created and joined once per test, so no thread outlives its
test. The one case this does not catch is a test that never reaches an
assertion after the limit (an endless loop with none in it, a blocking call
that never returns): such a test hangs its binary, and ptest.h says so; the
CI job's own time limit is the backstop. A ThreadSanitizer build of every test
binary but `test_crt0` (which reads where the linker put the game's objects, and
ThreadSanitizer moves them) is part of the gate (`make -C platform tsan`, 64-bit,
`build/64-tsan`); `test_arm_division_posix` alone runs with
`report_signal_unsafe=0`, because it leaves a signal handler by `siglongjmp`
on purpose and ThreadSanitizer then calls every later `free()` unsafe. A test
that forks has no thread alive at the fork: `psup_run_child` (posix_child.h)
stops the watchdog, joining its thread, before the fork and starts it again
after, so the child inherits no thread and no handle to one. (The
handshake in `host_watchdog_start`, which returns only when the thread waits,
stays: it keeps a start from racing a stop.) The POSIX wait is on
`CLOCK_MONOTONIC` (the condition variable's clock is set with
`pthread_condattr_setclock`), so a step of the wall clock cannot move a deadline.

Amended 2026-10-14, after the independent code review. The POSIX watchdog
thread is created with every signal blocked (`pthread_sigmask` around
`pthread_create`, the caller's mask put back after it) and keeps that mask. A
signal sent to the process goes to any thread that does not block it, so
until then a test's own handler (an `alarm(2)`'s, say) could run on the
watchdog thread, against the ownership model above.
`test/posix/test_host_thread_posix.c` sends the process a signal two hundred
times while the test thread blocks it, and requires every one to wait for the
test thread. And the thread now prints nothing on its error paths either: a
failed lock or wait is recorded in the watchdog, and `host_watchdog_stop`
reports it after the join (it printed through stdio from the thread before).
The start handshake's reason, more exactly than "it keeps a start from racing
a stop" above (a stop joins a thread that has not reached its loop just as
well): `host_watchdog_start` returns only a watchdog whose thread is waiting
on the deadline, so a thread that cannot take its lock is a reported failed
start rather than a test that silently has no limit.

The Windows source of `host_thread.h` (`host_windows_thread.c`, written with
the Windows host layer) is the same thread with Win32 calls: it waits on an
event with a timeout, stores the flag once, and is joined by a wait on its
handle. MinGW-w64 has no ThreadSanitizer; the POSIX build's is the race check
of the design, and the Windows source shares only relaxed atomics, which the
event's set announces.

The tests that only some OS can run (fork, signals, pipes, file
permissions, `setrlimit`, Linux's `pthread_getattr_np`) are not `#ifdef`s in
the portable tests: each such test file is split into the portable part, in
`platform/test/test_<name>.c`, and the per-OS part, in
`platform/test/<dir>/test_<name>_<dir>.c`, which the Makefile builds only
for a `HOST_OS` whose `HOST_TEST_DIRS` (`platform/host.mk`) names the
directory (`posix` for Linux and macOS, `linux` for Linux). What the tests
share of the OS is declared once in `test/test_support.h` and implemented
once per OS family in `test/<dir>/support.c`. A port adds a directory and a
support file; it edits no existing test.

## 2026-10-14: The Windows image links its writable sections last

`host_image_writable` is one stretch, the program's mutable data, which
`transfer_bounds` allows a transfer to write; on Linux it is `__data_start` to
`_end`. MinGW's default PE script puts `.data` before `.rdata`, `.pdata` and
`.xdata`, and `.bss` after them, so on Windows no one stretch held the data
and the bss without read-only data in between, and the portable test
`the_writable_part_holds_the_mutable_globals_only` could not pass. The
alternative was a second contract, a predicate or a list of writable spans,
which would have changed `host_image.h`, `transfer_bounds.c` and their Linux
tests for a layout detail of one linker. Instead `platform/gba_game.pe.ld`
moves every initialised data input (the game's `.gamedat`, everyone else's
`.data` into `.rwdata`, and `script_data`) after `.xdata`, defining
`__data_start__` and `__data_end__` again around what the default `.data` held
(an emptied `.data` is dropped, and its symbols would be left undefined in
the symbol table). The image is then code and read-only data, then writable
data to the end, as an ELF image is, and `check_pe_layout.py` checks that order
on every link. Nothing the loader or the C runtime uses moves: `.rdata` with
its pseudo-relocation list, `.pdata`, `.xdata`, `.idata`, `.CRT` and `.tls`
keep their names and places.

## 2026-10-14: A crash on Windows ends the process for its parent to start again

Windows has no exec. The replay's `restart` starts the program again through
`host_process_restart`, which on Windows creates a child of the same
executable with the same arguments, standard handles, environment and
working directory, waits for it, and ends the process with the child's exit
status by `TerminateProcess` (no DLL detach, no C runtime flush, as `execve`
drops unflushed buffers). Whoever waits for the process, a shell or a test
driver, sees the child's output on the same handles and its status; what
differs from Linux is that the child has a process id of its own and that
the first process stays, waiting, until it ends. The child is in a job object
that ends it when the process waiting for it ends
(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), so that a time limit that kills the
process a driver started kills the restarted one too and the driver's pipes
reach their end, as on Linux, where the restarted program is the same
process. And the child inherits the standard handles and nothing else
(`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), where `execve` keeps every descriptor
without `O_CLOEXEC`: a file the C runtime had open (its handles are
inheritable and do not share deletion) would otherwise stay open in the
child and refuse the child's own replacement of that file.

The crash reboot does not start a process. The owner ruled that no exception
handler creates a process, with no exception to docs/C-RULES.md's handler
rule: the heap may be corrupt at crash time, and `CreateProcessW` allocates
from it (Crashpad and Breakpad work out of process for the same reason). Two
designs keep the rule. (a) The parent restarts: the unhandled-exception
filter encodes the persistent bytes into a buffer prepared at install time,
writes the report, which ends with `--persistent-state <512 hex digits>`, to
stderr with `WriteFile`, and ends the process with `TerminateProcess` and
exit code 75 (`CRASH_REBOOT_EXIT_CODE`, sysexits' `EX_TEMPFAIL`, which
nothing else here exits with); the program that started the process (the
Windows test driver, task W8) starts it again on that code with the same
arguments plus the state. (b) A helper restarts: at install time the program
creates a copy of itself, suspended, in the job; the filter writes the state
through a handle opened then and resumes the helper, which becomes the
restarted run, and which needs a suspended helper of its own.

(a) is taken, as the simpler here. It leaves the filter two system calls,
neither of which allocates: `WriteFile` (`host_file_write_from_handler`, on
the handle `host_file_for_handler` resolved at install time with
`_get_osfhandle`, so that no C runtime call runs in the filter; a failed write
is left at that, with no `GetLastError` and no `errno`, which msvcrt would
allocate a block for on a thread that had not used it) and
`TerminateProcess`. The report line is built on the handler's stack. The
state it carries is encoded into the module's one static buffer, which two
threads crashing at once would both write, with the same persistent bytes:
harmless, and the same on Linux, whose handler encodes into that buffer
too. Nothing changes in how a run starts. (b) would create a second process at the start of every
run on Windows, a player's included, and keep it suspended for the whole
run; it would need a channel for the state besides the command line, held
open in both processes (against the restart's rule of inheriting the
standard handles only); and the crashed process would wait in its handler
for the helper's end to pass its status on. The cost of (a): a run on
Windows with no such parent ends at its first crash with exit code 75 and no
reboot. Only the test runner's crashing tests reboot, and on Windows only
the driver runs them. A crash with the persistent bytes as the process
started with them still ends with the exception's code, which no parent
takes for a reboot. On Linux nothing changed: the handler `execve`s the
executable, which is async-signal-safe. `host_process_restart_is_handler_safe`
tells `crash_reboot` which of the two it may do, so that it has no
conditional compilation.

## 2026-10-14: Windows has no directory sync

On Linux a replacement is durable once the directory that holds it is
fsynced. Windows has no documented call that writes a directory's entries
through: `FlushFileBuffers` is documented for a file, a volume or a pipe.
`MoveFileExW` with `MOVEFILE_WRITE_THROUGH`, which `host_file_replace` passes,
is documented to return only once the move is on the disk, and NTFS journals
the rename. So on Windows `host_file_sync_directory_of` checks the path's
length, as on Linux, and does nothing else; the save and the clock's offset
stay temporary file, flush, replace, as docs/C-RULES.md asks. The
replacement is atomic on NTFS, the file system Windows installs on; Microsoft
does not document it for every file system, and a save on a FAT32 or exFAT
volume (a USB stick, an SD card) can lose the old file in a crash.

## 2026-10-14: Narrow strings on Windows are UTF-8

The platform's paths and arguments are `char` strings, UTF-8 as on Linux. On
Windows the C runtime (msvcrt) builds `main`'s arguments from `GetCommandLineA`
and opens files by narrow paths through the system's "A" functions, both in
the process's code page, by default the ANSI code page of the system's
language. The owner ruled that out: the save path sits under the user's
profile, and a user name that code page cannot write (a Japanese name on a
Western system, an accented one on a Japanese system) would break it.

Every Windows executable, the game and each test alike, carries an
application manifest that sets the process's active code page to UTF-8
(`platform/build_glue/windows_manifest.xml`, compiled by `windres` and linked
through `platform/target.mk`; `gba_game.pe.ld` keeps the `.rsrc` section
among the read-only ones, and `check_pe_layout.py` fails a link without the
manifest). Microsoft's "Use UTF-8 code pages in Windows apps"
(learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page)
documents the setting from Windows version 1903 (the May 2019 Update) on;
that is the minimum Windows version (`BUILDING.md`), and an older one ignores
the setting and keeps the legacy code page. `host_windows.c` converts between
UTF-8 and the wide calls' UTF-16 with `CP_UTF8` named outright, as that page
recommends, not with `CP_ACP`, and refuses invalid input either way (bytes
that are not UTF-8, a lone surrogate) rather than altering it. The tests'
own support (`test/windows/support.c`) keeps its "A" calls, which the same
manifest makes UTF-8.

Not verified yet. Nothing Windows runs here, and that msvcrt, the C runtime
the Ubuntu MinGW-w64 GCC links by default, hands `main` the UTF-8 bytes of a
non-ASCII argument unchanged under the manifest follows from the
documentation (it parses `GetCommandLineA`'s string, which the code page makes
UTF-8) but is not something the build can show. The claim is verified only
once the Windows runner (tasks W8 and W9) has run
`platform/test/windows/test_utf8_windows.c` (the process's code page is UTF-8;
a non-ASCII argument passed to `CreateProcessW` in UTF-16 arrives in `argv` as
its UTF-8 bytes; a save stored through a non-ASCII directory the host layer
made loads back byte for byte under that name),
`platform/test/windows/test_host_process_windows.c` (the restart carries a
non-ASCII argument through `CP_UTF8` and back) and
`platform/test_runner/run_windows_smoke.sh` (the game run from a non-ASCII
directory with non-ASCII save and clock paths on its command line). Should
msvcrt alter the bytes, the way out is to read the arguments with
`GetCommandLineW` and `CommandLineToArgvW` and convert them with `CP_UTF8`
before `main` uses them.

## 2026-10-14: The battle runner's stack switch on Windows is hand-written assembly, not a fiber

`PlatformCallWithStack` must run the battle tests' functions on one fixed
stack (the same addresses every call, below 2^27; `game_hooks.h`), which on
Windows is the `.callstk` section at 0x01801000. Windows' own switch, a
fiber, cannot do that: `CreateFiber` and `CreateFiberEx` allocate the fiber's
stack wherever the system chooses, and a fiber's stack and context are not
documented for editing. The alternatives were a fiber whose locals would land
elsewhere (breaking the runner's 27-bit fields and its reliance on fixed
addresses) or a switch of our own. `platform/host_windows_stack.c` is the
latter, about seventy lines of GNU as in the C file: it saves the registers a
Win64 callee keeps (RBX, RBP, RDI, RSI, R12 to R15, XMM6 to XMM15, MXCSR, the
x87 control word) on the caller's stack, moves RSP to the stack's top less the
32-byte home space, calls the entry, and restores; `host_stack_leave` jumps to
the same restore. Its unwind information (`.seh_*`) names RBP as its frame
register, so the system's exception dispatch can walk from the entry's
frames back to the caller's; and while the entry runs, the thread's stack
limits in NT_TIB are widened to hold both stacks, since the dispatcher stops
at a frame outside them, and a crash on the battle runner's stack must still
reach the crash reboot's filter. Fibers' own approach, setting the limits to
the new stack alone, would cut that walk off. What it does not do: no
AddressSanitizer annotation (MinGW-w64 has no sanitizers), no shadow-stack
support (the executable is not marked CET-compatible, so none is enforced),
and no `longjmp` across it (game_hooks leaves the stack first, as on Linux).
What cannot be checked here: that the dispatch and the switch behave as
reasoned; `test_host_stack`, `test_game_hooks` and the suite's battle tests
on the Windows runner are the evidence.

## 2026-10-14: macOS waits for the owner's own Windows verification

The owner's ruling: no macOS work (tasks M2 to M5) starts until Windows is
fully done (the executable, the unit tests, the replays and the suite on
the Windows runner) and the owner has verified it by hand on a Windows
machine. The macOS decision of earlier today (x86_64 under Rosetta, arm64
not started) stands; only its start moves.

Lifted 2026-10-14 (evening): the owner verified Windows by hand (see "The
owner's rulings from the Windows hands-on check"). macOS may start; the
order is the audio-rate task, the two tasks from the hands-on check, then
macOS.

## 2026-10-14: The owner's five rulings on the independent review's open questions

(1) The audio rate. The PSG decision of milestone 3 said never to
point-sample the PSG at m4a's 13379 Hz, and that is the rate the build
runs at (`FRAME_AUDIO_SAMPLE_RATE_HZ`, so the FIFO frame mixes sample for
sample): the corrected reasoning is that at 13379 Hz the square and noise
channels alias into the audible band (psg.c, "RESAMPLING"). The owner
rules: run the device at the GBA's 32768 Hz with the FIFO frame
upsampled, as its own task after the owner has verified Windows by hand,
with an A/B recording against the current path as its evidence.

(2) Headerless save files. A file without this project's header was
loaded as a bare 32-bit native image and could be handed to the
converter. The game's sector checksum is a sum over raw bytes, so no
checksum or signature can tell a GBA-layout image from a native one: a
probe is impossible, and the owner rules the legacy path out. A file
without the header is refused with a message that it looks like an
emulator or cartridge save, that importing those is not supported yet,
and that the file is left untouched; the converter never runs on a
headerless image. The only headerless native saves ever written were
this project's own early test saves, which have no users. Done now, as
a data-integrity bug, with tests for an emulator save, a truncated file
and a valid native save at each width.

(3) Two instances on one save. The owner does not accept "unsupported":
with a fixed temporary name, two instances saving at once can tear the
save, and launching the game twice is an easy way to do it. Because the
save is atomically replaced, a lock on the save's own inode is lost at
the first save, so the lock is an exclusive OS lock (flock on POSIX,
LockFileEx on Windows) on a companion file beside the save,
`<save>.lock`, created once and never deleted: its presence means
nothing, only the held lock does, and the OS releases it on any exit
including a crash. A second instance refuses to start with a message
naming the save; test-runner mode, which writes no save, is unaffected.
Tests: a second instance is refused, the lock survives a save, and it is
released after the process is killed. Done as its own task after the
Windows-runner branch merges (it owns host_file.h and host_windows.c).

(4) `src/siirtc.c`. Filtered out of the native build, the SiiRtc API
provided from the platform layer, its guards removed and
UPSTREAM_MODIFICATIONS.md updated: its own task after the Windows-runner
branch merges (it owns Makefile.native).

(5) The project logs. Not a `docs/log/` directory: STATUS.md, PLAN.md,
the milestone inventory, the build-glue logs and the process-only
DECISIONS entries move to a gitignored folder the orchestration keeps
using, and nothing public mentions orchestrators, workers or models;
after milestone 8 and before the repository goes public. Noted for the
owner: earlier commits still contain these files, so a public history
would need rewriting or a fresh start.

macOS stays on hold until the owner has verified Windows by hand.

## 2026-10-14: The suite on Windows runs under MSYS2

Gate (b) on Windows is expansion's own suite, through expansion's own hydra
(`tools/mgba-rom-test-hydra`), on the Windows test runner build, with the
per-test results equal to the Linux 64-bit build's of the same commit. The
inventory offered three routes (`research/m8-inventory.md` 5.2, W8): (i) the
hydra built under MSYS2 on the Windows runner, (ii) the Linux hydra driving
the Windows executable through Wine on a Linux runner, (iii) a Windows-native
driver of our own. (i) is taken.

Why (i). The hydra is a small POSIX program: fork, pipe, poll, waitpid,
regex, and `stdbuf` for its runners. The `windows-latest` image has MSYS2 at
`C:\msys64`, whose runtime provides all of them; measured on the runner
(run 38601458286): `pacman -S --noconfirm --needed gcc make` takes 24 s, MSYS2's
coreutils has `stdbuf` (with its `libstdbuf.dll`), and both the hydra and
patchelf build with MSYS2's gcc once the hydra includes `<sys/prctl.h>` on
Linux only (an `UPSTREAM_MODIFICATIONS.md` row). Upstream's "MSYS2 builds do
not work" is about building the game, which this route does not do: the game
is cross-built on Linux. (ii) would run the suite under Wine, which is not
Windows: the loader that maps the image, the exception dispatch that walks
the battle runner's stack switch to the crash filter, and msvcrt are exactly
what the Windows gate is for (the Windows host layer's DECISIONS entries list
them as unverifiable anywhere but Windows), and it adds a Wine install to a
Linux job. (iii) would re-implement the hydra's protocol parsing and summary,
a second copy of upstream's logic to keep in step with it, for no gain once
(i) works.

What the route needs from the platform side: patchelf patches a PE file
through its COFF symbol table (a `tools/` row); `native-rom-test`, which the
hydra starts in mgba-rom-test's place, copies the hydra's copy to a name ending
in `.exe` (Windows runs nothing else) and is the supervising parent the crash
reboot needs on Windows ("A crash on Windows ends the process for its parent
to start again"): on exit code 75 it starts the runner again, on the same
standard output, with the state from the last `crash_reboot:` line of its
standard error, so the hydra sees one run go on past a crash as it does on
Linux; `platform/test_runner/run_windows_suite.sh` does what `make check` does
(the headless copy patched, the hydra started from the repository's root,
the executable's directory on `PATH` so that the hydra's copies in the
temporary directory find `SDL2.dll`). The comparison,
`platform/test_runner/compare_suite_results.sh`, drops the runner numbers and
Windows' `\r` and requires the two sorted lists of "test name: result" lines
to be equal. The Windows workflow runs the Linux suite of the same commit in a
parallel job for it, rather than comparing with a committed list that would
have to follow every change to the tests. Two things MSYS2 needed besides: the
test runner's `setvbuf` asks for a buffer size msvcrt accepts (it refuses 0,
and every runner ended at its start), and the hydra takes a read of nothing
for a runner's end, since MSYS2's `poll` reports a pipe whose writer has gone
as readable and never as `POLLHUP` (an `UPSTREAM_MODIFICATIONS.md` row);
without it the hydra spun after the first runner ended. Result (run
38629824732, the merged branch): the 5875 results on Windows equal Linux's
line for line (5418 passed, 2 expected crashes, 7 expected failures, 18 known
failing, 9 failed assumptions, 421 to do), in 5 minutes with two runners on
the 2-vCPU runner.

## 2026-10-14: The Windows unit tests run from copies removed after them

Each executable of the Windows layout costs about 1 GiB of the system's
commit charge from its first start until its file is deleted, whether or not
it is running. Measured on the runner (run 38601458286, a 2-vCPU image with
8 GiB of memory and a 10,107 MiB commit limit): starting a test executable
and letting it exit raised the committed bytes by 1,000 MiB, starting it
again by nothing, starting a copy by another 1,000 MiB, and deleting the copy
gave the 1,000 MiB back. The image spans 0x01800000 to 0x41xxxxxx, and the
system charges the image's pages, the 1 GiB of read-only gaps included, to the
image's section object, which it keeps cached while the file exists; probe W1
measured the process's own commit charge (3 MiB), which does not include it.
With the 69 test executables of that run (70 since) the runner's commit ran
out after the 24th had started (the limit grows with the system-managed page
file, but not without end), and every later start failed ("Resource
temporarily unavailable", run 38598389601).
`run_windows_tests.sh` therefore runs each test from a copy under its own
name in a directory of its own and deletes it when the test ends; the suite's
runners already run from copies the hydra and `native-rom-test` delete.

What it means beyond the tests: the game too costs 1 GiB of commit while it is
installed and has been run once, on top of what it uses. That is a property of
the memory model on Windows (one image holding the GBA regions at their
addresses, the code at 0x40001000; DECISIONS.md, "The ports share a thin host
layer"), recorded here for the owner; nothing in this task changes the model.

## 2026-10-14: Amendment to ruling (3): the save's lock across a restart and a crash

Ruling (3) as built (`platform/save_lock.h`), and the choice it left open.

The lock is `host_file.h`'s: `flock(LOCK_EX | LOCK_NB)` on POSIX,
`LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY)` over every
byte on Windows, on `<save>.lock` opened through `host_file_open` (`O_RDWR |
O_CREAT`, 0644, not inherited). flock, not fcntl's record locks, which belong
to the process and drop when it closes any descriptor of the file; flock's
belongs to the open file. `main` takes it after parsing the options, before
any module starts, so a second instance opens no window and reads or writes
neither the save nor the clock's file, and releases it after every module has
stopped, so after the last store. Test-runner mode has no save path and takes
no lock. A refused start prints one line and exits with status 1:

    save_lock: another instance of the game holds the save <save> (the lock on <save>.lock); not starting, so that the two cannot write over each other's saves

Any other failure is reported by call (`save_lock: open(<save>.lock): ...`,
`flock(...)`) and stops the start too.

The restart: released and taken again, on both systems, never handed on.
`main` releases the lock after the modules stop and before `restart_exec`; the
new run takes it in its own `main`, as any start does.
- Windows: the new run is a child the restarting process waits for
  (`host_process_restart`). A lock its parent held would refuse it, since
  the child inherits no handle but the standard ones and a `LockFileEx` lock
  is the handle's that took it. So the parent releases first, with an
  explicit `UnlockFileEx`: of a closed handle's locks Microsoft writes that
  "the time it takes for the operating system to unlock these locks depends
  upon available system resources" (`LockFileEx`).
- Linux: a descriptor without `O_CLOEXEC` would survive the exec, and the new
  image would hold the lock through a descriptor it does not know, and refuse
  itself at its own take (a new open). `host_file_open`'s `O_CLOEXEC` closes
  it at the exec, so even an exec with the lock still held leaves it free for
  the new image; `main` releases it first anyway, in the same order as on
  Windows.

So the two runs never both hold it (the old one has dropped it before the new
one exists: before `CreateProcessW`, before `execve`), and the new run always
finds it free unless another instance took it in the gap between the release
and the take, in which case the restarted run refuses with the line above and
exits with status 1: still one game on the save, which is the rule.

The crash reboot runs in test-runner mode only, which has no save path, so no
lock is held when a crash reboots (Linux: the handler's `execve`; Windows: the
parent's new start after exit code 75). Were one held, the exec would close it
(`O_CLOEXEC`) and the crashed Windows process's end would drop it before the
parent starts the next run. A crash in a normal run ends the process, and the
system drops the lock with it.

Tests: `platform/test/posix/test_save_lock_posix.c` (a) a forked child holds
the lock and the parent is refused with the line, (b) the lock outlives two
stores, which replace the save, a child still refused, (c) a child killed with
`SIGKILL` while holding it leaves it free, and the restart: a child takes it,
releases it (or keeps it) and restarts through `host_process_restart`, and the
restarted run takes it. `test/windows/test_save_lock_windows.c` has the twins
(Windows has no fork: there this process holds and a child is refused, and the
holder ended at once is a child ending itself with `TerminateProcess`).
`tests/smoke/run_cross_width.sh`, step `busy/`: TO is refused, with that one
line and before any module starts (its own clock file, which a start creates,
is not created, nor any temporary file), on a save FROM is playing, and starts
once FROM is killed; the replays'
`restart` with a save path is `run.sh`'s and `again/`'s.

Not covered by the lock: the clock's offset file, also written through a fixed
temporary name; two instances on different saves that share one offset file
are not kept apart (`rtc_offset_store.h`). And a save reached by two names, a
symbolic or a hard link (`a/link.sav` to `a/real.sav`): the lock file is the
path as given plus `.lock`, so the two names have two lock files and two
instances on them both start. A known limit, written in `save_lock.h`:
resolving the path to its file (`realpath`, `GetFinalPathNameByHandleW`, and
for a hard link not even those) is not worth a host call for a link someone
made on purpose.

Not testable: that `main` releases the lock after the last store. The release
is the last thing before the process ends, so only an instance started during
that final store could see the order; `main.c` and the POSIX test's header say
so.

## 2026-10-14: The owner's rulings from the Windows hands-on check

The owner ran the Windows build from run 38648131073 by hand (boot, the
opening, the first Pokémon, the first battle; audio and saving fine) and
ruled on two things seen while doing it. Both are tasks after the
audio-rate task (ruling 1 of the independent review), before macOS.

**A settings file for the keys.** The keyboard map is a fixed table
(`platform/input.c`: A=Z, B=X, Start=Return, Select=Backspace, L=A, R=S,
the arrows; VisualBoyAdvance's defaults). The owner: a settings file
beside the executable rebinds the keys. Design when the task starts; the
owner also liked an on-screen help overlay (a key such as F1 showing the
map, the save path and the version) and a `--keys` option printing the
map; a native menu bar was declined for now, because SDL2 has none and it
would be three implementations (Win32, GTK, Cocoa).

**No console window on Windows.** The executable is linked as a console
program (`-mconsole`, `platform/target.mk`) because it prints: the test
runner's results for the hydra, the replays' output, the refusals. Started
from Explorer, Windows opens a console beside the game window. The owner:
"people wont like that"; remove it. The design to try: link as a GUI
program (`-mwindows`); attach to the parent's console when there is one
(`AttachConsole(ATTACH_PARENT_PROCESS)` at startup, so a command prompt
still sees the messages); otherwise write the messages to a log file
beside the executable; pipes (the hydra, the replays' harness) are
inherited handles and keep working. Verify each of those three on the
runner.

Also confirmed for the owner: the translucent oval under every overworld
character is expansion's own default (`OW_OBJECT_VANILLA_SHADOWS FALSE`,
`OW_SHADOW_INTENSITY 4` of 16 in `include/config/overworld.h`); the Linux
replay screenshot shows the same.

## 2026-10-14: Two adjustments from the owner to the tasks from the Windows check

Quit and the file locations, ruled by the owner after the hands-on check;
they refine "The owner's rulings from the Windows hands-on check" above.

**Quit lives in the platform's F1 overlay, not in the game's Start menu.**
Adding it to the Start menu would be another patch to expansion's code,
against the upstream-modification policy (UPSTREAM_MODIFICATIONS.md). The
Start menu's EXIT stays as in the original game: it closes the menu.

**Default file locations move to the per-user directory.** Today the save
defaults to the bare relative path `pokeemerald.sav` (`OPTIONS_DEFAULT_SAVE_PATH`),
so it depends on the working directory: a shortcut or a different launch
folder silently starts a fresh game; and a folder beside the executable may
not be writable (Program Files; a 64-bit process gets no write
virtualization). The owner rules: the save, the RTC offset file and the new
settings file default to the per-user directory SDL_GetPrefPath gives; the
executable's directory is used instead only when a settings file already
exists there (portable mode); explicit `--save` and `--rtc-offset` paths
still win. The F1 overlay and the `--keys` output show the active paths.
Both modes are documented in BUILDING.md or a short docs/RUNNING.md. For the
owner: the saves from the builds of 2026-10-14 live in the launch directory
and will need moving once, by hand, to the per-user directory.

## 2026-10-14: Two standing orders from the owner: documentation as a gate, and a release pipeline without a public release

**Documentation is gated like code.** After each task lands, README and
every public document must reflect the current state: Windows supported and
hand-verified, how to run on each OS, the settings file and portable mode,
the save and settings locations, the F1 overlay and `--keys`, the Quit path,
and macOS once it exists; anything stale is removed (README's "Windows ...
no replay or suite run has happened there yet" and "macOS: not attempted"
are the first). A short docs/RUNNING.md is for players, as opposed to
BUILDING.md for builders. A branch is not mergeable with stale public
documents, and the reviewer checks them (PLAN.md "Worker protocol").

**A release pipeline, not a public release.** A `workflow_dispatch` release
workflow builds the Linux 64-bit tarball and the Windows 64-bit zip (the
executable, SDL2.dll, the licence texts, a short README) with ccache,
generates SHA-256 sums, attaches GitHub artifact attestations
(actions/attest-build-provenance) so anyone can verify the files came from
CI and a named commit, and publishes to a GitHub release only when run with
an explicit `publish` input; without it the artifacts stay on the workflow
run for the owner's own testing. No public release and no tag is created by
anyone but the owner. Built and dispatched once on 2026-10-15 (run
38660260570, publish off): the archives and sums are right; the attestation
step fails on a user-owned private repository ("Feature not available ...
please make this repository public") and needs no change once it is public.

**Why no public release.** A built executable contains the complete game's
assets, so a public release is legally the same as distributing a ROM,
which README says this project does not do and which every decomp project
avoids. The default plan is source-only distribution with a one-command
build per OS (BUILDING.md, with a verified script); a public release
happens only on the owner's explicit instruction after that note (STATUS.md
carries it).

**Code signing is not done.** It costs money and is needed only for a public
Windows release (SmartScreen's "Windows protected your PC" on an unsigned,
reputation-less executable). **VirusTotal** scan links would be added to
release notes only if a public release ever happens, with the known caveat
that MinGW-built executables draw false positives from some engines.

## 2026-10-14: The settings file, the data directory and the F1 overlay

The owner's rulings from the Windows hands-on check (a settings file for the
keys, an overlay with the map, the save path and the version, `--keys`) with
the two adjustments (the files in the per-user directory with a portable
mode; Quit in the platform's overlay), as built. The player's account is
`docs/RUNNING.md`; the modules are `platform/data_dir.h`, `settings.h`,
`help_text.h`, `overlay.h`, `font.h` and `version.h`.

**The names.** `SDL_GetPrefPath("pokeemerald-expansion-native",
"pokeemerald-native")`: `~/.local/share/pokeemerald-expansion-native/pokeemerald-native/`
on Linux (or under `$XDG_DATA_HOME`), `%APPDATA%\pokeemerald-expansion-native\pokeemerald-native\`
on Windows, `~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/`
on macOS. The application is the executable's name, as ruled. SDL asks for an
organisation too, and its header documents no empty one (the behaviour of
`""` on Windows and macOS is not written anywhere SDL 2 promises), so it gets
a name: the project's own, which claims nothing on behalf of pret or
rh-hideout, as a name of theirs would. The files keep their names,
`pokeemerald.sav` (with `pokeemerald.sav.lock`) and `pokeemerald.rtc`, and
the new one is `settings.ini`: plain text that every system opens in an
editor (Notepad, by default, on Windows), which is what a file the player
writes by hand needs.

**The rule** (`data_dir_resolve`): `--data-dir DIR`, made if missing with
`host_file_make_directory` (its parent must exist); else the executable's
directory (`SDL_GetBasePath`) if `settings.ini` is there, "there" meaning it
opens or exists without read permission (`EACCES`, then reported when read);
else the per-user directory. `--save` and `--rtc-offset` win for their file,
as given (relative to the working directory). The directory is looked for
only when a file needs it, and in that order, because `SDL_GetPrefPath`
creates the directory: portable mode and `--data-dir` never call it, and a
headless run with both paths given looks for nothing. A headless run reads no
settings file, since it has no keyboard and no window for one to set; that
is also what keeps every gate off the per-user directory without a new
argument. Test-runner mode has no files, as before. No per-user directory
(SDL fails) stops the start with a message naming `--data-dir`; an unknown
executable directory is reported and portable mode skipped. The rule is pure
but for SDL's two answers, which the unit tests replace with a host of their
own (counting that the per-user one is not asked for when unused); SDL's own
answers are checked on Linux through `XDG_DATA_HOME` and `/proc/self/exe`.

**The settings file** (`settings_parse`, pure): `setting = value` lines, `#`
comments on lines of their own (SDL names a key `#`), blank lines; the ten
buttons by their names (`a`, `b`, `select`, `start`, `right`, `left`, `up`,
`down`, `r`, `l`) with SDL's key names (`SDL_GetScancodeFromName`, through
`input.c`, which owns the keyboard), and `scale`, 1 to 16, which `--scale`
overrides. Names and keys in any case, blanks around them ignored, CRLF and a
UTF-8 byte order mark accepted. Two lines for one setting: the later wins,
as in every file of this kind, so a line added at the end takes effect. Keys
are scancodes, physical positions, as the defaults always were. F1 is refused
as a button's key (it would open the overlay and close the keypad). A bad
line is reported on stderr with its number and what is wrong and ignored; the
rest applies. A file that cannot be read, or one over 16 KiB, stops the start
with a message: it is the player's own, written to be read, and running
without it would leave them guessing. The game never writes it. `input.c`'s
fixed table became the defaults the file overrides; the translators take the
keymap and stay pure.

**The overlay.** F1 (the scancode, the same key on every layout) shows and
hides it; Q, while it is shown, quits (`SDLK_q`, the key that types q, so
that the hint `Q: quit  F1: close` names the key on the keyboard). It shows
`--keys`'s lines (`help_text_write`, one function for both): the version, the
keys in force, the controller line, the data directory and its mode, and the
three paths. Quit is the window-close path: the frame loop ends, `main`
stores the save only if the game wrote one and it is pending, as at any exit,
stops the modules and releases the lock; the platform writes nothing of its
own. The game's Start menu is not touched. While the overlay is shown the
game keeps running, as ruled, and KEYINPUT reads every button released,
from the keyboard, a controller or a script alike: the keys are the overlay's
then, so F1, Q (which may be a button's key) or anything pressed while
reading never reaches the game, and nothing is half-pressed when it closes.
It exists only for a window played from the keyboard; `main` does not start
it headless, in test-runner mode or for a replay, so no gate or replay can
open it, and it is drawn by `video.c` as a second texture over the frame in
the window, never into the frame buffer the screenshots come from.

**The text.** A bitmap font of our own, not SDL_ttf: SDL2 draws no text, and
SDL_ttf would be a second library, plus a font file with its own licence to
ship, for one screen of ASCII; the game's font is tiles in the game's own
encoding, not ASCII. The glyphs (`font_glyphs.h`) were drawn for this project
on a 5x8 grid, 5x7 capitals with a descender row, in 6x10 cells (a column and
two rows of space); no font was copied or followed. The overlay is an ARGB
texture of the window's initial size (240 and 160 times the scale), black at
7/8 opacity under white text, drawn once at start-up; the glyphs are drawn
at half the window's scale (at least 1), so that a line holds some 80 to 120
characters at every scale from 2; a longer line goes on in the next, and
lines past the bottom are left out. Characters outside ASCII (a path with an
accent) show as one `?` each. Tested on buffers: the glyphs against
pictures written in the tests, the drawing pixel by pixel at scales 1 and 2,
clipping, the layout at scales 1, 3 and 4, the wrapping and the cut.

**The version.** `git describe --always --dirty --match 'v[0-9]*'`, from
`Makefile.native`, kept to the characters a tag or a hash has; `unknown`
without git. The match is needed because the history carries expansion's
tags (`expansion/1.10.0`, `1.11.0`), after which plain `git describe` would
name the build (`expansion/1.10.0-5060-g...`, read as expansion 1.10.0); it
takes a release tag of this project's own if the owner makes one named
`v...`, else the abbreviated commit. It is defined for `version.c` alone and
rebuilt only when it changes (`platform/Makefile`, `version.txt`), so that
ccache still serves every other object after a commit.

**What the gates pass.** `tests/smoke/lib.sh` (both replays, `run_cross_os.sh`)
and `save_steps.sh` and `run_cross_width.sh` already pass `--save` and
`--rtc-offset` headless, so they look for no data directory and read no
settings: the replays and the cross-width script were run with
`XDG_DATA_HOME` pointing at a scratch path, which stayed absent, and their
outputs are unchanged. `native-rom-test` runs `--test-runner`, which has no
files. `run_windows_smoke.sh` now passes `--data-dir` (relative, and a
non-ASCII one) or both paths, checks the files where they were told to go,
and checks `--keys` with `--data-dir`, with a settings file (its key, and its
bad line's number on stderr), `--version`, and portable mode (a copy of the
executable beside a `settings.ini`, the printed directory checked by a marker
file in it, since Windows may print short names); it runs against the Linux
executable as well. BUILDING.md's smoke boot passes `--data-dir out`. No
gate writes into the per-user directory of the machine running it.

**What a restart carries.** `restart.c` keeps every argument but
`--resume-at` and `--audio-dump`, so `--data-dir`, `--save` and
`--rtc-offset` reach the restarted run (`test_restart.c`), which finds the
same files; portable mode follows from the settings file beside the
executable, and the restart starts the same executable (`/proc/self/exe`;
`GetModuleFileNameW`) in the same working directory. The crash reboot runs
in test-runner mode only, which has no data directory; it keeps every
argument but `--persistent-state` in any case.

Not verified here: the overlay on a screen (no display; its picture is
unit-tested); SDL's per-user directory on Windows, which a gate may not
create; and the portable smoke's run on Windows, which the Windows runner
does next.
