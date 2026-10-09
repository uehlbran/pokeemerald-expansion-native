"""Assemble a converted data listing with clang and compare it with its ELF object.

Stands in for nothing on the GBA: it is build glue. elf_to_flat.py writes
each data object of the Linux 64-bit build as a listing for Mach-O or COFF
(docs/DECISIONS.md, "The data for Windows and macOS is assembled on Linux
and converted"); this script assembles the listing with clang for the
target's triple (elf_to_flat.TARGETS) and compares the object clang writes
(macho_object.py, coff_object.py) with the ELF one (elf_object.py), in the
ELF object's terms. It fails unless:

  * every ELF section that holds bytes or symbols has its counterpart under
    the target's spelling, and no other section of the converted object
    holds anything; each the same size, zero-fill exactly where the ELF
    section has no bytes in the file, on COFF writable exactly where the
    ELF section is (a Mach-O section has no protection of its own: its
    segment has), aligned at least as the ELF section is, and with the same
    bytes wherever neither object has a relocated word;
  * the relocated words are at the same offsets, of the same width, and
    name the same symbol (after the prefix rule) with the same addend, the
    addend read from the word on both targets; where the ELF relocation
    names a section symbol (as does for a local label), the converted word
    must reach the same section and offset, whatever label it names.
    RELOCATION_TYPES maps each format's relocation types to the ELF ones;
  * each 16-bit word the ELF object relocates (R_X86_64_16) has no
    relocation and holds the value of its symbol, from the --symbols table,
    plus its addend;
  * the symbols have the same names after the prefix rule, each in the same
    section at the same offset, or absolute with the same value (on COFF,
    modulo 2^32: its symbol values are 32 bits), with the same binding; the
    undefined symbols are those the ELF object's 8- and 4-byte words name.

Each difference names the object, the section and offset, and the symbol
(the word's, or the label at or before the offset).

Usage:
  python3 -I check_flat.py --target macho-x86_64|coff-x86_64 --symbols TABLE --listing LISTING
                           --output OBJECT [--clang CLANG] ELF_OBJECT
  python3 -I check_flat.py --self-test
Exit 0 when the object clang wrote (OBJECT) matches; otherwise one line per
problem (at most MAX_REPORTS), OBJECT removed, and exit 1.
"""

import bisect
import collections
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, capped, parse_options, problems_match  # noqa: E402
from coff_object import (IMAGE_REL_AMD64_ADDR32, IMAGE_REL_AMD64_ADDR64, IMAGE_SCN_CNT_UNINITIALIZED_DATA,  # noqa: E402
                         IMAGE_SCN_MEM_WRITE, IMAGE_SYM_ABSOLUTE, IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC,
                         IMAGE_SYM_UNDEFINED, read_coff64)
from elf_object import (ABSOLUTE, R_X86_64_16, R_X86_64_32, R_X86_64_64, SHF_ALLOC, SHF_WRITE, SHT_NOBITS,  # noqa: E402
                        STB_GLOBAL, UNDEFINED, read_elf64, relocation_name)
from elf_to_flat import TARGETS, build_symbol_table, convert, read_symbol_table  # noqa: E402
from elf_to_flat import self_test_objects as converter_test_objects  # noqa: E402
from macho_object import N_ABS, N_SECT, N_UNDF, S_ZEROFILL, X86_64_RELOC_UNSIGNED, read_macho64  # noqa: E402

# Each format's relocations that stand for the ELF ones the converter writes:
#   ELF, the System V x86-64 psABI, "Relocation Types": R_X86_64_64 is S + A
#     in 8 bytes, R_X86_64_32 S + A in 4 bytes (zero-extended);
#   Mach-O, <mach-o/x86_64/reloc.h>: X86_64_RELOC_UNSIGNED, "for absolute
#     addresses", not PC-relative, r_length 3 (8 bytes) or 2 (4 bytes), the
#     addend in the word;
#   COFF, the PE Format, "Type Indicators", x64 processors:
#     IMAGE_REL_AMD64_ADDR64, "the 64-bit VA of the relocation target", and
#     IMAGE_REL_AMD64_ADDR32, "the 32-bit VA", the addend in the word.
# Keyed by (type, PC-relative, width in bytes); a COFF type has one width.
RELOCATION_TYPES = {
    "macho-x86_64": {(X86_64_RELOC_UNSIGNED, False, 8): R_X86_64_64,
                     (X86_64_RELOC_UNSIGNED, False, 4): R_X86_64_32},
    "coff-x86_64": {(IMAGE_REL_AMD64_ADDR64, False, 8): R_X86_64_64,
                    (IMAGE_REL_AMD64_ADDR32, False, 4): R_X86_64_32},
}
COFF_WIDTHS = {IMAGE_REL_AMD64_ADDR64: 8, IMAGE_REL_AMD64_ADDR32: 4}
ELF_WIDTHS = {R_X86_64_64: 8, R_X86_64_32: 4}
VALUE_WIDTH = 2
MAX_REPORTS = 20
DEFAULT_CLANG = "clang"

