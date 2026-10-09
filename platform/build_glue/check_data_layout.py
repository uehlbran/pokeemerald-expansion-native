"""Check the struct layouts in the 64-bit build's assembled data.

Stands in for nothing on the GBA: it is build glue. The assembler macros
and asm_filter.py lay out the structs the data holds (struct ToneData,
Song, MusicPlayer, SongHeader, the map structs) by hand; at 64 bits that
means 8-byte pointers and the compiler's padding. check_relocations.py
sees only where the addresses are; this script compares every byte.

For each symbol in RULES it takes the struct's layout at 32 and at 64 bits
from data_layout_probe.c (compiled with the game's flags and run by
Makefile.native) and compares the 64-bit object with the same object
assembled with 4-byte pointers (the relocation check's reference): every
member's bytes must be equal (a pointer: an R_X86_64_64 where the
reference has an R_X86_64_32, or else the same value zero-extended), and
every padding byte at 64 must be zero with no relocation in it.

An array's length is found from the distance to the next symbol at both
widths: with n entries of size s32 and s64 and K bytes of other data after
them (the key-split tables after the last voicegroup), the 64-bit
distance exceeds the 32-bit one by n * (s64 - s32) plus the difference of
the alignment padding, between -3 and 7 bytes. Where s64 - s32 is more
than 10 that gives n alone, and the K bytes must then be equal at both
widths; for the structs that grow less the array must reach the next
symbol (K = 0).

Usage:
  python3 -I check_data_layout.py --layout32 FILE --layout64 FILE --objdir DIR --reference DIR
  python3 -I check_data_layout.py --self-test
Exit 0 and no output when consistent; otherwise one line per problem (at
most MAX_REPORTS per symbol) and exit 1.
"""

import bisect
import fnmatch
import glob
import os
import struct
import subprocess
import sys
import tempfile

ONE, ARRAY, SKIP = "one", "array", "skip"
# object glob, symbol glob ('{stem}': the object's name without .o), struct, how
# many; the first matching rule applies. A symbol no rule names is not checked.
RULES = (
    ("data/sound_data.o", "keysplit_*", None, SKIP),   # labels inside the voicegroups
    ("data/sound_data.o", "voicegroup_*", "ToneData", ARRAY),
    ("data/sound_data.o", "gCryTable", "ToneData", ARRAY),
    ("data/sound_data.o", "gCryTable_Reverse", "ToneData", ARRAY),
    ("data/sound_data.o", "gSongTable", "Song", ARRAY),
    ("data/sound_data.o", "gMPlayTable", "MusicPlayer", ARRAY),
    ("sound/songs/midi/*.o", "{stem}", "SongHeader", ONE),
    ("data/maps.o", "*_Layout", "MapLayout", ONE),
    ("data/maps.o", "*_MapConnections", "MapConnections", ONE),
    ("data/maps.o", "*_Layout_*", None, ONE),            # border and block data
    ("data/maps.o", "*_MapConnectionsList", None, ONE),  # struct MapConnection has no pointer
    ("data/maps.o", "gMap*", None, ONE),                 # pointer arrays: check_relocations.py
    ("data/maps.o", "*", "MapHeader", ONE),
    ("data/map_events.o", "*_ObjectEvents", "ObjectEventTemplate", ARRAY),
    ("data/map_events.o", "*_MapCoordEvents", "CoordEvent", ARRAY),
    ("data/map_events.o", "*_MapBGEvents", "BgEvent", ARRAY),
    ("data/map_events.o", "*_MapEvents", "MapEvents", ONE),
)
COUNT_FROM_GROWTH = 10      # s64 - s32 above this: n follows from the distances alone
ALIGNMENT_SLACK = 3         # the 32-bit padding before a symbol is at most 3 bytes,
POINTER_ALIGNMENT_SLACK = 7  # the 64-bit padding at most 7
POINTER_ARRAY_COUNT = "trackCount"   # SongHeader.part has one pointer per track
MAX_REPORTS = 10

R_X86_64_64 = 1
R_X86_64_32 = 10
SHT_SYMTAB, SHT_RELA, SHT_NOBITS = 2, 4, 8
STT_SECTION, STT_FILE = 3, 4
SHN_LORESERVE = 0xFF00


