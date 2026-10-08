# Modifications to upstream files

Policy. Expansion's files are modified only where a hardware assumption
cannot be isolated by link-time substitution, build-time filtering, or a
shim in `platform/`. Every modification is guarded by `PLATFORM_NATIVE`
(`#if PLATFORM_NATIVE` or `#if !PLATFORM_NATIVE`), is as small as it can
be, and has a row in the table below with the file, the reason, a summary
of the diff, and which milestone added it. The GBA build must produce
the same ROM before and after (check with `make compare` where
applicable, and `make check` always). A modification that changes GBA
behaviour is a bug.

When upgrading expansion (see `UPGRADING.md`), every row is re-verified:
the guard still applies, the surrounding code still has the assumption,
and the modification still compiles on both targets. Rows that become
unnecessary are removed, not left behind.

Flag for 64-bit: a row marked "64-bit" needs attention in the 64-bit
milestone, for example because it fixes a pointer width or an address
range.

## Whole-file replacements and moves

| File | Reason | What changed | Since |
|---|---|---|---|
| `README.md` | This repository's front page describes this project | Upstream's README moved to `docs/upstream-README.md`; new README written | M1 |
| `CONTRIBUTING.md` | Contribution rules differ (C rules, gates) | Upstream's moved to `docs/upstream-CONTRIBUTING.md`; new file written in M6 | M1 |
| `CREDITS.md` | Credit the reference ports and sources | A section added at the top; upstream content kept verbatim below | M1 |
| `LICENSE` | Scoped license for the platform layer | New file; upstream has none | M1 |

## Source modifications

None yet. Expected in milestone 2, each recorded here when merged:

| File | Reason | What changed | Since |
|---|---|---|---|