# Both objects as the comparison sees them, in the ELF object's names.
#   sections {ELF name: ViewSection}, writable None where the format has no say
#   words    {ELF name: {offset: Word}}: name "" where only the place counts;
#            place a Place, or None for an undefined symbol
#   values   {ELF name: {offset: (value, symbol name)}}: the 16-bit words
#   symbols  {name: ViewSymbol}: where is an ELF section name, ABSOLUTE or UNDEFINED
View = collections.namedtuple("View", "sections words values symbols")
ViewSection = collections.namedtuple("ViewSection", "size alignment zerofill writable contents")
Word = collections.namedtuple("Word", "width name addend place")
Place = collections.namedtuple("Place", "section offset")
ViewSymbol = collections.namedtuple("ViewSymbol", "where value external")


# --------------------------------------------------------------------------
# The ELF object as it should come out

def elf_view(obj, absolute_values, value_bits, problems):
    """The View the converted object must match."""
    holding = {symbol.section for symbol in obj.symbols}
    keys = [key for key, section in obj.sections.items()
            if section.flags & SHF_ALLOC and (section.size or key in holding)]
    sections = {obj.sections[key].name: ViewSection(obj.sections[key].size, obj.sections[key].alignment,
                                                    obj.sections[key].type == SHT_NOBITS,
                                                    bool(obj.sections[key].flags & SHF_WRITE),
                                                    obj.sections[key].contents) for key in keys}
    defined = {symbol.name: symbol for symbol in obj.symbols}
    words, values, used = {}, {}, set()
    for key in keys:
        name = obj.sections[key].name
        words[name], values[name] = {}, {}
        for entry in obj.relocation_entries.get(key, []):
            if entry.type == R_X86_64_16:
                value = absolute_values.get(entry.name)
                if value is None:
                    problems.append("%s: %s+0x%x (%s): no value for the 16-bit word in the table"
                                    % (obj.path, name, entry.offset, entry.name or "a section"))
                else:
                    values[name][entry.offset] = ((value + entry.addend) & 0xFFFF, entry.name)
            elif entry.type in ELF_WIDTHS:
                words[name][entry.offset] = elf_word(obj, entry, defined)
                used.add(entry.name)
            else:
                problems.append("%s: %s+0x%x (%s): %s has no counterpart" % (obj.path, name, entry.offset, entry.name,
                                                                              relocation_name(entry.type)))
    symbols = {symbol.name: ViewSymbol(obj.sections[symbol.section].name, symbol.value, symbol.binding == STB_GLOBAL)
               for symbol in obj.symbols}
    symbols.update({symbol.name: ViewSymbol(ABSOLUTE, symbol.value % (1 << value_bits), symbol.binding == STB_GLOBAL)
                    for symbol in obj.absolutes})
    symbols.update({symbol.name: ViewSymbol(UNDEFINED, 0, True) for symbol in obj.undefined if symbol.name in used})
    return View(sections, words, values, symbols)


def elf_word(obj, entry, defined):
    width = ELF_WIDTHS[entry.type]
    if not entry.name:   # a section symbol, or the null symbol, which has no place
        place = Place(obj.sections[entry.section].name, entry.addend) if entry.section in obj.sections else None
        return Word(width, "", entry.addend, place)
    symbol = defined.get(entry.name)
    place = Place(obj.sections[symbol.section].name, symbol.value + entry.addend) if symbol else None
    return Word(width, entry.name, entry.addend, place)


# --------------------------------------------------------------------------
# The converted object

