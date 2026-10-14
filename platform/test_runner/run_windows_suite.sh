#!/usr/bin/env bash
# run_windows_suite.sh: expansion's test suite on Windows, as
# `make -f Makefile.native check` runs it on Linux: a copy of the test runner
# build with gTestRunnerArgv, gTestRunnerHeadless and gTestRunnerSkipIsFail
# patched in (tools/patchelf, which reads a PE file's COFF symbol table), run
# by expansion's hydra (tools/mgba-rom-test-hydra) through
# platform/test_runner/native-rom-test in place of mgba-rom-test:
#
#   platform/test_runner/run_windows_suite.sh build/native64-test-windows/pokeemerald-native-test.exe
#
# Run from the repository's root (the hydra runs tools/patchelf/patchelf by
# that path) under MSYS2's bash, with the two tools built by MSYS2's gcc
# (the hydra needs fork, poll and stdbuf, which Git Bash lacks; DECISIONS.md,
# "The suite on Windows runs under MSYS2"). MAKEFLAGS=-jN sets how many
# runners the hydra starts, as `make -jN check` does, and TESTS filters the
# tests by name prefix, as make's TESTS does. SDL2.dll must be beside the
# executable, as the build leaves it: the hydra's copies run from the
# temporary directory, so that directory is put on PATH, where Windows looks
# for a DLL its executable's own directory does not have. The output is the
# hydra's, and so is the exit status.
set -u

if [ $# -ne 1 ] || [ ! -f "$1" ]; then
    echo "usage: run_windows_suite.sh EXE (the test runner build, pokeemerald-native-test.exe)" >&2
    exit 2
fi
exe=$1
headless=${exe%.exe}-headless
# As Makefile.native's check outside rh-hideout's own repository.
skip_is_fail='\x00'
# A trailing * is dropped, as check's $(TESTS:%*=%) drops it.
tests=${TESTS:-}
tests=${tests%\*}

cp -- "$exe" "$headless" || exit 2
tools/patchelf/patchelf "$headless" gTestRunnerArgv "$tests"'\0' gTestRunnerHeadless '\x01' \
    gTestRunnerSkipIsFail "$skip_is_fail" || exit 2
PATH="$(cd "$(dirname "$exe")" && pwd):$PATH" \
    tools/mgba-rom-test-hydra/mgba-rom-test-hydra platform/test_runner/native-rom-test objcopy "$headless"
