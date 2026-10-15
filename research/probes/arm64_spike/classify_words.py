"""classify_words.py BUILD_DIR: every 4-byte absolute address word
(R_X86_64_32) in the native 64-bit build's assembled data objects, by what
its target is in the linked executable (section of the target address), and
how many carry the script tag (platform/script_tag.h, bit 25). Uses readelf
and nm from binutils."""
import bisect, collections, os, subprocess, sys
build = sys.argv[1]
exe = os.path.join(build, "pokeemerald-native")
secs = []
for l in subprocess.run(["readelf", "-SW", exe], capture_output=True, text=True).stdout.splitlines():
    p = l.replace("[ ", "[").split()
    if len(p) > 5 and p[0].startswith("[") and p[0] != "[Nr]":
        addr, size = int(p[3], 16), int(p[5], 16)
        if addr:
            secs.append((addr, addr + size, p[1]))
secs.sort()
secnames = {s[2] for s in secs} | {'.rodata', '.data', '.text'}
starts = [s[0] for s in secs]
def section_of(a):
    i = bisect.bisect_right(starts, a) - 1
    return secs[i][2] if i >= 0 and a < secs[i][1] + 1 else "none(%#x)" % (a & 0xff000000)
syms = {}
for l in subprocess.run(["nm", exe], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3:
        syms.setdefault(p[2], int(p[0], 16))
objs = subprocess.run(["find", os.path.join(build, "data"), os.path.join(build, "sound"), "-name", "*.o"],
                      capture_output=True, text=True).stdout.split()
by = collections.Counter(); tagged = 0; unresolved = 0; total = 0
for o in objs:
    out = subprocess.run(["readelf", "-rW", o], capture_output=True, text=True).stdout
    for l in out.splitlines():
        p = l.split()
        if len(p) >= 5 and p[2] == "R_X86_64_32":
            total += 1
            name = p[4]
            addend = int(p[6], 16) * (-1 if p[5] == "-" else 1) if len(p) >= 7 else 0
            if name in secnames:  # a section symbol: the object's own data
                by[name + " (section-relative)"] += 1
                continue
            if name not in syms:
                unresolved += 1; by["(unresolved %s)" % name] += 1; continue
            a = syms[name] + addend
            if a & 0x02000000 and a >= 0x40000000:
                tagged += 1
                a &= ~0x02000000
            by[section_of(a)] += 1
print("total R_X86_64_32 words:", total)
for k, v in by.most_common():
    print("%7d %s" % (v, k))
print("of which carry the script tag (bit 25 set on a code address):", tagged)