def section_names(target_name):
    """{the converted object's name of a section: its ELF name}."""
    return {spelling.split('"')[0].rstrip(",").replace(",zerofill", ""): elf_name
            for elf_name, spelling in TARGETS[target_name].sections.items()}


def strip_prefix(name, prefix, problems, path):
    if not name.startswith(prefix):
        problems.append("%s: symbol %s does not have the target's prefix %r" % (path, name, prefix))
        return None
    return name[len(prefix):]


def in_place(contents, offset, width):
    """The signed little-endian addend a relocated word holds."""
    return int.from_bytes(contents[offset:offset + width], "little", signed=True)


def macho_view(obj, target_name, problems):
    """The View of a Mach-O object (macho_object.py)."""
    names = section_names(target_name)
    prefix = TARGETS[target_name].prefix
    elf_names = {}       # ordinal: ELF name
    sections = {}
    holding = {symbol.section for symbol in obj.symbols.values() if symbol.kind == N_SECT}
    for ordinal, section in enumerate(obj.sections, 1):
        spelling = "%s,%s" % (section.segment, section.name)
        if not section.size and ordinal not in holding:
            continue   # clang's own empty __text
        if spelling in names:
            elf_names[ordinal] = names[spelling]
            sections[names[spelling]] = ViewSection(section.size, section.alignment, section.type == S_ZEROFILL,
                                                    None, section.contents)
        else:
            problems.append("%s: section %s is not one the converter writes" % (obj.path, spelling))
    symbols, by_index = {}, {}
    for index, symbol in obj.symbols.items():
        name = strip_prefix(symbol.name, prefix, problems, obj.path)
        if name is None:
            continue
        if symbol.kind == N_SECT and symbol.section in elf_names:
            section = obj.sections[symbol.section - 1]
            view = ViewSymbol(elf_names[symbol.section], symbol.value - section.address, symbol.external)
        elif symbol.kind == N_ABS:
            view = ViewSymbol(ABSOLUTE, symbol.value, symbol.external)
        elif symbol.kind == N_UNDF and symbol.value == 0:
            view = ViewSymbol(UNDEFINED, 0, symbol.external)
        else:
            problems.append("%s: symbol %s is of kind 0x%x in section %d, which the converter does not write"
                            % (obj.path, symbol.name, symbol.kind, symbol.section))
            continue
        symbols[name] = view
        by_index[index] = (name, view)
    words = {elf_names[ordinal]: {} for ordinal in elf_names}
    for ordinal, section in enumerate(obj.sections, 1):
        for relocation in section.relocations if ordinal in elf_names else []:
            where = "%s: %s+0x%x" % (obj.path, elf_names[ordinal], relocation.offset)
            if (relocation.type, relocation.pcrel, relocation.length) not in RELOCATION_TYPES[target_name]:
                problems.append("%s: Mach-O relocation type %d (PC-relative %s, %d bytes) is not an absolute word"
                                % (where, relocation.type, relocation.pcrel, relocation.length))
                continue
            addend = in_place(section.contents, relocation.offset, relocation.length)
            if relocation.extern:
                word = named_word(relocation.length, by_index.get(relocation.symbol), addend)
            elif relocation.symbol in elf_names:
                target = obj.sections[relocation.symbol - 1]
                word = Word(relocation.length, "", addend, Place(elf_names[relocation.symbol], addend - target.address))
            else:
                word = None
            if word is None:
                problems.append("%s: the relocation names %s %d, which the converter does not write"
                                % (where, "symbol" if relocation.extern else "section", relocation.symbol))
            else:
                add_word(words[elf_names[ordinal]], relocation.offset, word, where, problems)
    return View(sections, words, {}, symbols)


def add_word(words, offset, word, where, problems):
    if offset in words:
        problems.append("%s: two relocations at one offset" % where)
    words[offset] = word


def named_word(width, named, addend):
    """The Word of a relocation naming a symbol: named is (name, ViewSymbol) or None."""
    if named is None:
        return None
    name, symbol = named
    place = Place(symbol.where, symbol.value + addend) if symbol.where not in (ABSOLUTE, UNDEFINED) else None
    return Word(width, name, addend, place)


