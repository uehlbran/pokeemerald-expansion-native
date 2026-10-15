"""Write an ELF64 data object of the 64-bit build as a listing for Mach-O or COFF.

Stands in for nothing on the GBA: it is build glue. Expansion's data is
assembler text only GNU as reads, so the ports to macOS and Windows link the
objects GNU as assembles for the 64-bit build and the Linux build checks
(docs/DECISIONS.md, "The data for Windows and macOS is assembled on Linux
and converted", and "The data on a Mac is assembled there with GNU as and
converted": there the same steps run on the Mac), and on macOS also the
event scripts of the test objects
(Makefile.native, OVERWORLD_SCRIPT on macOS): this
script reads one of them (elf_object.py) and writes a flat listing, sections,
labels and words with nothing left to evaluate, that clang assembles for the
target. check_flat.py assembles it and compares the result with the ELF
object. The rules, each the one place it is decided:

  * Sections: SECTION_TYPES gives the ELF type and flags each converted
    section must have, TARGETS each target's spelling of it, chosen as clang
    spells the C compiler's own: read-only data that holds addresses is
    __DATA,__const on Mach-O and .rdata on COFF; script_data keeps its name
    as a writable data section. .data and .bss are the game's own
    variables, which crt0 keeps a copy of and zeroes on a soft reset: the
    Linux link brackets them by object path (platform/gba_game.ld,
    game_data and game_bss), which ld64 cannot, so they get the bracket's
    names, __DATA,__game_data and zero-fill __DATA,__game_bss on Mach-O,
    game_data and game_bss on COFF (game_bss fits COFF's 8-byte name
    field). The order of the bytes in
    a section, and with it every offset, is the ELF object's. A section with
    no bytes and no symbols (as's empty .text, .data and .bss, and
    .note.GNU-stack, an ELF marker) is left out; the symbol, string and
    relocation tables are what the listing is made from; any other section
    is refused.
  * Alignment: a section starts with .p2align of the ELF section's
    alignment, and every byte in it is written, padding included.
  * Bytes: .ascii lines (octal escapes for the bytes that are not printable,
    the quote and the backslash);
    a section with no bytes in the file (.bss) is .space.
  * Symbols: every label, local and global, at its offset, and every
    absolute symbol (.set) at the end, each with the target's prefix: "_"
    on Mach-O, where the C compiler gives every C name one, none on COFF
    x86-64. The prefix also keeps the 3,463 names that begin with "L" (map
    scripts such as LittlerootTown_MapScripts) out of Mach-O's
    assembler-local namespace, with no rename: the assembler sees
    _LittlerootTown_MapScripts, the name clang gives the C declaration of
    the same symbol, so the C side and the data agree. A label inside a
    relocated word (the key-split labels of sound/keysplit_tables.inc point
    back into the voicegroup before them) is set back from the word's end,
    ".set name, . - n", as the source sets it (at expansion 1.17.1 there is
    one, keysplit_piano, 4 bytes into the word before it; clang writes it as
    an N_ALT_ENTRY symbol on Mach-O). Names are plain
    identifiers; two symbols that would get one name are refused. The
    undefined symbols are those a relocation of the listing names: one that
    no relocation names (25 at expansion 1.17.1, C enum constants the
    preprocessor leaves as names, met in .equiv aliases and .if
    comparisons) is left out, because Mach-O's linker would ask for its
    definition. On Mach-O every absolute symbol is written local: 7,930
    global ones are defined, with one value, in several objects, which GNU
    ld accepts and a Mach-O linker may refuse as duplicates, and no data
    word needs them global, since no 8- or 4-byte word names one (the
    --symbols pass checks that) and the 16-bit words take their values from
    the table; the test objects' event scripts, which do name them, take
    their values from a table of them all (below). COFF keeps ELF's binding: the test runner build's objects
    name the global ones from their event scripts (inline assembly,
    include/test/overworld_script.h; ITEM_POTION, SPECIAL_*), which the link
    resolves against the data objects, and GNU ld for PE takes an absolute
    symbol defined in several objects with one value (and refuses two
    values).
    Symbol types and sizes (.type, .size) have no counterpart and are
    dropped; weak and common symbols are refused. COFF's symbol values are
    32 bits: an absolute value that does not fit (signed or not) is
    refused, and one that fits is held modulo 2^32 (CONNECTION_INVALID, -1,
    reads back as 0xffffffff).
  * Relocations: R_X86_64_64 is an 8-byte word (.quad), R_X86_64_32 a
    4-byte one (.long), symbol plus addend, the addend in range for the
    width. A relocation against a section symbol (GNU as writes one for a
    local label) names the last label at or before its target instead
    (the first label of the section if none is), with the difference as the
    addend. R_X86_64_16 (8 words: 3 B_MSG_MON_* in battle_anim_scripts.o, 5
    SPECIAL_* in mystery_gift.o) and R_X86_64_8 (none in the data) are given
    their values, since neither target has a relocation that narrow (COFF
    none under 32 bits, Mach-O's x86-64 none under 4 bytes): the symbol must
    be an absolute symbol another object defines (the table), and value
    plus addend must fit the width (VALUE_RANGES). So is a 4- or 8-byte
    word that names an undefined symbol the table holds. The data has none
    (the --symbols pass refuses one), but a test's event scripts name the
    data's absolute symbols (ITEM_POTION, SPECIAL_*, EQUAL) in words of
    every width, which GNU ld resolves on Linux and Mach-O's linker cannot,
    the listings having written them local; for those the table is every
    global absolute symbol of the data objects (--absolutes).
    Any other relocation is refused.

Usage:
  python3 -I elf_to_flat.py --symbols TABLE OBJECT...
      the table of the absolute symbols the objects' 8- and 16-bit words
      name, over all of them (rewritten only when it changes)
  python3 -I elf_to_flat.py --absolutes TABLE OBJECT...
      the table of every global absolute symbol of the objects, for the
      event scripts (rewritten only when it changes)
  python3 -I elf_to_flat.py --target macho-x86_64|coff-x86_64 --symbols TABLE --output LISTING OBJECT
  python3 -I elf_to_flat.py --self-test
Exit 0 when the listing (or table) is written; otherwise one line per
problem, naming the object, section, offset and symbol, and exit 1.
"""

