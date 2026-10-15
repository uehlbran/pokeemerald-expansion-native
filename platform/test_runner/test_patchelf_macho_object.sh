#!/bin/bash
# Tests tools/patchelf's Mach-O path on any host with clang: on 64-bit Mach-O
# object files that clang makes for macOS (`-target x86_64-apple-macos11 -c`),
# which hold the same load commands, sections and symbol table as the
# executable ld64 links from them (an object's one segment lists every
# section). test_patchelf_macho.sh covers the linked executable on a Mac and
# runs it; this one puts the path in the Linux gate (`make -C platform
# tools-test`). Each section's bytes and each symbol's offset come from this
# script's own reading of the file (<mach-o/loader.h>, <mach-o/nlist.h>), not
# from patchelf's. Checked: only the named symbols' bytes change, each up to
# the next symbol of its section; a value longer than that is refused; only
# N_SECT symbols whose name has Mach-O's leading underscore match (a symbol
# named "xgVictim" is not "gVictim", an absolute symbol is not patched); and
# these are refused with the file left as it was and the reason named: a
# zero-filled symbol (also when its section's header names an offset), a
# symbol at or past its section's end, a section whose bytes are said to lie
# past the end of the file, a load command whose size runs past the load
# commands or is below the smallest, and a file with no symbol table.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
patchelf=$root/tools/patchelf/patchelf
clang=${CLANG:-clang}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
command -v "$clang" > /dev/null || { echo "test_patchelf_macho_object.sh: no $clang (set CLANG)" >&2; exit 1; }
# Each constant sits in __TEXT,__const before the next, which bounds the room
# patchelf gives it, and gLast, the section's last, has its room up to the
# section's end.
# The arrays are zero-filled (-fno-common), the second past its section's start.
cat > "$tmp/t.c" <<'C'
const char gTestRunnerArgv[32] = "orig";
const unsigned char gTestRunnerHeadless = 0xAA;
const unsigned char gTestRunnerSkipIsFail = 0xBB;
const unsigned char gShort = 0xCC;
const unsigned char gVictim __asm__("xgVictim") = 0xDD;
const unsigned char gLast = 0xEE;
unsigned char gBssFirst[64];
unsigned char gBssSecond[64];
C
obj=$tmp/t.o
"$clang" -target x86_64-apple-macos11 -fno-common -O1 -c -o "$obj" "$tmp/t.c"

# macho.py FILE COMMAND...: reads (and for the edits, rewrites) a Mach-O file.
cat > "$tmp/macho.py" <<'PY'
import struct, sys

def load(path):
    f = bytearray(open(path, "rb").read())
    assert struct.unpack_from("<I", f, 0)[0] == 0xFEEDFACF, "not a 64-bit Mach-O file"
    count, at = struct.unpack_from("<I", f, 16)[0], 32
    sections, symtab, commands = [], None, []
    for _ in range(count):
        command, size = struct.unpack_from("<II", f, at)
        commands.append((at, command, size))
        if command == 0x19:  # LC_SEGMENT_64
            for header in range(at + 72, at + 72 + 80 * struct.unpack_from("<I", f, at + 64)[0], 80):
                addr, length, offset = struct.unpack_from("<QQI", f, header + 32)
                flags = struct.unpack_from("<I", f, header + 64)[0]
                sections.append(dict(header=header, addr=addr, size=length, offset=offset,
                                     zerofill=(flags & 0xFF) in (0x1, 0xC, 0x12)))
        elif command == 0x2:  # LC_SYMTAB
            symtab = struct.unpack_from("<IIII", f, at + 8)
        at += size
    symbols = {}
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        for i in range(nsyms):
            record = symoff + 16 * i
            strx, kind, sect, _, value = struct.unpack_from("<IBBHQ", f, record)
            name = bytes(f[stroff + strx:f.index(b"\0", stroff + strx)]).decode()
            symbols[name] = dict(record=record, type=kind, sect=sect, value=value)
    return f, sections, symbols, commands

def offset_of(sections, symbols, name):
    s = symbols[name]
    section = sections[s["sect"] - 1]
    return section["offset"] + s["value"] - section["addr"]

def main():
    path, what = sys.argv[1], sys.argv[2]
    f, sections, symbols, commands = load(path)
    if what == "expect":  # expect ORIGINAL name=bytes...: only those bytes changed
        original = load(sys.argv[3])[0]
        want = {}
        for pair in sys.argv[4:]:
            name, value = pair.split("=", 1)
            value = value.encode().decode("unicode_escape").encode("latin-1")
            o = offset_of(sections, symbols, name)
            for i, b in enumerate(value):
                want[o + i] = b
        assert len(f) == len(original)
        changed = {i: f[i] for i in range(len(f)) if f[i] != original[i]}
        expect = {i: v for i, v in want.items() if original[i] != v}
        assert changed == expect and changed, "changed bytes %s, expected %s" % (changed, expect)
        return
    if what == "absolute":  # make NAME an N_ABS symbol, its section byte kept
        record = symbols[sys.argv[3]]["record"]
        f[record + 4] = (f[record + 4] & ~0x0E) | 0x02
    elif what == "end":  # cut NAME's section at NAME
        s = symbols[sys.argv[3]]
        section = sections[s["sect"] - 1]
        assert s["value"] > section["addr"], "the symbol must not be first in its section"
        struct.pack_into("<Q", f, section["header"] + 40, s["value"] - section["addr"])
    elif what == "far":  # move NAME's section's bytes past the end of the file
        section = sections[symbols[sys.argv[3]]["sect"] - 1]
        struct.pack_into("<I", f, section["header"] + 48, 0x7FFFFF00)
    elif what == "long-command":  # the last load command one byte past the load commands
        at, _, _ = commands[-1]
        commands_size = struct.unpack_from("<I", f, 20)[0]
        struct.pack_into("<I", f, at + 4, 32 + commands_size - at + 1)
    elif what == "zerofill-offset":  # NAME's zero-filled section's header names an offset in the file
        section = sections[symbols[sys.argv[3]]["sect"] - 1]
        assert section["zerofill"]
        struct.pack_into("<I", f, section["header"] + 48, sections[0]["offset"])
    elif what == "tiny-command":  # the first load command 4 bytes long
        struct.pack_into("<I", f, commands[0][0] + 4, 4)
    elif what == "no-symtab":  # LC_SYMTAB renamed to a command patchelf ignores
        at = next(c[0] for c in commands if c[1] == 0x2)
        struct.pack_into("<I", f, at, 0x26)  # LC_FUNCTION_STARTS
    else:
        sys.exit("unknown command " + what)
    open(path, "wb").write(f)

