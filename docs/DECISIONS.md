# Decisions

Each entry records a decision that was not obvious, the alternatives, and
why. Newest at the bottom. Dates are when the decision was made.

## 2026-10-08: Fork point and remote

Clone of rh-hideout/pokeemerald-expansion at tag `expansion/1.17.1`,
branch `main` from it, upstream kept as remote `rhh`. No `origin` exists
yet; add it when the repository is published. Never force-push.

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
(`docs/research/memory-map-experiment.md`). Cost: a native linker script,
`MAP_FIXED_NOREPLACE` on Linux, and later an equivalent on Windows and
macOS (both have fixed-address reservation; macOS needs the page-zero
size reduced for the low addresses). Flagged for the 64-bit work:
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
the rule that tests precede adaptation. The ports are consulted
for structure and pitfalls.

## 2026-10-08: Frame rate is the GBA's

Frames are paced at 280896 cycles of 16.777216 MHz, 59.7275 Hz, and audio
at the engine's native sample rate, not the ports' 60 Hz and 42060 Hz.
Reason: the test suite, the replay and the RTC all count emulated time;
matching the hardware removes a class of divergence from the GBA
reference. Vsync is not used for pacing.

Amendment 2026-10-14: audio no longer runs at the engine's rate but at the
GBA's 32768 Hz PWM grid, the engine's frame read on it (decision (1) in
"Decisions on an independent review's open questions"; the amendment to "PSG trigger, wave-bank and output-scale rules").

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
`docs/research/memory-map-experiment.md`. Both were tried with 32-bit and 64-bit
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
`.sav` files is not supported until a
conversion layer exists (README.md lists it). Consequence for the platform layer: `CpuSet`,
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

Required after the host-ABI decision. The native
save file carries a small header (magic, format version, a layout tag
naming the pointer width and struct-layout identifier the image was
written with, image size, CRC32) ahead of the 128 KiB flash image. A
bare 131072-byte file is accepted as legacy with an unknown layout and
is rewritten with a header on the next store. Gate (h), run with the 64-bit work: a save written by the 32-bit native build loads correctly in the
64-bit build, which will need a layout conversion keyed on the tag. The
header makes any future layout change detectable instead of silent.

Amendment, 2026-10-14: the bare-file clause no longer holds. A file without
the header is refused, never accepted as legacy and never rewritten (decision 2 in
"Decisions on an independent review's open questions").

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

Amendment 2026-10-14: until decision (1) of the same day ("Decisions on an independent review's open questions") the
shipped build ran the PSG at m4a's 13379 Hz (`FRAME_AUDIO_SAMPLE_RATE_HZ`),
so that the 224-sample FIFO frame mixed sample for sample with the PSG
output. The corrected reasoning: at 13379 Hz the square and noise channels
alias into the audible band. A square wave's odd harmonics fall off only as
1/k and the noise channel clocks at up to 524288 Hz, so every component
above 6689.5 Hz folds back below it as a tone the GBA never plays.
What ships now, by that decision: the device and the PSG run at 32768 Hz, the
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
path against this one, are that decision's evidence; they hold the game's music
and are kept outside the repository. Not followed: Emerald itself selects
the next SOUNDBIAS mode, 8 bits at 65536 Hz (`src/m4a.c`: m4aSoundInit asks
for SOUND_MODE_DA_BIT_8 and SoundInit writes SOUNDBIAS_H bits 6-7 = 1), so
the console this game runs on samples every 256 cycles; the grid is one
constant (`SOUND_GRID_RATE_HZ`), and moving it to 65536 Hz is a later decision
("Fast-forward focus loss, the 65536 Hz audio grid, the macOS signature and the arm64 rewrite").

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
the pointer-dependent ones flagged for the 64-bit work. Where the
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
GBA the tagged address is a valid ROM mirror. The test-runner work
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
redone (for 64-bit). Rejected: guarding every division site in
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
reads past the object and the sanitized gate stops. The early platform work
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
address alone) is in the review of that change, summarised in the row.

## 2026-10-09: The 64-bit work starts from a measured inventory, not from the flags

The 64-bit build was measured before any code was written. What the
measurement found that a read of the "flagged 64-bit"
comments would not have: the link fails on exactly two static assertions,
one of which (RecordedBattleSave) has no pointer in it at all (a u64 that
x86-64 aligns to 8); the 8-byte INTR_VECTOR store runs off the end of
IWRAM; the assembled data holds 61,154 address words of which 17,033 must
widen and 44,121 must not (bytecode) (the count of the first measurement; after the
battle scripts were found to be read through packed structs with pointer
members it is 19,208 widened and 41,946 left); GCC's x86-64 data alignment spaces
the test runner's `.tests` entries 64 bytes apart; tools/patchelf is
ELF32-only; and in the save only SaveBlock1 changes layout (two members).
Each step of the 64-bit work is one section of that design, gated on the 32-bit
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
main cannot be compared with a build in another worktree; the gate is
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
never runs on it (decision 2 in "Decisions on an independent review's open questions"). SAVE_FILE_LEGACY,
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
is placed after the existing text (the 32-bit line numbers for the sanitized build were kept). The duplicated plumbing
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

## 2026-10-09: The 64-bit refactoring pass keeps expansion's tools in their smallest form

The refactoring pass over the build glue, before the 64-bit work was declared done, had rewritten the 64-bit changes to
tools/patchelf and the hydra (ELF32 or ELF64 chosen by class at every
use) into a helper layer of structs and three readers, about 75 more
lines of difference from upstream per tool for the same behaviour. The
review recommended reverting it, and it was reverted: expansion's
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

The port needs CI for both targets and for Windows and macOS.
Upstream's workflows push-trigger on branches this repository does not
have (`master`, `upcoming`), so no push has run them here; they were left
as they were rather than edited. (Amended 2026-10-14: `build.yml` and
`labels.yml` also triggered on every pull request, which would have started
upstream's ROM jobs on the first one. They now filter pull requests to the
same two branches, a row in UPSTREAM_MODIFICATIONS.md; deleting them was
rejected because every upgrade would conflict on the deletion.) This project's workflow,
`.github/workflows/native.yml`, runs the gate (BUILDING.md lists it), on
`ubuntu-latest`, on pushes to `main` and by hand only: a run is about
an hour of a 4-core runner (the suite alone is four runs of 5862 tests)
and the repository's Actions minutes are a budget, so
a run per branch push would spend them on work the local
gates already do. Windows and macOS get no CI job before the port
exists: a `macos-latest` minute counts ten times, and a job that fails
to build teaches nothing that reading the build does not. The Linux workflow
goes ahead at once: a workflow file is reverted by deleting it, and
Actions can be disabled in the repository settings at any time.

## 2026-10-09: The ports share a thin host layer; Windows is x86_64 only; macOS arm64 waits for a decision

An inventory of the Windows and macOS ports measured that 13
of the 59 platform modules touch the OS, in six concerns. The port is
built as one `host` layer with a header per concern and a source per
OS, not as `#ifdef` blocks: three OS branches in 13 modules and their
tests would break the rule that a module's header is its only interface
and its logic is tested once. The layer is introduced on Linux first as
a pure refactor under every Linux gate, so its correctness is proven
before a second OS exists. Windows is built for x86_64 only: i686 would
rename every C symbol (the underscore prefix) through the linker scripts
and the data, for a target Windows 11 no longer ships. macOS arm64 is
not attempted until a rebased memory model is decided on: the
measured facts (every arm64 executable slid at load; 41,946 4-byte
absolute address words in the data; the bit-25 script tag; no divide
trap) mean the fixed-address model cannot hold there, and a rebased
model is a project of its own that could reasonably be declined
(x86_64 builds run on Apple silicon through Rosetta). Loader probes (W1 on a Windows runner, M0 on a macOS runner) were run once, before any port code was written, because those facts cannot be measured on a Linux build machine; their results are recorded in this file where they matter.

## 2026-10-09: The data for Windows and macOS is assembled on Linux and converted, not re-assembled

