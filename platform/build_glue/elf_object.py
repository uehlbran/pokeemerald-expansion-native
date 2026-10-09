"""Read the sections, symbols and relocations of an ELF64 relocatable object.

Stands in for nothing on the GBA: it is build glue. check_relocations.py,
check_data_layout.py and the data converter (elf_to_flat.py, check_flat.py)
ask the 64-bit build's assembled data objects where their symbols, address
words and bytes are; this is the one place that reads the file (the ELF-64
Object File Format, version 1.5, and the System V x86-64 psABI, "Relocation
Types"). Only little-endian ELF64 is accepted: that is what the host
assembler writes for the 64-bit build.

An ElfObject holds
  sections     {key: Section(name, size, alignment, contents, type, flags)};
               the key is the section header index (any hashable in a
               hand-made one), contents b"" for a section with no bytes in
               the file, type and flags the header's sh_type and sh_flags
  symbols      [Symbol(name, section key, value, binding)] in symbol-table
               order: the symbols defined in a section, not the section and
               file symbols
  absolutes    [Symbol(name, None, value, binding)]: the symbols with an
               absolute value (SHN_ABS: .set and .equ), file symbols aside
  undefined    [Symbol(name, None, 0, binding)]: the named symbols the
               object uses or declares but does not define (SHN_UNDEF)
  other_symbols [Symbol(name, section index, value, binding)]: the rest
               (SHN_COMMON and the other reserved indices), which nothing
               here interprets
  relocations  {section key: {offset: relocation type number}}, for the
               section the relocations apply to
  relocation_entries {section key: [Relocation(offset, type, name, section,
               addend)]} in file order: the same relocations with the symbol
               each names ("" for a section symbol, and for the null symbol,
               which is UNDEFINED) and where that symbol is defined (a
               section key, ABSOLUTE or UNDEFINED)

extents() is the distance from each symbol to the next one that counts, the
bytes it owns.

'python3 -I elf_object.py --self-test' assembles small objects with the
host assembler and checks each of the above and each refusal.
"""

import bisect
import collections
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError  # noqa: E402

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
ELFDATA2LSB = 1
SECTION_HEADER_FIELDS = "<IIQQQQIIQQ"   # Elf64_Shdr: name, type, flags, addr, offset, size, link, info, addralign, entsize
SECTION_HEADERS_AT_FIELD = 0x28          # e_shoff, a u64
SECTION_TABLE_AT = 0x3A               # e_shentsize, e_shnum, e_shstrndx (u16 each)
SECTION_TABLE_FIELDS = "<HHH"
SYMBOL_ENTRY_SIZE = 24                 # Elf64_Sym
SYMBOL_FIELDS = "<IBBHQQ"              # name, info, other, shndx, value, size
RELA_ENTRY_SIZE = 24                   # Elf64_Rela
RELA_FIELDS = "<QQq"                   # offset, info, addend
RELA_TYPE_MASK = 0xFFFFFFFF            # ELF64_R_TYPE: the low 32 bits of r_info
RELA_SYMBOL_SHIFT = 32                 # ELF64_R_SYM: the high 32 bits
SYMBOL_TYPE_MASK = 0xF                 # st_info & 0xf
SYMBOL_BINDING_SHIFT = 4               # st_info >> 4
SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA, SHT_NOBITS = 0, 1, 2, 3, 4, 8
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4
STT_SECTION, STT_FILE = 3, 4
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
SHN_UNDEF = 0
SHN_LORESERVE = 0xFF00                 # indices from here (ABS, COMMON) are not sections
SHN_ABS = 0xFFF1
# Header fields by name, for the tuples struct.unpack_from returns.
SH_TYPE, SH_FLAGS, SH_OFFSET, SH_SIZE, SH_LINK, SH_INFO, SH_ADDRALIGN = 1, 2, 4, 5, 6, 7, 8

R_X86_64_64 = 1
R_X86_64_PC32 = 2
R_X86_64_32 = 10
R_X86_64_32S = 11
R_X86_64_16 = 12
RELOCATION_NAMES = {
    R_X86_64_64: "R_X86_64_64",
    R_X86_64_PC32: "R_X86_64_PC32",
    R_X86_64_32: "R_X86_64_32",
    R_X86_64_32S: "R_X86_64_32S",
    R_X86_64_16: "R_X86_64_16",
}

# Where a relocation's symbol is defined when it is not in a section.
ABSOLUTE, UNDEFINED = "absolute", "undefined"