main()
PY
macho() { python3 -I "$tmp/macho.py" "$@"; }

cp "$obj" "$tmp/p.o"
"$patchelf" "$tmp/p.o" gTestRunnerArgv "Random*\0" gTestRunnerHeadless '\x01' gTestRunnerSkipIsFail '\x00' gShort '\x02'
macho "$tmp/p.o" expect "$obj" "_gTestRunnerArgv=Random*\\0" "_gTestRunnerHeadless=\\x01" \
    "_gTestRunnerSkipIsFail=\\x00" "_gShort=\\x02"
echo "patchelf Mach-O object: the named symbols' bytes changed, nothing else"

# Refused for the reason given, and the file left as it was: a refusal must
# come before any write. A refusal is exit status 1 with that message; a
# crash (a write outside the file, say) is not one.
refused_unchanged() {
    local file=$1 what=$2 reason=$3; shift 3
    local status=0
    cp "$file" "$tmp/before"
    "$patchelf" "$file" "$@" 2> "$tmp/err" || status=$?
    if [ "$status" -ne 1 ] || ! grep -qF "$reason" "$tmp/err"; then
        echo "patchelf did not refuse $what with \"$reason\" (exit status $status: $(head -c 120 "$tmp/err"))" >&2; exit 1
    fi
    cmp -s "$tmp/before" "$file" || { echo "patchelf changed the file while refusing $what" >&2; exit 1; }
    echo "patchelf Mach-O object: $what refused ($(head -c 80 "$tmp/err"))"
}
unchanged() {
    local file=$1 what=$2; shift 2
    cp "$file" "$tmp/before"
    "$patchelf" "$file" "$@" || { echo "patchelf failed on $what" >&2; exit 1; }
    cmp -s "$tmp/before" "$file" || { echo "patchelf changed the file for $what" >&2; exit 1; }
    echo "patchelf Mach-O object: $what skipped, the file unchanged"
}
# A value too long is refused once its room is full (try_patch_value writes
# as it goes, on every format), so only the refusal is checked; on a copy.
refused() {
    local what=$1; shift
    local status=0
    cp "$obj" "$tmp/long-value.o"
    "$patchelf" "$tmp/long-value.o" "$@" 2> "$tmp/err" || status=$?
    if [ "$status" -ne 1 ] || [ ! -s "$tmp/err" ]; then
        echo "patchelf did not refuse $what (exit status $status)" >&2; exit 1
    fi
    echo "patchelf Mach-O object: $what refused ($(head -c 80 "$tmp/err"))"
}
refused "a value longer than its symbol's room" gTestRunnerArgv "$(printf 'x%.0s' {1..33})"
refused "a value one byte past the next symbol" gTestRunnerSkipIsFail 'ab'
refused "a value past its section's end" gLast 'ab'
refused_unchanged "$tmp/p.o" "a zero-filled symbol (gBssFirst)" "not stored in the file" gBssFirst '\x01'
refused_unchanged "$tmp/p.o" "a zero-filled symbol (gBssSecond)" "not stored in the file" gBssSecond '\x01'
unchanged "$tmp/p.o" "a name the file lacks" gNoSuchSymbol '\x01'
unchanged "$tmp/p.o" "a symbol without the leading underscore (xgVictim for gVictim)" gVictim '\x01'
cp "$obj" "$tmp/abs.o"; macho "$tmp/abs.o" absolute _gShort
unchanged "$tmp/abs.o" "an absolute symbol" gShort '\x01'
cp "$obj" "$tmp/end.o"; macho "$tmp/end.o" end _gShort
refused_unchanged "$tmp/end.o" "a symbol at the end of its section's bytes" "not stored in the file" gShort 'x'
cp "$obj" "$tmp/far.o"; macho "$tmp/far.o" far _gShort
refused_unchanged "$tmp/far.o" "a section whose bytes lie past the end of the file" "not stored in the file" gShort 'x'
cp "$obj" "$tmp/zero.o"; macho "$tmp/zero.o" zerofill-offset _gBssSecond
refused_unchanged "$tmp/zero.o" "a zero-filled symbol whose section names an offset" "not stored in the file" gBssSecond '\x01'
cp "$obj" "$tmp/long.o"; macho "$tmp/long.o" long-command
refused_unchanged "$tmp/long.o" "a load command running past the load commands" "outside the load commands" gShort 'x'
cp "$obj" "$tmp/tiny.o"; macho "$tmp/tiny.o" tiny-command
refused_unchanged "$tmp/tiny.o" "a load command shorter than a load command's header" "outside the load commands" gShort 'x'
cp "$obj" "$tmp/nosym.o"; macho "$tmp/nosym.o" no-symtab
refused_unchanged "$tmp/nosym.o" "a file with no symbol table" "no Mach-O symbol table" gShort 'x'
