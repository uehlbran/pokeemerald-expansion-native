"""gen_cases.py RECORD OUT.h: every row of libgcc_division.txt as a C table
(kind, n, d, the apcs-gnu quotient and remainder)."""
import re
import sys
rows = []
for line in open(sys.argv[1]):
    m = re.match(r"(u32|s32|u64|s64) (0x\w+) / (0x\w+)\s+apcs-gnu (0x\w+) (0x\w+)", line)
    if m:
        rows.append(f'    {{"{m[1]}", {m[2]}u, {m[3]}u, {m[4]}u, {m[5]}u}},')
open(sys.argv[2], "w").write("\n".join(rows) + "\n")
print(f"{len(rows)} rows", file=sys.stderr)