def coff_view(obj, target_name, problems):
    """The View of a COFF object (coff_object.py)."""
    names = section_names(target_name)
    prefix = TARGETS[target_name].prefix
    elf_names, sections = {}, {}
    holding = {symbol.section for symbol in obj.symbols.values() if not symbol.aux}
    for number, section in enumerate(obj.sections, 1):
        if not section.size and number not in holding:
            continue   # clang's own empty .text, .data and .bss
        if section.name in names:
            elf_names[number] = names[section.name]
            sections[names[section.name]] = ViewSection(
                section.size, section.alignment, bool(section.characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA),
                bool(section.characteristics & IMAGE_SCN_MEM_WRITE), section.contents)
        else:
            problems.append("%s: section %s is not one the converter writes" % (obj.path, section.name))
    symbols, by_index = {}, {}
    for index, symbol in obj.symbols.items():
        if symbol.aux and symbol.storage_class == IMAGE_SYM_CLASS_STATIC and symbol.section > 0 \
                and obj.sections[symbol.section - 1].name == symbol.name:
            if symbol.section in elf_names:   # a section's own symbol: a word naming it names a place
                by_index[index] = ("", ViewSymbol(elf_names[symbol.section], 0, False))
            continue
        name = strip_prefix(symbol.name, prefix, problems, obj.path)
        if name is None:
            continue
        external = symbol.storage_class == IMAGE_SYM_CLASS_EXTERNAL
        if symbol.storage_class not in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC):
            view = None
        elif symbol.section in elf_names:
            view = ViewSymbol(elf_names[symbol.section], symbol.value, external)
        elif symbol.section == IMAGE_SYM_ABSOLUTE:
            view = ViewSymbol(ABSOLUTE, symbol.value, external)
        elif symbol.section == IMAGE_SYM_UNDEFINED and symbol.value == 0:
            view = ViewSymbol(UNDEFINED, 0, external)
        else:
            view = None
        if view is None:
            problems.append("%s: symbol %s (storage class %d, section %d, value 0x%x) is not one the converter writes"
                            % (obj.path, symbol.name, symbol.storage_class, symbol.section, symbol.value))
            continue
        symbols[name] = view
        by_index[index] = (name, view)
    words = {elf_names[number]: {} for number in elf_names}
    for number, section in enumerate(obj.sections, 1):
        for relocation in section.relocations if number in elf_names else []:
            where = "%s: %s+0x%x" % (obj.path, elf_names[number], relocation.offset)
            width = COFF_WIDTHS.get(relocation.type)
            if (relocation.type, False, width) not in RELOCATION_TYPES[target_name]:
                problems.append("%s: COFF relocation type 0x%x is not an absolute word" % (where, relocation.type))
                continue
            word = named_word(width, by_index.get(relocation.symbol), in_place(section.contents, relocation.offset,
                                                                               width))
            if word is None:
                problems.append("%s: the relocation names symbol %d, which the converter does not write"
                                % (where, relocation.symbol))
            else:
                add_word(words[elf_names[number]], relocation.offset, word, where, problems)
    return View(sections, words, {}, symbols)


FORMAT_VIEWS = {"macho-x86_64": (read_macho64, macho_view), "coff-x86_64": (read_coff64, coff_view)}


# --------------------------------------------------------------------------
# The comparison

class Owners:
    """The label at or before an offset, for messages."""

    def __init__(self, symbols):
        by_section = collections.defaultdict(list)
        for name, symbol in symbols.items():
            if symbol.where not in (ABSOLUTE, UNDEFINED):
                by_section[symbol.where].append((symbol.value, name))
        self.labels = {section: sorted(labels) for section, labels in by_section.items()}

    def where(self, path, section, offset):
        labels = self.labels.get(section, [])
        index = bisect.bisect_right(labels, (offset, "\U0010ffff")) - 1
        return "%s: %s+0x%x (%s)" % (path, section, offset, labels[index][1] if index >= 0 else "no label")


def place_text(place):
    return "%s+0x%x" % place if place else "no place"


def word_text(word):
    target = word.name + ("%+d" % word.addend if word.addend else "") if word.name else place_text(word.place)
    return "%d-byte word to %s" % (word.width, target)


