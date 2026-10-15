#!/usr/bin/env bash
# compare.sh: clang's record layouts of every struct reachable from global.h,
# global.fieldmap.h, gba/m4a_internal.h and save.h (layout.c), for the Linux
# x86_64 build and for arm64 macOS (stub libc headers: no macOS SDK here;
# only memcpy-like declarations are needed to parse), and their difference.
set -u
cd "$(dirname "$0")"
R=$(cd ../../../.. && pwd)
mkdir -p out/stub
for h in string.h stdlib.h stdio.h strings.h assert.h math.h; do
    printf '#pragma once\ntypedef __SIZE_TYPE__ size_t;\nvoid *memcpy(void *, const void *, size_t); void *memset(void *, int, size_t); int memcmp(const void *, const void *, size_t); size_t strlen(const char *);\n' > out/stub/$h
done
F="-ffreestanding -funsigned-char -iquote $R/include -iquote $R -DMODERN=1 -DPLATFORM_NATIVE=1 -DTESTING=0 -DEMERALD -std=gnu17 -w -fsyntax-only -Xclang -fdump-record-layouts-complete -Xclang -fdump-record-layouts-simple"
clang --target=x86_64-linux-gnu $F layout.c > out/x86_64-linux-gnu.txt
clang --target=arm64-apple-macos11 -nostdlibinc -isystem out/stub $F layout.c > out/arm64-apple-macos11.txt
echo "records: x86_64-linux-gnu $(grep -c '^Type:' out/x86_64-linux-gnu.txt), arm64-apple-macos11 $(grep -c '^Type:' out/arm64-apple-macos11.txt)"
echo "records whose layout differs (by name):"
python3 -I - out/x86_64-linux-gnu.txt out/arm64-apple-macos11.txt <<'PY'
import re, sys
def records(path):
    text = open(path).read()
    out = {}
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        m = re.search(r"Type: (.*)\n", block)
        lay = re.search(r"Layout: <ASTRecordLayout(.*?)>", block, re.S)
        if m and lay:
            name = re.sub(r" at [^)]*", "", m.group(1))
            out[name] = " ".join(lay.group(1).split())
    return out
a, b = records(sys.argv[1]), records(sys.argv[2])
same = [k for k in a if k in b and a[k] == b[k]]
print("  same in both:", len(same))
for k in sorted(set(a) | set(b)):
    if a.get(k) != b.get(k):
        print("  differs or only in one:", k)
PY
echo "line diff of the two dumps (unnamed records included, in order): $(diff out/x86_64-linux-gnu.txt out/arm64-apple-macos11.txt | grep -c '^[<>]') lines, in the records:"
diff out/x86_64-linux-gnu.txt out/arm64-apple-macos11.txt | grep '^[<>] Type:' | sort -u
