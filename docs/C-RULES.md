# C rules for the platform layer

These rules apply to everything this project writes: the platform layer
under `platform/`, build glue, and tests. Expansion's game code is
pret-style GBA C with its own conventions; we do not rewrite or re-lint
it. Workers receive this text verbatim with every task.

## Code quality, how the principles apply to C

This is a hardware shim in C, so apply the standards in their C form
rather than contorting the code into class-shaped patterns. One module per
hardware subsystem (video, audio mixer, DMA, timers and RTC, input, save
storage, BIOS calls) each with a small header that is its only public
interface, static for everything else; that is single responsibility and
dependency inversion here. The domain language is GBA hardware: name
things as GBATEK names them so the code reads like the spec, and put every
register address, timing, size and mode in a named constant with a comment
citing the GBATEK section; no magic numbers, no magic strings, no bare hex
in logic. No global mutable state outside the module that owns it; no god
files, so if a file is growing past a few hundred lines ask what subsystem
it is really two of. Functions do one thing and are readable without the
reference open. Where we touch expansion's own game code, which is the
real game domain, pragmatic domain-driven design applies properly: rich
data and behavior together, no anemic structs with logic scattered in
helpers, but no aggregate ceremony and no abstractions that exist only to
be abstract. Avoid code smells: duplicated hardware logic across modules,
long parameter lists, boolean flag parameters, dead code kept just in case,
commented-out code, clever macros where a function would do. Unnecessary
complexity is what kills software projects: if something works and is
clear, stop there; no layered protections for hypothetical problems.
Document every module with a short header comment saying what hardware it
stands in for, what the real hardware did, and which reference port or
GBATEK section it follows, with links.

## C safety and correctness, non-negotiable for everything we write

Compile the platform layer with the same GNU C standard expansion's modern
toolchain uses (gnu17, documented), with -Wall -Wextra -Wshadow
-Wstrict-prototypes -Wconversion and warnings as errors applied to our
files only; include expansion's headers as system headers (-isystem, or -idirafter where -isystem would shadow libc headers; see DECISIONS.md) so
their warnings are not ours to fix, and document that choice. Sanitizers:
every unit test binary runs under AddressSanitizer and
UndefinedBehaviorSanitizer with leak detection, unconditionally, and a
test run is not green with any sanitizer report; any leak-sanitizer
suppression needs a written reason in the repo. Use fixed-width types
(uint8_t, uint16_t, uint32_t, and expansion's u8/u16/u32 where
interfacing) for anything that models hardware; never int for a register.
Every allocation has one owner and its release lives in the same module,
with init and shutdown functions paired and shutdown safe to call on a
partially initialized module; every SDL resource is released on every
exit path, including error paths; prefer static or stack allocation and
fixed-size buffers sized by named constants over heap allocation where the
hardware itself had fixed sizes. Every buffer access is bounds-checked
against the named constant for that region; DMA and save code must reject
out-of-range source, destination and length rather than trust the caller.
No undefined behavior that the GBA code got away with: no unaligned access
through casts, no signed integer overflow, no type punning through pointer
casts (use memcpy), no reliance on evaluation order, no uninitialized
reads; UBSan enforces this. Const-correct pointers everywhere, no casting
away const. Check every return value from SDL and the C library and fail
loudly with a message naming the subsystem and the call; never swallow
errors; modules return error codes and never call exit(), main decides.
Use static assertions for every struct whose layout must match a hardware
or save format, and document the endianness and alignment assumptions.
32-bit and 64-bit cleanliness: no storing pointers in integers, no assuming
sizeof(long), use uintptr_t where unavoidable and flag it. Threading, if
the audio or main loop needs it, uses SDL's primitives with a documented
ownership model and no shared mutable state without a lock; prefer
single-threaded designs where the references prove they suffice.

## Concurrency, non-negotiable (owner's standing addition, 2026-10-14)

The platform is single-threaded by design: push-model audio, no callback
thread, one thread that runs the game and every module. Keep it that
way. Any new thread requires a DECISIONS.md entry justifying why a
single-threaded design cannot work, plus a written ownership model
naming which thread owns each piece of state.

