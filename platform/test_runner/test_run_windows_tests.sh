#!/bin/bash
# Tests run_windows_tests.sh on Linux, with fake test executables (bash
# scripts named test_*.exe, as the script finds the Windows ones): the
# verdict counts a test that exits non-zero and one without ptest's summary
# line as failures, a test's standard error is kept apart and shown, each
# test runs from its own copy in DIR/run, which is removed before the next
# starts (a later test lists the directory while it runs), the .symbols file
# goes with the copy, and nothing is left in DIR/run at the end.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

CHECKED=run_windows_tests.sh
. "$here/script_checks.sh"

# fake DIR NAME BODY: a test executable that runs BODY.
fake() {
    printf '#!/bin/bash\n%s\n' "$3" > "$1/$2.exe"
    chmod +x "$1/$2.exe"
}

# run DIR: the script on DIR; its output in $tmp/out, its status in $status.
run() {
    status=0
    "$here/run_windows_tests.sh" "$1" > "$tmp/out" 2>&1 || status=$?
}

mixed=$tmp/mixed
mkdir "$mixed"
: > "$mixed/SDL2.dll"
fake "$mixed" test_a_passes 'echo "2 passed, 0 failed"'
fake "$mixed" test_b_exits_non_zero 'echo "1 passed, 0 failed"; exit 3'
fake "$mixed" test_c_has_no_summary 'echo "the test ended early"'
fake "$mixed" test_d_writes_to_stderr \
    'echo "a message on standard error" >&2; ls "$(dirname "$0")" > "$LISTING"; echo "1 passed, 0 failed"'
LISTING=$tmp/listing run "$mixed"
check "two of four fail, and the status says so" [ "$status" -eq 1 ]
check "the last line counts them" [ "$(tail -n 1 "$tmp/out")" = "2 of 4 Windows test executables failed" ]
check "a non-zero exit fails its test" grep -qx "FAILED: test_b_exits_non_zero exited with status 3" "$tmp/out"
check "no summary line fails its test" grep -qx "FAILED: test_c_has_no_summary ended without its summary line" "$tmp/out"
check "standard error is shown under its heading" grep -qx -- "-- standard error" "$tmp/out"
check "standard error is kept apart" grep -qx "a message on standard error" "$mixed/test_d_writes_to_stderr.stderr.log"
check "the output log has no standard error" lacks "standard error" "$mixed/test_d_writes_to_stderr.log"
check "a test runs from a copy alone with SDL2.dll, the earlier copies removed" \
    [ "$(tr '\n' ' ' < "$tmp/listing")" = "SDL2.dll test_d_writes_to_stderr.exe " ]
check "nothing is left in DIR/run" [ ! -e "$mixed/run" ]

passing=$tmp/passing
mkdir "$passing"
: > "$passing/SDL2.dll"
fake "$passing" test_e_passes 'echo "5 passed, 0 failed"'
fake "$passing" test_f_reads_its_map '[ -f "$0.symbols" ] && echo "1 passed, 0 failed"'
: > "$passing/test_f_reads_its_map.exe.symbols"
run "$passing"
check "all passing is status 0" [ "$status" -eq 0 ]
check "and says so" [ "$(tail -n 1 "$tmp/out")" = "all 2 Windows test executables passed" ]
check "the .symbols file goes with its copy" grep -qx "1 passed, 0 failed" "$passing/test_f_reads_its_map.log"
check "nothing is left in DIR/run" [ ! -e "$passing/run" ]

finish
