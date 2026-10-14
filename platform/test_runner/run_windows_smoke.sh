#!/usr/bin/env bash
# run_windows_smoke.sh: the Windows executable's start-up, on Windows under
# Git Bash: started with --headless --silent --frames 60 in an empty scratch
# directory, it must map its regions, install its handlers, write the clock's
# offset file (host_file's durable write) and run 60 frames of the main loop:
#
#   platform/test_runner/run_windows_smoke.sh build/native64-windows/pokeemerald-native.exe
#
# Passes, exit status 0, only if the executable exits 0, its standard output
# is the one line "main: 60 frames", its standard error is empty (every module
# start that fails says so there), and pokeemerald.rtc holds one decimal
# number and a "\n" with no "\r" (the file is written in binary mode). SDL2.dll
# must be beside the executable, as the build leaves it.
set -u

FRAMES=60
LIMIT_SECONDS=120

if [ $# -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: run_windows_smoke.sh EXE" >&2
    exit 2
fi
exe=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

(cd "$scratch" && timeout $LIMIT_SECONDS "$exe" --headless --silent --frames $FRAMES) > "$scratch/stdout" 2> "$scratch/stderr"
code=$?
failed=0
fail() {
    echo "FAILED: $*"
    failed=1
}
[ $code -eq 0 ] || fail "exit status $code"
[ "$(tr -d '\r' < "$scratch/stdout")" = "main: $FRAMES frames" ] || fail "standard output is not \"main: $FRAMES frames\""
[ ! -s "$scratch/stderr" ] || fail "standard error is not empty"
grep -qE '^-?[0-9]+$' "$scratch/pokeemerald.rtc" 2>/dev/null || fail "pokeemerald.rtc does not hold one number"
! grep -q $'\r' "$scratch/pokeemerald.rtc" 2>/dev/null || fail "pokeemerald.rtc has a \\r (not written in binary mode)"
for stream in stdout stderr; do
    echo "-- $stream"
    cat "$scratch/$stream"
done
if [ $failed -ne 0 ]; then
    exit 1
fi
echo "PASS: the Windows executable started, ran $FRAMES frames and wrote its clock's offset"