def compare(path, expected, converted):
    """The differences between the ELF object's View and the converted one."""
    owners = Owners(expected.symbols)
    problems = []
    for name in sorted(set(expected.sections) | set(converted.sections)):
        if name not in converted.sections:
            problems.append("%s: section %s is missing" % (path, name))
        elif name not in expected.sections:
            problems.append("%s: section %s holds what the ELF object does not" % (path, name))
        else:
            problems.extend(section_problems(path, name, expected, converted, owners))
    problems.extend(symbol_problems(path, expected.symbols, converted.symbols))
    return problems


def section_problems(path, name, expected, converted, owners):
    want, got = expected.sections[name], converted.sections[name]
    problems = []
    for what, wanted, found in (("size", want.size, got.size), ("zero-fill", want.zerofill, got.zerofill),
                                ("writable", want.writable, want.writable if got.writable is None else got.writable)):
        if wanted != found:
            problems.append("%s: section %s: %s %s, the ELF section's %s" % (path, name, what, found, wanted))
    if got.alignment < want.alignment:
        problems.append("%s: section %s: aligned to %d, the ELF section to %d" % (path, name, got.alignment,
                                                                                   want.alignment))
    if problems:
        return problems
    want_words, got_words = expected.words.get(name, {}), converted.words.get(name, {})
    values = expected.values.get(name, {})
    for offset in sorted(set(want_words) | set(got_words)):
        where = owners.where(path, name, offset)
        if offset in values:
            continue
        if offset not in got_words:
            problems.append("%s: the %s is missing" % (where, word_text(want_words[offset])))
        elif offset not in want_words:
            problems.append("%s: a %s the ELF object does not have" % (where, word_text(got_words[offset])))
        else:
            problems.extend(word_problem(where, want_words[offset], got_words[offset]))
    for offset, (value, symbol) in sorted(values.items()):
        held = int.from_bytes(got.contents[offset:offset + VALUE_WIDTH], "little")
        if offset in got_words:
            problems.append("%s: a relocation where the 16-bit word should hold %s's value" % (owners.where(
                path, name, offset), symbol))
        elif held != value:
            problems.append("%s: the 16-bit word holds 0x%x, %s is 0x%x" % (owners.where(path, name, offset), held,
                                                                              symbol, value))
    problems.extend(byte_problems(path, name, want, got, [want_words, got_words, values], owners))
    return problems


def word_problem(where, want, got):
    if want.width != got.width:
        return ["%s: a %s, the ELF word %d bytes" % (where, word_text(got), want.width)]
    modulus = 1 << (8 * want.width)
    if want.name and (got.name != want.name or (got.addend - want.addend) % modulus):
        return ["%s: a %s, the ELF word to %s%s" % (where, word_text(got), want.name,
                                                   "%+d" % want.addend if want.addend else "")]
    if not want.name and got.place != want.place:
        return ["%s: a %s reaching %s, the ELF word %s" % (where, word_text(got), place_text(got.place),
                                                            place_text(want.place))]
    return []


def byte_problems(path, name, want, got, word_tables, owners):
    """Bytes that differ where no word is."""
    if want.zerofill:
        return []
    mask_want, mask_got = bytearray(want.contents), bytearray(got.contents)
    for table in word_tables:
        for offset, word in table.items():
            width = word.width if isinstance(word, Word) else VALUE_WIDTH
            mask_want[offset:offset + width] = bytes(width)
            mask_got[offset:offset + width] = bytes(width)
    if mask_want == mask_got:
        return []
    return ["%s: byte 0x%02x, the ELF byte 0x%02x" % (owners.where(path, name, offset), mask_got[offset],
                                                      mask_want[offset])
            for offset in range(len(mask_want)) if mask_want[offset] != mask_got[offset]][:MAX_REPORTS]


def symbol_text(symbol):
    place = "= 0x%x" % symbol.value if symbol.where == ABSOLUTE else (
        "undefined" if symbol.where == UNDEFINED else "at %s+0x%x" % (symbol.where, symbol.value))
    return "%s %s" % ("global" if symbol.external else "local", place)


def symbol_problems(path, expected, converted):
    problems = []
    for name in sorted(set(expected) | set(converted)):
        if name not in converted:
            problems.append("%s: symbol %s (%s) is missing" % (path, name, symbol_text(expected[name])))
        elif name not in expected:
            problems.append("%s: symbol %s (%s) is not in the ELF object" % (path, name, symbol_text(converted[name])))
        elif converted[name] != expected[name]:
            problems.append("%s: symbol %s is %s, the ELF one %s" % (path, name, symbol_text(converted[name]),
                                                                     symbol_text(expected[name])))
    return problems