import bisect
import collections
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, gnu_as, parse_options  # noqa: E402
from elf_object import (ABSOLUTE, R_X86_64_8, R_X86_64_16, R_X86_64_32, R_X86_64_64, SHF_ALLOC, SHF_WRITE,  # noqa: E402
                        SHT_NOBITS, SHT_NULL, SHT_PROGBITS, SHT_RELA, SHT_STRTAB, SHT_SYMTAB, STB_GLOBAL, STB_LOCAL,
                        UNDEFINED, ElfObject, Relocation, Section, Symbol, read_elf64, relocation_name)

# The ELF sections the data objects have, with the type and flags each must have.
SECTION_TYPES = {
    ".rodata": (SHT_PROGBITS, SHF_ALLOC),
    "script_data": (SHT_PROGBITS, SHF_ALLOC | SHF_WRITE),
    ".data": (SHT_PROGBITS, SHF_ALLOC | SHF_WRITE),
    ".bss": (SHT_NOBITS, SHF_ALLOC | SHF_WRITE),
}
# The tables an object is made of rather than sections it holds.
STRUCTURE_TYPES = (SHT_NULL, SHT_SYMTAB, SHT_STRTAB, SHT_RELA)

# prefix: what the target's C compiler puts before a C name; value_bits: the
# width of a symbol's value in the format; clang: the triple check_flat.py
# assembles for; sections: the .section operands, by ELF name;
# global_absolutes: whether a global absolute symbol stays global (the
# docstring says why only COFF's do).
Target = collections.namedtuple("Target", "prefix value_bits clang sections global_absolutes")
TARGETS = {
    "macho-x86_64": Target("_", 64, "x86_64-apple-macos11", {
        ".rodata": "__DATA,__const",
        "script_data": "__DATA,__script_data",
        ".data": "__DATA,__game_data",
        ".bss": "__DATA,__game_bss,zerofill",
    }, False),
    "coff-x86_64": Target("", 32, "x86_64-w64-windows-gnu", {
        ".rodata": '.rdata,"dr"',
        "script_data": 'script_data,"dw"',
        ".data": 'game_data,"dw"',
        ".bss": 'game_bss,"bw"',
    }, True),
}

WORD_WIDTHS = {R_X86_64_64: 8, R_X86_64_32: 4, R_X86_64_16: 2, R_X86_64_8: 1}
WORD_DIRECTIVES = {8: ".quad", 4: ".long", 2: ".short", 1: ".byte"}
VALUE_WIDTHS = (1, 2)               # the words always given their values: R_X86_64_8 and R_X86_64_16
# The values a word of each width may be given: a signed or an unsigned field
# of 8 and 16 bits, unsigned 32 bits (R_X86_64_32 is zero-extended), any 64.
# GNU ld (2.42, measured) accepts the same, and a little more below zero for
# the narrow two (down to -256 and -65536).
VALUE_RANGES = {1: (-0x80, 0xFF), 2: (-0x8000, 0xFFFF), 4: (0, 0xFFFFFFFF), 8: (-(1 << 63), (1 << 64) - 1)}
BYTES_PER_LINE = 64
NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
FIRST_PRINTABLE, LAST_PRINTABLE = 0x20, 0x7E
ESCAPED = {ord('"'), ord("\\")}
ASCII_ESCAPES = {byte: chr(byte) if FIRST_PRINTABLE <= byte <= LAST_PRINTABLE and byte not in ESCAPED
                 else "\\%03o" % byte for byte in range(256)}
TABLE_HEADER = ("# The absolute symbols the data's 16-bit words name (R_X86_64_16) and their\n"
                "# values, from the data objects that define them: elf_to_flat.py --symbols.\n")
ABSOLUTES_HEADER = ("# Every global absolute symbol of the data objects and its value:\n"
                    "# elf_to_flat.py --absolutes.\n")


# --------------------------------------------------------------------------
# The table of absolute symbols for the 16-bit words

def word_name(object_name, obj, key, offset):
    """'object: section+0xoffset' for messages."""
    return "%s: %s+0x%x" % (object_name, obj.sections[key].name, offset)


def build_symbol_table(objects):
    """({name: value} for every symbol an 8- or 16-bit word of the objects
    ({object name: ElfObject}) names, problems). Also a problem: an 8- or
    4-byte word naming another object's absolute symbol, which the listings
    write local."""
    wanted, addressed = {}, {}
    for object_name in sorted(objects):
        obj = objects[object_name]
        for key, entries in obj.relocation_entries.items():
            for entry in entries:
                width = WORD_WIDTHS.get(entry.type)
                if width in VALUE_WIDTHS and entry.name:
                    wanted.setdefault(entry.name, (word_name(object_name, obj, key, entry.offset), 8 * width))
                elif entry.section == UNDEFINED and entry.name:
                    addressed.setdefault(entry.name, word_name(object_name, obj, key, entry.offset))
    values, definers, problems = {}, {}, []
    for object_name in sorted(objects):
        obj = objects[object_name]
        for symbol in obj.absolutes:
            if symbol.binding == STB_GLOBAL and symbol.name in addressed:
                problems.append("%s (%s): the word names an absolute symbol of %s, which the listings write local"
                                % (addressed[symbol.name], symbol.name, object_name))
            if symbol.binding != STB_GLOBAL or symbol.name not in wanted:
                continue
            if symbol.name in values and values[symbol.name] != symbol.value:
                problems.append("%s: %s is 0x%x here and 0x%x in %s" % (object_name, symbol.name, symbol.value,
                                                                       values[symbol.name], definers[symbol.name]))
            else:
                values[symbol.name], definers[symbol.name] = symbol.value, object_name
        for symbol in obj.symbols:
            if symbol.binding == STB_GLOBAL and symbol.name in wanted:
                problems.append("%s (%s): the %d-bit word names a label of %s, not an absolute symbol"
                                % (wanted[symbol.name][0], symbol.name, wanted[symbol.name][1], object_name))
    for name in sorted(set(wanted) - set(values)):
        problems.append("%s (%s): no data object defines the symbol as an absolute value" % (wanted[name][0], name))
    return values, problems