Section = collections.namedtuple("Section", "name size alignment contents type flags",
                                 defaults=(SHT_PROGBITS, SHF_ALLOC))
Symbol = collections.namedtuple("Symbol", "name section value binding", defaults=(STB_LOCAL,))
Relocation = collections.namedtuple("Relocation", "offset type name section addend")


def relocation_name(number):
    return RELOCATION_NAMES.get(number, "R_X86_64_%d" % number)


class ElfObject:
    """One relocatable object; path is only for messages. A hand-made one
    (the checkers' self-tests) may leave out everything after relocations."""

    def __init__(self, path, sections, symbols, relocations, absolutes=(), undefined=(), other_symbols=(),
                 relocation_entries=None):
        self.path = path
        self.sections = sections
        self.symbols = symbols
        self.relocations = relocations
        self.absolutes = list(absolutes)
        self.undefined = list(undefined)
        self.other_symbols = list(other_symbols)
        self.relocation_entries = relocation_entries or {}

    def extents(self, is_boundary):
        """For each symbol (a list parallel to .symbols): the bytes from it to
        the next symbol in its section for which is_boundary(symbol) is true
        and whose value is greater, or to the section's end."""
        starts = collections.defaultdict(set)
        for symbol in self.symbols:
            if is_boundary(symbol):
                starts[symbol.section].add(symbol.value)
        sorted_starts = {section: sorted(values) for section, values in starts.items()}
        result = []
        for symbol in self.symbols:
            values = sorted_starts.get(symbol.section, [])
            index = bisect.bisect_right(values, symbol.value)
            end = values[index] if index < len(values) else self.sections[symbol.section].size
            result.append(end - symbol.value)
        return result


def read_elf64(path):
    """The ElfObject of the file at path; CheckError if it is not one."""
    with open(path, "rb") as handle:
        data = handle.read()
    try:
        return parse_elf64(path, data)
    except (struct.error, ValueError, IndexError) as error:
        raise CheckError("%s: truncated or malformed ELF64 object (%s)" % (path, error))


def parse_elf64(path, data):
    if data[:4] != ELF_MAGIC or data[4:6] != bytes((ELFCLASS64, ELFDATA2LSB)):
        raise CheckError("%s: expected a little-endian ELF64 object" % path)
    section_headers_at, = struct.unpack_from("<Q", data, SECTION_HEADERS_AT_FIELD)
    entry_size, count, names_index = struct.unpack_from(SECTION_TABLE_FIELDS, data, SECTION_TABLE_AT)
    headers = [struct.unpack_from(SECTION_HEADER_FIELDS, data, section_headers_at + index * entry_size)
               for index in range(count)]

    def string(table, offset):
        start = table[SH_OFFSET] + offset
        return data[start:data.index(b"\0", start)].decode()

    sections = {}
    for index, header in enumerate(headers):
        contents = b"" if header[SH_TYPE] == SHT_NOBITS else data[header[SH_OFFSET]:header[SH_OFFSET] + header[SH_SIZE]]
        sections[index] = Section(string(headers[names_index], header[0]), header[SH_SIZE],
                                  header[SH_ADDRALIGN], contents, header[SH_TYPE], header[SH_FLAGS])
    table = []   # every entry of the symbol table: (name, type, binding, section index, value)
    for header in headers:
        if header[SH_TYPE] == SHT_SYMTAB:
            strings = headers[header[SH_LINK]]
            for start in range(header[SH_OFFSET], header[SH_OFFSET] + header[SH_SIZE], SYMBOL_ENTRY_SIZE):
                name, info, _, section, value, _ = struct.unpack_from(SYMBOL_FIELDS, data, start)
                table.append((string(strings, name), info & SYMBOL_TYPE_MASK, info >> SYMBOL_BINDING_SHIFT,
                              section, value))
    obj = ElfObject(path, sections, [], {}, relocation_entries={})
    for name, kind, binding, section, value in table:
        if kind in (STT_SECTION, STT_FILE):
            continue
        if 0 < section < SHN_LORESERVE:
            obj.symbols.append(Symbol(name, section, value, binding))
        elif section == SHN_ABS:
            obj.absolutes.append(Symbol(name, None, value, binding))
        elif section == SHN_UNDEF:
            if name:
                obj.undefined.append(Symbol(name, None, 0, binding))
        else:
            obj.other_symbols.append(Symbol(name, section, value, binding))
    for header in headers:
        if header[SH_TYPE] == SHT_RELA:
            types = obj.relocations.setdefault(header[SH_INFO], {})
            entries = obj.relocation_entries.setdefault(header[SH_INFO], [])
            for start in range(header[SH_OFFSET], header[SH_OFFSET] + header[SH_SIZE], RELA_ENTRY_SIZE):
                offset, info, addend = struct.unpack_from(RELA_FIELDS, data, start)
                types[offset] = info & RELA_TYPE_MASK
                entries.append(relocation_entry(table[info >> RELA_SYMBOL_SHIFT], offset, info & RELA_TYPE_MASK,
                                                addend))
    return obj


