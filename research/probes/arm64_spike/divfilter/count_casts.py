"""count_casts.py FILE.ll...: in clang's IR before optimisation (expansion_ir.sh's
<name>.x86_64.ll), the pointer/integer casts a "32-bit pointers are offsets
from one base" IR pass would rewrite: ptrtoint to i32/i16/i8, and inttoptr
split by where its operand comes from (zext or sext from i32, another i64
value, or a constant: a fixed address). Instructions and constant
expressions both counted. Totals, then the top 5 files per kind."""
import collections
import re
import sys

DEF = re.compile(r"^\s*(%[-\w.$]+)\s*=\s*(zext|sext)\s+i32\s")
PTI = re.compile(r"\bptrtoint\s*\(?\s*ptr\s+[^,]*?\s+to\s+(i32|i16|i8)\b")
ITP = re.compile(r"\binttoptr\s*\(?\s*i64\s+(%[-\w.$]+|-?\d+)\s+to\s+ptr")
total = collections.Counter()
per = collections.defaultdict(collections.Counter)
for path in sys.argv[1:]:
    name = path.rsplit("/", 1)[-1].split(".")[0]
    defs = {}
    for line in open(path):
        if line.startswith("define"):
            defs = {}
        m = DEF.match(line)
        if m:
            defs[m[1]] = m[2] + " i32"
        for m in PTI.finditer(line):
            per[name]["ptrtoint to " + m[1]] += 1
        for m in ITP.finditer(line):
            src = m[1]
            if not src.startswith("%"):
                kind = "inttoptr of a constant"
            else:
                kind = "inttoptr of " + defs.get(src, "another i64 value")
            per[name][kind] += 1
for name, c in per.items():
    total.update(c)
for kind in sorted(total):
    top = sorted(((c[kind], n) for n, c in per.items() if c[kind]), reverse=True)[:5]
    files = sum(1 for c in per.values() if c[kind])
    print(f"{kind}: {total[kind]} in {files} files; top: " + ", ".join(f"{n} {v}" for v, n in top))
