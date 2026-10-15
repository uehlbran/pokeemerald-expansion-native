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
# the next symbol of its section at a higher address (another symbol at the
# same address, a debugging or an absolute symbol does not end it); a value
# longer than that is refused; only N_SECT symbols whose name has Mach-O's
# leading underscore, lies inside the string table with its NUL, and whose
# section index names a section match (a symbol named "xgVictim" is not
# "gVictim"; an absolute or a debugging symbol is not patched); and these are
# refused with the file left as it was and the reason named: a zero-filled
# symbol (of each of the three zero-filled types, also when its section's
# header names an offset), a symbol at or past its section's end, a section
# whose bytes end past the end of the file, a load command whose header or
# size runs past the load commands or whose size is below the smallest, a
# segment with more sections than its command holds, more than 255 sections
# in all, a symbol or string table that runs past the end of the file, a
# short LC_SYMTAB, and a file with no symbol table. Each bound is tried one
# past what the file holds.
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

def sections_file(path, counts):
    # A Mach-O object with one segment of COUNT empty zero-filled sections
    # for each count, and an empty symbol table.
    segments = b""
    for count in counts:
        segments += struct.pack("<II16sQQQQIIII", 0x19, 72 + 80 * count, b"", 0, 0, 0, 0, 3, 3, count, 0)
        segments += struct.pack("<16s16sQQIIIIIIII", b"__s", b"__DATA", 0, 0, 0, 0, 0, 0, 0x1, 0, 0, 0) * count
    commands = segments + struct.pack("<IIIIII", 0x2, 24, 0, 0, 0, 0)
    end = 32 + len(commands)
    commands = commands[:-16] + struct.pack("<IIII", end, 0, end, 0)
    header = struct.pack("<IIIIIIII", 0xFEEDFACF, 0x01000007, 3, 0x1, len(counts) + 1, len(commands), 0, 0)
    open(path, "wb").write(header + commands)

def main():
    path, what = sys.argv[1], sys.argv[2]
    if what == "sections":  # sections COUNT...: a new file, its segments holding COUNT sections each
        sections_file(path, [int(count) for count in sys.argv[3:]])
        return
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
    elif what == "short-symtab":  # LC_SYMTAB 16 bytes long, an 8-byte command after it
        at = next(c[0] for c in commands if c[1] == 0x2)
        struct.pack_into("<IIII", f, at + 4, 16, 0, 0, 0)  # an offset of 0 and no symbols, were they read
        struct.pack_into("<II", f, at + 16, 0x26, 8)
        struct.pack_into("<I", f, 16, len(commands) + 1)
    elif what == "header-cut":  # one command more than the load commands hold, 4 bytes of its header in them
        struct.pack_into("<II", f, 16, len(commands) + 1, struct.unpack_from("<I", f, 20)[0] + 4)
    elif what == "extra-section":  # NAME's segment says it has one section more than its command holds
        at = max(c[0] for c in commands if c[0] < sections[symbols[sys.argv[3]]["sect"] - 1]["header"])
        struct.pack_into("<I", f, at + 64, struct.unpack_from("<I", f, at + 64)[0] + 1)
    elif what == "section-past-end":  # NAME's section's bytes end one byte past the end of the file
        section = sections[symbols[sys.argv[3]]["sect"] - 1]
        struct.pack_into("<Q", f, section["header"] + 40, len(f) - section["offset"] + 1)
    elif what == "section-type":  # NAME's section's type (the low byte of its flags) set to TYPE
        section = sections[symbols[sys.argv[3]]["sect"] - 1]
        f[section["header"] + 64] = int(sys.argv[4], 0)
    elif what == "section-index":  # NAME's n_sect set to N, or to "past" the last section
        n = len(sections) + 1 if sys.argv[4] == "past" else int(sys.argv[4])
        f[symbols[sys.argv[3]]["record"] + 5] = n
    elif what == "place":  # place NAME TYPE AT OFFSET: NAME's n_type TYPE, its section and address AT's plus OFFSET
        record, at = symbols[sys.argv[3]]["record"], symbols[sys.argv[5]]
        f[record + 4] = int(sys.argv[4], 0)
        f[record + 5] = at["sect"]
        struct.pack_into("<Q", f, record + 8, at["value"] + int(sys.argv[6]))
    elif what in ("symbols-past-end", "strings-past-end", "strings-end-at", "strings-cut-nul"):
        at = next(c[0] for c in commands if c[1] == 0x2)
        symoff, nsyms, stroff, strsize = struct.unpack_from("<IIII", f, at + 8)
        if what == "symbols-past-end":  # one symbol record more than fits before the end of the file
            nsyms = (len(f) - symoff) // 16 + 1
        elif what == "strings-past-end":  # the string table one byte past the end of the file
            strsize = len(f) - stroff + 1
        else:  # the string table ends where NAME starts, or just before NAME's NUL
            strx = struct.unpack_from("<I", f, symbols[sys.argv[3]]["record"])[0]
            strsize = strx if what == "strings-end-at" else strx + len(sys.argv[3])
            assert f[stroff + strsize] == (ord("_") if what == "strings-end-at" else 0)
        struct.pack_into("<IIII", f, at + 8, symoff, nsyms, stroff, strsize)
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
cp "$obj" "$tmp/cut.o"; macho "$tmp/cut.o" header-cut
refused_unchanged "$tmp/cut.o" "a load command whose header the load commands cut short" "header is past the load commands" gShort 'x'
cp "$obj" "$tmp/symtab.o"; macho "$tmp/symtab.o" short-symtab
refused_unchanged "$tmp/symtab.o" "an LC_SYMTAB shorter than its fields" "LC_SYMTAB is too short" gShort 'x'

