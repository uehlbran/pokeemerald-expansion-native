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