# --------------------------------------------------------------------------

def assemble(clang, target_name, listing, output):
    """Problems running clang on the listing (none when it wrote output)."""
    result = subprocess.run([clang, "-target", TARGETS[target_name].clang, "-c", "-x", "assembler", "-o", output,
                             listing], stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
    if result.returncode == 0:
        return []
    return ["%s: %s -target %s could not assemble it:" % (listing, clang, TARGETS[target_name].clang)] \
        + result.stderr.splitlines()[:MAX_REPORTS]


def check(target_name, elf_path, absolute_values, converted_path):
    """The differences between the ELF object and the converted one."""
    obj = read_elf64(elf_path)
    problems = []
    expected = elf_view(obj, absolute_values, TARGETS[target_name].value_bits, problems)
    reader, view = FORMAT_VIEWS[target_name]
    converted = view(reader(converted_path), target_name, problems)
    return capped(problems + compare(elf_path, expected, converted), MAX_REPORTS, elf_path)


# --------------------------------------------------------------------------
# Self-test: elf_to_flat.py's own objects, converted, assembled and compared,
# then listings edited as each mistake would leave them.

def self_test_objects(directory):
    """elf_to_flat.py's self-test object a.o, its path and its table."""
    objects = converter_test_objects(directory)
    return objects["a.o"], os.path.join(directory, "a.o"), build_symbol_table(objects)[0]


def run_listing(directory, target_name, elf_path, values, text):
    listing, output = os.path.join(directory, "a.s"), os.path.join(directory, "a.flat.o")
    with open(listing, "w") as handle:
        handle.write(text)
    problems = assemble(DEFAULT_CLANG, target_name, listing, output)
    return problems or check(target_name, elf_path, values, output)


# Edits of the Mach-O listing (old text, new text) and the problems they leave.
MACHO_EDITS = (
    ("a dropped relocation", ("\t.quad _gExternal+4\n", "\t.quad 4\n"),
     ["a.o: .rodata+0x0 (gTable): the 8-byte word to gExternal+4 is missing",
      "a.o: symbol gExternal (global undefined) is missing"]),
    ("a wrong addend", ("\t.quad _gExternal+4\n", "\t.quad _gExternal+5\n"),
     ["a.o: .rodata+0x0 (gTable): a 8-byte word to gExternal+5, the ELF word to gExternal+4"]),
    ("a wrong width", ("\t.quad _Script+1\n", "\t.long _Script+1\n\t.long 0\n"),
     ["a.o: .rodata+0x8 (gTable): a 4-byte word to Script+1, the ELF word 8 bytes"]),
    ("a word reaching another place", ("\t.long _gTable+30\n", "\t.long _gTable+31\n"),
     ["a.o: .rodata+0x10 (gTable): a 4-byte word to gTable+31 reaching .rodata+0x1f, the ELF word .rodata+0x1e"]),
    ("an extra word", ('\t.ascii "4\\022\\000\\000\\000\\000\\000\\000"\n',
                       '\t.ascii "4\\022\\000\\000"\n\t.long _gTable\n'),
     ["a.o: .rodata+0x1c (gTable): a 4-byte word to gTable the ELF object does not have"]),
    ("a word naming another label at the same place", ("\t.long _gTable+30\n", "\t.long _LocalLabel-2\n"), []),
    ("a missed prefix", ("_gBuffer", "gBuffer"),
     ["a.flat.o: symbol gBuffer does not have the target's prefix '_'",
      "a.o: symbol gBuffer (global at .bss+0x0) is missing"]),
    ("a symbol made local", ("\t.globl _Lname\n", ""),
     ["a.o: symbol Lname is local at .rodata+0x25, the ELF one global at .rodata+0x25"]),
    ("a symbol moved", ("_end:\n", "\t.set _end, . - 1\n"),
     ["a.o: symbol end is local at .rodata+0x2d, the ELF one local at .rodata+0x2e"]),
    ("a 16-bit value", ("\t.short 0x0005", "\t.short 0x0007"),
     ["a.o: .rodata+0x14 (gTable): the 16-bit word holds 0x7, B_CONST is 0x5"]),
    ("a 16-bit word left a 16-bit relocation", ("\t.short 0x0005", "\t.short _B_CONST+5"),
     ["a.flat.o: .rodata+0x14: Mach-O relocation type 0 (PC-relative False, 2 bytes) is not an absolute word",
      "a.o: symbol B_CONST (global undefined) is not in the ELF object"]),
    ("a 16-bit word under a 4-byte relocation", ("\t.short 0x0005  # B_CONST (R_X86_64_16)\n"
                                                 "\t.short 0x0006  # B_CONST+1 (R_X86_64_16)\n",
                                                 "\t.long _B_CONST+0x60005\n"),
     ["a.o: .rodata+0x14 (gTable): a relocation where the 16-bit word should hold B_CONST's value",
      "a.o: symbol B_CONST (global undefined) is not in the ELF object"]),
    ("a byte", ('\t.ascii "\\007"\n', '\t.ascii "\\010"\n'),
     ["a.o: .rodata+0x2d (_under): byte 0x08, the ELF byte 0x07"]),
    ("alignment lowered", ("\t.section __DATA,__const\n\t.p2align 3\n", "\t.section __DATA,__const\n\t.p2align 2\n"),
     ["a.o: section .rodata: aligned to 4, the ELF section to 8"]),
    ("alignment raised is no problem", ("\t.p2align 0\n", "\t.p2align 4\n"), []),
    ("a section under another name", ("__DATA,__script_data", "__DATA,__other"),
     ["a.flat.o: section __DATA,__other is not one the converter writes",
      "a.flat.o: symbol _Script is of kind 0xe in section 4, which the converter does not write",
      "a.flat.o: .rodata+0x8: the relocation names symbol 1, which the converter does not write",
      "a.o: .rodata+0x8 (gTable): the 8-byte word to script_data+0x1 is missing",
      "a.o: section script_data is missing", "a.o: symbol Script (local at script_data+0x0) is missing"]),
    ("an absolute value", ("\t.set _LOCAL_CONST, 5\n", "\t.set _LOCAL_CONST, 6\n"),
     ["a.o: symbol LOCAL_CONST is local = 0x6, the ELF one local = 0x5"]),
    ("an extra symbol", ("_after:\n", "_after:\n_more:\n"),
     ["a.o: symbol more (local at .bss+0x10) is not in the ELF object"]),
    ("a difference of addresses", ("\t.long _gTable+8\n", "\t.long _gTable+8-.\n"),
     ["a.flat.o: script_data+0x2: Mach-O relocation type 5 (PC-relative False, 4 bytes) is not an absolute word",
      "a.o: script_data+0x2 (Script): a 4-byte word to gTable+6, the ELF word to gTable+8"]),
)
COFF_EDITS = (
    ("COFF: a dropped relocation", ("\t.quad gExternal+4\n", "\t.quad 4\n"),
     ["a.o: .rodata+0x0 (gTable): the 8-byte word to gExternal+4 is missing",
      "a.o: symbol gExternal (global undefined) is missing"]),
    ("COFF: read-only script_data", ('script_data,"dw"', 'script_data,"dr"'),
     ["a.o: section script_data: writable False, the ELF section's True"]),
    ("COFF: an absolute value modulo 2^32", ("\t.set GLOBAL_CONST, -1\n", "\t.set GLOBAL_CONST, 0xfffffffe\n"),
     ["a.o: symbol GLOBAL_CONST is global = 0xfffffffe, the ELF one global = 0xffffffff"]),
    ("COFF: an image-relative word", ("\t.long gTable+8\n", "\t.long gTable@IMGREL+8\n"),
     ["a.flat.o: script_data+0x2: COFF relocation type 0x3 is not an absolute word",
      "a.o: script_data+0x2 (Script): the 4-byte word to gTable+8 is missing"]),
)


def self_test_edits(failures, directory):
    obj, elf_path, values = self_test_objects(directory)
    listings = {target_name: convert("a.o", obj, target_name, values)[0] for target_name in TARGETS}
    for target_name, text in listings.items():
        expect_problems(failures, "%s: the converted listing" % target_name,
                        run_listing(directory, target_name, elf_path, values, text), [], directory)
    for target_name, edits in (("macho-x86_64", MACHO_EDITS), ("coff-x86_64", COFF_EDITS)):
        for name, (old, new), wanted in edits:
            if old not in listings[target_name]:
                failures.append("%s: %r is not in the listing" % (name, old))
                continue
            found = run_listing(directory, target_name, elf_path, values, listings[target_name].replace(old, new))
            expect_problems(failures, name, found, wanted, directory)


def expect_problems(failures, name, found, wanted, directory):
    """wanted names the files without the self-test's directory."""
    shortened = [problem.replace(directory + os.sep, "") for problem in found]
    if not problems_match(wanted, shortened):
        failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, shortened))


