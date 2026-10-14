#!/usr/bin/env bash
# run_windows_tests.sh: runs the unit tests cross-built for Windows
# (`make -C platform test TARGET_OS=windows BITS=64`, platform/build/64-windows/test/)
# on Windows, under Git Bash, with the checks `make -C platform test` makes on
# Linux, the sanitizers' aside (MinGW-w64 has none):
#
#   platform/test_runner/run_windows_tests.sh DIR
#
# DIR holds the test_*.exe files, SDL2.dll and test_symbols.exe.symbols, as
# the build leaves them. Every executable runs to its end even after another
# failed, from DIR, with its standard output kept in <name>.log beside it and
# its standard error in <name>.stderr.log, both shown; one fails if it exits
# non-zero, if its standard output has no ptest's "N passed, 0 failed" line
# (a binary that ends early passes no test), or if it outlasts
# TEST_SECONDS (ptest's own time limit ends a stuck test; this one catches a
# hang outside a test). The last line says how many ran and whether all
# passed; the exit status is 0 only then.
#
# Each runs from a copy under its own name in DIR/run/ (with what it needs
# beside it), removed when it ends: Windows keeps the image of an executable
# it has started, and the image's commit charge, for as long as the file is
# there, about 1 GiB for this layout's (the image spans 0x01800000 to
# 0x41xxxxxx); the runner's commit ran out after two dozen of them
# (docs/DECISIONS.md, "The Windows unit tests run from copies removed after
# them").
set -u

TEST_SECONDS=600
KILL_SECONDS=10

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    echo "usage: run_windows_tests.sh DIR (the directory of the cross-built test_*.exe)" >&2
    exit 2
fi
dir=$(cd "$1" && pwd)
shopt -s nullglob
tests=("$dir"/test_*.exe)
if [ ${#tests[@]} -eq 0 ]; then
    echo "run_windows_tests.sh: no test_*.exe in $dir" >&2
    exit 1
fi

run_dir=$dir/run
rm -rf "$run_dir" && mkdir "$run_dir" && cp -- "$dir/SDL2.dll" "$run_dir/" || {
    echo "run_windows_tests.sh: cannot set up $run_dir" >&2
    exit 1
}
failed=0
for test in "${tests[@]}"; do
    name=$(basename "$test" .exe)
    log=$dir/$name.log
    errors=$dir/$name.stderr.log
    echo "== $name"
    cp -- "$test" "$run_dir/" && { [ ! -f "$test.symbols" ] || cp -- "$test.symbols" "$run_dir/"; } || {
        echo "FAILED: $name could not be copied to $run_dir"
        failed=$((failed + 1))
        continue
    }
    (cd "$dir" && timeout --kill-after=$KILL_SECONDS $TEST_SECONDS "$run_dir/$name.exe") > "$log" 2> "$errors"
    code=$?
    rm -f -- "$run_dir/$name.exe" "$run_dir/$name.exe.symbols"
    cat "$log"
    if [ -s "$errors" ]; then
        echo "-- standard error"
        cat "$errors"
    fi
    # The C runtime writes "\r\n" for "\n" on a text-mode stream.
    if [ $code -ne 0 ]; then
        echo "FAILED: $name exited with status $code"
        failed=$((failed + 1))
    elif ! grep -qE $'^[0-9]+ passed, 0 failed\r?$' "$log"; then
        echo "FAILED: $name ended without its summary line"
        failed=$((failed + 1))
    fi
done
rm -rf "$run_dir"
if [ $failed -ne 0 ]; then
    echo "$failed of ${#tests[@]} Windows test executables failed"
    exit 1
fi
echo "all ${#tests[@]} Windows test executables passed"