class CheckError(Exception):
    """Input this script cannot read; the build must stop."""


# --------------------------------------------------------------------------
# Inputs

class Layout:
    """One struct: its size and its members (name, offset, size, kind)."""

    def __init__(self, size):
        self.size = size
        self.members = []


def read_layouts(text):
    layouts = {}
    for line in text.splitlines():
        fields = line.split()
        if fields[:1] == ["struct"] and len(fields) == 3:
            layouts[fields[1]] = Layout(int(fields[2]))
        elif fields[:1] == ["member"] and len(fields) == 6 and fields[1] in layouts:
            layouts[fields[1]].members.append((fields[2], int(fields[3]), int(fields[4]), fields[5]))
        elif fields:
            raise CheckError("cannot read the layout line %r" % line)
    return layouts


class ElfObject:
    """The sections, symbols and relocations of an ELF64 relocatable object."""

    def __init__(self, data):
        if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
            raise CheckError("not a little-endian ELF64 object")
        shoff, = struct.unpack_from("<Q", data, 0x28)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
        headers = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + index * shentsize) for index in range(shnum)]
        names = headers[shstrndx]

        def string(table, offset):
            start = table[4] + offset
            return data[start:data.index(b"\0", start)].decode()

        self.sections = {}     # index -> (name, bytes)
        for index, header in enumerate(headers):
            contents = b"" if header[1] == SHT_NOBITS else data[header[4]:header[4] + header[5]]
            self.sections[index] = (string(names, header[0]), contents, header[5])
        self.symbols = []      # (name, section index, value)
        self.relocations = {}  # section index -> {offset: type}
        for header in headers:
            if header[1] == SHT_SYMTAB:
                strings = headers[header[6]]
                for start in range(header[4], header[4] + header[5], 24):
                    name, info, _, shndx, value, _ = struct.unpack_from("<IBBHQQ", data, start)
                    if info & 0xF not in (STT_SECTION, STT_FILE) and 0 < shndx < SHN_LORESERVE:
                        self.symbols.append((string(strings, name), shndx, value))
            elif header[1] == SHT_RELA:
                target = self.relocations.setdefault(header[7], {})
                for start in range(header[4], header[4] + header[5], 24):
                    offset, info, _ = struct.unpack_from("<QQq", data, start)
                    target[offset] = info & 0xFFFFFFFF


def read_object(path):
    with open(path, "rb") as handle:
        return ElfObject(handle.read())


# --------------------------------------------------------------------------
# The check

def rule_for(object_name, symbol):
    stem = os.path.splitext(os.path.basename(object_name))[0]
    for object_glob, symbol_glob, layout, count in RULES:
        if fnmatch.fnmatchcase(object_name, object_glob) and \
                fnmatch.fnmatchcase(symbol, symbol_glob.replace("{stem}", stem)):
            return layout, count
    return None, ONE


def extents(obj, object_name):
    """symbol name -> (section index, value, bytes to the next symbol or the
    section's end); SKIP symbols are not ends."""
    starts = {}
    for name, section, value in obj.symbols:
        if rule_for(object_name, name)[1] != SKIP:
            starts.setdefault(section, set()).add(value)
    starts = {section: sorted(values) for section, values in starts.items()}
    result = {}
    for name, section, value in obj.symbols:
        values = starts.get(section, [])
        index = bisect.bisect_right(values, value)
        end = values[index] if index < len(values) else obj.sections[section][2]
        result[name] = (section, value, end - value)
    return result


