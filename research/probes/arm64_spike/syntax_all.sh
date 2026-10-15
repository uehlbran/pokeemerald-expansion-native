#!/usr/bin/env bash
# syntax_all.sh OUTDIR [extra cc flags...]: every src/*.c of the native 64-bit
# build through the build's own preprocessing pipeline (Makefile.native
# COMPILE_C: gcc -E, preproc, c_filter.sed), then gcc -fsyntax-only with the
# build's CFLAGS plus the extra flags; stderr per file in OUTDIR.
# PRE_IQUOTE (environment): an include directory searched before include/.
# CC_COMPILE (environment): the compiler of the second step (default gcc).
# MODE (environment): "-c -o /dev/null" compiles fully instead of -fsyntax-only.
# Run from the repository root after `make -f Makefile.native native BITS=64`.
set -u
out=$1; shift
mkdir -p "$out"
export EXTRA="$*" OUT="$out" PRE="${PRE_IQUOTE:+-iquote $PRE_IQUOTE}"
one() {
    f=$1; b=$(basename "$f" .c)
    gcc -E -m64 -funsigned-char $PRE -iquote include -iquote . -Wno-trigraphs -DMODERN=1 -DPLATFORM_NATIVE=1 \
        -DTESTING=0 -DEMERALD -std=gnu17 "$f" 2>"$OUT/$b.cpp.err" \
      | tools/preproc/preproc -i -g build/assets-native "$f" charmap.txt \
      | sed -f platform/build_glue/c_filter.sed > "$OUT/$b.i"
    ${CC_COMPILE:-gcc} -m64 -funsigned-char -O2 -std=gnu17 -Wall -Wno-strict-aliasing -Wno-attribute-alias -Woverride-init \
        -Wnonnull -Wenum-conversion ${MODE:--fsyntax-only} $EXTRA -x c "$OUT/$b.i" 2>"$OUT/$b.err"
    echo "$? $f" >> "$OUT/status.txt"
    rm -f "$OUT/$b.i"
}
export -f one
rm -f "$out/status.txt"
ls src/*.c | xargs -P 16 -I{} bash -c 'one {}'
echo "files: $(wc -l < "$out/status.txt"), failed: $(grep -vc '^0 ' "$out/status.txt")"
