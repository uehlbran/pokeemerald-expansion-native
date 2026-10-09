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
comment in the arm64 assembler, so `regions.S` did not assemble (A:217-226); it is now `regions.S`,
with C comments, and the next run shows what ld64 does with the layout link on arm64.