def build_absolutes_table(objects):
    """({name: value} for every global absolute symbol of the objects
    ({object name: ElfObject}), problems: a name given two values)."""
    values, definers, problems = {}, {}, []
    for object_name in sorted(objects):
        for symbol in objects[object_name].absolutes:
            if symbol.binding != STB_GLOBAL:
                continue
            if symbol.name not in values:
                values[symbol.name], definers[symbol.name] = symbol.value, object_name
            elif values[symbol.name] != symbol.value:
                problems.append("%s: %s is 0x%x here and 0x%x in %s" % (object_name, symbol.name, symbol.value,
                                                                       values[symbol.name], definers[symbol.name]))
    return values, problems


def format_symbol_table(values, header=TABLE_HEADER):
    return header + "".join("%s 0x%x\n" % (name, values[name]) for name in sorted(values))


def read_symbol_table(path):
    """{name: value} from a table format_symbol_table wrote."""
    values = {}
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            try:
                if len(fields) != 2 or fields[0] in values:
                    raise ValueError
                values[fields[0]] = int(fields[1], 16)
            except ValueError:
                raise CheckError("%s:%d: expected 'name 0xvalue', each name once, got %r"
                                 % (path, number, line.strip()))
    return values


def write_if_changed(path, text):
    """Write text to path unless the file already holds it, so that what
    depends on the file is remade only when it changes. The text is written
    whole to a file beside it and renamed over it, so that a build stopped
    part way leaves the old file or the new one, never part of one."""
    try:
        with open(path, encoding="utf-8") as handle:
            if handle.read() == text:
                return
    except FileNotFoundError:
        pass
    temporary = "%s.%d.tmp" % (path, os.getpid())
    try:
        with open(temporary, "w", encoding="utf-8") as handle:
            handle.write(text)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.remove(temporary)


# --------------------------------------------------------------------------
# The listing

