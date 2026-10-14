#!/bin/bash
# Tests tools/patchelf on a Windows (PE) executable, the twin of
# test_patchelf.sh: built with MinGW-w64 as the Windows build is
# (docs/BUILDING.md, "Windows (cross-build from Linux)"), patched as
# platform/test_runner/run_windows_suite.sh patches the test runner; only the
# bytes of the named symbols change, at the file offsets the section headers
# and the COFF symbol table give, for a name kept in the string table
# (longer than 8 bytes) and one kept in the symbol's record; a value longer
# than the room up to the next symbol is refused, and so are a file with no
# PE header and an executable with no symbol table.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
patchelf=$root/tools/patchelf/patchelf
cross=x86_64-w64-mingw32-
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# gShort's name fits in its symbol record. Each constant sits in .rdata
# before the next, which bounds the room patchelf gives it.
cat > "$tmp/t.c" <<'C'
const char gTestRunnerArgv[32] = "orig";
const unsigned char gTestRunnerHeadless = 0xAA;
const unsigned char gTestRunnerSkipIsFail = 0xBB;
const unsigned char gShort = 0xCC;
int main(void)
{
    const volatile char *argv = gTestRunnerArgv;
    const volatile unsigned char *flags[] = { &gTestRunnerHeadless, &gTestRunnerSkipIsFail, &gShort };
    return argv[0] + *flags[0] + *flags[1] + *flags[2];
}
C
exe=$tmp/t.exe
"${cross}gcc" -O1 -o "$exe" "$tmp/t.c"
cp "$exe" "$exe.p"
"$patchelf" "$exe.p" gTestRunnerArgv "Random*\0" gTestRunnerHeadless '\x01' gTestRunnerSkipIsFail '\x00' gShort '\x02'
python3 -I - "$exe" "$exe.p" "$cross" <<'PY'
import subprocess, sys
orig, patched, cross = sys.argv[1], sys.argv[2], sys.argv[3]
addresses = {}
for l in subprocess.check_output([cross + "nm", orig], text=True).splitlines():
    f = l.split()
    if len(f) == 3:
        addresses[f[2]] = int(f[0], 16)
sections = []
for l in subprocess.check_output([cross + "objdump", "-h", orig], text=True).splitlines():
    f = l.split()
    if len(f) == 7 and f[0].isdigit():
        sections.append((int(f[2], 16), int(f[3], 16), int(f[5], 16)))
def offset(name):
    a = addresses[name]
    for size, vma, file_offset in sections:
        if file_offset and vma <= a < vma + size:
            return file_offset + a - vma
    sys.exit("no offset for " + name)
want = {}
for name, val in (("gTestRunnerArgv", b"Random*\0"), ("gTestRunnerHeadless", b"\x01"),
                  ("gTestRunnerSkipIsFail", b"\x00"), ("gShort", b"\x02")):
    o = offset(name)
    for i, b in enumerate(val):
        want[o + i] = b
a, b = open(orig, "rb").read(), open(patched, "rb").read()
assert len(a) == len(b)
changed = {i: b[i] for i in range(len(a)) if a[i] != b[i]}
expect = {i: v for i, v in want.items() if a[i] != v}
assert changed == expect and changed, "PE: changed bytes differ from the symbols' bytes: %s" % changed
print("patchelf PE: ok (%d bytes changed)" % len(changed))
PY
# 33 bytes: one more than gTestRunnerArgv's 32, into the room of the next symbol.
if "$patchelf" "$exe.p" gTestRunnerArgv "$(printf 'x%.0s' {1..33})" 2>/dev/null; then
    echo "patchelf wrote past the next symbol of a PE file" >&2; exit 1
fi
echo "patchelf: a PE value longer than its symbol refused"
printf 'MZ' > "$tmp/bad"; head -c 200 /dev/zero >> "$tmp/bad"
if "$patchelf" "$tmp/bad" gTestRunnerHeadless '\x01' 2>/dev/null; then
    echo "patchelf accepted a file with no PE header" >&2; exit 1
fi
echo "patchelf: a file with no PE header refused"
"${cross}strip" -o "$tmp/stripped.exe" "$exe"
if "$patchelf" "$tmp/stripped.exe" gTestRunnerHeadless '\x01' 2>/dev/null; then
    echo "patchelf accepted a PE file with no symbol table" >&2; exit 1
fi
echo "patchelf: a PE file with no symbol table refused"
