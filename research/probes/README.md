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