def relocation_entry(symbol, offset, kind, addend):
    """The Relocation at offset naming symbol, an entry of the symbol table."""
    name, symbol_kind, _, section, _ = symbol
    if section == SHN_ABS:
        where = ABSOLUTE
    elif section == SHN_UNDEF:
        where = UNDEFINED
    else:
        where = section
    return Relocation(offset, kind, "" if symbol_kind == STT_SECTION else name, where, addend)


# --------------------------------------------------------------------------
# Self-test: small objects made by the host assembler

SELF_TEST_SOURCE = """
	.section .rodata
	.balign 8
first:
	.8byte target
	.4byte 7
	.balign 8
second:
	.4byte target
	.section script_data, "a"
	.balign 2
inner:
	.byte 1
	.section .bss
buffer:
	.space 16
	.type typed, @function
typed:
"""

# What the converter needs beyond the labels: bindings, absolute, undefined
# and common symbols, and each relocation's symbol and addend (a reference to
# a local label is to its section symbol, as GNU as writes it).
SELF_TEST_KINDS_SOURCE = """
	.section .rodata
	.balign 8
	.global head
head:
	.8byte target + 4
	.4byte local - 2
local:
	.2byte CONSTANT
	.4byte head
	.set LOCAL_VALUE, 5
	.global GLOBAL_VALUE
	.set GLOBAL_VALUE, -1
	.global declared
	.weak soft
soft:
	.comm shared, 4, 4
	.section script_data, "aw", %progbits
	.byte 1
"""


def expect(failures, name, actual, wanted):
    if actual != wanted:
        failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))


def self_test_parse(failures, directory):
    path = os.path.join(directory, "a.o")
    subprocess.run(["as", "--64", "-o", path], input=SELF_TEST_SOURCE, universal_newlines=True, check=True)
    obj = read_elf64(path)
    by_name = {section.name: key for key, section in obj.sections.items()}
    rodata, script, bss = by_name[".rodata"], by_name["script_data"], by_name[".bss"]
    expect(failures, "symbols (labels only, in table order)",
           [(symbol.name, obj.sections[symbol.section].name, symbol.value) for symbol in obj.symbols
            if symbol.name != "target"],
           [("first", ".rodata", 0), ("second", ".rodata", 16), ("inner", "script_data", 0),
            ("buffer", ".bss", 0), ("typed", ".bss", 16)])
    expect(failures, "undefined symbols are not listed", [s.name for s in obj.symbols if s.name == "target"], [])
    expect(failures, "relocations", obj.relocations[rodata], {0: R_X86_64_64, 16: R_X86_64_32})
    expect(failures, "no relocations in a section without any", script in obj.relocations, False)
    expect(failures, "alignment and size", (obj.sections[rodata].alignment, obj.sections[rodata].size,
                                            obj.sections[script].alignment, obj.sections[script].size),
           (8, 20, 2, 1))
    expect(failures, "contents of a section", obj.sections[script].contents, b"\x01")
    expect(failures, "contents of a section with no bytes in the file", obj.sections[bss].contents, b"")
    expect(failures, "its size is still known", obj.sections[bss].size, 16)
    everything = obj.extents(lambda symbol: True)
    expect(failures, "extents: to the next symbol or the section's end", everything, [16, 4, 1, 16, 0])
    without_second = obj.extents(lambda symbol: symbol.name != "second")
    expect(failures, "extents: a symbol that is not a boundary does not end the one before it",
           without_second, [20, 4, 1, 16, 0])
    expect(failures, "relocation_name", [relocation_name(R_X86_64_32S), relocation_name(99)],
           ["R_X86_64_32S", "R_X86_64_99"])
    return path


