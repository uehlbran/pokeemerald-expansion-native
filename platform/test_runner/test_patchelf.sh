#!/bin/bash
# Tests tools/patchelf on ELF32 and ELF64 executables, as Makefile.native's
# check target runs it: only the bytes of the named symbols change, at their
# file offsets, and a file with a wrong ELF class is refused.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/t.c" <<'C'
char gTestRunnerArgv[32] = "orig";
volatile unsigned char gTestRunnerHeadless = 0xAA;
volatile unsigned char gTestRunnerSkipIsFail = 0xBB;
int main(void) { return gTestRunnerHeadless + gTestRunnerSkipIsFail + gTestRunnerArgv[0]; }
C
for bits in 32 64; do
    exe=$tmp/t$bits
    gcc -m$bits -O1 -no-pie -fno-pie -o "$exe" "$tmp/t.c"
    cp "$exe" "$exe.p"
    "$root/tools/patchelf/patchelf" "$exe.p" gTestRunnerArgv "Random*\0" gTestRunnerHeadless '\x01' gTestRunnerSkipIsFail '\x00'
    python3 -I - "$exe" "$exe.p" "$bits" <<'PY'
import subprocess, sys
orig, patched, bits = sys.argv[1], sys.argv[2], sys.argv[3]
def sym(name):
    for l in subprocess.check_output(["nm", orig], text=True).splitlines():
        f = l.split()
        if len(f) == 3 and f[2] == name:
            addr = int(f[0], 16)
    for l in subprocess.check_output(["readelf", "-SW", orig], text=True).splitlines():
        f = l.replace("[", " ").replace("]", " ").split()
        if len(f) > 6 and f[1].startswith("."):
            a, o, s = int(f[3], 16), int(f[4], 16), int(f[5], 16)
            if f[2] != "NOBITS" and a <= addr < a + s:
                return o + addr - a
    sys.exit("no offset for " + name)
want = {}
for name, val in (("gTestRunnerArgv", b"Random*\0"), ("gTestRunnerHeadless", b"\x01"), ("gTestRunnerSkipIsFail", b"\x00")):
    o = sym(name)
    for i, b in enumerate(val):
        want[o + i] = b
a, b = open(orig, "rb").read(), open(patched, "rb").read()
assert len(a) == len(b)
changed = {i: b[i] for i in range(len(a)) if a[i] != b[i]}
expect = {i: v for i, v in want.items() if a[i] != v}
assert changed == expect and changed, "ELF%s: changed bytes differ from the symbols' bytes: %s" % (bits, changed)
print("patchelf ELF%s: ok (%d bytes changed)" % (bits, len(changed)))
PY
done
printf '\x7fELF\x03' > "$tmp/bad"; head -c 200 /dev/zero >> "$tmp/bad"
if "$root/tools/patchelf/patchelf" "$tmp/bad" gTestRunnerHeadless '\x01' 2>/dev/null; then
    echo "patchelf accepted an unknown ELF class" >&2; exit 1
fi
echo "patchelf: unknown ELF class refused"