def array_length(name, layout32, layout64, range32, range64):
    growth = layout64.size - layout32.size
    if growth > COUNT_FROM_GROWTH:
        count = (range64 - range32 + ALIGNMENT_SLACK) // growth
        if count < 0 or count * layout32.size > range32 or count * layout64.size > range64:
            return None, "%s: %d and %d bytes to the next symbol fit no array of %d- and %d-byte entries" \
                % (name, range32, range64, layout32.size, layout64.size)
        return count, None
    count = range32 // layout32.size
    if range64 // layout64.size != count or range32 - count * layout32.size > ALIGNMENT_SLACK:
        return None, "%s: %d and %d bytes to the next symbol hold %d and %d entries of %d and %d bytes" \
            % (name, range32, range64, count, range64 // layout64.size, layout32.size, layout64.size)
    return count, None


def compare_entry(where, obj, ref, at64, at32, layout32, layout64, problems):
    """Compare one struct at obj[at64] with ref[at32]; at64, at32 are
    (section index, offset)."""
    section64, base64 = at64
    section32, base32 = at32
    bytes64, bytes32 = obj.sections[section64][1], ref.sections[section32][1]
    relocations64 = obj.relocations.get(section64, {})
    relocations32 = ref.relocations.get(section32, {})
    members32 = {name: (offset, size) for name, offset, size, _ in layout32.members}
    covered = set()
    size64 = layout64.size
    for name, offset64, member_size64, kind in layout64.members:
        offset32, member_size32 = members32[name]
        repeat = 1
        if kind == "pointers":
            count_offset = members32[POINTER_ARRAY_COUNT][0]
            repeat = bytes32[base32 + count_offset]
            size64 = offset64 + repeat * member_size64   # a trailing array: as long as the data says
        for index in range(repeat):
            start64 = base64 + offset64 + index * member_size64
            start32 = base32 + offset32 + index * member_size32
            covered.update(range(start64, start64 + member_size64))
            field = "%s%s" % (name, "[%d]" % index if kind == "pointers" else "")
            if kind == "value":
                if bytes64[start64:start64 + member_size64] != bytes32[start32:start32 + member_size32]:
                    problems.append("%s: %s differs from the 4-byte-pointer data" % (where, field))
                inner64 = {o - start64: relocations64[o] for o in range(start64, start64 + member_size64)
                           if o in relocations64}
                inner32 = {o - start32: relocations32[o] for o in range(start32, start32 + member_size32)
                           if o in relocations32}
                if inner64 != inner32:
                    problems.append("%s: %s relocations %r, with 4-byte pointers %r" % (where, field, inner64, inner32))
                continue
            moved = relocations64.get(start64)
            if relocations32.get(start32) == R_X86_64_32:
                if moved != R_X86_64_64:
                    problems.append("%s: %s is an address with 4-byte pointers, not an 8-byte one here" % (where, field))
            elif moved is not None:
                problems.append("%s: %s has a relocation here only" % (where, field))
            elif int.from_bytes(bytes64[start64:start64 + member_size64], "little") != \
                    int.from_bytes(bytes32[start32:start32 + member_size32], "little"):
                problems.append("%s: %s differs from the 4-byte-pointer data" % (where, field))
    for offset in range(base64, min(base64 + size64, len(bytes64))):
        if offset not in covered and (bytes64[offset] != 0 or offset in relocations64):
            problems.append("%s: padding byte %d is not zero" % (where, offset - base64))
            break
    return size64


def check_object(name, obj, ref, layouts32, layouts64):
    problems = []
    extents64, extents32 = extents(obj, name), extents(ref, name)
    for symbol, section, _ in obj.symbols:
        layout_name, count = rule_for(name, symbol)
        if layout_name is None:
            continue
        if layout_name not in layouts64 or layout_name not in layouts32:
            raise CheckError("no layout for struct %s" % layout_name)
        if symbol not in extents32:
            problems.append("%s: %s is not in the 4-byte-pointer object" % (name, symbol))
            continue
        layout32, layout64 = layouts32[layout_name], layouts64[layout_name]
        section64, value64, range64 = extents64[symbol]
        section32, value32, range32 = extents32[symbol]
        where = "%s: %s" % (name, symbol)
        found = []
        if count == ARRAY:
            length, problem = array_length(where, layout32, layout64, range32, range64)
            if problem:
                problems.append(problem)
                continue
            for index in range(length):
                compare_entry("%s[%d]" % (where, index), obj, ref, (section64, value64 + index * layout64.size),
                              (section32, value32 + index * layout32.size), layout32, layout64, found)
            # What follows the array up to the next symbol is the same data at
            # both widths, less the padding at its end.
            rest32 = range32 - length * layout32.size
            rest64 = range64 - length * layout64.size
            same = max(0, min(rest32 - ALIGNMENT_SLACK, rest64 - POINTER_ALIGNMENT_SLACK))
            after64 = value64 + length * layout64.size
            after32 = value32 + length * layout32.size
            if obj.sections[section64][1][after64:after64 + same] != ref.sections[section32][1][after32:after32 + same]:
                found.append("%s: the %d bytes after its %d entries differ from the 4-byte-pointer data"
                             % (where, same, length))
        else:
            size = compare_entry(where, obj, ref, (section64, value64), (section32, value32), layout32, layout64, found)
            if size > range64:
                found.append("%s: the %d-byte struct runs into the next symbol, %d bytes on" % (where, size, range64))
        problems.extend(found[:MAX_REPORTS])
        if len(found) > MAX_REPORTS:
            problems.append("%s: %d more problems" % (where, len(found) - MAX_REPORTS))
    return problems


def objects_to_check(objdir):
    names = []
    for object_glob in sorted({rule[0] for rule in RULES}):
        names.extend(sorted(os.path.relpath(path, objdir) for path in glob.glob(os.path.join(objdir, object_glob))))
    if not names:
        raise CheckError("no data objects under %s" % objdir)
    return names


def check(layout32_text, layout64_text, objdir, reference):
    layouts32, layouts64 = read_layouts(layout32_text), read_layouts(layout64_text)
    problems = []
    for name in objects_to_check(objdir):
        problems.extend(check_object(name, read_object(os.path.join(objdir, name)),
                                     read_object(os.path.join(reference, name)), layouts32, layouts64))
    return problems


# --------------------------------------------------------------------------
# Self-test: tiny objects made by the host assembler

SELF_TEST_LAYOUT32 = """struct ToneData 12
member ToneData type 0 1 value
member ToneData key 1 1 value
member ToneData wav 4 4 pointer
member ToneData attack 8 1 value
struct SongHeader 12
member SongHeader trackCount 0 1 value
member SongHeader tone 4 4 pointer
member SongHeader part 8 4 pointers
"""
SELF_TEST_LAYOUT64 = """struct ToneData 24
member ToneData type 0 1 value
member ToneData key 1 1 value
member ToneData wav 8 8 pointer
member ToneData attack 16 1 value
struct SongHeader 24
member SongHeader trackCount 0 1 value
member SongHeader tone 8 8 pointer
member SongHeader part 16 8 pointers
"""
# Two voicegroups, a key-split label inside the second and key-split bytes
# after it; a square voice keeps its duty byte in the pointer slot.
SELF_TEST_SOUND32 = """
	.section .rodata
voicegroup_a:
	.byte 0, 60, 0, 0 ; .4byte sample ; .byte 1, 0, 0, 0
voicegroup_b:
	.byte 1, 61, 0, 0 ; .4byte 2 ; .byte 2, 0, 0, 0
keysplit_x:
	.byte 3, 62, 0, 0 ; .4byte sample ; .byte 3, 0, 0, 0
	.byte 9, 9, 9, 9, 9
	.p2align 2
other:
	.byte 7
"""
SELF_TEST_SOUND64 = """
	.section .rodata
voicegroup_a:
	.byte 0, 60, 0, 0 ; .space 4 ; .8byte sample ; .byte 1, 0, 0, 0 ; .space 4
voicegroup_b:
	.byte 1, 61, 0, 0 ; .space 4 ; .8byte 2 ; .byte 2, 0, 0, 0 ; .space 4
keysplit_x:
	.byte 3, 62, 0, 0 ; .space 4 ; .8byte sample ; .byte 3, 0, 0, 0 ; .space 4
	.byte 9, 9, 9, 9, 9
	.p2align 2
other:
	.byte 7
"""
SELF_TEST_SONG32 = """
	.section .rodata
mus_a_1:
	.byte 0xb1
mus_a:
	.byte 2, 0, 0, 0 ; .4byte voicegroup_a ; .4byte mus_a_1, mus_a_1
"""
SELF_TEST_SONG64 = """
	.section .rodata
mus_a_1:
	.byte 0xb1
	.balign 8
mus_a:
	.byte 2, 0, 0, 0 ; .space 4 ; .8byte voicegroup_a ; .8byte mus_a_1, mus_a_1
"""


def self_test_run(directory, sources):
    """Assemble {path: (text32, text64)} and check them."""
    for name, (text32, text64) in sources.items():
        for subdirectory, text in (("ref", text32), ("obj", text64)):
            path = os.path.join(directory, subdirectory, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            subprocess.run(["as", "--64", "-o", path], input=text, universal_newlines=True, check=True)
    try:
        return check(SELF_TEST_LAYOUT32, SELF_TEST_LAYOUT64, os.path.join(directory, "obj"),
                     os.path.join(directory, "ref"))
    except CheckError as error:
        return ["CheckError: %s" % error]


def self_test():
    cases = (
        ("consistent", SELF_TEST_SOUND64, SELF_TEST_SONG64, []),
        ("padding not zero", SELF_TEST_SOUND64.replace("2, 0, 0, 0 ; .space 4", "2, 0, 0, 0 ; .byte 0, 0, 5, 0"),
         SELF_TEST_SONG64, ["data/sound_data.o: voicegroup_b[0]: padding byte 22 is not zero"]),
        ("pointer left at 4 bytes", SELF_TEST_SOUND64.replace(".8byte sample ; .byte 1", ".4byte sample ; .4byte 0 ; .byte 1"),
         SELF_TEST_SONG64, ["voicegroup_a[0]: wav is an address with 4-byte pointers, not an 8-byte one here"]),
        ("duty byte wrong", SELF_TEST_SOUND64.replace(".8byte 2 ;", ".8byte 3 ;"), SELF_TEST_SONG64,
         ["voicegroup_b[0]: wav differs from the 4-byte-pointer data"]),
        ("stride wrong", SELF_TEST_SOUND64.replace("; .byte 2, 0, 0, 0 ; .space 4", "; .byte 2, 0, 0, 0"),
         SELF_TEST_SONG64, ["voicegroup_b[0]: padding byte 20 is not zero",
                            "voicegroup_b: the 17 bytes after its 1 entries differ"]),
        ("song header unpadded", SELF_TEST_SOUND64, SELF_TEST_SONG64.replace(" ; .space 4", ""),
         ["mus_a: tone is an address with 4-byte pointers, not an 8-byte one here",
          "mus_a: part[0] is an address with 4-byte pointers, not an 8-byte one here",
          "mus_a: part[1] is an address with 4-byte pointers, not an 8-byte one here",
          "mus_a: padding byte 4 is not zero",
          "mus_a: the 32-byte struct runs into the next symbol, 28 bytes on"]),
        ("array cut short", SELF_TEST_SOUND64.replace("\t.byte 9, 9, 9, 9, 9\n", ""), SELF_TEST_SONG64,
         ["voicegroup_b: the 17 bytes after its 1 entries differ"]),
    )
    failures = []
    for name, sound64, song64, expected in cases:
        with tempfile.TemporaryDirectory() as directory:
            problems = self_test_run(directory, {"data/sound_data.o": (SELF_TEST_SOUND32, sound64),
                                                 "sound/songs/midi/mus_a.o": (SELF_TEST_SONG32, song64)})
        if len(problems) != len(expected) or any(e not in p for e, p in zip(expected, problems)):
            failures.append("%s\n  expected %r\n  got      %r" % (name, expected, problems))
    for failure in failures:
        sys.stderr.write("check_data_layout self-test: %s\n" % failure)
    return 1 if failures else 0


def main(argv):
    args = argv[1:]
    if args == ["--self-test"]:
        return self_test()
    options = {}
    while len(args) >= 2 and args[0] in ("--layout32", "--layout64", "--objdir", "--reference"):
        options[args[0]] = args[1]
        args = args[2:]
    if len(options) != 4 or args:
        sys.stderr.write("usage: check_data_layout.py --layout32 FILE --layout64 FILE --objdir DIR --reference DIR"
                         " | --self-test\n")
        return 2
    try:
        with open(options["--layout32"]) as handle:
            layout32 = handle.read()
        with open(options["--layout64"]) as handle:
            layout64 = handle.read()
        problems = check(layout32, layout64, options["--objdir"], options["--reference"])
    except CheckError as error:
        sys.stderr.write("check_data_layout: %s\n" % error)
        return 1
    for problem in problems:
        sys.stderr.write("check_data_layout: %s\n" % problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
