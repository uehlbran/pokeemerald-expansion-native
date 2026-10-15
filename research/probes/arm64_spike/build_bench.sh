#!/usr/bin/env bash
# build_bench.sh: iobench natively on x86-64 (gcc, PIE, -O2) and run it; the
# AArch64 code of the three loops (clang, compile only), with their sizes.
set -eu
cd "$(dirname "$0")"
mkdir -p out
gcc -O2 -fPIE -pie -std=gnu17 -Wall -Wextra iobench.c iobench_loops.c -o out/iobench
./out/iobench | tee out/iobench.txt
for t in x86_64-linux-gnu aarch64-linux-gnu; do
    clang --target=$t -O2 -fPIE -ffreestanding -c iobench_loops.c -o out/iobench.$t.o
    echo "== $t: function sizes (bytes)"
    llvm-nm-18 -S --size-sort out/iobench.$t.o | grep run_
done
echo "== aarch64 run_fixed / run_symbol / run_variable loop bodies"
llvm-objdump-18 -d --no-show-raw-insn out/iobench.aarch64-linux-gnu.o | cat
