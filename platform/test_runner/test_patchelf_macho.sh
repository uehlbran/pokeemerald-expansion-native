#!/bin/bash
# Tests tools/patchelf on a macOS (Mach-O) executable, the twin of
# test_patchelf.sh and test_patchelf_pe.sh: built with the Mac's own clang and
# linker, as the macOS build is (docs/BUILDING.md, "macOS"), patched as
# Makefile.native's check target and the hydra patch the test runner; only the
# bytes of the named symbols change, at the file offsets the section headers
# and the symbol table give, and the patched executable still runs and reads
# the patched values; a value longer than the room up to the next symbol is
# refused, and so are a zero-filled symbol (no bytes in the file), a symbol at
# the end of its section's bytes, a file whose load commands run past its end
# and one with no symbol table; a name the executable lacks is skipped, as the
# ELF and PE paths do, leaving the file as it was. Runs on macOS only
# (`make -C platform tools-test` there).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
patchelf=$root/tools/patchelf/patchelf
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# Each constant sits in __TEXT,__const before the next, which bounds the room
# patchelf gives it. The two arrays are zero-filled, at least one past its
# section's start. The exit status reads every patched value.
cat > "$tmp/t.c" <<'C'
const char gTestRunnerArgv[32] = "orig";
const unsigned char gTestRunnerHeadless = 0xAA;
const unsigned char gTestRunnerSkipIsFail = 0xBB;
const unsigned char gShort = 0xCC;
unsigned char gBssFirst[64];
unsigned char gBssSecond[64];
int main(void)
{
    const volatile char *argv = gTestRunnerArgv;
    const volatile unsigned char *flags[] = { &gTestRunnerHeadless, &gTestRunnerSkipIsFail, &gShort };
    volatile unsigned char *bss[] = { gBssFirst, gBssSecond };
    return (argv[0] == 'R' && argv[6] == '*' && argv[7] == 0 ? 0x10 : 0) + *flags[0] + *flags[1] + *flags[2]
           + bss[0][0] + bss[1][0];
}
C
exe=$tmp/t
cc -O1 -o "$exe" "$tmp/t.c"
cp "$exe" "$exe.p"
"$patchelf" "$exe.p" gTestRunnerArgv "Random*\0" gTestRunnerHeadless '\x01' gTestRunnerSkipIsFail '\x00' gShort '\x02'
# The offsets from the tools' own view of the file: nm's addresses, otool's
# section headers.
python3 -I - "$exe" "$exe.p" <<'PY'
import subprocess, sys
orig, patched = sys.argv[1], sys.argv[2]
addresses = {}
for l in subprocess.check_output(["nm", orig], text=True).splitlines():
    f = l.split()
    if len(f) == 3:
        addresses[f[2]] = int(f[0], 16)
sections, current = [], None
for l in subprocess.check_output(["otool", "-l", orig], text=True).splitlines():
    f = l.split()
    if f == ["Section"]:
        current = {}
        sections.append(current)
    elif current is not None and len(f) >= 2 and f[0] in ("addr", "size", "offset"):
        current[f[0]] = int(f[1], 0)
def offset(name):
    a = addresses["_" + name]
    for s in sections:
        if s["offset"] and s["addr"] <= a < s["addr"] + s["size"]:
            return s["offset"] + a - s["addr"]
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
assert changed == expect and changed, "Mach-O: changed bytes differ from the symbols' bytes: %s" % changed
print("patchelf Mach-O: ok (%d bytes changed)" % len(changed))
PY
status=0
"$exe.p" || status=$?
# 0x10 for the filter, 0x01 + 0x00 + 0x02 for the flags, the arrays zero.
if [ "$status" -ne $((0x13)) ]; then
    echo "the patched Mach-O executable exited with $status, not $((0x13))" >&2; exit 1
fi
echo "patchelf: the patched Mach-O executable runs and reads the patched values"
# 33 bytes: one more than gTestRunnerArgv's 32, into the room of the next symbol.
if "$patchelf" "$exe.p" gTestRunnerArgv "$(printf 'x%.0s' {1..33})" 2>/dev/null; then
    echo "patchelf wrote past the next symbol of a Mach-O file" >&2; exit 1
fi
echo "patchelf: a Mach-O value longer than its symbol refused"
# Refused, and the file left as it was: a refusal must come before any write.
refused_unchanged() {
    local file=$1 what=$2; shift 2
    cp "$file" "$tmp/before"
    if "$patchelf" "$file" "$@" 2>/dev/null; then
        echo "patchelf accepted $what" >&2; exit 1
    fi
    cmp -s "$tmp/before" "$file" || { echo "patchelf changed the file while refusing $what" >&2; exit 1; }
    echo "patchelf: $what refused"
}
refused_unchanged "$exe.p" "a zero-filled Mach-O symbol (gBssFirst)" gBssFirst '\x01'
refused_unchanged "$exe.p" "a zero-filled Mach-O symbol (gBssSecond)" gBssSecond '\x01'
# The size of gShort's section cut to the symbol's offset in it: its byte is
# then the first past the section's bytes. The Mach-O layout
# (<mach-o/loader.h>): a 32-byte header with the command count at 16, then
# the load commands; LC_SEGMENT_64 (0x19) holds its section count at 64 and
# its 80-byte section headers from 72, each with the address at 32 and the
# size at 40.
cp "$exe" "$tmp/short.exe"
python3 -I - "$tmp/short.exe" <<'PY'
import struct, subprocess, sys
exe = sys.argv[1]
address = next(int(f[0], 16) for f in (l.split() for l in subprocess.check_output(["nm", exe], text=True).splitlines())
               if len(f) == 3 and f[2] == "_gShort")
f = bytearray(open(exe, "rb").read())
count, at = struct.unpack_from("<I", f, 16)[0], 32
for _ in range(count):
    command, size = struct.unpack_from("<II", f, at)
    if command == 0x19:
        for header in range(at + 72, at + 72 + 80 * struct.unpack_from("<I", f, at + 64)[0], 80):
            start, length = struct.unpack_from("<QQ", f, header + 32)
            if start <= address < start + length:
                assert address > start, "gShort is first in its section; the test needs it later"
                struct.pack_into("<Q", f, header + 40, address - start)
                open(exe, "wb").write(f)
                sys.exit(0)
    at += size
sys.exit("no section holds gShort")
PY
refused_unchanged "$tmp/short.exe" "a Mach-O symbol at the end of its section's bytes" gShort 'x'
cp "$exe.p" "$tmp/before"
"$patchelf" "$exe.p" gNoSuchSymbol '\x01' || { echo "patchelf failed on a name the Mach-O file lacks" >&2; exit 1; }
cmp -s "$tmp/before" "$exe.p" || { echo "patchelf changed a Mach-O file for a name it lacks" >&2; exit 1; }
echo "patchelf: a name the Mach-O file lacks skipped, the file unchanged"
# A header whose load commands are said to be 4096 bytes in a 64-byte file,
# and one with no load command at all.
python3 -I - "$tmp/bad" "$tmp/empty" <<'PY'
import struct, sys
header = lambda count, size: struct.pack("<8I", 0xFEEDFACF, 0x01000007, 3, 2, count, size, 0, 0)
open(sys.argv[1], "wb").write(header(1, 4096) + bytes(32))
open(sys.argv[2], "wb").write(header(0, 0))
PY
refused_unchanged "$tmp/bad" "a Mach-O file whose load commands run past its end" gTestRunnerHeadless '\x01'
refused_unchanged "$tmp/empty" "a Mach-O file with no symbol table" gTestRunnerHeadless '\x01'