class Conversion:
    """The listing of one object for one target, and the problems found
    while writing it."""

    def __init__(self, object_name, obj, target, absolute_values):
        self.object_name, self.obj, self.target = object_name, obj, target
        self.absolute_values = absolute_values
        self.problems = []
        self.lines = []
        self.sections = self.converted_sections()
        self.labels = {key: sorted((symbol.value, order, symbol) for order, symbol in enumerate(obj.symbols)
                                   if symbol.section == key) for key in self.sections}
        self.label_values = {key: [entry[0] for entry in labels] for key, labels in self.labels.items()}
        self.names = self.target_names()
        self.words = {key: self.section_words(key) for key in self.sections}

    def where(self, key, offset, symbol):
        return "%s (%s)" % (word_name(self.object_name, self.obj, key, offset), symbol)

    def converted_sections(self):
        """The keys of the sections the listing holds, in header order."""
        holding = {symbol.section for symbol in self.obj.symbols}
        keys = []
        for key, section in self.obj.sections.items():
            if section.type in STRUCTURE_TYPES or (section.size == 0 and key not in holding):
                continue
            if SECTION_TYPES.get(section.name) != (section.type, section.flags):
                self.problems.append("%s: section %s (type %d, flags 0x%x) is not one the converter knows (%s)"
                                     % (self.object_name, section.name, section.type, section.flags,
                                        ", ".join(sorted(SECTION_TYPES))))
            elif section.alignment & (section.alignment - 1):
                self.problems.append("%s: section %s is aligned to %d, not a power of two"
                                     % (self.object_name, section.name, section.alignment))
            else:
                keys.append(key)
        return keys

    def section_words(self, key):
        """[(offset, width, text)] for the relocations of a section, by offset."""
        words = []
        for entry in self.obj.relocation_entries.get(key, []):
            width = WORD_WIDTHS.get(entry.type)
            symbol = entry.name or ("a section" if entry.section in self.obj.sections else "no symbol")
            where = self.where(key, entry.offset, symbol)
            if width is None:
                self.problems.append("%s: %s is not a relocation the converter writes"
                                     % (where, relocation_name(entry.type)))
                continue
            if width in VALUE_WIDTHS or (entry.section == UNDEFINED and entry.name in self.absolute_values):
                text = self.value_word(where, entry, width)
            else:
                text = self.address_word(where, entry, width)
            if text is not None:
                words.append((entry.offset, width, text, where))
        words.sort(key=lambda word: word[0])
        size = self.obj.sections[key].size
        if self.obj.sections[key].type == SHT_NOBITS and words:
            self.problems.append("%s: relocations in a section with no bytes" % words[0][3])
        for (offset, width, _, where), following in zip(words, [word[0] for word in words[1:]] + [size]):
            if offset + width > following:
                self.problems.append("%s: the %d-byte word overlaps the next word or the section's end"
                                     % (where, width))
        return [word[:3] for word in words]

    def value_word(self, where, entry, width):
        """The text of a word given its value: its symbol's, from the table,
        plus its addend."""
        bits = 8 * width
        if not entry.name or entry.section not in (UNDEFINED, ABSOLUTE) or entry.name not in self.absolute_values:
            self.problems.append("%s: the %d-bit word names no absolute symbol of the data objects" % (where, bits))
            return None
        value = signed64(self.absolute_values[entry.name]) + entry.addend
        if not VALUE_RANGES[width][0] <= value <= VALUE_RANGES[width][1]:
            self.problems.append("%s: the %d-bit word's value %d is out of range" % (where, bits, value))
            return None
        return "%s 0x%0*x  # %s%s (%s)" % (WORD_DIRECTIVES[width], 2 * width, value & ((1 << bits) - 1), entry.name,
                                          addend_text(entry.addend), relocation_name(entry.type))

    def address_word(self, where, entry, width):
        """The text of an address word: a symbol plus an addend."""
        name, addend = entry.name, entry.addend
        if not name and entry.section in (UNDEFINED, ABSOLUTE):
            self.problems.append("%s: the word names no symbol (the null symbol)" % where)
            return None
        if not name:
            name, addend = self.label_for(where, entry)
            if name is None:
                return None
        elif entry.section == ABSOLUTE:
            self.problems.append("%s: the word names an absolute symbol of its own object" % where)
            return None
        elif name not in self.names:
            self.problems.append("%s: the word names a symbol of a section the listing does not hold" % where)
            return None
        if not -(1 << (8 * width - 1)) <= addend < 1 << (8 * width):
            self.problems.append("%s: addend %d does not fit the %d-byte word" % (where, addend, width))
            return None
        return "%s %s%s" % (WORD_DIRECTIVES[width], self.names[name], addend_text(addend))

    def label_for(self, where, entry):
        """(label, addend) standing for a relocation against a section
        symbol: the last label at or before the target, else the first."""
        if entry.section not in self.labels or not self.labels[entry.section]:
            self.problems.append("%s: the word names a section with no label to name it by" % where)
            return None, None
        index = max(bisect.bisect_right(self.label_values[entry.section], entry.addend) - 1, 0)
        value, _, symbol = self.labels[entry.section][index]
        return symbol.name, entry.addend - value

    def target_names(self):
        """{ELF name: the target's name} for every symbol the listing may
        name (an undefined one only where a word names it)."""
        symbols = ([symbol for symbol in self.obj.symbols if symbol.section in self.sections] + self.obj.absolutes
                   + self.obj.undefined)
        names, owners = {}, {}
        for symbol in symbols:
            name = self.target.prefix + symbol.name
            if not NAME_PATTERN.match(symbol.name):
                self.problems.append("%s: symbol %r is not a plain identifier" % (self.object_name, symbol.name))
            elif name in owners:
                self.problems.append("%s: symbols %s and %s would both be %s on the target"
                                     % (self.object_name, owners[name], symbol.name, name))
            elif symbol.binding not in (STB_LOCAL, STB_GLOBAL):
                self.problems.append("%s: symbol %s is weak (binding %d)" % (self.object_name, symbol.name,
                                                                            symbol.binding))
            owners[name] = symbol.name
            names[symbol.name] = name
        for symbol in self.obj.other_symbols:
            self.problems.append("%s: symbol %s is common or in reserved section 0x%x"
                                 % (self.object_name, symbol.name, symbol.section))
        return names

    def listing(self, header):
        """The listing's text, header its first line, or None if there are problems."""
        if self.problems:
            return None
        self.lines = [header]
        for key in self.sections:
            self.write_section(key)
        self.write_absolutes()
        return "\n".join(self.lines) + "\n" if not self.problems else None

    def write_label(self, symbol):
        if symbol.binding == STB_GLOBAL:
            self.lines.append("\t.globl %s" % self.names[symbol.name])
        self.lines.append("%s:" % self.names[symbol.name])

    def write_label_inside_word(self, symbol, distance):
        """A symbol inside the word just written: an assembler label cannot
        split the word, so it is set back from the word's end, as
        sound/keysplit_tables.inc's labels are."""
        if symbol.binding == STB_GLOBAL:
            self.lines.append("\t.globl %s" % self.names[symbol.name])
        self.lines.append("\t.set %s, . - %d" % (self.names[symbol.name], distance))

    def write_bytes(self, section, start, end):
        if start >= end:
            return
        if section.type == SHT_NOBITS:
            self.lines.append("\t.space %d" % (end - start))
            return
        for at in range(start, end, BYTES_PER_LINE):
            chunk = section.contents[at:min(at + BYTES_PER_LINE, end)]
            self.lines.append('\t.ascii "%s"' % chunk.decode("latin-1").translate(ASCII_ESCAPES))

    def write_section(self, key):
        section = self.obj.sections[key]
        self.lines.append("\t.section %s" % self.target.sections[section.name])
        self.lines.append("\t.p2align %d" % max(section.alignment - 1, 0).bit_length())
        marks = [(value, 0, order, symbol) for value, order, symbol in self.labels[key]]
        marks += [(offset, 1, 0, (width, text)) for offset, width, text in self.words[key]]
        marks.sort(key=lambda mark: mark[:3])
        at = 0
        for offset, kind, _, item in marks:
            if offset > section.size:
                self.problems.append("%s: the symbol is past the section's end" % self.where(key, offset, item.name))
                continue
            if offset < at:
                self.write_label_inside_word(item, at - offset)
                continue
            self.write_bytes(section, at, offset)
            at = offset
            if kind == 0:
                self.write_label(item)
            else:
                width, text = item
                self.lines.append("\t" + text)
                at = offset + width
        self.write_bytes(section, at, section.size)

    def write_absolutes(self):
        limit = 1 << self.target.value_bits
        for symbol in self.obj.absolutes:
            value = signed64(symbol.value)
            if not -(limit >> 1) <= value < limit:
                self.problems.append("%s: absolute symbol %s = %d does not fit the target's %d-bit symbol value"
                                     % (self.object_name, symbol.name, value, self.target.value_bits))
                continue
            if self.target.global_absolutes and symbol.binding == STB_GLOBAL:
                self.lines.append("\t.globl %s" % self.names[symbol.name])
            self.lines.append("\t.set %s, %d" % (self.names[symbol.name], value))


def addend_text(addend):
    return "%+d" % addend if addend else ""


def signed64(value):
    """An ELF symbol's 64-bit value read as two's complement (CONNECTION_INVALID's -1)."""
    return value - (1 << 64) if value >= 1 << 63 else value


def convert(object_name, obj, target_name, absolute_values):
    """(listing text or None, problems) for one ElfObject."""
    conversion = Conversion(object_name, obj, TARGETS[target_name], absolute_values)
    text = conversion.listing("# %s, converted by elf_to_flat.py --target %s: generated, do not edit"
                              % (object_name, target_name))
    return text, conversion.problems


# --------------------------------------------------------------------------
# Self-test: objects made by the host assembler

