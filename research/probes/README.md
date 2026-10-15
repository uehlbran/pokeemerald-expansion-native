# Milestone 8 loader probes

Three small programs that answer, on the real runners, the questions
`research/m8-inventory.md` could not answer from Linux. No SDL, no `platform/` code.
Nothing here is part of the game build.

## What each probe decides

**`windows_layout/` (probe W1, inventory 2.2 and 5.2).** Does the NT loader map a PE
image whose sections are the GBA regions at their GBA addresses ("layout B": image base
0x01800000, regions at 0x02000000 ..., code at 0x40001000, no ASLR, no `.reloc`)?
Three variants of one program:

| Variant | Sections | Decides |
|---|---|---|
| `as_linked` | regions at their addresses, gaps between them | whether the loader accepts non-adjacent sections |
| `filled_gaps` | the same, gaps filled by uninitialised read-only sections | whether adjacency is what the loader needs, and what the fill costs in commit charge |
| `virtualalloc` | none; `main` calls `VirtualAlloc` at the GBA addresses | whether the fallback works at all, and which regions the loader's early allocations block |

If `as_linked` or `filled_gaps` passes 100 of 100, the Windows port keeps the memory model
as one image (the inventory's verdict); if neither does, the fallback result says what is left.

**`macos_layout/` (probe M0, inventory 2.4, 2.5 and 5.3).** On `macos-15-intel`: does
ld64 build, and the loader run, an x86_64 `-no_pie` image at 0x40000000 with a 4 KiB
`__PAGEZERO` and each GBA region as its own `-segaddr` segment, with slide zero? On
`macos-latest` (arm64): the same build is expected to be refused or to run slid; the job
records which, tries `MAP_FIXED` at 0x02000000 and 0x04000000 with the default and a small
page zero, and runs the x86_64 build under Rosetta 2. The `SIGFPE` `si_code` and the
`mcontext` fields the inventory lists for M0 are not in this probe.

**`macos_link/` (probe M2, the port's tasks M2 and M3).** On `macos-15-intel`, the link
and toolchain questions the macOS port rests on, which probe M0 did not ask: can ld64's
`-alias` give a section's or a segment's bounds (`section$start$SEG$SECT`,
`segment$end$SEG`), an absolute symbol and the image's header the names the linker
scripts define, and leave a weak reference null with `-U`; in what order does ld64 lay
out a segment's sections, and does an empty declaration fix a place; does a 1-byte
section aligned to 256 KiB land on EWRAM's end; do 4- and 8-byte absolute words and
absolute symbols link in the non-PIE image; what `si_code` a SIGFPE carries for a
divide error, `INT_MIN / -1`, `raise` and `kill`; whether `<ucontext.h>` compiles under
the platform's flags; whether AddressSanitizer has LeakSanitizer; whether
`pthread_cond_timedwait_relative_np` holds a monotonic deadline under ThreadSanitizer;
what `F_FULLFSYNC` costs on the runner's disk; what `_NSGetExecutablePath` returns for a
relative path and a symbolic link. `gh workflow run probes.yml --ref work/macos -f
probe=m2`; `probes.yml`'s `probe` input picks the probe of a run.

## How to run

Push the branch, then:

    gh workflow run probes.yml --ref work/m8-probes
    gh run list --workflow probes.yml --limit 1
    gh run view <run-id> --log        # the full output of every job
    gh run download <run-id>          # the out/ files of each job

The workflow is `workflow_dispatch` only. `windows-layout` and `macos-layout` should be
green when the programs ran, whatever they found; `macos-arm64-layout` has
`continue-on-error`. The scripts exit 0 after reporting: read the lines below, not the
job colour. Each `build.sh` can also be run by hand on the matching OS.

## How to read the output

Every command is echoed after a `+`. In each program's output a line starts with `PASS` or
`FAIL` per check and ends with `SUMMARY pass=N fail=M`. In `build.sh`:

- `RESULT <variant>: 100 of 100 runs exited 0` is the success line; any other count is the
  loader's randomisation (or a refusal) showing; the exit statuses are listed, and the
  output of each failed run is in `out/<variant>.failed_run_<n>.txt`.
- `LINK FAILED` / `BUILD FAILED` means the toolchain refused; the lines above it say why.
- Windows: `image base: 0x...` against the expected 0x01800000; each region's
  `VirtualQuery` lines (state 0x1000 is `MEM_COMMIT`; protect 0x4 read-write, 0x2 read-only);
  `commit charge: start ... KiB, end ... KiB; the writable regions are 528 KiB`. The
  `objdump -h` listing of each exe shows where the linker put every section. The loader
  refusing an image shows as a non-zero status of every run and an empty output.
- macOS: `slide 0`, the header at 0x40000000, the `segment` lines (every region at its
  address), and `mach_vm_region` per region.

Local checks (no Windows or macOS here): the linker scripts were run through
`ld -m i386pep` on clang-built COFF objects; the section table came out as intended and in
ascending order, with the layout of the inventory's probe B.

## What run 37922990222 showed (first run, Windows log W, macOS intel log M, macOS arm64 log A)

Line numbers are those of the saved job logs (the report appears twice in each; the first copy is cited).

**Windows x86_64: the memory model can be kept as one PE image if the gaps between sections are
filled.** `as_linked` linked (W:238-270: the exe is a valid `pei-x86-64` with every region at its GBA
address and `.text` at 0x40001000) but could not be started: "Exec format error", status 126, in all
100 runs (W:407-409). Its section table differs from `filled_gaps` only by the holes, so the NT
image mapper refuses non-adjacent sections (an inference from that comparison; the second
iteration asks Windows for the real status). `filled_gaps` loaded at 0x01800000 with main at
0x400015dd (W:413-418); every region is `MEM_COMMIT`, `MEM_IMAGE`, the ROM `PAGE_READONLY`, and
every write read back (W:419-476). Its 16 FAILs were the probe's check: writable image sections
are mapped `PAGE_WRITECOPY` (0x8), which the check did not accept. The `VirtualAlloc` fallback
obtained all nine regions (W:486-505); its one failure is the mGBA debug page, rounded down to the
64 KiB granule (0x04FFF000 gave 0x04FF0000, W:494), which is committed and usable (W:518); the
commit charge rose by 33,456 KiB, the 528 KiB of writable regions plus the 32 MiB ROM
(W:571). The image's own commit charge, 3,104 KiB at start (W:480), is larger than the regions
explain; the second iteration adds `baseline.exe` to measure it. Whether the base is stable
over many starts was hidden by the failing checks; the second iteration records it.

**macOS x86_64: probably kept; the image base is not yet confirmed.** On macos-15-intel
(Xcode 16.4, ld-1167.5, minos 15.0) the link warns "prefered load addresses (-seg1addr) are
disabled with chained fixups" (M:253), so `-image_base` was ignored and `__TEXT` followed the 4 KiB
page zero: header at 0x1000, not 0x40000000 (M:659-662, M:675-676, 59 checks passed, 2 failed). Everything
else held: slide 0, all eight region segments at their GBA addresses, read-write, written and read
back (M:663-672). The same x86_64 source built on the arm64 runner (Xcode 26.6, ld-1267) and run under
Rosetta 2 had the header at 0x40000000 and 61 of 61 checks passed (A:417-521). `MAP_FIXED` at
0x02000000 and 0x04000000 fails with ENOMEM under the default 4 GiB page zero (M:765-767) and succeeds
in 14 of 100 runs with a 4 KiB page zero (M:772-776): a runtime reservation is unreliable, while
segments set by the kernel at exec are not. The second iteration adds `-Wl,-no_fixup_chains`, an old
deployment target and `-ld_classic` as separate attempts.

**macOS arm64: the model cannot hold.** `MAP_FIXED` at the GBA addresses fails with ENOMEM under
the default page zero (A:302-306), and a binary linked with `-pagezero_size 0x1000` (rounded up to
0x4000 by ld, A:308) is killed on start, status 137 in all 100 runs (A:412): an arm64 process needs a
page zero of at least 4 GiB. The arm64 layout probe itself never reached the linker: `#` is not a
comment in the arm64 assembler, so `regions.s` did not assemble (A:217-226); it is now `regions.S`,
with C comments, and the next run shows what ld64 does with the layout link on arm64.

## Runs 37923542701 and 37925659422 (2026-10-09, the second iteration)

macOS x86_64 (macos-15-intel): `layout_nofixup` (`-Wl,-no_fixup_chains`) and `layout_minos`
(`-mmacosx-version-min=10.13`) put the header at 0x40000000 with a zero slide and every region at
its address: 100 of 100 starts, 61 of 61 checks; `layout_plain` and `layout_classic` keep the
regions but leave the header at 0x1000 (59 of 61). `mapfixed`: 0 of 100 with the default page
zero, 16 of 100 with a 4 KiB one. Verdict: the model is kept on macOS x86_64, with
`-no_fixup_chains`.

macOS arm64 (macos-latest): every layout link fails, the linker saying "-no_pie ignored for
arm64*" and refusing the debug page's address against the 16 KiB page size; `mapfixed` 0 of 100
(the small page zero is killed, status 137); the x86_64 build under Rosetta passes 61 of 61,
100 of 100. Verdict: the inventory's stands, the model cannot hold on arm64.

Windows x86_64 (windows-latest, run 37925659422; the second run's job hung in the `cmd.exe`
start-status command until its timeout, so those commands are bounded now): `as_linked` is
refused by Windows itself ("The specified executable is not a valid application for this OS
platform", PowerShell; Git Bash's "Exec format error", status 126, in 100 of 100);
`filled_gaps` starts 100 of 100 at image base 0x1800000 with 40 of 40 checks, commit charge
3100 KiB against the 604 KiB baseline (the read-only gaps are not charged); `virtualalloc`
starts 100 of 100 with 49 of 49 checks, commit charge 34072 KiB (the 32 MiB ROM committed).
Verdict: the model is kept on Windows as one PE image with the gaps filled.

## Run 38702323158 (2026-10-15, probe M2: the macOS link and toolchain)

macos-15-intel: macOS 15.7.9, Apple clang 17.0.0 (clang-1700.0.13.5), ld-1167.5, SDK 15.5,
`nm` is llvm-nm (Apple LLVM 17), 4 KiB pages, Homebrew 6.0.18 with its taps as of
2026-08-24; Homebrew's `sdl2` is `sdl2-compat` 2.32.70 (SDL2's interface on SDL3),
`x86_64-elf-binutils` 2.47 is bottled, `libpng` and `pkgconf` are on the image. 47 s.

**The link.** `-alias` binds `section$start$`, `section$end$`, `segment$start$` and
`segment$end$`, an absolute `.set` symbol and `__mh_execute_header` to the names the
platform reads; `-U` leaves an undefined weak object and function null; 4- and 8-byte
words holding a function, an I/O address and an absolute symbol link and read back. ld64
lays out a segment's sections in the order it first meets each with something in it, object
by object; an empty declaration does not count (`__empty_probe`, declared in the first
object, came after every initialised section that held something), and a segment's
zero-filled sections follow its initialised ones. With the layout
object first, IWRAM's 0x7F00 bytes and the persistent section at 0x03007F00 come out
right and the segment ends at 0x03008000; with it last, the persistent section went first
(the `alias_layout_last` variant, 4 failures). A 1-byte section aligned to 256 KiB after
EWRAM's data starts at 0x02040000, but only when the data comes first: declared with
something in it in the first object, it went before EWRAM_DATA, which then started at
0x02040010. Hence the port's order: the regions' object first, EWRAM's end in an object
linked last. The same with `-ld_classic` and under AddressSanitizer. The link warns that
`-no_pie` and a non-standard `__PAGEZERO` are deprecated. The one failure in each variant
("gInitialMainCB2 is CB2_Probe", the same address on both sides) was the compiler folding a
comparison of two distinct symbols' addresses to false; the values are equal. `__DATA,__const`
stays in `__DATA` with `-no_fixup_chains`. `nm --format=sysv` gives Mach-O symbols a class
(T, t, S, s, A, b) and no type or size.

**SIGFPE.** `si_code` is `FPE_INTDIV` (7) for a 32- and 64-bit division by zero and for
`INT_MIN / -1`, and also for `raise(SIGFPE)` and `kill(getpid(), SIGFPE)`: `SI_USER` is
never seen. `si_addr` is the faulting instruction's address, as `rip` is; the same under
AddressSanitizer.

**ucontext.** Without `_XOPEN_SOURCE`, `<ucontext.h>` stops with "The deprecated ucontext
routines require _XOPEN_SOURCE to be defined"; with it, `getcontext`, `makecontext` and
`swapcontext` are each "deprecated: first deprecated in macOS 10.6", errors under
`-Werror`; with the warnings allowed the switch runs.

**LeakSanitizer.** `ASAN_OPTIONS=detect_leaks=1` aborts at start-up: "AddressSanitizer:
detect_leaks is not supported on this platform" (status 134); by default a leak is not
reported.

**The watchdog's wait.** `pthread_cond_timedwait_relative_np` with what is left of a 300 ms
deadline on `CLOCK_MONOTONIC` recomputed after each wake-up: expired at 300 ms after 3
wake-ups, and at 301 ms under ThreadSanitizer, with no report.

**F_FULLFSYNC** on the runner's APFS volume (a SATA virtual disk): the 128 KiB file in
4.5 to 24 ms (fsync 0.6 to 1 ms), its directory in 3.5 to 4.3 ms (fsync 0.01 ms); both
succeed.

**`_NSGetExecutablePath`** gives an absolute path for a relative start and for an absolute
one, and the link's own path for a symbolic link (`realpath` resolves it).

