#!/usr/bin/env bash
# compare_suite_results.sh: the per-test results of two runs of expansion's
# suite through the hydra (the output of `make -f Makefile.native check` on
# Linux, of run_windows_suite.sh on Windows) compared test by test:
#
#   platform/test_runner/compare_suite_results.sh LINUX_LOG WINDOWS_LOG
#
# A result is a line "[runner] test name: RESULT" of the hydra's output; the
# runner's number (which depends on how many ran) and a Windows "\r" are
# dropped, and the two sorted lists must be the same lines: the same tests,
# each with the same result. Prints the count of each result, and the lines
# that differ if any; exit status 0 only if the lists are the same.
set -u

if [ $# -ne 2 ] || [ ! -f "$1" ] || [ ! -f "$2" ]; then
    echo "usage: compare_suite_results.sh LINUX_LOG WINDOWS_LOG" >&2
    exit 2
fi

# The sorted results of hydra output $1.
results() {
    tr -d '\r' < "$1" | grep -a -E '^\[[0-9]+\] ' | sed -E 's/^\[[0-9]+\] //' | LC_ALL=C sort
}

# The count of each result in sorted results $1 (the result is the last word,
# colour codes dropped).
counts() {
    sed -E 's/\x1b\[[0-9;]*m//g; s/.*: //' "$1" | LC_ALL=C sort | uniq -c
}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
results "$1" > "$scratch/first"
results "$2" > "$scratch/second"
echo "== $1: $(wc -l < "$scratch/first") results"
counts "$scratch/first"
echo "== $2: $(wc -l < "$scratch/second") results"
counts "$scratch/second"
if [ ! -s "$scratch/first" ]; then
    echo "FAILED: $1 has no results"
    exit 1
fi
if ! diff "$scratch/first" "$scratch/second" > "$scratch/diff"; then
    echo "FAILED: the results differ (< $1, > $2):"
    cat "$scratch/diff"
    exit 1
fi
echo "PASS: the same $(wc -l < "$scratch/first") tests with the same results"