# Longer than BYTES_PER_LINE and followed by a word, so that write_bytes
# splits it and a chunk that ran past its end would show.
LONG_RUN = ("abcdefghijklmnopqrstuvwxyz" * 8)[:200]
SELF_TEST_SOURCE = """
	.section .rodata
	.balign 8
	.global gTable
gTable:
	.8byte gExternal + 4
	.8byte Script + 1
	.4byte LocalLabel - 2
	.2byte B_CONST
	.2byte B_CONST + 1
	.2byte 0x1234
	.balign 8
LocalLabel:
	.ascii "a\\"\\\\\\001\\377"
	.global Lname
Lname:
	.8byte gTable
	.set inside, . - 3
	.global _under
_under:
	.byte 7
end:
	.section script_data, "aw", %progbits
Script:
	.byte 1, 2
	.4byte gTable + 8
	.ascii "LONG_RUN"
	.4byte gTable
	.bss
	.balign 8
	.global gBuffer
gBuffer:
	.space 16
after:
	.set LOCAL_CONST, 5
	.global GLOBAL_CONST
	.set GLOBAL_CONST, -1
	.global UNUSED_UNDEFINED
""".replace("LONG_RUN", LONG_RUN)
SELF_TEST_CONSTANTS = """
	.global B_CONST
	.set B_CONST, 5
	.set B_LOCAL, 7
	.global NEG_CONST
	.set NEG_CONST, -1
"""
# An object like a test's event scripts (Makefile.native, the macOS test
# objects): words of every width naming the data objects' absolute symbols,
# given their values from the --absolutes table, beside address words.
SELF_TEST_VALUES = """
	.section .rodata
	.global script
script:
	.byte B_CONST
	.byte B_CONST - 6
	.2byte B_CONST + 1
	.4byte B_CONST + 2
	.8byte B_CONST
	.8byte NEG_CONST
	.4byte script + 1
	.4byte Function
"""
SELF_TEST_VALUES_MACHO = """
	.section __DATA,__const
	.p2align 0
	.globl _script
_script:
	.byte 0x05  # B_CONST (R_X86_64_8)
	.byte 0xff  # B_CONST-6 (R_X86_64_8)
	.short 0x0006  # B_CONST+1 (R_X86_64_16)
	.long 0x00000007  # B_CONST+2 (R_X86_64_32)
	.quad 0x0000000000000005  # B_CONST (R_X86_64_64)
	.quad 0xffffffffffffffff  # NEG_CONST (R_X86_64_64)
	.long _script+1
	.long _Function
"""
# The expected listing for Mach-O: every line (the first, which names the
# object, aside), the sections in the object's header order.
SELF_TEST_MACHO = """
	.section __DATA,__game_bss,zerofill
	.p2align 3
	.globl _gBuffer
_gBuffer:
	.space 16
_after:
	.section __DATA,__const
	.p2align 3
	.globl _gTable
_gTable:
	.quad _gExternal+4
	.quad _Script+1
	.long _gTable+30
	.short 0x0005  # B_CONST (R_X86_64_16)
	.short 0x0006  # B_CONST+1 (R_X86_64_16)
	.ascii "4\\022\\000\\000\\000\\000\\000\\000"
_LocalLabel:
	.ascii "a\\042\\134\\001\\377"
	.globl _Lname
_Lname:
	.quad _gTable
	.set _inside, . - 3
	.globl __under
__under:
	.ascii "\\007"
_end:
	.section __DATA,__script_data
	.p2align 0
_Script:
	.ascii "\\001\\002"
	.long _gTable+8
	.ascii "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl"
	.ascii "mnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwx"
	.ascii "yzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij"
	.ascii "klmnopqr"
	.long _gTable
	.set _LOCAL_CONST, 5
	.set _GLOBAL_CONST, -1
"""


def self_test_coff_listing():
    """The COFF listing: the Mach-O one without the prefix, with COFF's section
    names, and the global absolute symbol global."""
    text = SELF_TEST_MACHO
    for elf_name, spelling in TARGETS["macho-x86_64"].sections.items():
        coff = TARGETS["coff-x86_64"].sections[elf_name]
        text = text.replace(".section %s\n" % spelling, ".section %s\n" % coff)
    text = text.replace("\t.set _GLOBAL_CONST, -1\n", "\t.globl _GLOBAL_CONST\n\t.set _GLOBAL_CONST, -1\n")
    return re.sub(r"(?<![A-Za-z0-9_])_", "", text)


def assemble(directory, name, source):
    path = os.path.join(directory, name)
    subprocess.run(gnu_as() + ["--64", "-o", path], input=source, universal_newlines=True, check=True)
    return path


def self_test_objects(directory):
    """{object name: ElfObject} of the self-test's two objects."""
    return {name: read_elf64(assemble(directory, name, source))
            for name, source in (("a.o", SELF_TEST_SOURCE), ("constants.o", SELF_TEST_CONSTANTS))}


def self_test_listings(failures, directory):
    objects = self_test_objects(directory)
    values, problems = build_symbol_table(objects)
    expect(failures, "table of absolute symbols", (values, problems), ({"B_CONST": 5}, []))
    for target_name, wanted in (("macho-x86_64", SELF_TEST_MACHO), ("coff-x86_64", self_test_coff_listing())):
        text, problems = convert("a.o", objects["a.o"], target_name, values)
        expect(failures, "%s problems" % target_name, problems, [])
        if text is not None:
            first, rest = text.split("\n", 1)
            expect(failures, "%s listing" % target_name, rest, wanted.lstrip("\n"))
            expect(failures, "%s first line" % target_name, first,
                   "# a.o, converted by elf_to_flat.py --target %s: generated, do not edit" % target_name)


