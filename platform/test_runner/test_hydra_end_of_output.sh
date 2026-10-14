#!/bin/bash
# Tests expansion's hydra (tools/mgba-rom-test-hydra) at the end of a runner's
# output: a runner that writes its results in one burst, several times what
# the hydra reads at once, and has ended before the hydra reads any of it must
# have every result reported. Linux's poll gives POLLIN and POLLHUP together
# then, with the pipe still full; a read of nothing, not POLLHUP, is the end
# (MSYS2's poll never gives POLLHUP: docs/DECISIONS.md, "The suite on Windows
# runs under MSYS2"). The fake runner stops the hydra before it writes and has
# it continued once it has ended, so the order does not depend on scheduling.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
hydra=$root/tools/mgba-rom-test-hydra/mgba-rom-test-hydra
CHECKED=mgba-rom-test-hydra
. "$here/script_checks.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 400 results of 3 lines, about 24 KiB: six times the hydra's first read
# (4096 bytes), under the 64 KiB a Linux pipe holds, so the runner never
# waits for the stopped hydra.
export FAKE_RESULTS=400
cat > "$tmp/runner" <<'SH'
#!/bin/bash
# The hydra forks the process that becomes `stdbuf -oL RUNNER ARGS... ROM`:
# the hydra is this process's parent.
hydra=$PPID
kill -STOP "$hydra"
for ((i = 1; i <= FAKE_RESULTS; i++)); do
    printf 'GBA Debug: :Ntest %05d\nGBA Debug: :Lfake.c:%d\nGBA Debug: :PPASS\n' "$i" "$i"
done
# Continues the hydra once this process is a zombie, its descriptors closed:
# a zombie still answers kill -0, and the stopped hydra cannot reap it.
runner=$$
(
    while [ "$(sed -n 's/^State:[[:space:]]*\([A-Z]\).*/\1/p' "/proc/$runner/status" 2> /dev/null)" != Z ]; do
        sleep 0.05
    done
    kill -CONT "$hydra"
) > /dev/null 2>&1 < /dev/null &
SH
chmod +x "$tmp/runner"
# What the hydra copies and patches as the "ROM": any ELF file with a symbol
# table; it has no gTestRunnerN or gTestRunnerI, which patchelf then skips.
printf 'int main(void) { return 0; }\n' | cc -x c -o "$tmp/rom" -

# The hydra runs tools/patchelf/patchelf from the working directory, as
# `make check` does from the repository's root; one runner.
status=0
(cd "$root" && MAKEFLAGS=-j1 timeout 30 "$hydra" "$tmp/runner" objcopy "$tmp/rom") > "$tmp/out" 2> "$tmp/err" || status=$?
reported=$(grep -c -E '^\[0\] test [0-9]{5}: PASS$' "$tmp/out" || true)
total=$(sed -n -E 's/^- Tests .*TOTAL.*: +([0-9]+)$/\1/p' "$tmp/out")
check "the hydra exits 0 (status $status)" test "$status" -eq 0
check "every result reported ($reported of $FAKE_RESULTS)" test "$reported" -eq "$FAKE_RESULTS"
check "the summary counts every result (TOTAL ${total:-none})" test "${total:-0}" -eq "$FAKE_RESULTS"
check "the last result reported" grep -q -x "\[0\] test $(printf '%05d' "$FAKE_RESULTS"): PASS" "$tmp/out"
check "nothing on standard error" test ! -s "$tmp/err"
finish