# Each bound of the sections and the symbol table, one past what the file
# holds: refused, the file as it was.
cp "$obj" "$tmp/nsects.o"; macho "$tmp/nsects.o" extra-section _gShort
refused_unchanged "$tmp/nsects.o" "a segment with one section more than its command holds" "sections are outside its load command" gShort 'x'
macho "$tmp/256.o" sections 128 128
refused_unchanged "$tmp/256.o" "256 sections in two segments" "more than 255 sections" gShort 'x'
macho "$tmp/255.o" sections 128 127
unchanged "$tmp/255.o" "255 sections in two segments (no symbols)" gShort 'x'
cp "$obj" "$tmp/past.o"; macho "$tmp/past.o" section-past-end _gLast
refused_unchanged "$tmp/past.o" "a section whose bytes end one byte past the end of the file" "not stored in the file" gLast 'x'
cp "$obj" "$tmp/syms.o"; macho "$tmp/syms.o" symbols-past-end
refused_unchanged "$tmp/syms.o" "a symbol table one record past the end of the file" "symbol table is outside the file" gShort 'x'
cp "$obj" "$tmp/strs.o"; macho "$tmp/strs.o" strings-past-end
refused_unchanged "$tmp/strs.o" "a string table one byte past the end of the file" "symbol table is outside the file" gShort 'x'
# Mach-O's other two zero-filled section types have no bytes in the file
# either, whatever offset their header names.
for type in 0xc 0x12; do
    cp "$obj" "$tmp/zero$type.o"
    macho "$tmp/zero$type.o" section-type _gBssSecond "$type"; macho "$tmp/zero$type.o" zerofill-offset _gBssSecond
    refused_unchanged "$tmp/zero$type.o" "a symbol in a zero-filled section of type $type that names an offset" \
        "not stored in the file" gBssSecond '\x01'
done

# A name matches only inside the string table, its NUL included; a symbol
# matches only if its section index names one of the file's sections, and
# not if it is a debugging (N_STAB) symbol, here N_BNSYM, whose type bits
# read as N_SECT.
cp "$obj" "$tmp/at.o"; macho "$tmp/at.o" strings-end-at _gTestRunnerSkipIsFail
unchanged "$tmp/at.o" "a name that starts at the string table's end" gTestRunnerSkipIsFail '\x5a'
cp "$obj" "$tmp/nul.o"; macho "$tmp/nul.o" strings-cut-nul _gTestRunnerSkipIsFail
unchanged "$tmp/nul.o" "a name whose NUL is past the string table's end" gTestRunnerSkipIsFail '\x5a'
cp "$obj" "$tmp/nosect.o"; macho "$tmp/nosect.o" section-index _gShort 0
unchanged "$tmp/nosect.o" "an N_SECT symbol with no section (n_sect 0)" gShort '\x5a'
cp "$obj" "$tmp/badsect.o"; macho "$tmp/badsect.o" section-index _gShort past
unchanged "$tmp/badsect.o" "an N_SECT symbol whose n_sect is one past the last section" gShort '\x5a'
cp "$obj" "$tmp/stab.o"; macho "$tmp/stab.o" place _gShort 0x2e _gShort 0
unchanged "$tmp/stab.o" "a debugging symbol" gShort '\x5a'

# A symbol's room ends at the next symbol at a higher address: another
# symbol at the same address, a debugging symbol and an absolute symbol do
# not end it.
patched() {
    local file=$1 what=$2 symbol=$3 value=$4
    cp "$file" "$tmp/before"
    "$patchelf" "$file" "$symbol" "$value" || { echo "patchelf did not patch $symbol with $what" >&2; exit 1; }
    macho "$file" expect "$tmp/before" "_$symbol=$value"
    echo "patchelf Mach-O object: $symbol patched with $what, nothing else changed"
}
cp "$obj" "$tmp/alias.o"; macho "$tmp/alias.o" place xgVictim 0x0f _gShort 0
patched "$tmp/alias.o" "a second symbol at its address" gShort 'ab'
cp "$obj" "$tmp/inside-stab.o"; macho "$tmp/inside-stab.o" place xgVictim 0x2e _gTestRunnerArgv 8
patched "$tmp/inside-stab.o" "a debugging symbol 8 bytes in" gTestRunnerArgv 'RandomSeed\0'
cp "$obj" "$tmp/inside-abs.o"; macho "$tmp/inside-abs.o" place xgVictim 0x03 _gTestRunnerArgv 8
patched "$tmp/inside-abs.o" "an absolute symbol 8 bytes in" gTestRunnerArgv 'RandomSeed\0'