def self_test_values(failures, directory):
    """The --absolutes table, and an object whose words take their values from it."""
    objects = self_test_objects(directory)
    values, problems = build_absolutes_table(objects)
    expect(failures, "table of the global absolute symbols", (values, problems),
           ({"B_CONST": 5, "GLOBAL_CONST": (1 << 64) - 1, "NEG_CONST": (1 << 64) - 1}, []))
    other = read_elf64(assemble(directory, "other.o", ".global B_CONST\n.set B_CONST, 6\n.set NEG_CONST, 1\n"))
    expect(failures, "table of the global absolute symbols: two values",
           build_absolutes_table({"constants.o": objects["constants.o"], "other.o": other})[1],
           ["other.o: B_CONST is 0x6 here and 0x5 in constants.o"])
    expect(failures, "its file", format_symbol_table({"A": 1}, ABSOLUTES_HEADER), ABSOLUTES_HEADER + "A 0x1\n")
    text, problems = convert("values.o", read_elf64(assemble(directory, "values.o", SELF_TEST_VALUES)),
                             "macho-x86_64", values)
    expect(failures, "value words: problems", problems, [])
    if text is not None:
        expect(failures, "value words: listing", text.split("\n", 1)[1], SELF_TEST_VALUES_MACHO.lstrip("\n"))


def self_test_table_file(failures, directory):
    path = os.path.join(directory, "table.txt")
    write_if_changed(path, format_symbol_table({"B": 0x10, "A": 0x1}))
    expect(failures, "table file", open(path, encoding="utf-8").read(), TABLE_HEADER + "A 0x1\nB 0x10\n")
    expect(failures, "table read back", read_symbol_table(path), {"A": 1, "B": 16})
    os.utime(path, ns=(1, 1))
    write_if_changed(path, format_symbol_table({"A": 0x1, "B": 0x10}))
    expect(failures, "an unchanged table is not rewritten", os.stat(path).st_mtime_ns, 1)
    os.link(path, path + ".before")
    write_if_changed(path, format_symbol_table({"A": 0x2}))
    expect(failures, "a changed table is", read_symbol_table(path), {"A": 2})
    # Written whole beside it and renamed over it: the old file (its second
    # link here) is left as it was, and nothing else is left in the directory.
    expect(failures, "a changed table replaces the file", read_symbol_table(path + ".before"), {"A": 1, "B": 16})
    expect(failures, "and leaves no temporary file", sorted(name for name in os.listdir(directory)
                                                            if name.startswith("table.txt")),
           ["table.txt", "table.txt.before"])
    for text in ("A\n", "A 0x1 extra\n", "A zz\n", "A 0x1\nA 0x2\n"):
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        try:
            read_symbol_table(path)
            failures.append("read_symbol_table accepted %r" % text)
        except CheckError as error:
            if "%s:" % path not in str(error) or "expected 'name 0xvalue'" not in str(error):
                failures.append("read_symbol_table message for %r: %s" % (text, error))


# Objects that build_symbol_table refuses beside a.o (with constants.o or
# not), with the problems it reports.
TABLE_REFUSALS = (
    ("a label, not an absolute symbol", True, ".global B_CONST\nB_CONST:\n.byte 0\n",
     ["a.o: .rodata+0x14 (B_CONST): the 16-bit word names a label of other.o, not an absolute symbol"]),
    ("defined nowhere", False, ".set B_OTHER, 1\n",
     ["a.o: .rodata+0x14 (B_CONST): no data object defines the symbol as an absolute value"]),
    ("only as a local", False, ".set B_CONST, 5\n",
     ["a.o: .rodata+0x14 (B_CONST): no data object defines the symbol as an absolute value"]),
    ("two values", True, ".global B_CONST\n.set B_CONST, 6\n",
     ["other.o: B_CONST is 0x6 here and 0x5 in constants.o"]),
    ("an address word naming another object's absolute symbol", True, ".global gExternal\n.set gExternal, 9\n",
     ["a.o: .rodata+0x0 (gExternal): the word names an absolute symbol of other.o, which the listings write local"]),
)


def self_test_table_refusals(failures, directory):
    objects = self_test_objects(directory)
    for name, with_constants, source, wanted in TABLE_REFUSALS:
        chosen = {"a.o": objects["a.o"], "other.o": read_elf64(assemble(directory, "other.o", source))}
        if with_constants:
            chosen["constants.o"] = objects["constants.o"]
        expect(failures, "table: %s" % name, build_symbol_table(chosen)[1], wanted)