Signal handlers, and their Windows replacements (structured or vectored
exception handlers), may only call async-signal-safe functions per
signal-safety(7); may share state with normal code only through
`volatile sig_atomic_t` or lock-free C11 atomics; and never allocate,
print through stdio, or take locks. `volatile` is never a
synchronization mechanism.

Shared state between threads uses C11 atomics with each memory order
justified in a comment, or an SDL mutex with a documented lock order.

The test-timeout replacement for SIGALRM must not let a second thread
touch game state: prefer a design where a timer only sets an atomic flag
the game thread checks, and if a thread is unavoidable it may do nothing
else.

Any change that introduces a thread adds a ThreadSanitizer build of the
affected unit tests and replays to the gate (64-bit, separate from the
AddressSanitizer build, since the two cannot combine).

Save writes are crash-safe: write to a temporary file, flush, then
rename (verified for the save file, the RTC offset and the replay
outputs on 2026-10-14; any new durable write follows `host_file.h`).

Reviewers explicitly check every diff for data races, signal safety,
lock ordering, fork or exec in a process that may have threads, and
file-operation races, and report what they checked.

## Code quality for a public repository (owner's standing addition, 2026-10-14)

People will judge this repository for being AI-built. Names read
clearly without the spec open. Comments explain why, never restate what
the code does; no generated-sounding filler ("This function handles..."),
no comment on every line. Small cohesive modules with narrow headers:
high cohesion, low coupling, no speculative abstraction. Leave everything
we own better than you found it on every touched call chain. Expansion's
own files stay untouched except where a hardware assumption forces it
(UPSTREAM_MODIFICATIONS.md).

Workers receive this file, the concurrency section included, verbatim in
every task; the first-pass reviewer checks each diff against it line by
line before it reaches the orchestrator.

## Testing, non-negotiable

Test-driven development for every piece of the platform layer that can
run without a window: the save container, DMA semantics, timer and RTC
behavior, input mapping, BIOS call implementations, the test-runner
protocol shim, and the audio mixer where it is testable in isolation. Most
of this code is adapted from the reference ports, and that does not exempt
it: the tests are written from the GBATEK specification before the
reference code is adapted, they fail against an empty module, and the
adapted port is made to pass them. No tests that are skipped, empty,
always pass, or only cover the happy path; edge cases from the start,
meaning boundary addresses, zero-length and maximum-length DMA,
wraparound, invalid inputs, corrupted or missing save data, RTC rollover,
and error-path cleanup. Rendering and audio cannot be unit tested; the
input-replay smoke test and the native run of expansion's suite are the
integration tests.

## Licensing rule

Do not copy code from the reference ports. Two of them credit GPL and MPL
emulators (VBA-M, NanoBoyAdvance, mGBA) as the source of their BIOS and
renderer code, and none of that can be relicensed under this project's
MIT license. Read them for structure and pitfalls; write ours from GBATEK
and from expansion's own sources.

## Style and static analysis

`.clang-format` at the repository root encodes the platform layer's style as
the code was written: 4-space indent, a function's opening brace on its own
line and every other brace on the line of its statement, 120 columns, the
pointer star on the name, `&&`, `||` and `?:` at the start of a continuation
line, comments never re-flowed and includes left in the order written; each
option beyond LLVM's defaults was kept because it shrank the one-time
reformat. It applies to whatever clang-format is run on, so enforcement is by
file list: `make -C platform format` and `make -C platform format-check` cover
every `.c` and `.h` under `platform/` (the m4a engine, its difftest and the
tests included) and never expansion's. `.clang-tidy` (with a relaxation in
`platform/test/.clang-tidy`) names the checks and, in comments, every
exclusion and why; a `// NOLINT(check): reason` or `// cppcheck-suppress`
needs its reason on the same line and is rare. `make -C platform
format-check` and `make -C platform lint` are part of the platform gate,
`make -C platform gate` (docs/BUILDING.md).