def self_test_unassembled(failures, directory):
    found = run_listing(directory, "coff-x86_64", os.path.join(directory, "a.o"), {}, "\t.nonsense\n")
    if not found or "could not assemble it" not in found[0]:
        failures.append("an unassemblable listing: %r" % found)


def self_test_units(failures):
    """What clang will not write for the edits above: two relocations at
    one offset; a .bss that is not zero-fill (clang makes __DATA,__bss and
    .bss zero-fill whatever the listing says)."""
    words, problems = {}, []
    add_word(words, 4, Word(8, "a", 0, None), "x.o: .rodata+0x4", problems)
    add_word(words, 4, Word(8, "b", 0, None), "x.o: .rodata+0x4", problems)
    if problems != ["x.o: .rodata+0x4: two relocations at one offset"]:
        failures.append("two relocations at one offset: %r" % problems)
    bss = ViewSection(16, 8, True, True, b"")
    found = compare("x.o", View({".bss": bss}, {}, {}, {}),
                    View({".bss": bss._replace(zerofill=False, contents=bytes(16))}, {}, {}, {}))
    if found != ["x.o: section .bss: zero-fill False, the ELF section's True"]:
        failures.append("not zero-fill: %r" % found)


def self_test_tables(failures):
    if set(FORMAT_VIEWS) != set(TARGETS) or set(RELOCATION_TYPES) != set(TARGETS):
        failures.append("FORMAT_VIEWS and RELOCATION_TYPES must cover TARGETS: %r" % sorted(TARGETS))
    for target_name in TARGETS:
        if sorted(section_names(target_name).values()) != sorted(TARGETS[target_name].sections):
            failures.append("%s: section_names %r" % (target_name, section_names(target_name)))
    expected = {"macho-x86_64": {"__DATA,__const": ".rodata", "__DATA,__script_data": "script_data",
                                 "__DATA,__data": ".data", "__DATA,__bss": ".bss"},
                "coff-x86_64": {".rdata": ".rodata", "script_data": "script_data", ".data": ".data", ".bss": ".bss"}}
    for target_name, wanted in expected.items():
        if section_names(target_name) != wanted:
            failures.append("%s: section_names %r, expected %r" % (target_name, section_names(target_name), wanted))