# Sources the converter refuses: (case, source, target, table, the problems).
CONVERSION_REFUSALS = (
    ("unknown section", '.section .weird, "a"\n.byte 1\n', "coff-x86_64", {},
     ["x.o: section .weird (type 1, flags 0x2) is not one the converter knows"]),
    ("script_data without its write flag", '.section script_data, "a"\n.byte 1\n', "coff-x86_64", {},
     ["x.o: section script_data (type 1, flags 0x2) is not one the converter knows"]),
    ("a PC-relative word", ".section .rodata\n.4byte elsewhere - .\n", "coff-x86_64", {},
     ["x.o: .rodata+0x0 (elsewhere): R_X86_64_PC32 is not a relocation the converter writes"]),
    ("a signed 32-bit word", ".section .rodata\n.reloc 0, R_X86_64_32S, elsewhere\n.4byte 0\n", "coff-x86_64", {},
     ["x.o: .rodata+0x0 (elsewhere): R_X86_64_32S is not a relocation the converter writes"]),
    ("addend too big for 4 bytes", ".section .rodata\n.reloc 0, R_X86_64_32, elsewhere+0x100000000\n.4byte 0\n",
     "coff-x86_64", {}, ["x.o: .rodata+0x0 (elsewhere): addend 4294967296 does not fit the 4-byte word"]),
    ("addend too small for 4 bytes", ".section .rodata\n.reloc 0, R_X86_64_32, elsewhere-0x80000001\n.4byte 0\n",
     "coff-x86_64", {}, ["x.o: .rodata+0x0 (elsewhere): addend -2147483649 does not fit the 4-byte word"]),
    ("overlapping words", ".section .rodata\n.reloc 2, R_X86_64_32, elsewhere\n.8byte elsewhere\n", "coff-x86_64",
     {}, ["x.o: .rodata+0x0 (elsewhere): the 8-byte word overlaps the next word or the section's end"]),
    ("a word past the end", ".section .rodata\n.reloc 2, R_X86_64_32, elsewhere\n.4byte 0\n", "coff-x86_64", {},
     ["x.o: .rodata+0x2 (elsewhere): the 4-byte word overlaps the next word or the section's end"]),
    ("a 16-bit word naming a symbol not in the table", ".section .rodata\n.2byte B_CONST\n", "coff-x86_64", {},
     ["x.o: .rodata+0x0 (B_CONST): the 16-bit word names no absolute symbol of the data objects"]),
    ("a 16-bit word naming a label", ".section .rodata\nhere:\n.2byte here\n", "coff-x86_64", {"here": 0},
     ["x.o: .rodata+0x0 (a section): the 16-bit word names no absolute symbol of the data objects"]),
    ("a 16-bit value out of range", ".section .rodata\n.2byte B_CONST\n", "coff-x86_64", {"B_CONST": 0x10000},
     ["x.o: .rodata+0x0 (B_CONST): the 16-bit word's value 65536 is out of range"]),
    ("a 16-bit value out of range by its addend", ".section .rodata\n.2byte B_CONST - 0x8001\n", "coff-x86_64",
     {"B_CONST": 0}, ["x.o: .rodata+0x0 (B_CONST): the 16-bit word's value -32769 is out of range"]),
    ("an 8-bit word naming a symbol not in the table", ".section .rodata\n.byte B_CONST\n", "macho-x86_64", {},
     ["x.o: .rodata+0x0 (B_CONST): the 8-bit word names no absolute symbol of the data objects"]),
    ("an 8-bit word naming a label", ".section .rodata\nhere:\n.byte here\n", "macho-x86_64", {"here": 0},
     ["x.o: .rodata+0x0 (a section): the 8-bit word names no absolute symbol of the data objects"]),
    ("an 8-bit value out of range", ".section .rodata\n.byte B_CONST\n", "macho-x86_64", {"B_CONST": 0x100},
     ["x.o: .rodata+0x0 (B_CONST): the 8-bit word's value 256 is out of range"]),
    ("an 8-bit value out of range by its addend", ".section .rodata\n.byte B_CONST - 0x81\n", "macho-x86_64",
     {"B_CONST": 0}, ["x.o: .rodata+0x0 (B_CONST): the 8-bit word's value -129 is out of range"]),
    ("a 32-bit word given a value below zero", ".section .rodata\n.4byte B_CONST\n", "macho-x86_64",
     {"B_CONST": (1 << 64) - 1}, ["x.o: .rodata+0x0 (B_CONST): the 32-bit word's value -1 is out of range"]),
    ("a 32-bit word given a value past 32 bits", ".section .rodata\n.4byte B_CONST + 1\n", "macho-x86_64",
     {"B_CONST": 0xFFFFFFFF}, ["x.o: .rodata+0x0 (B_CONST): the 32-bit word's value 4294967296 is out of range"]),
    ("a word naming no symbol (as resolves an absolute symbol of the object)",
     ".global K\n.set K, 3\n.section .rodata\n.reloc 0, R_X86_64_64, K\n.8byte 0\n", "coff-x86_64", {},
     ["x.o: .rodata+0x0 (no symbol): the word names no symbol (the null symbol)"]),
    ("a word naming an empty section", ".section .rodata\n.8byte .text\n", "coff-x86_64", {},
     ["x.o: .rodata+0x0 (a section): the word names a section with no label to name it by"]),
    ("a weak symbol", ".section .rodata\n.weak soft\nsoft:\n.byte 0\n", "coff-x86_64", {},
     ["x.o: symbol soft is weak (binding 2)"]),
    ("a common symbol", ".comm shared, 4, 4\n", "coff-x86_64", {},
     ["x.o: symbol shared is common or in reserved section 0xfff2"]),
    ("a name that is not an identifier", '.section .rodata\n"odd.name":\n.byte 0\n', "macho-x86_64", {},
     ["x.o: symbol 'odd.name' is not a plain identifier"]),
    ("an absolute value COFF cannot hold", ".set BIG, 0x100000000\n", "coff-x86_64", {},
     ["x.o: absolute symbol BIG = 4294967296 does not fit the target's 32-bit symbol value"]),
    ("nor a negative one", ".set SMALL, -0x80000001\n", "coff-x86_64", {},
     ["x.o: absolute symbol SMALL = -2147483649 does not fit the target's 32-bit symbol value"]),
)


def self_test_conversion_refusals(failures, directory):
    for name, source, target_name, values, wanted in CONVERSION_REFUSALS:
        text, problems = convert("x.o", read_elf64(assemble(directory, "x.o", source)), target_name, values)
        if text is not None or len(problems) != len(wanted) or not all(
                expected in problem for expected, problem in zip(wanted, problems)):
            failures.append("refusal: %s\n  expected %r\n  got      %r" % (name, wanted, problems))
    for name, source in (("COFF holds 0xffffffff", ".set BIG, 0xffffffff\n"),
                         ("and -0x80000000", ".set SMALL, -0x80000000\n"),
                         ("Mach-O holds 0x100000000", ".set BIG, 0x100000000\n")):
        target_name = "macho-x86_64" if "Mach-O" in name else "coff-x86_64"
        expect(failures, name, convert("x.o", read_elf64(assemble(directory, "x.o", source)), target_name, {})[1], [])


