"""compare_div.py: divprobe's qemu-aarch64 output against the GBA build's
libgcc record (platform/arm_division_libgcc/libgcc_division.txt, apcs-gnu
column, the routines expansion's code calls). Run from the repository root:
python3 -I research/probes/arm64_spike/compare_div.py"""
import re
lib = {}
for l in open("platform/arm_division_libgcc/libgcc_division.txt"):
    m = re.match(r"(u32|s32|u64|s64) (0x\w+) / (0x\w+)\s+apcs-gnu (0x\w+) (0x\w+)\s+aapcs (0x\w+) (0x\w+)", l)
    if m:
        lib[(m[1], int(m[2], 16), int(m[3], 16))] = tuple(int(x, 16) for x in m.groups()[3:])
same = diff = 0
for l in open("research/probes/arm64_spike/out/divprobe.txt"):
    w, n, d, q, r = l.split()
    kind = {"3u": "u32", "3s": "s32", "6u": "u64", "6s": "s64"}[w]
    aq, ar, _, _ = lib[(kind, int(n, 16), int(d, 16))]
    ok = (int(q, 16), int(r, 16)) == (aq, ar)
    same += ok
    diff += not ok
    print(f"{'SAME' if ok else 'DIFF'} {kind} {n}/{d}: arm64 q={q} r={r}  libgcc q={aq:#x} r={ar:#x}")
print(f"same={same} diff={diff}")
