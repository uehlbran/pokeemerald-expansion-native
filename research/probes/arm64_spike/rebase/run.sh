#!/usr/bin/env bash
# run.sh: window_test.c built through rebasefilter.py and plainly, each linked
# with the image at 0x140000000 and the EWRAM and I/O sections at 0x102000000
# and 0x104000000 (WINDOW_BASE 2^32 + the Linux addresses), and run.
# --no-relax: the code is compiled position-independent (as on arm64) and must
# not be relaxed to 32-bit absolute addresses, which cannot reach 2^32 and up.
# Needs the repository's generated headers (make -f Makefile.native native).
set -u
cd "$(dirname "$0")"
R=$(cd ../../../.. && pwd)
mkdir -p out
CPPF="-iquote $R/include -iquote $R -DMODERN=1 -DPLATFORM_NATIVE=1 -DTESTING=0 -DEMERALD -std=gnu17 -funsigned-char -w"
LDF="-no-pie -Wl,--no-relax -Wl,-Ttext-segment=0x140000000 -Wl,--section-start=.gba_ewram=0x102000000 -Wl,--section-start=.gba_io=0x104000000"
set -e
clang $CPPF -O2 -S -emit-llvm -Xclang -disable-llvm-passes window_test.c -o out/window_test.ll
python3 -I rebasefilter.py out/window_test.ll out/window_test.filtered.ll
clang -O2 -w -c out/window_test.filtered.ll -o out/window_test.filtered.o
clang $CPPF -O2 -c window_test.c -o out/window_test.plain.o
as --64 window_data.s -o out/window_data.o
clang -O2 -c window_main.c -o out/window_main.o
# The position-independent start files (Scrt1, crtbeginS): the default
# non-PIE ones hold 32-bit absolute addresses, which cannot reach 2^32 and up.
crt() { gcc -print-file-name="$1"; }
for v in filtered plain; do
    clang $LDF -nostartfiles "$(crt Scrt1.o)" "$(crt crti.o)" "$(crt crtbeginS.o)" \
        out/window_main.o out/window_test.$v.o out/window_data.o \
        "$(crt crtendS.o)" "$(crt crtn.o)" -o out/window_$v
done
set +e
echo "== through rebasefilter.py"
./out/window_filtered; echo "exit status $?"
echo "== plain (control)"
./out/window_plain; echo "exit status $?"
