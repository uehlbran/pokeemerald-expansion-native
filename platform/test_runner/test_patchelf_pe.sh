#!/bin/bash
# Tests tools/patchelf on a Windows (PE) executable, the twin of
# test_patchelf.sh: built with MinGW-w64 as the Windows build is
# (docs/BUILDING.md, "Windows (cross-build from Linux)"), patched as
# platform/test_runner/run_windows_suite.sh patches the test runner; only the
# bytes of the named symbols change, at the file offsets the section headers
# and the COFF symbol table give, for a name kept in the string table
# (longer than 8 bytes) and one kept in the symbol's record; a value longer
# than the room up to the next symbol is refused, and so are a symbol in
# .bss (no bytes in the file), a symbol at the end of its section's raw data,
# a file with no PE header and an executable with no symbol table; a name the
# executable lacks is skipped, as the ELF path does, leaving the file as it was.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
patchelf=$root/tools/patchelf/patchelf
cross=x86_64-w64-mingw32-
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
# gShort's name fits in its symbol record. Each constant sits in .rdata
# before the next, which bounds the room patchelf gives it. Of the two .bss
# arrays at least one is past offset 0 of its section, where the room check
# alone would not refuse it.
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
    return argv[0] + *flags[0] + *flags[1] + *flags[2] + bss[0][0] + bss[1][0];
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
refused_unchanged "$exe.p" "a PE symbol in .bss (gBssFirst)" gBssFirst '\x01'
refused_unchanged "$exe.p" "a PE symbol in .bss (gBssSecond)" gBssSecond '\x01'
# The raw size of gTestRunnerArgv's section cut to the symbol's offset in it:
# its first byte is then the first byte past the section's data in the file,
# though still inside the section's virtual size.
cp "$exe" "$tmp/short_raw.exe"
python3 -I - "$tmp/short_raw.exe" "$cross" <<'PY'
import struct, subprocess, sys
exe, cross = sys.argv[1], sys.argv[2]
address = next(int(f[0], 16) for f in (l.split() for l in subprocess.check_output([cross + "nm", exe], text=True).splitlines())
               if len(f) == 3 and f[2] == "gTestRunnerArgv")
f = bytearray(open(exe, "rb").read())
# The PE/COFF layout (Microsoft's "PE Format"): the PE header's offset at
# 0x3C, its signature, the COFF header, the PE32+ optional header with the
# 64-bit ImageBase at 24, then the section headers.
DOS_PE_HEADER_OFFSET_AT, PE_SIGNATURE_SIZE, COFF_HEADER_SIZE = 0x3C, 4, 20
COFF_SECTION_COUNT_AT, COFF_OPTIONAL_HEADER_SIZE_AT, OPTIONAL_IMAGE_BASE_AT = 2, 16, 24
SECTION_HEADER_SIZE, SECTION_VIRTUAL_SIZE_AT, SECTION_VIRTUAL_ADDRESS_AT, SECTION_RAW_SIZE_AT = 40, 8, 12, 16
coff = struct.unpack_from("<I", f, DOS_PE_HEADER_OFFSET_AT)[0] + PE_SIGNATURE_SIZE
count = struct.unpack_from("<H", f, coff + COFF_SECTION_COUNT_AT)[0]
optional = coff + COFF_HEADER_SIZE
image_base = struct.unpack_from("<Q", f, optional + OPTIONAL_IMAGE_BASE_AT)[0]
sections = optional + struct.unpack_from("<H", f, coff + COFF_OPTIONAL_HEADER_SIZE_AT)[0]
for header in range(sections, sections + count * SECTION_HEADER_SIZE, SECTION_HEADER_SIZE):
    start = image_base + struct.unpack_from("<I", f, header + SECTION_VIRTUAL_ADDRESS_AT)[0]
    if start <= address < start + struct.unpack_from("<I", f, header + SECTION_VIRTUAL_SIZE_AT)[0]:
        offset = address - start
        # At offset 0 the cut would leave no data at all, which proves less.
        assert offset > 0, "gTestRunnerArgv is first in its section; the test needs it later"
        struct.pack_into("<I", f, header + SECTION_RAW_SIZE_AT, offset)
        open(exe, "wb").write(f)
        sys.exit(0)
sys.exit("no section holds gTestRunnerArgv")
PY
refused_unchanged "$tmp/short_raw.exe" "a PE symbol at the end of its section's raw data" gTestRunnerArgv 'x'
cp "$exe.p" "$tmp/before"
"$patchelf" "$exe.p" gNoSuchSymbol '\x01' || { echo "patchelf failed on a name the PE file lacks" >&2; exit 1; }
cmp -s "$tmp/before" "$exe.p" || { echo "patchelf changed a PE file for a name it lacks" >&2; exit 1; }
echo "patchelf: a name the PE file lacks skipped, the file unchanged"
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
