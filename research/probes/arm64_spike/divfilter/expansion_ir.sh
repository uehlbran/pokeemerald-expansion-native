#!/usr/bin/env bash
# expansion_ir.sh FILE.c...: one of expansion's src/ files through the native
# 64-bit build's preprocessing (Makefile.native COMPILE_C, printed by
# `make -f Makefile.native BITS=64 -n -W src/X.c build/native64/src/X.o`) with
# clang in place of gcc, to IR before optimisation, through divfilter.py, then
# compiled filtered and unfiltered, for x86_64 and AArch64 (IR emitted per
# target from the same preprocessed text). Writes out/expansion/<name>.*. Run from anywhere.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
OUT=$HERE/out/expansion
mkdir -p "$OUT"
cd "$ROOT"
CPPFLAGS="-m64 -funsigned-char -iquote include -iquote . -Wno-trigraphs -DMODERN=1 -DPLATFORM_NATIVE=1 -DTESTING=0 -DEMERALD -std=gnu17"
CFLAGS="-funsigned-char -O2 -std=gnu17 -fno-pie -w -Wno-error=incompatible-function-pointer-types"
for src in "$@"; do
    name=$(basename "$src" .c)
    log=$OUT/$name.log
    : > "$log"
    if ! clang -E $CPPFLAGS "$src" 2>>"$log" | tools/preproc/preproc -i -g build/assets-native "$src" charmap.txt \
            | sed -f platform/build_glue/c_filter.sed > "$OUT/$name.i"; then
        echo "STAGE preprocess FAILED $name" >> "$log"; continue
    fi
    for t in x86_64 aarch64; do
        if ! clang --target=$t-linux-gnu $CFLAGS -x c -S -emit-llvm -Xclang -disable-llvm-passes \
                "$OUT/$name.i" -o "$OUT/$name.$t.ll" 2>>"$log"; then
            echo "STAGE clang-ir-$t FAILED $name" >> "$log"; continue
        fi
        if ! python3 -I "$HERE/divfilter.py" "$OUT/$name.$t.ll" "$OUT/$name.$t.filtered.ll" 2>>"$log"; then
            echo "STAGE filter-$t REFUSED $name" >> "$log"; continue
        fi
        clang --target=$t-linux-gnu -O2 -w -c "$OUT/$name.$t.filtered.ll" -o "$OUT/$name.filtered.$t.o" 2>>"$log" \
            || echo "STAGE compile-filtered-$t FAILED $name" >> "$log"
        clang --target=$t-linux-gnu -O2 -w -c "$OUT/$name.$t.ll" -o "$OUT/$name.plain.$t.o" 2>>"$log" \
            || echo "STAGE compile-plain-$t FAILED $name" >> "$log"
    done
    echo "STAGE done $name" >> "$log"
done
