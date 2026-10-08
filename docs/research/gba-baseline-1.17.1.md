# GBA regression baseline, expansion/1.17.1

Run on 2026-10-08 in WSL2 Ubuntu 24.04 with `gcc-arm-none-eabi` 13.2.1 and
the bundled `tools/mgba/mgba-rom-test` (mGBA 0.11-7856-dbffb46c4), using
`TEST=1 make -j32 check` from a clean clone. Wall time about 20 minutes
including the test ELF build. Exit code 0.

| Result | Count |
|---|---|
| PASSED | 5414 |
| KNOWN_FAILING | 18 |
| EXPECT_FAILING | 9 |
| TO_DO | 421 |
| TOTAL | 5862 |

Two tests report CRASH as their expected result (`Tests resume after
CRASH`, `fatalf counts as CRASH`). These are the native suite's target
numbers: gate (b) passes when the native run reports the same per-test
results as this table. The full log of the run is not checked in; rerun
`make check` to regenerate it.
