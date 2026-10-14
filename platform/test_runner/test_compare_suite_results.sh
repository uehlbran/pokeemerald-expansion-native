#!/bin/bash
# Tests compare_suite_results.sh: two runs with the same results pass whatever
# their runner numbers, Windows' "\r" and the other lines of the hydra's
# output; a result that differs in one character, a missing result and a
# duplicated one each fail with status 1 and are shown; a run with no results
# fails; a wrong call is status 2.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
compare=$here/compare_suite_results.sh
CHECKED=compare_suite_results.sh
. "$here/script_checks.sh"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

pass=$'\e[32mPASS\e[0m'
crash=$'\e[32mCRASH\e[0m'
known=$'\e[33mKNOWN_FAILING\e[0m'
{
    echo "[0] Spikes deal damage: $pass"
    echo "[1] Tests resume after CRASH: $crash"
    echo "some output of a test"
    echo "[0] Toxic Spikes: a known bug: $known"
    echo "[1] Wish heals the next turn: $pass"
} > "$tmp/linux.log"

# compare_with NAME: the script on the Linux log and $tmp/NAME.log; its status
# in $status, its output in $tmp/NAME.out.
compare_with() {
    status=0
    "$compare" "$tmp/linux.log" "$tmp/$1.log" > "$tmp/$1.out" 2>&1 || status=$?
}

sed -e 's/^\[0\]/[3]/' -e 's/^\[1\]/[0]/' -e 's/$/\r/' "$tmp/linux.log" > "$tmp/same.log"
compare_with same
check "the same results: status 0" [ "$status" -eq 0 ]
check "the same results: PASS with the count" grep -qx "PASS: the same 4 tests with the same results" "$tmp/same.out"

sed 's/Wish heals the next turn/Wish heals the next turm/' "$tmp/linux.log" > "$tmp/one_character.log"
compare_with one_character
check "one character: status 1" [ "$status" -eq 1 ]
check "one character: the line is shown" grep -q "^> Wish heals the next turm" "$tmp/one_character.out"

sed '/Spikes deal damage/d' "$tmp/linux.log" > "$tmp/missing.log"
compare_with missing
check "a missing result: status 1" [ "$status" -eq 1 ]
check "a missing result: the line is shown" grep -q "^< Spikes deal damage" "$tmp/missing.out"

sed '/Wish heals/p' "$tmp/linux.log" > "$tmp/duplicated.log"
compare_with duplicated
check "a duplicated result: status 1" [ "$status" -eq 1 ]
check "a duplicated result: the line is shown" grep -q "^> Wish heals the next turn" "$tmp/duplicated.out"

echo "no results here" > "$tmp/empty.log"
status=0
"$compare" "$tmp/empty.log" "$tmp/empty.log" > "$tmp/empty.out" 2>&1 || status=$?
check "no results: status 1" [ "$status" -eq 1 ]

status=0
"$compare" "$tmp/linux.log" > /dev/null 2>&1 || status=$?
check "one argument: status 2" [ "$status" -eq 2 ]
status=0
"$compare" "$tmp/linux.log" "$tmp/absent.log" > /dev/null 2>&1 || status=$?
check "a missing file: status 2" [ "$status" -eq 2 ]

finish
