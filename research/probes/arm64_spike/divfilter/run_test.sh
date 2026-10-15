#!/usr/bin/env bash
# run_test.sh: divtest.c through the IR filter, for x86_64 (run natively, no
# SIGFPE handler) and AArch64 (compiled only; its divisions are listed).
set -eu
cd "$(dirname "$0")"
ROOT=../../../..
mkdir -p out
python3 -I gen_cases.py $ROOT/platform/arm_division_libgcc/libgcc_division.txt out/cases.h
for target in x86_64-linux-gnu aarch64-linux-gnu; do
    clang --target=$target -O2 -I out -S -emit-llvm -Xclang -disable-llvm-passes divtest.c -o out/divtest.$target.ll
    python3 -I divfilter.py out/divtest.$target.ll out/divtest.$target.filtered.ll
done
clang --target=x86_64-linux-gnu -O2 -c out/divtest.x86_64-linux-gnu.filtered.ll -o out/divtest.x86_64.o
clang -O2 -c pen_div.c -o out/pen_div.x86_64.o
clang out/divtest.x86_64.o out/pen_div.x86_64.o -o out/divtest
echo "+ ./out/divtest (no SIGFPE handler)"
./out/divtest
echo "exit status $?"
clang --target=aarch64-linux-gnu -O2 -c out/divtest.aarch64-linux-gnu.filtered.ll -o out/divtest.aarch64.o
clang --target=aarch64-linux-gnu -O2 -ffreestanding -c pen_div.c -o out/pen_div.aarch64.o
echo "AArch64 divtest.o: sdiv/udiv instructions: $(llvm-objdump-18 -d out/divtest.aarch64.o | grep -cE '\s(s|u)div\s' || true)," \
     "calls to pen_*: $(llvm-objdump-18 -dr out/divtest.aarch64.o | grep -cE 'R_AARCH64_CALL26\s+pen_' || true)"
echo "x86_64 divtest.o: div/idiv instructions: $(objdump -d out/divtest.x86_64.o | grep -cE '\s(i?div)[lq]?\s' || true)"
# Control: the same program without the filter, where x86's divide error is
# what a zero divisor gives (expected: killed by SIGFPE, status 136).
clang -O2 -I out divtest.c -o out/divtest_unfiltered
echo "+ ./out/divtest_unfiltered (control, no filter, no handler)"
set +e
./out/divtest_unfiltered
echo "exit status $?"
