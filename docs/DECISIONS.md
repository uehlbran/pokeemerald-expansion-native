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