def self_test_kinds(failures, directory):
    path = os.path.join(directory, "kinds.o")
    subprocess.run(["as", "--64", "-o", path], input=SELF_TEST_KINDS_SOURCE, universal_newlines=True, check=True)
    obj = read_elf64(path)
    by_name = {section.name: key for key, section in obj.sections.items()}
    rodata, script, bss = by_name[".rodata"], by_name["script_data"], by_name[".bss"]
    expect(failures, "bindings of the labels",
           obj.symbols, [Symbol("local", rodata, 12, STB_LOCAL), Symbol("head", rodata, 0, STB_GLOBAL),
                         Symbol("soft", rodata, 18, STB_WEAK)])
    expect(failures, "absolute symbols", obj.absolutes,
           [Symbol("LOCAL_VALUE", None, 5, STB_LOCAL), Symbol("GLOBAL_VALUE", None, (1 << 64) - 1, STB_GLOBAL)])
    expect(failures, "undefined symbols, used or only declared", obj.undefined,
           [Symbol("target", None, 0, STB_GLOBAL), Symbol("CONSTANT", None, 0, STB_GLOBAL),
            Symbol("declared", None, 0, STB_GLOBAL)])
    expect(failures, "a common symbol is neither", [(s.name, s.section) for s in obj.other_symbols],
           [("shared", 0xFFF2)])
    expect(failures, "relocation entries: symbol, where it is defined, addend", obj.relocation_entries[rodata],
           [Relocation(0, R_X86_64_64, "target", UNDEFINED, 4), Relocation(8, R_X86_64_32, "", rodata, 10),
            Relocation(12, R_X86_64_16, "CONSTANT", UNDEFINED, 0), Relocation(14, R_X86_64_32, "head", rodata, 0)])
    expect(failures, "the relocation types agree with the entries", obj.relocations[rodata],
           {0: R_X86_64_64, 8: R_X86_64_32, 12: R_X86_64_16, 14: R_X86_64_32})
    expect(failures, "section types and flags",
           [(obj.sections[key].type, obj.sections[key].flags) for key in (rodata, script, bss)],
           [(SHT_PROGBITS, SHF_ALLOC), (SHT_PROGBITS, SHF_ALLOC | SHF_WRITE), (SHT_NOBITS, SHF_ALLOC | SHF_WRITE)])
    expect(failures, "a hand-made object has none of them",
           [ElfObject("x", {}, [], {}).relocation_entries, ElfObject("x", {}, [], {}).absolutes], [{}, []])
    expect(failures, "the null symbol is undefined and nameless",
           relocation_entry(("", 0, STB_LOCAL, SHN_UNDEF, 0), 4, R_X86_64_64, 7),
           Relocation(4, R_X86_64_64, "", UNDEFINED, 7))
    expect(failures, "an absolute symbol", relocation_entry(("K", 0, STB_GLOBAL, SHN_ABS, 3), 0, R_X86_64_16, 0),
           Relocation(0, R_X86_64_16, "K", ABSOLUTE, 0))
    expect(failures, "a section symbol is nameless even if its entry has a name",
           relocation_entry((".rodata", STT_SECTION, STB_LOCAL, 4, 0), 8, R_X86_64_32, 2),
           Relocation(8, R_X86_64_32, "", 4, 2))


def self_test_refusals(failures, directory, good_path):
    with open(good_path, "rb") as handle:
        good = handle.read()
    elf32 = good[:4] + bytes((1,)) + good[5:]
    big_endian = good[:5] + bytes((2,)) + good[6:]
    for name, data, wanted in (
            ("not ELF", b"hello" * 20, "expected a little-endian ELF64 object"),
            ("ELF32", elf32, "expected a little-endian ELF64 object"),
            ("big-endian", big_endian, "expected a little-endian ELF64 object"),
            ("truncated headers", good[:SECTION_HEADERS_AT_FIELD + 8 + 8], "truncated or malformed"),
            ("truncated section table", good[:len(good) - 40], "truncated or malformed")):
        path = os.path.join(directory, "bad.o")
        with open(path, "wb") as handle:
            handle.write(data)
        try:
            read_elf64(path)
            failures.append("%s: accepted" % name)
        except CheckError as error:
            if wanted not in str(error) or path not in str(error):
                failures.append("%s: the message %r does not name the file and %r" % (name, str(error), wanted))


def self_test():
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        good_path = self_test_parse(failures, directory)
        self_test_kinds(failures, directory)
        self_test_refusals(failures, directory, good_path)
    for failure in failures:
        sys.stderr.write("elf_object self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
