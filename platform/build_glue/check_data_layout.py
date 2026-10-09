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

import collections
import glob
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, capped, matching_rule, parse_options, problems_match  # noqa: E402
from elf_object import R_X86_64_32, R_X86_64_64, read_elf64  # noqa: E402

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
SCALAR_SIZES = (1, 2, 4, 8)
ALIGNMENT_SLACK = 3         # the 32-bit padding before a symbol is at most 3 bytes,
POINTER_ALIGNMENT_SLACK = 7  # the 64-bit padding at most 7
POINTER_ARRAY_COUNT = "trackCount"   # SongHeader.part has one pointer per track
MAX_REPORTS = 10

# The words of the probe's output (data_layout_probe.c prints them).
LAYOUT_STRUCT, LAYOUT_MEMBER = "struct", "member"
KIND_VALUE, KIND_POINTER, KIND_POINTERS = "value", "pointer", "pointers"
KINDS = (KIND_VALUE, KIND_POINTER, KIND_POINTERS)
LAYOUT_LINE_FORMS = "'%s NAME SIZE' or '%s STRUCT FIELD OFFSET SIZE %s'" % (LAYOUT_STRUCT, LAYOUT_MEMBER, "|".join(KINDS))


# --------------------------------------------------------------------------
# Inputs

class Layout:
    """One struct as the layout file at path describes it: its size and its
    members (name, offset, size, kind)."""

    def __init__(self, name, size, path):
        self.name, self.size, self.path = name, size, path
        self.members = []

    def alignment_of(self, size):
        """The alignment of a member of this size: its size if that is a
        scalar's, else 1 (a byte array or a blob of bytes)."""
        return size if size in SCALAR_SIZES else 1

    def is_padding(self, offset):
        """True if the byte at offset lies in a gap the compiler's alignment
        explains: before a member at its natural alignment, or at the end of
        the struct. Any other gap is a member the probe does not print."""
        previous_end = max([o + s for _, o, s, _ in self.members if o + s <= offset], default=0)
        later = [(o, s) for _, o, s, _ in self.members if o > offset]
        if later:
            next_offset, next_size = min(later)
            alignment = self.alignment_of(next_size)
        else:
            next_offset = self.size
            alignment = max([self.alignment_of(s) for _, _, s, _ in self.members], default=1)
        return next_offset == -(-previous_end // alignment) * alignment

    def member_places(self):
        """Member name -> (offset, size)."""
        return {name: (offset, size) for name, offset, size, _ in self.members}


def parse_layouts(text, path):
    """{struct name: Layout} from the probe's output, read from path."""
    layouts = {}
    for number, line in enumerate(text.splitlines(), 1):
        fields = line.split()
        if fields[:1] == [LAYOUT_STRUCT] and len(fields) == 3 and fields[2].isdigit():
            layouts[fields[1]] = Layout(fields[1], int(fields[2]), path)
        elif (fields[:1] == [LAYOUT_MEMBER] and len(fields) == 6 and fields[1] in layouts
              and fields[3].isdigit() and fields[4].isdigit() and fields[5] in KINDS):
            layouts[fields[1]].members.append((fields[2], int(fields[3]), int(fields[4]), fields[5]))
        elif fields:
            raise CheckError("%s:%d: expected %s (a member after its struct), got %r"
                             % (path, number, LAYOUT_LINE_FORMS, line))
    return layouts


def read_layouts(path):
    with open(path) as handle:
        return parse_layouts(handle.read(), path)


# --------------------------------------------------------------------------
# The check

def rule_for(object_name, symbol):
    index = matching_rule(RULES, object_name, symbol)
    return (RULES[index][2], RULES[index][3]) if index is not None else (None, ONE)


def extents(obj, object_name):
    """symbol name -> (section key, value, bytes to the next symbol or the
    section's end); SKIP symbols are not ends."""
    lengths = obj.extents(lambda symbol: rule_for(object_name, symbol.name)[1] != SKIP)
    return {symbol.name: (symbol.section, symbol.value, length) for symbol, length in zip(obj.symbols, lengths)}


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


class Side:
    """Where a struct lies in one object's section: the section's bytes and
    relocations, and the struct's offset in them."""

    def __init__(self, contents, relocations, base):
        self.contents, self.relocations, self.base = contents, relocations, base

    def shifted(self, offset):
        return Side(self.contents, self.relocations, self.base + offset)

    def span(self, offset, size):
        return Span(self, self.base + offset, size)


class Span:
    """size bytes at start in a Side's section."""

    def __init__(self, side, start, size):
        self.side, self.start, self.size = side, start, size

    def data(self):
        return self.side.contents[self.start:self.start + self.size]

    def offsets(self):
        return range(self.start, self.start + self.size)

    def relocations(self):
        """{offset from start: relocation type} of the relocations inside."""
        return {o - self.start: self.side.relocations[o] for o in self.offsets() if o in self.side.relocations}

    def address_type(self):
        return self.side.relocations.get(self.start)

    def value(self):
        return int.from_bytes(self.data(), "little")


def compare_value(where, field, span64, span32):
    """Problems with a member that holds no address: the same bytes and the
    same relocations (an address inside a bitfield-like blob) at both widths."""
    problems = []
    if span64.data() != span32.data():
        problems.append("%s: %s differs from the 4-byte-pointer data" % (where, field))
    if span64.relocations() != span32.relocations():
        problems.append("%s: %s relocations %r, with 4-byte pointers %r"
                        % (where, field, span64.relocations(), span32.relocations()))
    return problems


def compare_address(where, field, span64, span32):
    """Problems with a pointer member: an 8-byte address where there is a
    4-byte one in the reference, else the same value zero-extended."""
    if span32.address_type() == R_X86_64_32:
        if span64.address_type() != R_X86_64_64:
            return ["%s: %s is an address with 4-byte pointers, not an 8-byte one here" % (where, field)]
    elif span64.address_type() is not None:
        return ["%s: %s has a relocation here only" % (where, field)]
    elif span64.value() != span32.value():
        return ["%s: %s differs from the 4-byte-pointer data" % (where, field)]
    return []


def padding_problem(where, side64, layout64, size64, covered):
    """The first byte of the struct's size64 bytes that no member covers and
    is not padding, or is padding that is not zero or holds a relocation, or None."""
    for offset in range(side64.base, min(side64.base + size64, len(side64.contents))):
        if offset in covered:
            continue
        if not layout64.is_padding(offset - side64.base):
            return "%s: no layout entry covers offset %d of struct %s in %s (the probe does not print a member there)" \
                % (where, offset - side64.base, layout64.name, layout64.path)
        if side64.contents[offset] != 0 or offset in side64.relocations:
            return "%s: padding byte %d is not zero" % (where, offset - side64.base)
    return None


def compare_entry(where, side64, side32, layout32, layout64):
    """(problems, the entry's size at 64 bits) from comparing one struct at
    side64 with the same struct at side32. A trailing pointer array is as
    long as the data says."""
    places32 = layout32.member_places()
    problems = []
    covered = set()
    size64 = layout64.size
    for name, offset64, member_size64, kind in layout64.members:
        if name not in places32:
            raise CheckError("%s: member %s is in the 64-bit layout but not in the 32-bit one (%s, %s)"
                             % (where, name, layout32.path, layout64.path))
        offset32, member_size32 = places32[name]
        repeat = 1
        if kind == KIND_POINTERS:
            count_offset = places32[POINTER_ARRAY_COUNT][0]
            repeat = side32.contents[side32.base + count_offset]
            size64 = offset64 + repeat * member_size64
        for index in range(repeat):
            span64 = side64.span(offset64 + index * member_size64, member_size64)
            span32 = side32.span(offset32 + index * member_size32, member_size32)
            covered.update(span64.offsets())
            field = "%s%s" % (name, "[%d]" % index if kind == KIND_POINTERS else "")
            compare = compare_value if kind == KIND_VALUE else compare_address
            problems.extend(compare(where, field, span64, span32))
    problem = padding_problem(where, side64, layout64, size64, covered)
    if problem:
        problems.append(problem)
    return problems, size64


class ObjectPair:
    """An object and its 4-byte-pointer reference, with the struct layouts at
    both widths (the LayoutFiles)."""

    def __init__(self, name, obj, reference, layouts32, layouts64):
        self.name, self.obj, self.reference = name, obj, reference
        self.layouts32, self.layouts64 = layouts32, layouts64

    def layouts_of(self, struct):
        for layouts in (self.layouts32, self.layouts64):
            if struct not in layouts.structs:
                raise CheckError("%s: the rules name struct %s, which %s does not describe"
                                 % (self.name, struct, layouts.path))
        return self.layouts32.structs[struct], self.layouts64.structs[struct]

    def sides(self, section64, value64, section32, value32):
        return (Side(self.obj.sections[section64].contents, self.obj.relocations.get(section64, {}), value64),
                Side(self.reference.sections[section32].contents, self.reference.relocations.get(section32, {}), value32))

    def compare(self, where, at64, at32, layout32, layout64):
        side64, side32 = self.sides(*at64, *at32)
        return compare_entry(where, side64, side32, layout32, layout64)


def check_array(pair, where, at64, at32, ranges, layouts):
    """Problems with an array of structs at at64 and at32 (section key and
    offset), range64 and range32 bytes from the next symbol."""
    (section64, value64), (section32, value32) = at64, at32
    range64, range32 = ranges
    layout32, layout64 = layouts
    length, problem = array_length(where, layout32, layout64, range32, range64)
    if problem:
        return [problem]
    found = []
    for index in range(length):
        entry_problems, _ = pair.compare("%s[%d]" % (where, index), (section64, value64 + index * layout64.size),
                                         (section32, value32 + index * layout32.size), layout32, layout64)
        found.extend(entry_problems)
    # What follows the array up to the next symbol is the same data at
    # both widths, less the padding at its end.
    rest32 = range32 - length * layout32.size
    rest64 = range64 - length * layout64.size
    same = max(0, min(rest32 - ALIGNMENT_SLACK, rest64 - POINTER_ALIGNMENT_SLACK))
    after64 = value64 + length * layout64.size
    after32 = value32 + length * layout32.size
    if pair.obj.sections[section64].contents[after64:after64 + same] \
            != pair.reference.sections[section32].contents[after32:after32 + same]:
        found.append("%s: the %d bytes after its %d entries differ from the 4-byte-pointer data" % (where, same, length))
    return found


def check_one(pair, where, at64, at32, range64, layouts):
    """Problems with a single struct at at64 and at32."""
    layout32, layout64 = layouts
    found, size = pair.compare(where, at64, at32, layout32, layout64)
    if size > range64:
        found.append("%s: the %d-byte struct runs into the next symbol, %d bytes on" % (where, size, range64))
    return found


def check_object(pair, extents64, extents32):
    """Problems with every struct the rules name in pair's object."""
    problems = []
    for symbol in pair.obj.symbols:
        struct, count = rule_for(pair.name, symbol.name)
        if struct is None:
            continue
        layouts = pair.layouts_of(struct)
        if symbol.name not in extents32:
            problems.append("%s: %s is not in the 4-byte-pointer object" % (pair.name, symbol.name))
            continue
        section64, value64, range64 = extents64[symbol.name]
        section32, value32, range32 = extents32[symbol.name]
        where = "%s: %s" % (pair.name, symbol.name)
        if count == ARRAY:
            found = check_array(pair, where, (section64, value64), (section32, value32), (range64, range32), layouts)
        else:
            found = check_one(pair, where, (section64, value64), (section32, value32), range64, layouts)
        problems.extend(capped(found, MAX_REPORTS, where))
    return problems


def objects_to_check(objdir):
    names = []
    for object_glob in sorted({rule[0] for rule in RULES}):
        names.extend(sorted(os.path.relpath(path, objdir) for path in glob.glob(os.path.join(objdir, object_glob))))
    if not names:
        raise CheckError("%s: no data objects (expected %s)" % (objdir, ", ".join(sorted({rule[0] for rule in RULES}))))
    return names


LayoutFile = collections.namedtuple("LayoutFile", "path structs")


def check(layouts32, layouts64, objdir, reference):
    """The problems in the data objects under objdir, against the objects
    under reference (assembled with 4-byte pointers) and the LayoutFiles."""
    problems = []
    for name in objects_to_check(objdir):
        obj = read_elf64(os.path.join(objdir, name))
        ref = read_elf64(os.path.join(reference, name))
        problems.extend(check_object(ObjectPair(name, obj, ref, layouts32, layouts64),
                                     extents(obj, name), extents(ref, name)))
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
member SongHeader blockCount 1 1 value
member SongHeader priority 2 1 value
member SongHeader reverb 3 1 value
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
member SongHeader blockCount 1 1 value
member SongHeader priority 2 1 value
member SongHeader reverb 3 1 value
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


def self_test_layouts(text32=None, text64=None):
    return (LayoutFile("layout32.txt", parse_layouts(text32 or SELF_TEST_LAYOUT32, "layout32.txt")),
            LayoutFile("layout64.txt", parse_layouts(text64 or SELF_TEST_LAYOUT64, "layout64.txt")))


def self_test_run(directory, sources, layout_texts=(None, None)):
    """Assemble {path: (text32, text64)} and check them."""
    for name, (text32, text64) in sources.items():
        for subdirectory, text in (("ref", text32), ("obj", text64)):
            path = os.path.join(directory, subdirectory, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            subprocess.run(["as", "--64", "-o", path], input=text, universal_newlines=True, check=True)
    try:
        return check(*self_test_layouts(*layout_texts), os.path.join(directory, "obj"), os.path.join(directory, "ref"))
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
    sources = {"data/sound_data.o": (SELF_TEST_SOUND32, SELF_TEST_SOUND64),
               "sound/songs/midi/mus_a.o": (SELF_TEST_SONG32, SELF_TEST_SONG64)}
    for name, sound64, song64, expected in cases:
        with tempfile.TemporaryDirectory() as directory:
            problems = self_test_run(directory, {"data/sound_data.o": (SELF_TEST_SOUND32, sound64),
                                                 "sound/songs/midi/mus_a.o": (SELF_TEST_SONG32, song64)})
        if not problems_match(expected, problems):
            failures.append("%s\n  expected %r\n  got      %r" % (name, expected, problems))
    refusals = (
        ("a struct missing from the 64-bit layout", (None, SELF_TEST_LAYOUT64.split("struct SongHeader")[0]),
         "layout64.txt does not describe"),
        ("a member missing from the 32-bit layout",
         (SELF_TEST_LAYOUT32.replace("member ToneData attack 8 1 value\n", ""), None),
         "member attack is in the 64-bit layout but not in the 32-bit one"),
    )
    for name, layout_texts, wanted in refusals:
        with tempfile.TemporaryDirectory() as directory:
            problems = self_test_run(directory, sources, layout_texts)
        if not problems_match(["CheckError: "], problems) or wanted not in problems[0]:
            failures.append("%s\n  expected a CheckError with %r\n  got      %r" % (name, wanted, problems))
    probe_without_tone = SELF_TEST_LAYOUT64.replace("member SongHeader tone 8 8 pointer\n", "")
    with tempfile.TemporaryDirectory() as directory:
        problems = self_test_run(directory, sources, (None, probe_without_tone))
    wanted = ["mus_a: no layout entry covers offset 4 of struct SongHeader in layout64.txt"]
    if not problems_match(wanted, problems):
        failures.append("a member the probe does not print\n  expected %r\n  got      %r" % (wanted, problems))
    failures.extend(self_test_layout_files())
    for failure in failures:
        sys.stderr.write("check_data_layout self-test: %s\n" % failure)
    return 1 if failures else 0


def self_test_layout_files():
    failures = []
    layouts = parse_layouts(SELF_TEST_LAYOUT64, "layout64.txt")
    if (layouts["SongHeader"].size, layouts["SongHeader"].members[-1]) != (24, ("part", 16, 8, KIND_POINTERS)):
        failures.append("parse_layouts: %r" % (layouts["SongHeader"].members,))
    for name, text, wanted in (
            ("an unknown line", "struct A 4\nsurprise A\n", "layout.txt:2: expected 'struct NAME SIZE'"),
            ("a member before its struct", "member A x 0 4 value\n", "layout.txt:1: expected"),
            ("an unknown kind", "struct A 4\nmember A x 0 4 pointerish\n", "layout.txt:2: expected"),
            ("a size that is not a number", "struct A four\n", "layout.txt:1: expected")):
        try:
            parse_layouts(text, "layout.txt")
            failures.append("parse_layouts accepted %s" % name)
        except CheckError as error:
            if wanted not in str(error):
                failures.append("parse_layouts, %s: the message is %r, not %r" % (name, str(error), wanted))
    try:
        objects_to_check(tempfile.gettempdir() + "/no-such-data-objects-directory")
        failures.append("objects_to_check accepted a directory without data objects")
    except CheckError as error:
        if "no-such-data-objects-directory: no data objects" not in str(error):
            failures.append("objects_to_check: %s" % error)
    return failures


USAGE = "usage: check_data_layout.py --layout32 FILE --layout64 FILE --objdir DIR --reference DIR | --self-test\n"


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    options, rest = parse_options(argv[1:], {"--layout32": None, "--layout64": None, "--objdir": None,
                                             "--reference": None})
    if len(options) != 4 or rest:
        sys.stderr.write(USAGE)
        return 2
    try:
        layouts32 = LayoutFile(options["--layout32"], read_layouts(options["--layout32"]))
        layouts64 = LayoutFile(options["--layout64"], read_layouts(options["--layout64"]))
        problems = check(layouts32, layouts64, options["--objdir"], options["--reference"])
    except (CheckError, OSError) as error:   # an OSError names the file it could not open
        sys.stderr.write("check_data_layout: %s\n" % error)
        return 1
    for problem in problems:
        sys.stderr.write("check_data_layout: %s\n" % problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