Expansion's data is assembler text in GNU as's dialect (macros, `.if`
over symbols, `::` labels with `.size` from preproc) that only GNU as
accepts: clang's assembler, the only one on macOS, gives 20,134 errors
on event_scripts.s alone and COFF has no 16-bit relocation for the 8
words that need one. Two routes were considered: per-format rules in asm_filter.py for Windows (MinGW's
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

Decision on the open macOS question: the
macOS build is x86_64 only, run on Apple silicon through Rosetta 2, and
no arm64 build is started. The reason is the memory model: the loader
probes (run on a macOS runner) and the linker itself ("-no_pie
ignored for arm64") show that an arm64 executable is always slid at
load, so the fixed-address model (the GBA regions and the image below
4 GiB, the 41,946 four-byte absolute address words in the data, the
bit-25 script tag, the division trap the arm64 ISA lacks) cannot hold
there; an arm64 build needs a rebased model, a project of its own.
Known future revisit: Apple announced (WWDC 2025) that Rosetta 2 stays
available through macOS 27 and is then kept only for a subset of older
software, so an x86_64-only macOS build has a horizon. When that horizon
matters, the rebased memory model is the work to plan, with this entry
and "Native Apple silicon: the spike's findings" as the starting point; nothing else in
the port depends on the choice.

Not yet shown (2026-10-15, the final review of the port): the x86_64 game on
an Apple-silicon Mac under Rosetta 2. Probe M0 showed the layout there (61 of
61 checks, 100 of 100 starts); the game itself has not run there. CI does
not check it (only `macos-15-intel` runs this port's jobs: "macOS CI runs on
macos-15-intel only"); it is on the owner's hand-check list on a Mac ("The
macOS port waits for its first runs on a Mac", run 3): Rosetta 2, an x86_64
Homebrew in `/usr/local`, the unit tests built and run as x86_64,
`tools/build-native.sh --os macos` under `arch -x86_64`, and the opening
replay, including its divisions by zero (`FPE_INTDIV` with the DIV at the
program counter, which `test_arm_division*` and `test_host_fault*` check).

## 2026-10-14: CI runs in two tiers; the local pre-merge gate stays the full check

An hour of runner time on every push to main,
docs-only pushes included, is not acceptable. The workflow is split:
a per-push tier (the platform gate, the 64-bit build, both 64-bit
replays, the plain 64-bit suite; docs-only pushes do not trigger it; a
newer push cancels an older run; the compiler cache is kept between
runs; the target is under 15 minutes warm) and a nightly tier
(`schedule` plus `workflow_dispatch`: the GBA ROM and its sha1, the
32-bit build and replays, both sanitized builds with their replays, all
four suite runs, the cross-width save gate, as parallel jobs). The
reasoning: the local gate before every merge is the full
check at both widths, plain and sanitized, and runs in about 20
minutes on 32 cores; CI is the backstop that proves a clean clone on a
documented image builds and passes, and a backstop that costs an hour a
push is paid for in runner minutes, which are budgeted. The measured
runtimes of each tier are in the entry below.

## 2026-10-14: The per-push CI tier runs no suite; the plain 64-bit suite is nightly

Measured on the runner (`.github/workflows/native.yml`, runs 38563489510
cold and 38567630409 warm): the per-push tier as first specified
(the platform gate, the 64-bit build, both 64-bit replays, the plain
64-bit suite) takes 31 minutes cold and 20 warm, against the 15-minute
target; the suite alone is 887 s cold and 595 s warm, and the compiler
cache barely helps because the game's C is compiled from a pipe that
ccache cannot hash (3,682 of 4,003 calls uncacheable; only the platform
library's 321 calls hit). The two parts of the specification cannot both
hold, so the suite moves to the nightly tier as its own job and the
per-push tier keeps the platform gate, the build and the replays (about
10 minutes warm). The reasoning is the one already recorded: the
local gate runs the suite at both widths before every
merge, so the per-push backstop loses nothing the project relies on, and
the nightly still runs every suite. Two follow-ups are open: compiling the game's C through a file so ccache can cache it
(the 64-bit build is 328 s warm), after which the suite could return to
the per-push tier if it is wanted there; and caching the converted
assets. The suite can be flipped back with one line in the workflow.
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

The crash reboot does not start a process. The decision is that no exception
handler creates a process, with no exception to docs/C-RULES.md's handler
rule: the heap may be corrupt at crash time, and `CreateProcessW` allocates
from it (Crashpad and Breakpad work out of process for the same reason). Two
designs keep the rule. (a) The parent restarts: the unhandled-exception
filter encodes the persistent bytes into a buffer prepared at install time,
writes the report, which ends with `--persistent-state <512 hex digits>`, to
stderr with `WriteFile`, and ends the process with `TerminateProcess` and
exit code 75 (`CRASH_REBOOT_EXIT_CODE`, sysexits' `EX_TEMPFAIL`, which
nothing else here exits with); the program that started the process (the
Windows test driver) starts it again on that code with the same
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
language. That is ruled out: the save path sits under the user's
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
once the Windows runner has run
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

## 2026-10-14: Decisions on an independent review's open questions

(1) The audio rate. The earlier PSG decision ("PSG trigger, wave-bank and output-scale rules") said never to
point-sample the PSG at m4a's 13379 Hz, and that is the rate the build
runs at (`FRAME_AUDIO_SAMPLE_RATE_HZ`, so the FIFO frame mixes sample for
sample): the corrected reasoning is that at 13379 Hz the square and noise
channels alias into the audible band (psg.c, "RESAMPLING"). Decision: run the device at the GBA's 32768 Hz with the FIFO frame
upsampled, with an A/B recording against the current path as its evidence.

(2) Headerless save files. A file without this project's header was
loaded as a bare 32-bit native image and could be handed to the
converter. The game's sector checksum is a sum over raw bytes, so no
checksum or signature can tell a GBA-layout image from a native one: a
probe is impossible, so the legacy path is ruled out. A file
without the header is refused with a message that it looks like an
emulator or cartridge save, that importing those is not supported yet,
and that the file is left untouched; the converter never runs on a
headerless image. The only headerless native saves ever written were
this project's own early test saves, which have no users. Done now, as
a data-integrity bug, with tests for an emulator save, a truncated file
and a valid native save at each width.

(3) Two instances on one save. "Unsupported" is not acceptable:
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
released after the process is killed. Built after the
Windows runner work merged (it owns host_file.h and host_windows.c).

(4) `src/siirtc.c`. Filtered out of the native build, the SiiRtc API
provided from the platform layer, its guards removed and
UPSTREAM_MODIFICATIONS.md updated: built after the Windows runner work
merged (it owns Makefile.native).



## 2026-10-14: The suite on Windows runs under MSYS2

Gate (b) on Windows is expansion's own suite, through expansion's own hydra
(`tools/mgba-rom-test-hydra`), on the Windows test runner build, with the
per-test results equal to the Linux 64-bit build's of the same commit. Three routes were considered: (i) the
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
layer"), recorded here; the model is unchanged.

## 2026-10-14: The save's lock across a restart and a crash

Decision (3) of "Decisions on an independent review's open questions" as built (`platform/save_lock.h`), and the choice it left open.

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

## 2026-10-14: A hands-on check of the Windows build: a settings file and no console window

The Windows build from run 38648131073 was run by hand (boot, the
opening, the first Pokémon, the first battle; audio and saving fine), and
two things seen while doing it were decided.

**A settings file for the keys.** The keyboard map is a fixed table
(`platform/input.c`: A=Z, B=X, Start=Return, Select=Backspace, L=A, R=S,
the arrows; VisualBoyAdvance's defaults). Decision: a settings file
beside the executable rebinds the keys (the design came with the work);
an on-screen help overlay is wanted too (a key such as F1 showing the
map, the save path and the version) and a `--keys` option printing the
map; a native menu bar was declined for now, because SDL2 has none and it
would be three implementations (Win32, GTK, Cocoa).

**No console window on Windows.** The executable is linked as a console
program (`-mconsole`, `platform/target.mk`) because it prints: the test
runner's results for the hydra, the replays' output, the refusals. Started
from Explorer, Windows opens a console beside the game window. Decision: the console window goes (players would not like it). The design to try: link as a GUI
program (`-mwindows`); attach to the parent's console when there is one
(`AttachConsole(ATTACH_PARENT_PROCESS)` at startup, so a command prompt
still sees the messages); otherwise write the messages to a log file
beside the executable; pipes (the hydra, the replays' harness) are
inherited handles and keep working. Verify each of those three on the
runner. (Built, with changes, as recorded in "2026-10-15: No console window
on Windows", below.)

Also confirmed: the translucent oval under every overworld
character is expansion's own default (`OW_OBJECT_VANILLA_SHADOWS FALSE`,
`OW_SHADOW_INTENSITY 4` of 16 in `include/config/overworld.h`); the Linux
replay screenshot shows the same.

## 2026-10-14: Quit lives in the F1 overlay; the default file locations are the per-user directory

Quit and the file locations, decided after the hands-on check;
they refine "A hands-on check of the Windows build: a settings file and no console window" above.

**Quit lives in the platform's F1 overlay, not in the game's Start menu.**
Adding it to the Start menu would be another patch to expansion's code,
against the upstream-modification policy (UPSTREAM_MODIFICATIONS.md). The
Start menu's EXIT stays as in the original game: it closes the menu.

**Default file locations move to the per-user directory.** Today the save
defaults to the bare relative path `pokeemerald.sav` (`OPTIONS_DEFAULT_SAVE_PATH`),
so it depends on the working directory: a shortcut or a different launch
folder silently starts a fresh game; and a folder beside the executable may
not be writable (Program Files; a 64-bit process gets no write
virtualization). Decision: the save, the RTC offset file and the new
settings file default to the per-user directory SDL_GetPrefPath gives; the
executable's directory is used instead only when a settings file already
exists there (portable mode); explicit `--save` and `--rtc-offset` paths
still win. The F1 overlay and the `--keys` output show the active paths.
Both modes are documented in BUILDING.md or a short docs/RUNNING.md. Note: the saves from the builds of 2026-10-14 live in the launch directory
and will need moving once, by hand, to the per-user directory.

## 2026-10-14: A release pipeline without a public release



**A release pipeline, not a public release.** A `workflow_dispatch` release
workflow builds the Linux 64-bit tarball and the Windows 64-bit zip (the
executable, SDL2.dll, the licence texts, a short README) with ccache,
generates SHA-256 sums, attaches GitHub artifact attestations
(actions/attest-build-provenance) so anyone can verify the files came from
CI and a named commit, and publishes to a GitHub release only when run with
an explicit `publish` input; without it the artifacts stay on the workflow
run for testing. No public release and no tag is created without an explicit
decision to publish. Built and dispatched once on 2026-10-15 (run
38660260570, publish off): the archives and sums are right; the attestation
step fails on a user-owned private repository ("Feature not available ...
please make this repository public") and needs no change once it is public.

**Why no public release.** A built executable contains the complete game's
assets, so a public release is legally the same as distributing a ROM,
which README says this project does not do and which every decomp project
avoids. The default plan is source-only distribution with a one-command
build per OS (BUILDING.md, with a verified script); a public release
happens only on an explicit decision to publish.

**Code signing is not done.** It costs money and is needed only for a public
Windows release (SmartScreen's "Windows protected your PC" on an unsigned,
reputation-less executable). **VirusTotal** scan links would be added to
release notes only if a public release ever happens, with the known caveat
that MinGW-built executables draw false positives from some engines.

## 2026-10-14: The settings file, the data directory and the F1 overlay

The result of the Windows hands-on check (a settings file for the
keys, an overlay with the map, the save path and the version, `--keys`) with
the two adjustments (the files in the per-user directory with a portable
mode; Quit in the platform's overlay), as built. The player's account is
`docs/RUNNING.md`; the modules are `platform/data_dir.h`, `settings.h`,
`help_text.h`, `overlay.h`, `font.h` and `version.h`.

**The names.** `SDL_GetPrefPath("pokeemerald-expansion-native",
"pokeemerald-native")`: `~/.local/share/pokeemerald-expansion-native/pokeemerald-native/`
on Linux (or under `$XDG_DATA_HOME`), `%APPDATA%\pokeemerald-expansion-native\pokeemerald-native\`
on Windows, `~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/`
on macOS. The application is the executable's name, as decided. SDL asks for an
organisation too, and its header documents no empty one (the behaviour of
`""` on Windows and macOS is not written anywhere SDL 2 promises), so it gets
a name: the project's own, which claims nothing on behalf of pret or
rh-hideout, as a name of theirs would. The files keep their names,
`pokeemerald.sav` (with `pokeemerald.sav.lock`) and `pokeemerald.rtc`, and
the new one is `settings.ini`: plain text that every system opens in an
editor (Notepad, by default, on Windows), which is what a file the player
writes by hand needs.

**The rule** (`data_dir_resolve`): `--data-dir DIR`, made if missing with
`host_file_make_directory` (its parent must exist), refused if what is there
is not a directory (`host_file_is_directory`), and made and checked whenever
it is given, even when every file has its own path; else the executable's
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
data directory and its mode, the three paths, then the keys in force and the
controller line, files first because a small window's overlay shows the
lines from the top until it is full (at `--scale 1` some 14 lines of 38
characters; `--keys` prints everything). Quit is the window-close path: the frame loop ends, `main`
stores the save only if the game wrote one and it is pending, as at any exit,
stops the modules and releases the lock; the platform writes nothing of its
own. The game's Start menu is not touched. While the overlay is shown the
game keeps running, as decided, and KEYINPUT reads every button released,
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

**Where it lives in the code.** `platform/player.c` holds what a run knows
of its player (the paths, the settings, the help text, the window's scale)
and the one decision of who gets the overlay (`player_wants_overlay`: not
headless, not test-runner mode, not a replay), all unit-tested; `main.c`
calls it. The settings file and the clock's offset are read through
`host_file_read` (read(2) retried on `EINTR`; Windows: `ReadFile` on the
descriptor's handle, as `host_file_write_all` writes), so that the OS calls
stay behind `host_file.h`. A bad line is reported with its printable ASCII
only, other bytes as `?`, so that a settings file cannot write control
sequences to the terminal.

**The version.** `git describe --always --dirty --match 'v[0-9]*'`, from
`Makefile.native`, kept to the characters a tag or a hash has; `unknown`
without git. The match is needed because the history carries expansion's
tags (`expansion/1.10.0`, `1.11.0`), after which plain `git describe` would
name the build (`expansion/1.10.0-5060-g...`, read as expansion 1.10.0); it
takes a release tag of this project's own if one is made, named
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

## 2026-10-15: Fast-forward in the frame pacer: the keys, the speed, the sound and the indicator

The fast-forward feature, as built. The player's
account is `docs/RUNNING.md` "Fast-forward"; the modules are
`platform/fastforward.h`, `frame_pacer.h` and `sound_speedup.h`.

**The module.** `fastforward.c` is a state machine with no SDL call: SDL
events in (`fastforward_handle_event`, from `frame.c`'s event loop beside
`input_handle_event` and `overlay_handle_event`), "on, and at what
multiplier" out (`fastforward_active`, `fastforward_multiplier`,
`fastforward_muted`). A key is a scancode and the modifiers held with it,
Shift, Ctrl and Alt on either side; a press is the key's when exactly those
are held, ignoring Caps Lock, Num Lock, GUI and SDL's Mode key (`KMOD_MODE`)
and a modifier key's own bit (SDL sets Left Shift's bit in Left Shift's own
press, so "left shift" alone would otherwise never match). AltGr is not
ignored: SDL reports it as Right Alt (Left Ctrl and Right Alt on Windows),
so Tab with AltGr held is Alt+Tab (Ctrl+Alt+Tab on Windows), not Tab.
Exact matching is what makes Tab and Shift+Tab two keys with no precedence
rule. The settings file writes a
key as SDL's name after any of `shift+`, `ctrl+`, `alt+` (blanks around the
`+`, any case and order); a name that itself holds a `+` (`Keypad +`) or
starts with "alt" (`AltErase`) is still the key, since a modifier needs its
`+`. Buttons take no modifiers (KEYINPUT reads keys, not chords), so
`a = shift+z` stays an unknown key. F1 is refused for both, with or without
a modifier, as for the buttons. A button and fast-forward may share a key,
as two buttons may: refusing it would break a settings file of the last
build with `select = tab`. One key for both is refused after the whole
file is read, on the later of the two lines (its setting keeps its default,
checked twice, since a default can clash with the other line), so that
swapping the two keys in two lines works in either order, which a check
line by line would refuse.

**The hold and the toggle.** The hold is on from its press to its
scancode's key-up, whatever the modifiers then, and is released on
`SDL_WINDOWEVENT_FOCUS_LOST` too: the key-up goes to the window that has
the focus by then, so without it the game could stay fast-forwarded with no
key held (SDL 2 also resets its keyboard state on focus loss on most
backends, but that is SDL's behaviour, not a promise this relies on). The
toggle flips on each press and on nothing else: not a key-up, not focus
loss, so it persists across frames until pressed again or Quit. Repeats do
nothing. While the F1 overlay is shown, presses are the overlay's (as
KEYINPUT reads no button then), so neither key changes fast-forward from
under it; a key-up still ends the hold, because ignoring it would leave the
game fast-forwarded by a key let go of while reading. A toggle switched on
stays on under the overlay and the game runs on fast beneath it (the
overlay never paused the game); the overlay hides the indicator.

**The pacer's arithmetic.** At n times the speed the period is the GBA's
divided by n with its exact remainder: `frame_pacer_period_ticks(hz, n,
&rem)` returns floor((hz * 280896 + rem) / (16777216 n)) and keeps the
remainder in units of 1/(16777216 n) tick, so n periods of a run sum to
exactly floor(k * 280896 * hz / (16777216 n)) with no drift (tested at 2, 3
and 4 against values worked by hand at 1 GHz, 10 MHz and 1 kHz). The
remainder stays below 16777216 * 4, inside its uint32_t (a static
assertion), and the counter limit (6e13 Hz) still keeps hz * 280896 inside
64 bits. Unlimited does not wait, as `FRAME_PACER_UNTHROTTLED` does not.
`frame.c` sets the multiplier once a frame, after the events are polled
(`frame_pacer_set_multiplier(fastforward_multiplier())`); a change of speed
restarts the schedule from now, as a long stall does, and resets the
remainder: frames owed at the old speed (behind at 4x on a slow host, or
the stretch run unlimited) are not run back to back at the new one. The
game's logic is untouched: each `frame_run` is one GBA frame (one V-Blank,
one frame count) at every speed, only closer together; the test that
applies the multiplier to the frame count instead of the period fails
(`test_frame.c`).

**The clock.** Confirmed by construction: the RTC's time source in a
played game is `time(NULL)` (`rtc.c` `system_clock`, chosen by `main`
whenever `--rtc-fixed` is absent), never the frame count, so the time of
day, Berries and tides keep host time at any speed. Only `--rtc-fixed`
reads frames, for replays, which never fast-forward. What the game counts
in frames (its play time, every animation) runs with the frames, as on an
emulator.

**The sound.** The bound alone (audio.c drops a block that would take the
queue past four frames' worth) keeps the queue bounded at any speed, but
what it plays is poor: the queue sits full, the sound lags the picture by
67 ms, it plays every n-th frame or so as a fragment with a click at each
seam, and after fast-forward the queue stays at the bound at 1x (producer
and consumer at the same rate), so any jitter drops a block for as long as
the game runs. Chosen instead, the simplest that sounds continuous:
`sound_speedup` makes each n consecutive stereo samples one, their mean
(rounded toward zero; a box filter, a crude low-pass against aliasing), so
the sound plays continuously n times as fast and as high, one frame
period's worth queued per period, and the queue stays as deep as at 1x
(300 frames on SDL's dummy device, measured: at most 2632, 3068 and 3288
bytes at 2, 3 and 4x, none dropped, against 3364 at the normal speed and
the bound's 8784; unlimited sat at the bound and dropped some 154000 sample
frames). Unlimited has no n to divide by (the host's
speed varies), so its frames are queued as they are and the bound drops
what does not fit: fragments at the normal pitch, as mGBA and VBA-M play
fast-forward. `fast_forward_audio = mute` pauses the device
(`audio_set_paused`) while fast-forward is on, and the frame loop queues
nothing then (the PSG and the mix still run, as the hardware's did). On
leaving mute or unlimited, whose queue is stale or full, the queue starts
again as at power-on: paused, emptied (`audio_clear`), and played after
`FRAME_AUDIO_PREROLL_FRAMES` frames are queued, so no stale sound bursts
out and the latency is back to normal; leaving 2 to 4x needs nothing. The
queue is bounded at every speed in every case (the 300-frame test on SDL's
dummy audio device, which drains in real time, at 2, 3, 4 and unlimited;
the bound's removal fails it at unlimited, and at the normal speed no frame
test would notice, since only fast-forward can fill the queue).
`--audio-dump` records what is queued, so a played game's recording has
its fast-forward sped up and its muted stretch missing; no replay or gate
fast-forwards, so theirs are unchanged.

**The indicator.** "FF 2x", "FF 3x", "FF 4x" or "FF max", the overlay's
font, white on the overlay's translucent black, a pixel of padding, two
pixels from the top and right edges of the 240x160 screen
(`fastforward_indicator_place`: 31x10 at x 207 for "FF 2x", 37x10 at x 201
for "FF max"). It is drawn at the LCD's resolution, not the window's: six
characters need no finer text, and a texture in LCD pixels copied to a
rectangle of the renderer's logical 240x160 scales and letterboxes with the
frame, as sharp as the game's own pixels. `video.c` makes it once at init
(the speed is a setting, fixed for the run) and copies it after the frame
while fast-forward is on and the overlay hidden; the frame buffer and the
screenshots never see it.

**What a replay and the gates see.** `main` starts fast-forward only where
it starts the overlay, renamed for both from `player_wants_overlay` to
`player_at_keyboard`: a window played from the keyboard, never headless,
in test-runner mode or for a replay. Unstarted, `fastforward_multiplier` is
the normal speed and the frame loop's speed step changes nothing, so every
replay, dump, screenshot and suite run is as before (checked: both 64-bit
replays and the cross-width script, outputs unchanged). `--unthrottled`
stays unthrottled at any multiplier.

**Not verified here** (no display, no sound card): the indicator on a
screen, the sped-up sound by ear, and the keys on a real keyboard (Tab and
Shift+Tab through a real window's focus changes; SDL's dummy driver
delivers pushed events only). Their logic is unit-tested and the queue's
depth is measured on SDL's dummy audio device.

## 2026-10-15: No console window on Windows: the game is a GUI program and prints to what it was given, to the parent's console, or to a log

The decision that the console beside the game when started from Explorer
goes is built as the design recorded in "A hands-on check of the Windows build: a settings file and no console window", with the details below.

**As built.** The game and the test-runner build link with `-mwindows`
(`GUI_LDFLAGS` in `platform/target.mk`; subsystem "Windows GUI", PE
`Subsystem` 2); the unit tests keep `-mconsole`. `main` is still the
program's own: MinGW-w64's start-up code for a GUI program
(`WinMainCRTStartup`) calls it with the parsed command line, as the console
one does, and the image's layout is unchanged. `check_pe_layout.py --gui` or
`--console` fails the build if the subsystem is not the one wanted, and the
Windows smoke reads it from the file it runs. `main` asks
`host_console.h` first of all what standard output and standard error are
(none, a pipe, a file, a console this process writes to; Windows:
`GetStdHandle`, `GetFileType`, `GetConsoleMode`, so the null device and an
inherited console handle that is not attached are "none"), and
`console_choose` (`platform/console.c`, pure, tested against a fake) picks:

| stdout, stderr | the parent has a console | mode | the messages go to |
|---|---|---|---|
| both given | not asked | INHERITED | the handles, as today (the hydra, the replays, `> file`) |
| one or both none | yes | PARENT | the console, for the streams that were none (`AttachConsole(ATTACH_PARENT_PROCESS)`, `freopen("CONOUT$")`); a given stream is kept |
| one none | no | INHERITED_AND_LOG | the given stream; the missing one, and only it, to `pokeemerald.log` |
| both none | no | LOG | `pokeemerald.log` in the data directory |

Every other OS reports both streams as given, so nothing changes there. The
case of one stream given and one none was not specified: a prompt running
`game.exe > out.txt` passes the file for stdout and perhaps nothing for
stderr, and the refusals are on stderr, so the missing stream is mended on
the console rather than lost, and without a console it goes to the log (the
first version left it unwritten, which a review found could
hide a refusal with a non-zero exit and no trace). No message box in that
row: a stream is given, so someone reads it.

**The log** is `pokeemerald.log` in the data directory by the save's rule
(`data_dir_find`: `--data-dir`, else the executable's folder in portable
mode, else the per-user directory), looked for even by a run that needs no
file; if no data directory can be found it is beside the executable and
says so. It is emptied at each start (a file made and closed empty, then
both streams reopened to append to it, so they share the file and neither
overwrites the other; both unbuffered, so a crash loses no line). Nothing
is written to a stream that has no handle. Before the log's place is looked
for, the streams the log will take are pointed at the null device (a defined
destination, rather than the C runtime's tolerance of a write to a descriptor
with no handle, which was never verified); the place comes from the command
line without a report (`options_data_dir_given`, which scans the arguments as
`options_parse` does), and the arguments are parsed once the log is open, so a
bad argument is reported into it, once. What `data_dir_find` reports while
looking is lost to the null device and reported again, into the log, when
`player_find` finds the files. A command line that names no directory puts the
log where the defaults would.

**One open file.** The two streams are one open file with one offset: stdout is
reopened on the log to append, and stderr is made a duplicate of it
(`host_console_share_stream`: `dup2`, `_dup2`). Two separate opens of the same
path, the first version, share nothing: the C runtime appends on each write,
but a fault handler's raw `WriteFile` on stderr's handle (`crash_reboot.c`,
`arm_division.c`) lands at that handle's own offset and could overwrite what
stdout wrote. A duplicate handle shares the file pointer, so the three
writers continue each other. In the one-stream row only the missing stream is
reopened and nothing is shared.

**The message box** (`SDL_ShowSimpleMessageBox`, usable before `SDL_Init`)
is shown only in LOG mode and only when `main` returns non-zero before every
module is running (a second instance on the save, a refused save, an
unreadable settings file or data directory, a bad argument, a module that
did not start). Its text is the first 1000 bytes of the log, cut at a UTF-8
character boundary, where the error is, and the log's path, in a buffer sized
from the path's limit. `console_shows_message_box` is the decision and is
tested for every mode; nothing shows a box under a pipe or in a gate.

**The crash reports** (`arm_division.c`, `crash_reboot.c`) wrote to descriptor
2 by number. A process with no handles, after its streams are reopened on the
log, does not have its log on 2: run 38683258948 showed stdin 4 and stdout and
stderr both 0. They now resolve `fileno(stderr)` (still 2 on Linux), which is
whatever the C runtime made it. A first design that reopened stdin too, to
keep 0, 1 and 2 in order, did not hold. A Windows test starts a child with
the log on stderr alone (where `freopen` picks the descriptor), installs the
crash reboot, crashes, and reads the report from the log.

**Accepted.** A second instance, refused because the save is in use, empties
the running instance's log before it stops, and the running one's next lines
are appended at the new end. The log is not durable state (it is rewritten at
every start and only read by a person), a lock on it would be a second lock
protocol for a diagnostic file, and the refused instance's own text (the
reason it stopped) is what the player needs to read.

**Verified on the runner** (run 38693203706 after the review's fixes, green; earlier runs found two of this task's own Windows child tests failing, fixed: a stream is mended by what it was before the attach, and the log cannot count on descriptor 2): the start-up smoke, both replays, the
cross-OS save and the suite through the hydra pass unchanged on the GUI
build, so inherited pipes and files work (the smoke also reads the PE
subsystem and runs `--version`, `--keys` and a bad argument through a pipe);
`run_windows_no_console.ps1` starts the game through `Win32_Process.Create`,
whose parent has no console, with no handles and `--version --data-dir`, and
finds the version line, and only it, in `pokeemerald.log`, again over a log
from an earlier start; `test_host_console_windows` starts the unit-test
executable with a pipe, a file, the null device, nothing, with a console and
without, and checks what `host_console.h` reports, that attaching works when
the parent has a console and the streams then write on it, that it is refused
when the parent has none, and that a process with no handles and no console
logs through `stdout`, `stderr` and a raw handler-style write to stderr's handle, all five lines surviving in order, and that a crash report reaches a log opened on stderr alone.

**Only a person at a Windows desktop can check**: a double-click on `pokeemerald-native.exe`
shows no console window and leaves `pokeemerald.log` (in
`%APPDATA%\pokeemerald-expansion-native\pokeemerald-native\`); `cmd.exe` and
PowerShell print `--keys` and `--version` on their own console (the prompt
returns at once, so the text may appear after it); a second copy on the same
save shows the message box and names the log (a runner has no desktop to
show a box on, so only the decision to show it is tested). A restart (a replay's `restart` command) in LOG mode
opens the log again in the new process and so empties it; replays run under a
harness with pipes, never in LOG mode.

## 2026-10-15: macOS CI runs on macos-15-intel only, and GitHub's x86_64 macOS runners end in fall 2027

The facts for the macOS work: GitHub
retired the `macos-13` image in December 2025; `macos-14` is in brownouts in
October 2026 and retires on November 2, 2026; `macos-15-intel` is the last
x86_64 macOS image, and GitHub ends x86_64 macOS runners altogether when the
macOS 15 image retires in fall 2027. So every macOS job names
`macos-15-intel` and no other label (probe M0 already ran there).

What the end date changes. From fall 2027 no GitHub runner runs the x86_64
macOS build on x86_64 hardware; CI could still build it and run it under
Rosetta 2 on an arm64 runner (probe M0 measured the x86_64 layout under
Rosetta: 61 of 61 checks, 100 of 100 starts), but only as long as Rosetta
itself is there, and Apple keeps Rosetta generally available only through
macOS 27 ("macOS ships as x86_64 under Rosetta" above). Both horizons fall
around 2027, so the native Apple-silicon question (the
spike) is no longer about speed on newer Macs: it decides whether macOS stays
supported and tested after 2027 at all. The spike's report weighs it so.

Amended after the first review of the port, and the amendment withdrawn at
the final review: a job, `rosetta`, ran on `macos-latest` (arm64) to show
the x86_64 build on Apple silicon (an x86_64 Homebrew in `/usr/local`
under Rosetta 2, the unit tests built and run as x86_64, the game built by
`tools/build-native.sh --os macos` under `arch -x86_64`, the opening
replay). The owner's rule for this port is the label `macos-15-intel` and
nothing else, and the job also installed Homebrew from the head of its
install repository at every run, so it is gone with that install. The
Rosetta check is the owner's, by hand on an Apple-silicon Mac ("The macOS
port waits for its first runs on a Mac", run 3), and is noted with the
Apple-silicon decision ("macOS ships as x86_64 under Rosetta").

## 2026-10-15: Steam Deck: controller functions, fullscreen and the package

The Steam Deck work, as built. The Deck is the
Linux 64-bit build, so this is the handheld experience on top of it. The
player's account is `docs/RUNNING.md` "Steam Deck", "Fullscreen" and the
controller table under "Keys".

**Controller bindings.** The Deck reaches SDL as a standard game controller
under Steam Input. The GBA buttons stay as input.c has them (A, B, Back as
Select, Start, shoulders, D-pad and left stick); they are not rebindable,
because Steam Input remaps the whole pad where a player wants that, and a
second remapping layer would only add a way to break the game's own layout.
The six other functions each have one control (button or trigger), chosen from
what the GBA layout leaves free:

| Function | Default | Why |
|---|---|---|
| fast-forward hold | right trigger | a held function suits a trigger, pulled past about half travel |
| fast-forward toggle | right stick click | a click is a deliberate switch |
| help overlay | Y | the one face button the GBA layout does not use |
| overlay close | B | B is "back"; closing is backing out |
| overlay quit | X | the other free face button; shown only in the overlay, so no accident in play |
| fullscreen | left trigger | deliberate, and not a stick click, which Deck players press by accident while steering; the right trigger holds, the left one switches the screen |

Rebinding is `pad_fast_forward`, `pad_fast_forward_toggle`, `pad_overlay`,
`pad_overlay_close`, `pad_quit`, `pad_fullscreen` in settings.ini, by SDL's
button names (`SDL_GameControllerGetButtonFromString`) plus `lefttrigger` and
`righttrigger`. One button serves one function (the later line reverts to its
default; the loop ends because each pass takes a function off a custom
button). A bound button still does its GBA job as well: refusing GBA buttons
would forbid choices a player might want (A to quit, say) for no gain. Triggers are pressed past
16000 and released below 8000 of 32767 (hysteresis, so a trigger resting at
the threshold does not chatter). The hint line of the overlay names the
controller's buttons (`X: quit  B: close`) after a controller button or a
stick or trigger pushed well over was the last input, and the keyboard's again
at the next key; video.c repaints the overlay texture when that changes.

**Per-controller state from events.** `controller.c` keeps what each controller
(by SDL instance id, up to the four input.c opens) holds from the events alone,
so tests push events and need no device. The reason for tracking: SDL sends no
button-up for a pulled cable, and a hold must not stay on. A controller's
removal and the window losing focus release what it held, and
`fastforward_handle_controller` ends the hold whenever no controller holds
its control.

**One behaviour change outside the Deck.** A button held at the first read
after the overlay hides stays released until it is let go (`overlay_game_keys`):
the frame loop closes the overlay and reads the keys in one poll, so the B
that closes it would otherwise reach the game and back it out of a menu (a
first version latched only keys seen while shown, which the loop never gives;
review caught it). It applies to the keyboard and scripts alike (the overlay
never exists in replays).

**Fullscreen.** `SteamDeck` in the environment set to exactly `1` is a Deck
(`fullscreen_on_steam_deck`, a pure function; `0`, unset, `true` or `11` are
not); a Deck starts in desktop fullscreen (`SDL_WINDOW_FULLSCREEN_DESKTOP`:
the screen's own resolution, no mode change) under the existing logical size,
integer scale and letterbox. F11 (rebindable, `fullscreen_toggle`) and the
left trigger toggle it for everyone. The setting `fullscreen = auto|yes|no`
(auto, the default, is the Deck rule) was added because without it a Deck player who
prefers a window would have to toggle on every launch; it also lets a desktop player start fullscreen.
Fullscreen and the controller start only for runs with a player at the
keyboard (`player_at_keyboard`), so `SteamDeck=1` changes nothing in a
replay, a gate or the test runner (checked below).

**The package.** The Deck work calls for a self-contained package
(binary, SDL2, licences, launcher script), so the Linux tarball now holds the
executable, `pokeemerald-native.sh`, `libSDL2-2.0.so.0` (the file the release
job's executable resolves, copied under its soname with `cp --dereference`,
from Ubuntu 24.04's libsdl2-2.0-0), its zlib licence as `SDL2-LICENSE.txt`
(Ubuntu's `copyright` file, as the Windows zip carries SDL2's licence), the
project `LICENSE` and the pages. The launcher puts its own folder first in
`LD_LIBRARY_PATH`, so an executable started directly still uses the system's
SDL2 and the launcher uses the bundled one, and it does not change the working
directory, so relative `--replay`, `--save`, `--data-dir` and `--out` mean
what the caller typed (portable mode finds `settings.ini` through
`SDL_GetBasePath`, not the working directory). Measured on the build image
(`objdump -T`, `objdump -p`): the executable needs glibc 2.34
(`pthread_create` and `__libc_start_main` moved into libc in 2.34); the bundled
SDL2 needs 2.38 and, besides glibc and libm, 17 libraries from the system
(libasound, libpulse, libsamplerate, libX11, libXext, libXcursor, libXi,
libXfixes, libXrandr, libXss, libdrm, libgbm, libwayland-egl,
libwayland-client, libwayland-cursor, libxkbcommon, libdecor). The floor
rises from 2.34 to 2.38 by bundling; a SteamOS 3.8 Deck reports glibc 2.41
(https://forum.godotengine.org/t/steam-deck-cant-launch-linux-build-due-to-outdated-glibc-version-build-targeting-newer-glibc-version/143218),
which meets it; no source was found for older SteamOS releases or for the
system libraries on a Deck, which the checklist covers. The CI image is
unchanged: the requirement is met on the current Deck.

**Tests, and the order.** The code was written before its tests, against the test-first rule; to show the tests are not decoration, every new test
was run against a mutant of the code and failed there. 54 mutants, and every
new test failed against at least one of them: the five named in the specification (hold
not released on removal: `removing_a_controller_releases_what_it_held`,
`a_controller_removed_while_the_hold_is_down_ends_the_hold` and the frame
test `the_right_trigger_fast_forwards_while_squeezed_and_the_pull_of_the_cable_ends_it`;
the overlay letting buttons through while shown: `f1_shows_and_hides_it_and_the_game_reads_no_button_while_shown`,
`the_game_reads_no_button_while_the_overlay_is_shown`,
`the_game_reads_every_button_released_while_the_overlay_opened_by_the_controller_is_shown`;
Deck true for `SteamDeck=0`: `unset_zero_and_other_values_are_not_a_deck` and
`a_window_starts_fullscreen_on_a_deck_and_not_elsewhere`; the trigger
threshold inverted: `a_trigger_is_pressed_past_its_threshold_and_released_below_the_lower_one`
and eight more; the toggle firing on every frame the button is held:
`a_button_already_held_or_released_twice_is_no_edge`, `a_held_toggle_button_flips_once`,
`the_trigger_let_go_ends_the_hold_and_the_stick_click_toggles_once`) and 49
others, one per behaviour. The review then found two gaps and each got a
test that fails first: the latch (`the_b_that_closes_the_overlay_in_the_same_poll_is_not_the_games`,
fails against the first version, and the loop-order frame test
`the_b_that_closes_the_overlay_is_not_read_by_the_game_until_it_is_let_go`) and
the fullscreen button firing on release too
(`letting_go_of_the_fullscreen_button_does_not_toggle_again`). The harness was a small mutation script, not kept.

**Only a person with a Deck can check** (RUNNING.md "Steam Deck" is the procedure): that the launcher starts the
game with the bundled SDL2 (`LD_DEBUG=libs` shows it) and the system libraries
the SDL2 needs are there; that the game starts fullscreen in Game Mode at 1280x800 with the
picture at 5x, black bars; that the right trigger, the stick click, Y, B, X and
the left trigger do what the table says on the real Steam Input layout for a
non-Steam game; that unplugging a controller (a Bluetooth pad turned off) while
the trigger is held ends fast-forward; the save's path; sound.

## 2026-10-15: The macOS executable's layout: segments placed by the link, the scripts' names from section bounds, the order by the order of the objects

ld64 has no linker scripts, and the native memory model needs what
`gba_regions.ld` and `gba_game.ld` give on Linux: each GBA region at its
address, mapped by the kernel before any code runs; names for the regions'
bounds and the soft reset's brackets; the regions' sections in a fixed order;
an image at 0x40000000 whose code ends below the script tag's 0x42000000.
Probe M0 showed ld64 builds such an image and macOS loads it unslid (100 of
100 starts), and probe M2 answered how the rest can be said
(`research/probes/README.md`). The choices:

- **Each region a segment, placed with `-segaddr`**, its section declared
  zero-filled in `platform/gba_regions.macho.s`, in a non-PIE image
  (`-no_pie`, `-image_base 0x40000000`) with a one-page `__PAGEZERO` (the
  default 4 GiB one covers the GBA's addresses) and no chained fixups (with
  them ld64 ignores the image base). The alternative, mapping the regions at
  run time with `mmap`, is what the platform does only for what the image
  cannot hold; a segment is in place before the first instruction, as a
  `NOLOAD` section is on Linux, and needs no code. ld64 warns that `-no_pie`
  and a small `__PAGEZERO` are deprecated; both still work for x86_64, and
  arm64 is not built.
- **The scripts' names are ld64's own bounds**, `section$start$SEG$SECT` and
  `segment$end$SEG`, aliased to the platform's names by two `-alias_list`
  files (`platform/gba_regions.macho.aliases`,
  `platform/gba_game.macho.aliases`), which stand where the two scripts stand
  on the link line (`GBA_LINKER_SCRIPT_FLAG`, `platform/target.mk`). The
  absolute values the scripts define (`gNumMusicPlayers`, `gMaxLines`,
  `__rom_end`) and `--defsym`'s `gba_game_objects_linked` are `.set` symbols
  in `platform/gba_game.macho.s`. The alternative, the platform finding the
  bounds itself through `getsectiondata` at run time, would change
  `host_linked.c`'s interface for one OS.
- **The order of sections comes from the order of objects.** ld64 lays out a
  segment's sections as it first meets each with something in it (probe M2:
  an empty declaration does not count), so the regions' object is linked
  first, the game's brackets' second, and EWRAM's end marker last
  (`platform/gba_regions_end.macho.s`, a byte aligned to 256 KiB that lands on
  0x02040000 while EWRAM's data fits, after a byte of its own so that an
  executable with no EWRAM variable, most unit tests, still has the marker
  there and not at 0x02000000); the test runner's own tests come first
  in the test table because `test/test_test_runner.o` is linked before the
  other test objects (`Makefile.native`, `NATIVE_LINK_OBJS`).
- **The soft reset's brackets are sections, not paths.** `gba_game.ld`
  selects the game's variables by the object's path, which ld64 cannot; on
  macOS the C filter puts every game object's variables in
  `__DATA,__game_data` and `__DATA,__game_bss` with `#pragma clang section`,
  and the converted data names its sections the same, so the brackets are
  those sections' bounds.
- **What the scripts' ASSERTs check, a script checks after the link**:
  `platform/build_glue/check_macho_layout.py` reads every macOS executable,
  the unit tests included, and fails the link if the image is PIE, has
  chained fixups or a large `__PAGEZERO`, if `__TEXT` is not at 0x40000000, if
  a section of instructions reaches 0x42000000 (the read-only data after the
  code in `__TEXT` may, as `.rodata` does on Linux: the sanitized test
  runner's code is 22 MB and its read-only data 23 MB), or if any region is
  not at its address with its size, its sections in order.

## 2026-10-15: The stack switch on macOS is the platform's own assembly, not ucontext

The battle runner calls the game on a stack of its own (`host_stack.h`).
Linux switches with `makecontext` and `swapcontext`. On macOS
`<ucontext.h>` refuses to compile without `_XOPEN_SOURCE`, and with it every
call is "deprecated: first deprecated in macOS 10.6", an error under the
platform's `-Werror` (probe M2); `docs/C-RULES.md` lets no warning be
silenced, and a deprecated interface on a platform with Rosetta's horizon is
no foundation. The alternatives: `sigaltstack` with a signal to enter the
stack (the GNU Pth trick), which needs a signal handler and a `setjmp`
across it and is harder to reason about under AddressSanitizer; or a
thread per call, which changes the battle runner's single-threaded model.
`platform/host_macos_stack.c` has two small System V routines instead, as
Windows has its own (`host_windows_stack.c`): one saves the callee-saved
registers, MXCSR's and the x87 control word on the caller's stack, moves RSP
to the new stack's top and calls the entry; the other comes back to the same
restore from inside the entry, for `host_stack_leave`. RBP stays the frame
pointer of the caller's frame, and the call frame information names it, so
an unwinder walks from the entry back to the caller. AddressSanitizer's
fiber annotations (`__sanitizer_start_switch_fiber`) are made around both
switches, as on Linux.

## 2026-10-15: The watchdog on macOS waits a relative time on the monotonic clock

The test time limit's watchdog (`host_thread.h`) waits for a deadline on a
condition variable. Linux makes the condition's clock `CLOCK_MONOTONIC` with
`pthread_condattr_setclock`, which macOS does not have: its absolute waits
are on the wall clock, and a wall clock stepped back would hold a test past
its limit, stepped forward end it early. The wait is therefore
`pthread_cond_timedwait_relative_np`, macOS's relative wait, with what is left
of the deadline on `CLOCK_MONOTONIC` computed before each wait and again
after every wake-up (`host_condition_wait_until`, `platform/host_macos.c`).
The kernel measures the relative time on its own monotonic clock, so a step
of the wall clock can move neither the deadline (never read from it) nor the
wait. Probe M2 measured it: a 300 ms deadline ended at 300 ms after three
wake-ups, clean under ThreadSanitizer. The alternative, polling with a short
sleep, wakes the process for nothing and is less precise.

## 2026-10-15: Saves on macOS are flushed to the disk with F_FULLFSYNC

`docs/C-RULES.md` asks for a crash-safe save: the new file is written, made
durable, renamed over the old one, and the directory made durable. On macOS
`fsync` hands the data to the drive, which may keep it in its volatile cache
(fsync(2) says so and offers `F_FULLFSYNC`), so a power loss after the game
reported the save written could lose it. `host_file_flush` on macOS is
`fcntl(F_FULLFSYNC)`, for the file and for its directory, and falls back to
`fsync` where the file system refuses it (some network and FAT volumes),
which is then the best the system offers: on `ENOTSUP`, `ENOTTY` and
`EINVAL` only. Any other error (`EIO` from the drive, say) is reported, not
covered by an `fsync` that may succeed (review: the first version fell back on
every error but `EBADF`; SQLite falls back on any, which reports a failed
flush as a success). The cost, measured on the
runner's APFS volume (probe M2): 4.5 to 24 ms for the 128 KiB save against
under 1 ms for `fsync`, 3.5 to 4.3 ms for the directory, once per save. The
alternative, plain `fsync`, is what Linux's `fsync` already gives on Linux
but not what it means on macOS.

## 2026-10-15: The game's files on a Mac are in ~/Library/Application Support

`data_dir.h`'s rule needs no change for macOS: SDL's per-user directory,
`SDL_GetPrefPath("pokeemerald-expansion-native", "pokeemerald-native")`, is
`~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/`
there, the place Apple's File System Programming Guide gives an
application's own files, and portable mode is the executable's directory
(`SDL_GetBasePath`). The alternatives were `~/Documents` (visible and synced
to iCloud, wrong for a save the game owns) and the XDG directories Linux
uses (not where a Mac user looks). Two things are left to the packaging
task: in an `.app` bundle `SDL_GetBasePath` names the bundle's `Resources`
folder, which is inside the signed bundle, so portable mode should not be
offered there; and the replays pass `--save` and `--rtc-offset`, so none of
them reads the per-user directory: the macOS workflow checks the path
`--keys` names instead.

## 2026-10-15: The data on a Mac is assembled there with GNU as and converted

Expansion's data is GNU as text that clang's assembler refuses ("The data for
Windows and macOS is assembled on Linux and converted" above). For Windows
the Linux tree's objects are converted on Linux. A Mac build cannot reach a
Linux tree, and the macOS build has to work from a fresh clone on the Mac. So
the Mac runs the same steps: Homebrew's `x86_64-elf-binutils` is GNU as for
ELF (the same assembler, 2.47 on the runner, against Ubuntu's 2.42), the data
objects are assembled with the same filter and flags, checked by the same
relocation and battle script checks, converted to Mach-O listings, assembled
by clang and compared with the ELF objects (`check-flat`) before the link.
Measured on Linux: clang's preprocessor (`clang -E`, the Mac's `CPP`) gives
the data the same text but for line markers, and the 14 data objects it
feeds assemble identically; the trainer headers through `-traditional-cpp`
are identical too.

The struct layout check is left out on a Mac: it compares the layouts a
probe prints when built and run at 32 and at 64 bits, and macOS runs no
32-bit program. Its 64-bit half is kept another way: CI builds the probe on
the Mac with the Mac's clang and diffs its output with the Linux build's
(`macos.yml`, compare), so a struct laid out differently by clang on macOS
fails there; the 32-bit half is the Linux gate's.

The test runner build adds the tests' event scripts
(`include/test/overworld_script.h`), inline assembly in the same dialect that
also names C functions without Mach-O's underscore and the data's constants
(`ITEM_POTION`, `SPECIAL_*`), which the converted data writes local. On macOS
they leave the C before clang compiles it (`overworld_scripts.py`), are
assembled by GNU as with the same macros, and are converted and compared as
the data is, every word that names a data constant given that constant's
value from a table of every global absolute symbol of the data objects, at
every width (`elf_to_flat.py --absolutes`), as GNU ld resolves them on Linux.
The alternative, defining the constants with `.set` before the scripts, would
change what GNU as's `.if` and `.ifdef` see in the event macros compared with
Linux, where they are undefined at assembly time.

## 2026-10-15: The sanitizers on macOS

The macOS build is built with Apple's clang, whose sanitizers differ from
GCC's on Linux in four ways, each handled where it arises:

- **No LeakSanitizer.** AddressSanitizer for x86_64 macOS aborts at start-up
  with `detect_leaks=1` ("detect_leaks is not supported on this platform",
  probe M2). The unit tests run with `detect_leaks=0` there
  (`platform/Makefile`, `SANITIZER_ENV`); every leak they could show is
  caught by the same tests on Linux.
- **Two UndefinedBehaviorSanitizer checks GCC does not make stay on**:
  `function` (a call through a pointer of another function type) and
  `pointer-overflow` in its C form (arithmetic on a null pointer). The
  sanitized replays of a clang build on Linux reported 8 sites in
  expansion's code, and nothing else; at first both checks were turned off
  for every expansion object, which review called what it was, 8 defects
  hidden and any future one with them. Now six are fixed under the guard
  (`src/decompress.c` three times, `src/field_player_avatar.c`,
  `src/event_object_movement.c`, `src/save.c`), and three objects, where one
  call takes functions of many types from a table that cannot say which, are
  built without the `function` check alone: `src/field_effect.o` (whose
  native result is now cut to the byte every native returns, since a `u8`
  return leaves EAX's upper bits undefined on x86-64), `src/scrcmd.o`
  (`gSpecials`) and `src/debug.o` (the debug menu's actions; a ninth site,
  which the debug menu replay showed).
  `platform/test_runner/SUITE-LOG.md`, "Clang's function and
  pointer-overflow checks", has every site and its root cause.
- **Section variables.** GCC's AddressSanitizer leaves a variable with a
  section of its own unpadded; clang's pads it with redzones, which would
  overflow EWRAM's 256 KiB and break the test table's array. The C filter
  adds `no_sanitize("address")` to those variables
  (`c_filter_macho.sed`, and `c_filter_clang.sed` for clang on Linux), so
  they are laid out as with GCC.
- **Where the writable data lies.** Expansion asks whether an address is in
  EWRAM by its bits 24-27 (`src/librfu_rfu.c`), which in the image are 2 for
  0x42000000-0x42FFFFFF. The sanitized builds' read-only data passes
  0x42000000 and their writable data followed it there: review saw
  `gRfuAPIBuffer` refused as "in EWRAM" in a clang build on Linux and the
  game die in `InitRFU`. The sanitized builds now link their writable data
  from 0x43000000 (`platform/target.mk`, `GBA_SANITIZED_LAYOUT_LDFLAGS`:
  `gba_writable_data_floor` on Linux, `-segaddr __DATA` on macOS), and every
  link check refuses writable data in such a window (`gba_regions.ld`'s
  ASSERTs, `check_pe_layout.py`, `check_macho_layout.py`). The plain builds
  are unchanged: their data ends 2.37 MiB below 0x42000000 at the least
  (the 64-bit game; `docs/ARCHITECTURE.md` has each build's), the checks
  fail the link when it grows into the window, and the remedy then is the
  sanitized builds' layout for every build.
- **Spellings**: clang takes AddressSanitizer's call threshold as a back-end
  option and has `-fsanitize-address-use-after-return=never` where GCC 13
  has a `--param` (`Makefile.native`).

ThreadSanitizer runs the unit tests on macOS as on Linux.

## 2026-10-15: Clang builds the game as GCC does: two code-generation choices made explicit, one undefined behaviour fixed

The macOS build's compiler is clang. Built with clang on Linux, the game
passed both replays at once, but the suite differed from GCC's in 6 of 5,875
results; four were a choice clang makes where GCC makes another, now made
explicitly (`Makefile.native`, with the reason where the flag is), and two
were undefined behaviour in expansion's test harness, now fixed:

- **Clang's narrow divisions by zero get libgcc's rule at their width.**
  Clang compiles a C division whose operands it knows fit 8 or 16 bits to an
  8- or 16-bit DIV or IDIV (23 sites in the game, against 9 in GCC's build,
  where GCC widens to 32 bits). By zero the decoder refused them, and 4
  tests crashed. `platform/arm_division.c` now decodes them and writes
  libgcc's rule at the instruction's width (all ones or zero, the most
  positive or negative value by the sign, zero), which keeps what a program
  can ask of the result, its sign and whether it is zero; the one such
  division a gate reaches only tests its remainder for zero.
- **Clang's 64-bit-to-32-bit division tuning is off**
  (`-idivq-to-divl`): it would divide two 64-bit values that fit 32 bits
  with a 32-bit DIV, which by zero gets the 32-bit rule where the GBA's code
  calls the 64-bit `__udivdi3`.
- **LICM's scalar promotion was off for the test objects; the cause is now
  fixed instead**: the battle tests' functions run once per phase, and a capture
  (`HP_BAR(..., captureDamage: &damage)`) made the runner write into a
  variable of a call that had returned, which a later call read from the
  same stack slot. `-mllvm -disable-licm-promotion` made clang's code read it
  as GCC's does, for the two Beat Up results that showed it; review showed
  the cause, undefined behaviour in expansion's test harness that one flag
  cannot cover (a 15-line reproducer reads 0 at clang -O2 with or without
  it). The flag is gone: natively the runner keeps a capture's value and
  runs SCENE again at the start of THEN's call, which writes it into that
  call's variable (`test/test_runner_battle.c`, "A capture";
  `include/test/battle.h`, `SCENE`), so the read is defined. Beat Up's 15
  tests pass at clang -O2. The replay's contract: SCENE's whole body runs
  a second time, in THEN's call, after the battle; its queue calls must
  queue the same events (else ERROR), and its other statements read the
  state after the battle. Seven tests whose captured variables have an
  initialiser passed on the GBA only because THEN's call reset them.
  Five fail now that the captures arrive, on expectations the captured
  values show wrong, and are KNOWN_FAILING natively. Two, Substitute's and
  Shed Tail's cost, read the maximum HP from `gParties` in SCENE, where it
  is 0 in SCENE's first run (the party is copied in at the battle's start)
  and the HP less the cost in the replay; they would fail with captures
  arriving by any means, and are corrected natively to read GIVEN's party's
  maximum HP, so they pass, as on the GBA, and check the cost
  (`platform/test_runner/SUITE-LOG.md`, class 11).

With the first two and the capture replay, the clang-built Linux suite
equals GCC's in all 5,884 results, plain and sanitized (5,419 PASS, 23
KNOWN_FAILING, and 3 ERROR, the replay's own tests of its errors, which
expect that result; `test/capture_replay.c` added 9 tests).

The rehearsal is reproducible from the tree (docs/BUILDING.md, "macOS",
"The clang rehearsal on Linux"): `TARGET_CC=clang` with an object directory
of its own selects `platform/build_glue/c_filter_clang.sed` (`c_filter.sed`'s
rule and the two of the Mach-O filter that clang needs on ELF too: `.hword`
written as `.2byte`, and the section variables left unpadded by
AddressSanitizer), and the test objects' event scripts, GNU as's dialect,
go to GNU as (`-fno-integrated-as`) as GCC's do. The platform library stays
GCC's there; its clang build is `make -C platform test CC=clang
BUILD_DIR=build/64-clang`.

## 2026-10-15: On macOS a SIGFPE sent by kill looks like a divide error

Linux tells a divide error from a SIGFPE another process sent by `si_code`
(`FPE_INTDIV` against `SI_USER`), and arm_division and the crash handler's
report go by it (`host_fault_is_divide_error`). macOS gives `FPE_INTDIV` for
both, and for `raise` (probe M2: `SI_USER` never seen). So on macOS
`host_fault_is_divide_error` also reads the instruction at the program
counter, and a SIGFPE is a divide error only when that is a DIV or IDIV
(`platform/host_macos.c`): a SIGFPE sent while the program is anywhere else
is not one, as on Linux, and the crash handler names it "signal 8". A SIGFPE
sent at the moment the program counter is at a division by zero still reads
as a divide error, and arm_division completes that division with libgcc's
rule and goes on. That needs a process to send SIGFPE to the game exactly
then, which no player and no tool does;
`platform/test/darwin/test_arm_division_darwin.c` pins it. The alternative,
`si_code` alone, would have named every raised SIGFPE a divide error (the
crash handler's report and `test_host_fault_posix.c` would differ from
Linux's); the exception state's trap number (`__es.__trapno`) might tell the
two apart, but no probe measured it, where the instruction is certain.

## 2026-10-15: Paths on macOS keep the 4096-byte limit; the long-path tests are Linux's

`HOST_FILE_PATH_SIZE` is 4096, Linux's `PATH_MAX`. macOS's `PATH_MAX` is
1024, and its file calls refuse a longer path (`ENAMETOOLONG`), so a save path
between the two limits is accepted by the platform and refused by the system
with a message naming it. The limit stays one number on every OS: the buffers
are sized by it, and a path the system refuses fails loudly at the first
open. The tests that pad a path to 4096 bytes with repeated slashes and
expect it to work (`test_rtc_linux.c`) are therefore Linux's; the refusal of
a path over the platform's limit is portable and runs everywhere.

## 2026-10-15: The macOS port waits for its first runs on a Mac

Nothing of the port has run on macOS beyond probes M0 and M2. The macOS
workflow's first runs (38714839300, and 38717390655 after a push) were
refused before any job started: "recent account payments have failed or your
spending limit needs to be increased", which stopped every job on the account
that night, the Windows nightly's too. Only the owner can change that, so the
branch was finished and rehearsed on Linux (a clang build of the game passes
both replays and equals GCC's suite, plain and sanitized, reproducible from
the tree: docs/BUILDING.md, "The clang rehearsal on Linux"; every game and
test source, filtered for Mach-O, compiles for macOS; the test objects' event
scripts go through their whole pipeline; patchelf's Mach-O path runs on
clang's Mach-O objects).

**The branch is parked.** `work/macos` stays unmerged, waiting for a run of
the full macOS gate on `macos-15-intel` (run 1 below), which needs GitHub
Actions back: the owner's ruling of 2026-10-15 ("The owner's rulings of the
morning after the night run", 1) leaves Actions off for now. Until then the
branch only follows `main`, by merges (never a rebase or a force-push), with
the full Linux gate on each merge. Merging it is the owner's call, once the
account's billing is restored and run 1 has shown everything listed under
it. The merge also decides the nightly: `macos.yml`'s schedule is left as
it is (a schedule fires only on the default branch, so it starts the night
the file reaches `main`), and at its jobs' time limits a night costs up to
2,340 billed minutes on a private repository (the three macOS jobs' 220
minutes at ten times, the Linux jobs' 140; 2,950 for the macOS jobs alone
while the `rosetta` job was in it), and nothing on a public one. The runs,
in this order:

1. The full gate: `macos.yml` by hand on the branch (`gh workflow run
   macos.yml --ref work/macos`; GitHub dispatches a workflow only if its
   file is on the default branch, so `macos.yml` must reach `main` first,
   alone or with a push trigger restored on the branch for the one run).
   Its jobs (`unit`, `game` and `suite` on `macos-15-intel`; `linux`,
   `linux-suite` and `compare` on `ubuntu-24.04`) must show:
   - **unit**: 100 unit test binaries (80 portable, 15 `posix/`, 5 `darwin/`)
     each "N passed, 0 failed" under ASan and UBSan, and 99 under TSan
     (`test_crt0` is not run under it); clang-tidy clean on the macOS
     sources; `test_patchelf_macho.sh` to its last line, including "the
     patched Mach-O executable runs and reads the patched values", and
     `test_patchelf_macho_object.sh` to its last line;
   - **game**: the link passes `check_macho_layout.py` (silent), plain and
     sanitized (the sanitized `__DATA` at 0x43000000, past the window where
     an address's bits 24-27 read as EWRAM's; if ld64 adds a writable
     `__DATA_CONST` before it, the check names it and it takes a `-segaddr`
     too); `otool -l`'s segments as `docs/ARCHITECTURE.md`'s table gives them; `otool -L` names
     Homebrew's SDL2 (expected `/usr/local/opt/sdl2-compat/lib/libSDL2-2.0.0.dylib`)
     and the system's libraries only; `--keys` names
     `~/Library/Application Support/pokeemerald-expansion-native/pokeemerald-native/`;
     both replays "PASS: 27 dumps, 26 screenshots" and "PASS: 20 dumps, 32
     screenshots", the same sanitized with no sanitizer report; gate h's
     load stage PASS on the Linux save;
   - **suite**: the test runner's own tests (two crash on purpose and are
     restarted), then 5,884 results (5,419 PASS, 2 CRASH, 3 ERROR (expected:
     `test/capture_replay.c`), 7 EXPECTED_FAIL, 23 KNOWN_FAILING, 9
     ASSUMPTION_FAIL, 421 TO_DO), plain and sanitized (the harness's
     summary counts the 3 expected ERRORs with the known failures: 26).
     Against `main`'s Linux suite (5,875 results, 5,418 PASS, 18
     KNOWN_FAILING) the difference is the branch's, the same with GCC on
     Linux: the capture replay's 9 self-tests (6 PASS, 3 ERROR) and 5 tests
     that pass on `main` only because their captured values never reach
     THEN, now KNOWN_FAILING natively (`platform/test_runner/SUITE-LOG.md`,
     class 11); every other test's result is `main`'s. Also
     no UndefinedBehaviorSanitizer `function` or `pointer-overflow`
     report (`platform/test_runner/SUITE-LOG.md` has the sites, and the three
     objects built without the `function` check);
   - **compare**: "PASS: the same 5884 tests with the same results" for the
     plain and for the sanitized suite against Linux 64-bit's; gate h's back
     stage PASS; the two 64-bit struct layouts identical.
   - **no upload of the game**: the run's artifacts are `linux-save`
     (gate h's first stage: `pokeemerald.sav`, `pokeemerald.rtc` and text),
     `macos-game` (text, and the second stage's save and clock),
     `suite-macos`, `suite-linux64`, and on a failure the `*-outputs`
     (text, logs, the gates' frame images and the save gates' saves and
     clocks, by extension); no executable, test runner, library, object,
     archive, converted data or output directory among them (no workflow
     uploads the game, ever: "No workflow uploads the game"; the
     platform gate's `check_workflow_uploads.py` passes on this file).
2. The same workflow on `main` after the merge, and its nightly from then on.
3. The owner's own Mac, Intel or Apple silicon under Rosetta: the build from a
   fresh clone with the Homebrew line of `docs/BUILDING.md`, the game in a
   window with sound, a save and its reload, and the save directory under
   `~/Library/Application Support`. On an Apple-silicon Mac this is also
   the only check of the x86_64 build under Rosetta 2 (CI runs this port on
   `macos-15-intel` alone): Rosetta 2 present or installed, an x86_64
   Homebrew in `/usr/local`, `arch -x86_64 make -C platform test BITS=64`
   with every unit test binary "N passed, 0 failed", `arch -x86_64
   tools/build-native.sh --os macos` (it says the Rosetta setup is
   unverified and goes on), and `arch -x86_64 tests/smoke/run.sh
   build/native64/pokeemerald-native out/open64`, "PASS: 27 dumps, 26
   screenshots". Probe M0 showed the layout under Rosetta 2 (61 of 61
   checks, 100 of 100 starts); this would be the first time the game itself
   runs there, its divisions by zero included (`FPE_INTDIV` with the DIV at
   the program counter, which `test_arm_division*` and `test_host_fault*`
   check).

What the first run is most likely to show, from what the probes did not
cover, the likeliest first: whether ld-prime takes `-alias_list` (the probe
passed each alias as `-alias`). If it refuses the files (an "unknown option"
or a symbol it cannot find), the fallback is the same pairs as flags: in
`platform/target.mk` the two `-Wl,-alias_list,FILE` become one
`-Wl,-alias,'NAME',ALIAS` per non-comment line of each `.aliases` file,
generated with `sed`, the `$` of the `section$start$` names doubled for make
and the word single-quoted for the shell; the files stay the single source.
Then: whether the sanitized suites compare clean, since a runner's result
line can be cut by another runner's AddressSanitizer report on the shared
stderr (`platform/test_runner/SUITE-LOG.md`, "The sanitized run at 64 bits";
the clang rehearsal's sanitized suite on Linux lost one line of 5,884 so,
"Autotomize cannot decrease weight below 0.1kg" read as "Shadow bytes ...");
a compare that fails on such a line alone is that, not a difference. And: whether `sdl2-compat` honours
`SDL_AUDIODRIVER` and `SDL_VIDEODRIVER` set after the library is loaded
(`test_audio`'s and `test_video`'s refused-driver tests, and the dummy
drivers the gates ask for); ThreadSanitizer's own memory layout beside the
fixed image; and `ld -r` joining a test object's C and its converted scripts.

## 2026-10-15: Native Apple silicon: the spike's findings

**The answer.** A sliding build for arm64 macOS is feasible without editing any
of expansion's headers (or any other expansion file beyond what the macOS x86_64 port
already carries), but not by redefining addresses in headers: the regions can
never be at the GBA addresses on arm64, so the build treats every 32-bit
address in the game as an offset in one window at 4 GiB or above that holds
the regions and the image at their Linux addresses, rewrites expansion's
compiled code at build time with two LLVM IR filters (integer/pointer
conversions; division), both prototyped and measured here, and links the
window as one image, which three runner probes must confirm; it costs about 7
to 9 tasks (about 1,500 new lines on top of the macOS x86_64 work), and the recommendation is
the Linux half first, as a bounded experiment with a stop gate, and the arm64
half only if macOS is to stay supported after 2027. The mechanism is proven
here on expansion's own headers and idioms and on the compiled IR of every
src/*.c, not yet on the whole running game, which is the first step of the
plan.

Everything below was measured on this machine on 2026-10-15 unless it names a
run. The probes are in commits 7a968e0964, 4e4399105a and b55d33ef3b on the branch work/spike (not merged), in a directory `arm64_spike/` with their
outputs in `results/`; they are not part of this tree. No runner was available, so
the arm64 macOS facts come from probe M0's runs and from XNU's source.

To reproduce, from the repository root after `make -f Makefile.native -j16
native BITS=64`, with `P` the probes' directory `arm64_spike/` and S a scratch directory:

    $P/build_div.sh && python3 -I $P/compare_div.py            # question 3
    $P/divfilter/run_test.sh                                    # division filter test
    ls src/*.c | xargs -n 4 -P 16 $P/divfilter/expansion_ir.sh  # every file's IR
    python3 -I $P/divfilter/count_casts.py $P/divfilter/out/expansion/*.x86_64.ll
    $P/rebase/run.sh && $P/rebase/all_rebase.sh                 # the window model
    $P/syntax_all.sh $S/casts -fno-pie -Wpointer-to-int-cast -Wint-to-pointer-cast
    PRE_IQUOTE=$P/wrap_symbols $P/syntax_all.sh $S/sym -fPIE -Wno-int-to-pointer-cast
    PRE_IQUOTE=$P/wrap_variable $P/syntax_all.sh $S/var -fPIE -Wno-int-to-pointer-cast
    (CC_COMPILE=clang on the last two for clang's errors)
    (cd build/native64 && python3 -I ../../$P/classify_words.py .)
    $P/build_bench.sh && $P/layout/compare.sh

**Question 1: can the regions be at their GBA addresses? No; settled without a
runner.** XNU's source (apple-oss-distributions/xnu, main at f6217f891a,
2025-10-04):

- `bsd/kern/mach_loader.c:897-904`, in `load_machfile`: "64 bit ARM binary
  must have 'hard page zero' of 4GB to cover the lower 32 bit address space";
  an arm64 executable whose map's minimum address is below 0x100000000 is
  refused (`LOAD_BADMACHO`). The only waivers are for 32-bit address spaces:
  an `#if __x86_64__` block (`:844-851`) and a 4 KiB-compatibility branch
  (`:874-893`). Neither applies to a 64-bit arm64 game. The test names
  `CPU_TYPE_ARM64`, which is why the x86_64 build keeps the GBA addresses under
  Rosetta (M0: 61 of 61 checks, 100 of 100 starts).
- `mach_loader.c:2368-2419`, in `load_segment`: the page zero is made
  "completely off limits by raising the VM map's minimum offset", and
  `vm_map_raise_min_offset` (`osfmk/vm/vm_map.c:22014-22036`) never lowers it.
- Every fixed-address allocation goes through `vm_map_locate_space_fixed`,
  which returns `KERN_INVALID_ADDRESS` below the minimum (`vm_map.c:2549-2553`).
  That covers `vm_map_enter` (`:3224`: `mmap` with `MAP_FIXED`, and
  `mach_vm_allocate` and `mach_vm_map` with `VM_FLAGS_FIXED`) and
  `vm_map_remap` (`:19782`: `mach_vm_remap`). No mechanism maps 0x02000000.
- posix_spawn's `_POSIX_SPAWN_DISABLE_ASLR` (`bsd/sys/spawn.h:62`) only skips
  the random slide (`mach_loader.c:788-799`).
- A page zero "of 4 GiB minus the regions" cannot exist: a page zero is one
  segment from address 0, and any that ends below 4 GiB is the refusal above.
- M0 agrees (run 37922990222, job log of macos-latest): `MAP_FIXED` at
  0x02000000 and 0x04000000 fails with ENOMEM under the default page zero
  (A:302-306). A binary linked with `-pagezero_size 0x1000` (rounded up to
  0x4000, A:308) is killed at start, status 137, in 100 of 100 runs (A:412).
  In the second iteration (runs 37923542701 and 37925659422 of the loader probes) every arm64 fixed-address layout link was
  refused ("-no_pie ignored for arm64"; the debug page's 0x04FFF000 is not on a
  16 KiB page), and `mapfixed` succeeded in 0 of 100 runs.
- What the same source allows: the same layout 4 GiB higher. That is the
  design below, and probes A1 and A2 test it.
- Size for the port: nothing to build. This answer fixes the design, a window
  base B of 2^32 or above.

**Question 2: can the code reach the data from a slid image?** Reaching is not
the problem. arm64 code addresses data relative to the program counter, and
dyld rebases 8-byte pointers. The problem is every place that keeps an address
in 32 bits or uses an integer as a GBA address. Counted from the sources and
the 64-bit build (`make -f Makefile.native native BITS=64` at cee043d4c5):

| What | Count (source) | Absorbed without touching expansion's headers? |
|---|---|---|
| Fixed addresses in headers | 11 macros in 3 headers: `REG_BASE` (`io_reg.h:4`, on which its 151 `REG_ADDR_` and 136 `REG_` macros are built); `SOUND_INFO_PTR`, `INTR_CHECK`, `INTR_VECTOR`, `ROM_START`, `EWRAM_START`, `IWRAM_START`, `PLTT`, `VRAM`, `OAM` (`defines.h:38-88`); `FLASH_BASE` (`flash_internal.h:4`). Uses: I/O registers 609 in src/*.c (28 `REG_ADDR_`, 581 `REG_`); in src/ and include/ outside the two defining headers (grep -ow), the VRAM forms 421, `OAM` 44, the palette forms 56, `FLASH_BASE` 11, `SOUND_INFO_PTR` 10, `EWRAM_START` 7, `INTR_CHECK` 4, `INTR_VECTOR` 3 | Yes, measured two ways. (a) Wrapper headers searched before `include/` (`#include_next`, then `#undef` and `#define`), with each region a link-time symbol: gcc compiles all 384 src/*.c with `-fPIE` and 0 errors. clang rejects 8 case labels (`scanline_effect.c:139-160`), because to clang a symbol minus a symbol is not an integer constant expression. With a run-time base variable instead, gcc fails 40 static initializers in 8 files, so a variable base cannot be absorbed by headers (`results/wrapper_errors.txt`). (b) The IR filter leaves the macros integers, which are the offsets, and adds B where an integer becomes a pointer: 1,137 constant conversions in 105 files. (b) is recommended: no wrapper, and it works under clang |
| GBA addresses written out in .c files | 6, `src/libisagbprn.c:19-26` (the debug print ports) | (b) yes |
| 4-byte address words in the data | 41,946 `R_X86_64_32` (in build/native64: `find data sound -name '*.o' \| xargs readelf -rW \| grep -c 'R_X86_64_32 '`). By target (`classify_words.py`): 20,570 script_data (the bytecode), 17,602 .rodata, 3,753 .text (58 of them carry the script tag), 21 EWRAM variables | Not by a header. By the data converter (platform/build_glue, not expansion), which writes each word as `target - window`, resolved at static link and the same at any slide. clang assembles this for arm64 Mach-O as `ARM64_RELOC_SUBTRACTOR` plus `ARM64_RELOC_UNSIGNED`, 4 bytes, unaligned and tagged words included (`delta32.s`). Whether ld64 links it is probe A3. Each word's value is unchanged: today's Linux address |
| 8-byte words in the data | 19,208 `R_X86_64_64` | ordinary rebases; nothing to do |
| 16-bit words | 8 `R_X86_64_16`, all enum constants (`B_MSG_MON_*`, `SPECIAL_*`) | not addresses |
| The bit-25 script tag | 5 test or strip sites (`include/script.h`, `src/script.c`, through `platform/script_tag.h`); 58 tagged words | unchanged: the tag is a bit of the offset, and code stays at 0x40000000-0x42000000 of the window |
| Integer/pointer conversions in expansion's C | gcc: `-Wpointer-to-int-cast` 149 warnings at 141 lines in 50 files, `-Wint-to-pointer-cast` 429 at 365 lines in 72 files (`results/casts_gcc.txt`). In clang's IR before optimisation: `ptrtoint` to i32 117, to i16 30, to i8 1, to i64 339 (and 11 in initializers); `inttoptr` 638 (from zext i32 123, sext i32 227, other i64 288) and 1,137 of constants (`results/ir_casts.txt`, `results/rebase_all.txt`) | Not by headers: these are casts in .c files (`T1_READ_PTR` and `T2_READ_PTR`, `global.h:115-122`, expand to them). By the IR filter, measured below |

The IR filter's rule (`rebase/rebasefilter.py`, 90 lines):

- An integer made from a pointer in the window is its offset.
- An integer from 1 to 2^32-1 made into a pointer becomes B plus it.
- 0 stays null.
- Values of 2^32 and up pass unchanged. No valid pointer on arm64 macOS is
  below 2^32, so the two never collide, and stack and heap pointers survive a
  round trip through `uintptr_t`.

With B = 2^32 a pointer's low 32 bits are already its offset, so the
truncating casts need no rewrite.

Measured on Linux (`rebase/run.sh`). `window_test.c` is compiled with
expansion's unmodified `global.h`, `io_reg.h` and `defines.h` and with
`platform/script_tag.h`. It is linked with the image at 0x140000000 and EWRAM
and I/O sections at 0x102000000 and 0x104000000 (B = 2^32). Through the filter,
14 of 14 checks pass:

- `T1_READ_PTR` and `T2_READ_PTR` of unaligned window words;
- a tagged native pointer tested, stripped and called;
- expansion's EWRAM range test;
- a pointer kept in two s16 task fields (`src/task.c:177-192`);
- `REG_BG0HOFS` reaching the I/O section;
- `BG_VRAM` arithmetic;
- stack and heap pointers through `uintptr_t`;
- a null word.

The same program built without the filter fails the first pointer read and
dies calling the function (SIGSEGV, status 139).

Over every src/*.c (`rebase/all_rebase.sh`), the filtered IR compiles at -O2
for x86_64 and for aarch64 in 765 of 767 cases. Both failures are libisagbprn.c's
inline `.hword 0xEFFF`, which the macOS x86_64 port's `c_filter_macho.sed` already
rewrites. clang makes no aarch64 IR for `src/script.c`: `__builtin_setjmp` (633,
677, 714) is unsupported on AArch64. That needs a C-filter rule to `setjmp` with
a `jmp_buf` (the buffer is `intptr_t breakTo[5]`, `script.c:611`), not a header.

The struct layouts are not a cost. clang's record layouts of everything
reachable from `global.h`, `global.fieldmap.h`, `gba/m4a_internal.h` and
`save.h` are identical for arm64-apple-macos11 and x86_64-linux-gnu. The only
differences are the compiler's `va_list` and libc's own records
(`layout/compare.sh`, `results/layout_compare.txt`), so the save blocks keep
their 64-bit Linux layout.

The representative case (`iobench.c`; `results/iobench.txt`,
`results/build_bench.txt`): four I/O writes and a read per iteration, 2x10^8
iterations, native x86_64, gcc -O2, PIE:

| Form | ns per iteration | AArch64 code (clang -O2 -fPIE) |
|---|---|---|
| fixed address | 0.554-0.562 | 88 bytes |
| link-time symbol | 0.575-0.577 | 88 bytes |
| run-time base | 0.575-0.576 | 112 bytes |

The base is loaded once per function. The 3 to 4% is on a loop that does
nothing else.

On real files (.text bytes), the fixed form against the symbol form:

| File | x86_64 | aarch64 |
|---|---|---|
| `gpu_regs.c` | 1,093 → 1,069 | 1,560 → 1,508 |
| `dma3_manager.c` | 1,449 → 1,509 | 1,048 → 1,048 |
| `scanline_effect.c` | 1,510 → 1,542 | not compiled (the case labels above) |

With B constant (variant W-b below) constant addresses fold and cost nothing.
With a slid B each costs a load of B. The game-level cost of the filter's
selects is not measured.

Size: the rebase filter made build glue with tests, about 400 lines; the
converter's delta form and its checker, about 150; the platform's own boundary,
where it turns GBA integers into pointers (67 `(uintptr_t)` casts in 16 of 83
platform .c files; 24 lines name 0x02000000-0x07000000, 0x0E000000, 0x40000000
or 0x42000000: DMA, the interrupt
vector, flash, crash_reboot's 0x03007F00, transfer_bounds, inspect), about 200.

**Question 3: what replaces the divide trap?** Measured under qemu-aarch64 8.2.2
(`divprobe.c`, freestanding, raw syscalls; `results/divprobe.txt`):

- SDIV or UDIV by zero gives a quotient of 0. The MSUB that clang emits for `%`
  (`divshape.c`) then gives the dividend as the remainder.
- INT_MIN / -1 gives INT_MIN, remainder 0, at 32 and at 64 bits.
- No trap. This is the architecture's definition, not qemu's choice: in the Arm
  ARM's (DDI 0487) A64 SDIV/UDIV pseudocode a zero divisor gives a zero result,
  and the INT_MIN / -1 quotient is the low bits of +2^31.

Against the GBA build's libgcc record (`platform/arm_division_libgcc/libgcc_division.txt`,
apcs-gnu, the routines expansion's code calls), 6 rows are equal and 22 differ
(`compare_div.py`): every n / 0 with n not 0 at both widths, and 0 / 0 at 64
bits (libgcc gives 1). INT_MIN / -1 matches at both widths. So arm64's own
result does not match, and something must replace the trap.

The BIOS `Div` is unaffected: `platform/bios_arith.c` already tests for a zero
divisor in C (1 call in src, `librfu_rfu.c:470`).

- Rejected, measured: clang's `-fsanitize=integer-divide-by-zero -fsanitize-trap`
  places one BRK per function after all of its checks (`divshape.c`, function
  `two`: both divisors tested, one shared brk). A SIGTRAP handler cannot tell
  which division faulted or where to resume.
- Recommended, measured: the division IR filter (`divfilter/divfilter.py`, 86
  lines; `pen_div.c`, 90). On IR before optimisation, so the optimiser never
  sees an undefined division, every sdiv, udiv, srem and urem by a
  non-constant divisor becomes a call to a helper with libgcc's results.
  Divisions by constants other than 0 and -1 are kept.
  - All 193 rows of `libgcc_division.txt` equal, with no trap and no handler;
    the unfiltered control dies of SIGFPE, status 136
    (`results/divfilter_test.txt`).
  - Over every src/*.c: 424 divisions rewritten in 106 files (sdiv i32 214,
    srem i32 107, udiv i32 87, urem i32 13, i64 3), 2,348 kept, 0 refused.
  - Hardware divisions in the aarch64 objects: 466 before, 1 after. That one,
    in `AnimTask_GlareEyeDots`, divides by plus or minus 4, which the optimiser
    built from two kept `/ 4`.
  - .text grows by 460 bytes over all the aarch64 objects
    (`results/divfilter_expansion.txt`).
- Size: the same build step as the rebase filter (one tool, two rewrites),
  about 100 lines more with tests. `arm_division.c` stays for the x86 builds.

**Cost.**

- P0: four runner probes, one workflow job, under a minute each on
  macos-latest.
- P1 (Linux, no runner), 3 to 4 tasks: the window model on Linux x86_64 with
  B = 2^32, the twin of the arm64 layout. It covers the filters as build glue,
  expansion's C through clang, the words as offsets, the linker scripts 2^32
  higher and the platform's boundary, with every Linux gate run in that mode.
- P2 (arm64 macOS), 3 to 4 tasks: the Mach-O arm64 link, the arm64 host
  pieces (fault context for crash_reboot, an AArch64 stack switch in place of
  the macOS x86_64 port's 154-line `host_macos_stack.c`), SDL2, a macos-latest CI
  job, packaging.
- Total: 7 to 9 tasks, about 1,500 new lines on top of the macOS x86_64 port (which at
  c8cd418835 is itself 5,813 added lines in 72 files, `git diff --shortstat
  main...work/macos` at the time). No expansion file changes; the two clang cases above are
  C-filter rules.
- What could raise it:
  - A pointer moved through memory as a 32-bit integer (a type pun) is
    invisible to the filter, which sees conversions and not stores. P1's gates
    find any.
  - The filters' game-level speed.
  - ld64 (A1 to A3).

**Needs a runner.** One job each on macos-latest, under a minute.

- A1: M0's layout probe linked for arm64 as a PIE, every segment address plus
  0x1_0000_0000 and the debug segment at 0x1_04FF_C000. Does ld64 accept
  `-segaddr` in an arm64 PIE, and over 100 runs is every region at its link
  address plus one common slide? If yes, variant W-a: the window is one slid
  image and B is the slide plus 2^32.
- A2: the same binary started through posix_spawn with
  `_POSIX_SPAWN_DISABLE_ASLR`, with and without the hardened runtime. Is the
  slide 0 in 100 of 100? If yes, variant W-b: a launcher, B = 2^32 constant,
  the cheapest code.
- A3: `delta32.s` linked by ld64 into A1's binary, with every word read back as
  target minus window, unaligned and tagged words included.
- A4 (completeness; the source settles it): `mach_vm_allocate` and
  `mach_vm_map` with `VM_FLAGS_FIXED` at 0x02000000 (expected
  `KERN_INVALID_ADDRESS`), and a binary with `-pagezero_size 0xFE000000`
  (expected killed, as M0's was).

If A1 and A2 both fail, the regions can still be reserved at run time (mmap is
free above 4 GiB), but nothing puts the image at B + 0x40000000. Code and data
addresses would then not be window offsets, and the port stops there.

**Recommendation.** P1 first, well before the
2027 horizons ("macOS CI runs on macos-15-intel only" above). It needs no
runner and no Apple hardware, and it decides the expansion side: if the replays
and the suite pass with B = 2^32 on Linux, the code side is done. P0 when a macOS runner
is available (minutes). P2 only if macOS is to stay supported and tested after 2027.
Otherwise stop here, and ship x86_64 under Rosetta until macOS 27.

**Open questions.**

1. Whether macOS should stay supported after 2027. If not, nothing more is done.
2. Whether the arm64 build may rewrite expansion's compiled code instead of
   editing it (decided: it may, "The arm64 build rewrites expansion's compiled code, on two conditions"). No expansion file changes, but at build time the meaning of
   about 2,300 integer/pointer conversions and 424 divisions changes. The
   alternative is about 500 guarded source edits, against the modification
   policy's preference.
3. W-a (keeps ASLR) or W-b (the game re-spawned without ASLR), once A1 and A2
   have run.


**Not settled in the time.** The whole game in the window model (P1). The
filters' cost in frame time. Everything that needs ld64 or the arm64 loader
(A1 to A4). The hardened runtime's view of W-b. Whether Apple clang's IR text
(a different LLVM than 18) parses with the filters' patterns: they refuse what
they do not recognise, so a mismatch is a build failure and not a wrong game.
The platform layer and SDL2 built for arm64 (there is no aarch64 sysroot
here).

## 2026-10-15: Fast-forward focus loss, the 65536 Hz audio grid, the macOS signature and the arm64 rewrite

Four decisions of 2026-10-15.

1. **Fast-forward's toggle ends on focus loss**, as the hold does: a game
   toggled to fast-forward and left in the background would otherwise run
   at full speed unseen (at `unlimited`, a busy core; on a Deck, the
   battery). The first specification named only the hold; this extends it.
   Not implemented yet.
2. **The audio grid moves to 65536 Hz**, the rate Emerald itself selects
   (SOUNDBIAS mode 1 in `SoundInit`, the engine's "PWM rate = 65536 Hz"),
   superseding the 32768 Hz of decision (1) of "Decisions on an independent
   review's open questions", with an A/B recording against the current
   build as the evidence. Not implemented yet.
3. **The macOS package keeps its ad-hoc signature** (`codesign -s -`, no
   identity, no account): the library path rewrite breaks the bundled
   libraries' signatures and an ad-hoc one keeps them loadable; Gatekeeper
   treats the archive as unsigned all the same, and no Developer ID or
   notarization is used.
4. **The Apple-silicon build may rewrite expansion's compiled code at build
   time** (the spike's two IR filters) instead of some 500 guarded source
   edits, with two conditions: the entry below, "The arm64 build rewrites
   expansion's compiled code, on two conditions", is the decision in full.
   No Apple-silicon work is scheduled.

## 2026-10-15: The arm64 build rewrites expansion's compiled code, on two conditions

The decision on question 2 of the Apple-silicon spike ("Native Apple
silicon: the spike's findings", "Open questions"): the arm64
build may rewrite expansion's compiled code at build time with the two IR
filters the spike prototyped, the conversion filter (an integer made into
a pointer gets the window base added, a pointer made into an integer
becomes its offset) and the division filter (every division and remainder
by a non-constant divisor calls a helper that gives libgcc's results), in
place of guarded source edits. The reasons are the spike's: expansion's
files stay untouched, so an upgrade stays the runbook in UPGRADING.md; the
data converter already rewrites expansion's data words the same way; the
filters are small, tested build glue that refuse what they do not
recognise, so a compiler change is a build failure and never a wrong game;
and the division results must be rewritten regardless, since arm64 has no
divide trap and its results for a zero divisor differ from libgcc's.

Two conditions are part of the decision.

First, the shipping Linux and Windows builds stay on the fixed-address
memory model exactly as they are. The window-mode build on Linux with the
base at 2^32 (P1 of the spike's recommendation) is a gate for the arm64 work and a twin of the
arm64 layout, never a replacement for the build players run or for the
per-push and nightly gates that exist. No fixed-address code path is
removed or made conditional in service of the window model; code the two
models share is factored so that the fixed-address build is unaffected, and
each model is covered by gates of its own.

Second, guarded edits to expansion's files (the PLATFORM_NATIVE policy,
`#line` preserved, a row each in UPSTREAM_MODIFICATIONS.md) stay permitted
for the arm64 work only for what the filters structurally cannot see: a
pointer stored through memory as a 32-bit integer, or an equivalent type
pun, that a P1 gate exposes. Nothing else: no edit to avoid a filter rule,
to work around a filter limitation that could be fixed in the filter, or to
handle a pattern the filter could recognise. Each such edit's row names the
gate failure that required it.

Where they live: the filters go in platform/build_glue beside the data
converter, each with its own tests and a rewrite rule documented in
ARCHITECTURE.md for a reader who has never seen LLVM IR; the division
helper's results are tested against
platform/arm_division_libgcc/libgcc_division.txt in full, every row.

This starts nothing. No Apple-silicon work is scheduled; the decision fixes only how P1 is built if it is ever
scheduled.

## 2026-10-15: No workflow uploads the game

The repository goes public soon, as source only. On a public repository
every workflow artifact is downloadable by any logged-in GitHub user, and a
built executable (the game, the test runner, any archive of either)
contains the complete game's assets, which this project never distributes
(README; "Why no public release" above). The rule: **no workflow
uploads the game or any part of it, at any retention, for any length of
time.** Game-bearing: the game, the test runner, any archive of either, any
save, data file or output directory containing expansion's assets or a
binary (an executable). Not game-bearing: logs, suite results, text
outputs, probe output with no game content, and (an adjustment the
same day) the frame images and the save and clock files of the replay,
smoke and save gates: a frame image and a save are not a playable build, and
they are how a red replay or save gate is diagnosed. (The first design of this rule,
the same day, kept the builds between jobs for one day and deleted them in
a last job; it was superseded before it ran, because a day is still a day
on a public page. Nothing of it remains but the release path below.)

**The audit.** Every `actions/upload-artifact` step of the workflows of this
project, what its `path` matched, and what was done. Upstream's build.yml
and labels.yml upload nothing; docs.yml's only upload is
`actions/upload-pages-artifact` of `docs/book` (upstream's mdBook output of
the documentation, no game; it is filtered to upstream's branches and does
not run here). The smoke scripts (tests/smoke/*.sh, lib.sh, save_steps.sh)
write into `out/` only what the game's `--out`, `--save` and
`--rtc-offset` write and what the scripts derive from it: dumps (`.txt`),
screenshots (`.ppm`), `expanded.replay`, saves (`.sav`), clock files
(`.rtc`), the game's `stdout.log`/`stderr.log`, sanitizer lists (`.all`,
`.known-patterns`) and `.diff` files. None copies or links an executable, a
ROM, converted assets or a symbol map there; the Windows smoke works in
`mktemp -d`, outside `out/`. A save holds the replay's game state (the
player, the party, the map), not expansion's assets, which live in the
executable, and a screenshot is a 240x160 frame of a scene. The gates write
the frames as PPM (`.ppm`; no gate writes PNG, but `.png` is allowed too).
So the failure uploads carry text and logs, frame images and the save and
clock files of the save gates, by an explicit list of file patterns, and
never an executable, shared library, object, archive or any other binary.

| Workflow, artifact | What `path` matched | Game-bearing | Disposition |
| --- | --- | --- | --- |
| native.yml `replay-outputs-*` (6 steps, failure only) | `out/**/*.EXT` for txt, log, ppm, sav, rtc, diff, replay, all, known-patterns | no | kept, one pattern per line; `.png` added to the list |
| windows.yml `windows-build` | the Windows game `.exe`, `.symbols`, `SDL2.dll`, the 70 unit-test `.exe`s, the Linux save directory | yes | removed: the Windows job builds them itself, in WSL |
| windows.yml `linux-build` | the Linux game and its `.symbols` | yes | removed: the Windows job builds the Linux game in WSL |
| windows.yml `windows-suite-build` | the Windows test runner `.exe` and `SDL2.dll` | yes | removed: built in the Windows job |
| windows.yml `suite-linux64` | `suite-linux64.log` | no | kept (text, the comparison needs it) |
| windows.yml `windows-results` | `out/suite-windows.log`, and the whole `again/` directory | log: no; the rest: dumps, screenshots, saves | only `out/suite-windows.log`; the stage's saves no longer travel (below) |
| windows.yml `cross-os-save-outputs`, `compare-outputs`, `windows-logs-before-the-suite` | `out/` by extension | no | removed with their jobs; the Windows job's own `windows-logs` (failure only) covers them |
| windows.yml `windows-logs` (failure only) | the unit tests' `*.log` and `out/**/*.EXT` | no | the same patterns as native.yml's, one per line |
| release.yml `linux-archive`, `windows-archive`, `release-files` | the tarball, the zip, the sums | yes | uploaded only with `publish` (the explicit input; a run without it uploads nothing, checked below), one day, deleted by `cleanup`, which now runs only with `publish`; the release is the publication |
| the loader-probe workflow (removed from the tree afterwards) | its `out/*.txt` | no | unchanged: reports and run logs are text; the probes' executables in the same `out/` do not match |

**The Windows job builds its own game.** windows.yml is now three jobs:
`linux-suite` (unchanged; its log is uploaded), `windows` and `compare`
(downloads the two suite logs). The Windows job does everything the three
Linux jobs and the Windows job did, in one place. The Windows build is a
cross-build whose data path needs GNU as for ELF and the converter
("The data for Windows and macOS is assembled on Linux and converted"),
which Windows does not have, so the job runs the same make commands as the
Linux cross-build (docs/BUILDING.md, "Windows (cross-build from Linux)") in
a WSL distribution on the Windows runner. The choice, and why:

- **WSL, not MSYS2.** The alternative is MSYS2 with GNU binutils for
  x86_64-elf built from source and cached: a toolchain build that is itself
  untested here, a second environment for the build next to the second
  environment for the suite (MSYS2's, already there), and a data path (GNU
  as in MSYS2, clang's COFF check, the converter's Python) that has never
  run there. WSL runs the commands of docs/BUILDING.md unchanged on the
  distribution the Linux jobs use (Ubuntu 24.04), so what differs from the
  Linux jobs is the environment, not the build.
- **The action.** `Vampire/setup-wsl`, pinned to the full commit of v7.2.0
  (`4c48163d9f981914dd8caa0fc335fe19cca5bff4`, node24; the tag object is
  `e707f0b61a07f8ea48a64d83edfcbd4a7af45926`, and a `uses:` pin names the commit
  the tag points to), distribution `Ubuntu-24.04` named explicitly because v7
  changed the default.
  Its README says WSL 1 has been enabled on the Windows images since
  windows-2019 and WSL 2 is also available since windows-2022 (its default is
  2).
- **WSL 2, explicitly, and no fallback.** The job sets
  `wsl-version: 2` and a step fails the job unless the kernel is WSL 2's
  (`microsoft-standard-WSL2` in `/proc/sys/kernel/osrelease`). It never
  falls back to WSL 1: the fixed-address Linux game (regions mapped with
  `MAP_FIXED` below 4 GiB, a restart through `/proc/self/exe`) must not be
  judged under WSL 1's syscall translation layer, which would say nothing
  about Linux. If a runner has no WSL 2 the job is red and says so.
- **The Linux file system only.** The checkout itself
  happens in WSL: `git init`, then a fetch of exactly `${{ github.sha }}`
  (depth 1) into `~/work` with the job's token (`contents: read`, passed
  through `WSLENV` and sent as an HTTP header for that one command with
  `git -c http.extraHeader=...`, never written into `.git/config`), and a
  check that HEAD is that commit. The build and every Linux run happen
  there, never under `/mnt` (9p: slow for 28,000 files, and not Linux file
  semantics, which the fixed-address game and its save locks should not be
  judged on). The Windows checkout serves the Windows steps only, and the
  Windows executables are copied out to it only to be run.
- **The commands.** apt, as root in WSL: `apt-get install -y
  --no-install-recommends build-essential gcc-multilib libc6-dev-i386
  pkg-config git python3 libpng-dev libsdl2-dev clang ccache
  gcc-mingw-w64-x86-64 binutils-mingw-w64-x86-64 mingw-w64-tools curl
  ca-certificates` (the Linux cross-build's list; curl for the pinned SDL2
  fetch). Then, in `~/work` (WSL's own file system): `make -f Makefile.native -j"$(nproc)" native
  BITS=64`; `tests/smoke/run_cross_os.sh save ...`; the same with
  `TARGET_OS=windows`; `make -C platform -j"$(nproc)" test TARGET_OS=windows
  BITS=64`; `make -C platform tools-test TARGET_OS=windows BITS=64`; `...
  TARGET_OS=windows TEST=1`. The tree is the fetch above. The `.exe`s and
  `SDL2.dll` are copied to the Windows file system of the same job, where
  the Windows steps (Git Bash, pwsh, MSYS2) run unchanged.
- **Time.** The Windows run is expected to grow from about 12 to about
  30 minutes, which is accepted: the three Linux builds (the Linux game, the
  Windows game, the Windows test runner; 7, 8 and 8.5 minutes warm on
  Linux runners) now run one after another in the Windows job, with the WSL
  install (about 1 to 2 minutes with the action's cache) and the apt
  installs on top, and the Windows steps (8 minutes) after them. The job's
  limit is 60 minutes. Windows minutes count double.
- **The cross-OS save gate in one job.** Gate h's three stages run in the
  one job: the Linux game in WSL writes the save, the Windows game on
  Windows continues it and saves again, the Linux game in WSL continues
  that. Nothing is passed between jobs: the saves and clock files stay in
  the workspace and in WSL's home, and the only things that leave the job
  are text. No `.sav` or `.rtc` is uploaded by any workflow. What is not
  known until it runs: whether the fixed-address Linux executable runs in
  WSL (its regions mapped with `MAP_FIXED` below 4 GiB, `/proc/self/exe`
  for the restart), which a WSL 2 kernel should allow and WSL 1 may not;
  if gate h's first stage fails there, that is the finding, and the stages
  can run on a Linux job again only by passing the two save files (the
  `.sav` and `.rtc`, named one by one, with text) between jobs, which this
  rule allows and this design does not need.

**Caches.** Caches may hold compiled objects, converted
assets and ccache data in every workflow, the Windows one included, and
never a linked game, a test runner, or an archive of either. native.yml's
caches stay as they are (the tools, ccache, the converted assets); the
Windows job gets the same kind (ccache, the converted assets, SDL2 for
MinGW), in the workspace, copied into WSL before the builds and back after.
The reason: a cache is not downloadable by the public; the converted assets
derive from source files that are public in the repository anyway; and the
legal line is a playable build, not its intermediate pieces. The one path to
a cache is a pull-request workflow run from a fork, which the repository's
setting "Require approval for all external contributors" closes; it
is set before the repository goes public. The Windows job caches no tools
(they are built in WSL's home each run, about a minute) so that no tool
built for one environment is restored into the other.

**The check.** `platform/build_glue/check_workflow_uploads.py` reads every
workflow under `.github/workflows` and every composite action under
`.github/actions`, and fails on any `upload-artifact` (or
upload-pages-artifact) path line that is an exclusion (a denylist), has a
brace or a `${{ }}`, is a directory or has no extension, has a wildcard in
its extension, or has an extension that is not on the allowed list: text and logs (`.log`,
`.txt`, `.diff`, `.replay`, `.all`, `.known-patterns`), frame images (`.ppm`,
which the gates write, and `.png`), and the save gates' `.sav` and `.rtc`.
Executables, shared libraries, objects, archives and every other binary
fail, as do directories and names without an extension. The exceptions are exact: release.yml's
`.tar.gz` and `.zip` in a step or a job whose `if` requires `inputs.publish`
(the condition is parsed, so `!inputs.publish`, `inputs.publish || ...` and
`== false` do not count), and upstream's docs.yml upload of `docs/book`.
`--self-test` runs good and bad workflows: each allowed class (text, log,
frame image, save, clock) passes, and each rejected class fails (executable,
shared library, object, archive, other binary, directory, no extension, and
the pattern rules above), one case per extension. Beyond the path lines it fails any action whose
name (compared lowercased, so `Actions/Upload-Artifact` is seen) says
upload, release, publish, deploy, asset or artifact and is not on its short
list of known ones (checkout, cache, download-artifact,
attest-build-provenance, configure-pages, deploy-pages, setup-wsl and
upstream's two label actions); `docker://` steps; external reusable
workflows (job or step); and, in any `run:` text, `gh release
upload/edit`, `gh api` writes and uploads, `curl` with an upload flag, `git
push`, `docker push` and the like. The one command allowed is release.yml's
`gh release create`, in a step or job whose `if` requires `inputs.publish`.
A third-party action whose name says none of those words is not caught; the
list of `uses:` in the tree is short and read at review. `make -C platform
glue-test` runs the self-test and then the check on the real tree, so the
platform gate and the local pre-merge gate fail on a bad upload; it also
runs as a step of native.yml's per-push
job, which installs python3-yaml for it. The rule is stated at the top of
each workflow, pointing here and naming the check.

**release.yml.** Without `publish` (the default) a run builds and checks
each archive in its own job and uploads nothing: the contents listed and
compared with the seven (Linux) or six (Windows) files each must hold, the
Linux executable started headless from the unpacked tarball through its
launcher, the Windows executable's PE format checked, and the SHA-256 of
each archive printed in the log. With `publish` the archives go to the jobs
after them as one-day artifacts, `checksums` makes `SHA256SUMS.txt` and the
attestations, `publish` creates the release, and `cleanup` deletes the
artifacts. `cleanup` and `.github/actions/delete-run-artifacts` stay for
this path only (the upload that remains is the one that is the
publication's means); windows.yml's cleanup job is gone, since it has
nothing to delete. native.yml never had one.

**Not run.** None of this had run on a runner when it was written; it was checked by running the make
commands the Windows job runs (the Linux cross-build, the unit tests, the
TEST=1 build), the new check and its self-test, the platform gate, by
parsing each workflow as YAML, and by reading. The first runs must show, in
order: windows.yml's `windows` job installing WSL and the packages and
failing if it does not get WSL 2 (the kernel check), fetching the commit in
WSL, and building in about the expected time (about 30 minutes cold, less warm); gate
h's first stage passing in WSL (the Linux game starts there and writes its
save); every gate passing as before (unit tests, smoke, no console, replays,
gate h's second and last stages, the suite through the hydra, the
comparison); the converted assets and ccache restoring and saving under the
Windows job's keys; WSL 2's memory ceiling (half the runner's 16 GB: if the
OOM killer shows in the Linux builds, a `.wslconfig` memory line or a lower
`-j`); `setup-wsl@4c48163d...` resolving; the SDL2 fetch (`curl`) working from
inside WSL; native.yml's per-push step running the check with python3-yaml;
the copy of the executables out to the Windows workspace; no artifact on any run's page but text, frame images, saves and clocks
(`suite-linux64`, `windows-results`, and the failure outputs); and a release.yml run without
`publish` with no artifact at all and `cleanup` skipped. Also open until
then: that `WSLENV: GITHUB_WORKSPACE/p` gives the WSL steps the workspace,
that `cp --preserve=timestamps` and `tar` onto the Windows file system do
not trip on ownership, that the `again/` directory's save reaches the last
stage intact, that a run by hand and the nightly share the caches as
intended, and that the token's fetch of `${{ github.sha }}` works (a scheduled or manual run on a fork-less private or public repository; if the fetch of a bare SHA is refused, fetch the branch and check out the SHA).
