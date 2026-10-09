"""Read the sections, symbols and relocations of an ELF64 relocatable object.

Stands in for nothing on the GBA: it is build glue. check_relocations.py and
check_data_layout.py both ask the 64-bit build's assembled data objects where
their symbols, address words and bytes are; this is the one place that reads
the file (the ELF-64 Object File Format, version 1.5, and the System V
x86-64 psABI, "Relocation Types"). Only little-endian ELF64 is accepted:
that is what the host assembler writes for the 64-bit build.

An ElfObject holds
  sections     {key: Section(name, size, alignment, contents)}; the key is
               the section header index (any hashable in a hand-made one),
               contents b"" for a section with no bytes in the file
  symbols      [Symbol(name, section key, value)] in symbol-table order:
               the symbols defined in a section, not the section and file
               symbols
  relocations  {section key: {offset: relocation type number}}, for the
               section the relocations apply to

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
SYMBOL_TYPE_MASK = 0xF                 # st_info & 0xf
SHT_SYMTAB, SHT_RELA, SHT_NOBITS = 2, 4, 8
STT_SECTION, STT_FILE = 3, 4
SHN_LORESERVE = 0xFF00                 # indices from here (ABS, COMMON) are not sections
# Header fields by name, for the tuples struct.unpack_from returns.
SH_TYPE, SH_OFFSET, SH_SIZE, SH_LINK, SH_INFO, SH_ADDRALIGN = 1, 4, 5, 6, 7, 8

R_X86_64_64 = 1
R_X86_64_32 = 10
R_X86_64_32S = 11
R_X86_64_16 = 12
RELOCATION_NAMES = {
    R_X86_64_64: "R_X86_64_64",
    R_X86_64_32: "R_X86_64_32",
    R_X86_64_32S: "R_X86_64_32S",
    R_X86_64_16: "R_X86_64_16",
}

Section = collections.namedtuple("Section", "name size alignment contents")
Symbol = collections.namedtuple("Symbol", "name section value")


def relocation_name(number):
    return RELOCATION_NAMES.get(number, "R_X86_64_%d" % number)


class ElfObject:
    """One relocatable object; path is only for messages."""

    def __init__(self, path, sections, symbols, relocations):
        self.path = path
        self.sections = sections
        self.symbols = symbols
        self.relocations = relocations

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
                                  header[SH_ADDRALIGN], contents)
    symbols = []
    relocations = {}
    for header in headers:
        end = header[SH_OFFSET] + header[SH_SIZE]
        if header[SH_TYPE] == SHT_SYMTAB:
            strings = headers[header[SH_LINK]]
            for start in range(header[SH_OFFSET], end, SYMBOL_ENTRY_SIZE):
                name, info, _, section, value, _ = struct.unpack_from(SYMBOL_FIELDS, data, start)
                if info & SYMBOL_TYPE_MASK not in (STT_SECTION, STT_FILE) and 0 < section < SHN_LORESERVE:
                    symbols.append(Symbol(string(strings, name), section, value))
        elif header[SH_TYPE] == SHT_RELA:
            target = relocations.setdefault(header[SH_INFO], {})
            for start in range(header[SH_OFFSET], end, RELA_ENTRY_SIZE):
                offset, info, _ = struct.unpack_from(RELA_FIELDS, data, start)
                target[offset] = info & RELA_TYPE_MASK
    return ElfObject(path, sections, symbols, relocations)


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
        self_test_refusals(failures, directory, good_path)
    for failure in failures:
        sys.stderr.write("elf_object self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