def self_test_hand_made(failures):
    """What as cannot write: two symbols of one name, a section aligned to
    3; and the prefix that must not merge 'name' and '_name'."""
    rodata = Section(".rodata", 2, 1, b"\0\0", SHT_PROGBITS, SHF_ALLOC)
    for name, obj, target_name, wanted in (
            ("two symbols of one name",
             ElfObject("x", {1: rodata}, [Symbol("twin", 1, 0), Symbol("twin", 1, 1, STB_GLOBAL)], {}),
             "coff-x86_64", ["x.o: symbols twin and twin would both be twin on the target"]),
            ("a label and an absolute symbol of one name",
             ElfObject("x", {1: rodata}, [Symbol("twin", 1, 0)], {}, absolutes=[Symbol("twin", None, 3)]),
             "macho-x86_64", ["x.o: symbols twin and twin would both be _twin on the target"]),
            ("a word naming an absolute symbol of its own object",
             ElfObject("x", {1: rodata._replace(size=8, contents=bytes(8))}, [], {},
                       absolutes=[Symbol("K", None, 3, STB_GLOBAL)],
                       relocation_entries={1: [Relocation(0, R_X86_64_64, "K", ABSOLUTE, 0)]}),
             "coff-x86_64", ["x.o: .rodata+0x0 (K): the word names an absolute symbol of its own object"]),
            ("a word naming a label of a section the listing does not hold",
             ElfObject("x", {1: rodata._replace(size=8, contents=bytes(8)),
                             2: Section(".weird", 1, 1, b"\0", SHT_PROGBITS, SHF_ALLOC)},
                       [Symbol("odd", 2, 0)], {},
                       relocation_entries={1: [Relocation(0, R_X86_64_64, "odd", 2, 0)]}), "coff-x86_64",
             ["x.o: section .weird (type 1, flags 0x2) is not one the converter knows (.bss, .data, .rodata, "
              "script_data)",
              "x.o: .rodata+0x0 (odd): the word names a symbol of a section the listing does not hold"]),
            ("two words at one offset",
             ElfObject("x", {1: rodata._replace(size=8, contents=bytes(8))}, [Symbol("here", 1, 0)], {},
                       undefined=[Symbol("a", None, 0, STB_GLOBAL)],
                       relocation_entries={1: [Relocation(0, R_X86_64_64, "a", UNDEFINED, 0),
                                               Relocation(0, R_X86_64_32, "a", UNDEFINED, 0)]}), "coff-x86_64",
             ["x.o: .rodata+0x0 (a): the 8-byte word overlaps the next word or the section's end"]),
            ("relocations out of offset order",
             ElfObject("x", {1: rodata._replace(size=12, contents=bytes(12))}, [Symbol("here", 1, 0)], {},
                       undefined=[Symbol("a", None, 0, STB_GLOBAL)],
                       relocation_entries={1: [Relocation(8, R_X86_64_32, "a", UNDEFINED, 0),
                                               Relocation(0, R_X86_64_64, "a", UNDEFINED, 0)]}), "coff-x86_64", []),
            ("a symbol past its section's end",
             ElfObject("x", {1: rodata}, [Symbol("beyond", 1, 3)], {}), "coff-x86_64",
             ["x.o: .rodata+0x3 (beyond): the symbol is past the section's end"]),
            ("alignment not a power of two",
             ElfObject("x", {1: rodata._replace(alignment=3)}, [], {}), "coff-x86_64",
             ["x.o: section .rodata is aligned to 3, not a power of two"]),
            ("a name and its prefixed form stay apart",
             ElfObject("x", {1: rodata}, [Symbol("name", 1, 0), Symbol("_name", 1, 1)], {}), "macho-x86_64", []),
            ("an undefined symbol and a label prefixed apart",
             ElfObject("x", {1: rodata._replace(size=8, contents=bytes(8))}, [Symbol("_used", 1, 0)], {},
                       undefined=[Symbol("used", None, 0, STB_GLOBAL)],
                       relocation_entries={1: [Relocation(0, R_X86_64_64, "used", UNDEFINED, 0)]}),
             "macho-x86_64", [])):
        text, problems = convert("x.o", obj, target_name, {})
        expect(failures, name, problems, wanted)
        if name.startswith("an undefined"):
            expect(failures, "its listing", text.split("\n")[3:], ["__used:", "\t.quad _used", ""])


def expect(failures, name, actual, wanted):
    if actual != wanted:
        failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))


def self_test():
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        self_test_listings(failures, directory)
        self_test_values(failures, directory)
        self_test_table_file(failures, directory)
        self_test_table_refusals(failures, directory)
        self_test_conversion_refusals(failures, directory)
    self_test_hand_made(failures)
    for failure in failures:
        sys.stderr.write("elf_to_flat self-test: %s\n" % failure)
    return 1 if failures else 0


# --------------------------------------------------------------------------

USAGE = ("usage: elf_to_flat.py --symbols TABLE OBJECT...\n"
         "       elf_to_flat.py --absolutes TABLE OBJECT...\n"
         "       elf_to_flat.py --target %s --symbols TABLE --output LISTING OBJECT\n"
         "       elf_to_flat.py --self-test\n" % "|".join(sorted(TARGETS)))


def run_table(table_path, object_paths):
    values, problems = build_symbol_table({path: read_elf64(path) for path in object_paths})
    if not problems:
        write_if_changed(table_path, format_symbol_table(values))
    return problems


def run_absolutes(table_path, object_paths):
    values, problems = build_absolutes_table({path: read_elf64(path) for path in object_paths})
    if not problems:
        write_if_changed(table_path, format_symbol_table(values, ABSOLUTES_HEADER))
    return problems


def run_conversion(target_name, table_path, output_path, object_path):
    text, problems = convert(object_path, read_elf64(object_path), target_name, read_symbol_table(table_path))
    if text is not None:
        with open(output_path, "w", encoding="utf-8") as handle:
            handle.write(text)
    return problems


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    options, paths = parse_options(argv[1:], {"--symbols": None, "--absolutes": None, "--target": tuple(TARGETS),
                                              "--output": None})
    try:
        if set(options) == {"--symbols"} and paths:
            problems = run_table(options["--symbols"], paths)
        elif set(options) == {"--absolutes"} and paths:
            problems = run_absolutes(options["--absolutes"], paths)
        elif set(options) == {"--symbols", "--target", "--output"} and len(paths) == 1:
            problems = run_conversion(options["--target"], options["--symbols"], options["--output"], paths[0])
        else:
            sys.stderr.write(USAGE)
            return 2
    except (CheckError, OSError) as error:   # an OSError names the file it could not open
        problems = [str(error)]
    for problem in problems:
        sys.stderr.write("elf_to_flat: %s\n" % problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
