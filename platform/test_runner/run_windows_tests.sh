#!/usr/bin/env bash
# run_windows_tests.sh: runs the unit tests cross-built for Windows
# (`make -C platform test TARGET_OS=windows BITS=64`, platform/build/64-windows/test/)
# on Windows, under Git Bash, with the checks `make -C platform test` makes on
# Linux, the sanitizers' aside (MinGW-w64 has none):
#
#   platform/test_runner/run_windows_tests.sh DIR
#
# DIR holds the test_*.exe files and SDL2.dll, as the build leaves them. Every
# executable runs to its end even after another failed, from DIR, with its
# output kept in <name>.log beside it and shown; one fails if it exits
# non-zero, ends without ptest's "N passed, 0 failed" line (a binary that ends
# early passes no test), or outlasts TEST_SECONDS (ptest's own time limit
# ends a stuck test; this one catches a hang outside a test). The last line
# says how many ran and whether all passed; the exit status is 0 only then.
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

failed=0
for test in "${tests[@]}"; do
    name=$(basename "$test" .exe)
    log=$dir/$name.log
    # A copy, deleted after the run: Windows keeps the image of an executable
    # it has started, and the image's commit charge, while the file exists.
    copy=$dir/$name.run.exe
    echo "== $name"
    cp -- "$test" "$copy"
    (cd "$dir" && timeout --kill-after=$KILL_SECONDS $TEST_SECONDS "./$name.run.exe") > "$log" 2>&1
    code=$?
    rm -f -- "$copy"
    cat "$log"
    # The C runtime writes "\r\n" for "\n" on a text-mode stream.
    if [ $code -ne 0 ]; then
        echo "FAILED: $name exited with status $code"
        failed=$((failed + 1))
    elif ! grep -qE $'^[0-9]+ passed, 0 failed\r?$' "$log"; then
        echo "FAILED: $name ended without its summary line"
        failed=$((failed + 1))
    fi
done
if [ $failed -ne 0 ]; then
    echo "$failed of ${#tests[@]} Windows test executables failed"
    exit 1
fi
echo "all ${#tests[@]} Windows test executables passed"
