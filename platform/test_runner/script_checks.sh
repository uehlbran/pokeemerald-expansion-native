# script_checks.sh: what the tests of the test runner's shell scripts share
# (test_run_windows_tests.sh, test_native_rom_test_windows.sh,
# test_compare_suite_results.sh). Sourced, not run; the sourcing test sets
# CHECKED, the script's name for its messages.
#
#   check WHAT COMMAND...   runs COMMAND; a non-zero status is a failed check
#   lacks PATTERN FILE      true if no line of FILE matches PATTERN (grep -E)
#   finish                  reports, and exits 1 if any check failed
checks=0
failed_checks=0

check() {
    local what=$1
    shift
    checks=$((checks + 1))
    if ! "$@"; then
        echo "$CHECKED: FAILED: $what" >&2
        failed_checks=$((failed_checks + 1))
    fi
}

lacks() {
    ! grep -qE -- "$1" "$2"
}

finish() {
    if [ "$failed_checks" -ne 0 ]; then
        echo "$CHECKED: $failed_checks of $checks checks failed" >&2
        exit 1
    fi
    echo "$CHECKED: $checks checks passed"
}