def self_test():
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        self_test_edits(failures, directory)
        self_test_unassembled(failures, directory)
    self_test_tables(failures)
    self_test_units(failures)
    for failure in failures:
        sys.stderr.write("check_flat self-test: %s\n" % failure)
    return 1 if failures else 0


USAGE = ("usage: check_flat.py --target %s --symbols TABLE --listing LISTING --output OBJECT [--clang CLANG] "
         "ELF_OBJECT\n       check_flat.py --self-test\n" % "|".join(sorted(TARGETS)))


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    options, paths = parse_options(argv[1:], {"--target": tuple(TARGETS), "--symbols": None, "--listing": None,
                                              "--output": None, "--clang": None})
    if not {"--target", "--symbols", "--listing", "--output"} <= set(options) or len(paths) != 1:
        sys.stderr.write(USAGE)
        return 2
    output = options["--output"]
    try:
        problems = assemble(options.get("--clang", DEFAULT_CLANG), options["--target"], options["--listing"], output)
        if not problems:
            problems = check(options["--target"], paths[0], read_symbol_table(options["--symbols"]), output)
    except (CheckError, OSError) as error:   # an OSError names the file it could not open (or clang)
        problems = [str(error)]
    for problem in problems:
        sys.stderr.write("check_flat: %s\n" % problem)
    if problems and os.path.exists(output):
        os.remove(output)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
