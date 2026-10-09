"""Read the sections, symbols and relocations of a COFF x86-64 object.

Stands in for nothing on the GBA: it is build glue. check_flat.py compares
the data the converter wrote for Windows (elf_to_flat.py, assembled by
clang for MinGW-w64) with the ELF object it came from; this is the one
place that reads the COFF file. It follows Microsoft's "PE Format"
(learn.microsoft.com/windows/win32/debug/pe-format): "COFF File Header
(Object and Image)", "Section Table (Section Headers)", "COFF Relocations
(Object Only)", "COFF Symbol Table" and "COFF String Table". Only an object
for IMAGE_FILE_MACHINE_AMD64 without an optional header is accepted: what
clang writes for -target x86_64-w64-windows-gnu.

A CoffObject holds
  sections  [CoffSection(name, characteristics, alignment, size, contents,
            relocations)] in file order; section number N (symbols' section
            numbers) is index N - 1. alignment is in bytes, from the
            IMAGE_SCN_ALIGN_* bits; contents is b"" for uninitialised data
            (IMAGE_SCN_CNT_UNINITIALIZED_DATA), whose size is still known;
            relocations [CoffRelocation(offset, symbol, type)] with symbol a
            symbol-table index. A section with more than 65,535 relocations
            (IMAGE_SCN_LNK_NRELOC_OVFL) is read as the format says: the
            count is in the first relocation, which is not one.
  symbols   {index: CoffSymbol(name, value, section, storage_class, aux)}
            for every symbol-table entry that is not an auxiliary record:
            section is the section number (IMAGE_SYM_UNDEFINED 0,
            IMAGE_SYM_ABSOLUTE -1), aux the number of auxiliary records
            after it (a section's own symbol has one).

'python3 -I coff_object.py --self-test' assembles small objects with clang
and checks each of the above and each refusal.
"""

import collections
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError  # noqa: E402

IMAGE_FILE_MACHINE_AMD64 = 0x8664
FILE_HEADER_FIELDS = "<HHIIIHH"   # Machine, NumberOfSections, TimeDateStamp, PointerToSymbolTable,
FILE_HEADER_SIZE = 20             # NumberOfSymbols, SizeOfOptionalHeader, Characteristics
SECTION_FIELDS = "<8sIIIIIIHHI"   # Name, VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData,
SECTION_SIZE = 40                 # PointerToRelocations, PointerToLinenumbers, NumberOfRelocations,
                                  # NumberOfLinenumbers, Characteristics
RELOCATION_FIELDS = "<IIH"        # VirtualAddress, SymbolTableIndex, Type
RELOCATION_SIZE = 10
SYMBOL_FIELDS = "<8sIhHBB"        # Name, Value, SectionNumber, Type, StorageClass, NumberOfAuxSymbols
SYMBOL_SIZE = 18
LONG_NAME_PREFIX = b"/"           # a section name "/N": N is the offset in the string table
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_LNK_NRELOC_OVFL = 0x01000000
IMAGE_SCN_MEM_WRITE = 0x80000000
IMAGE_SCN_ALIGN_SHIFT, IMAGE_SCN_ALIGN_MASK = 20, 0xF   # 1 = 1 byte ... 14 = 8192 bytes
RELOCATION_COUNT_OVERFLOW = 0xFFFF
IMAGE_SYM_UNDEFINED, IMAGE_SYM_ABSOLUTE = 0, -1
IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC = 2, 3
IMAGE_REL_AMD64_ADDR64 = 0x1      # the 64-bit address of the target
IMAGE_REL_AMD64_ADDR32 = 0x2      # its 32-bit address
IMAGE_REL_AMD64_ADDR32NB = 0x3    # its 32-bit address relative to the image base
IMAGE_REL_AMD64_REL32 = 0x4       # a 32-bit displacement from the end of the word

CoffSection = collections.namedtuple("CoffSection", "name characteristics alignment size contents relocations")
CoffRelocation = collections.namedtuple("CoffRelocation", "offset symbol type")
CoffSymbol = collections.namedtuple("CoffSymbol", "name value section storage_class aux")


class CoffObject:
    """One relocatable object; path is only for messages."""

    def __init__(self, path, sections, symbols):
        self.path = path
        self.sections = sections
        self.symbols = symbols


def read_coff64(path):
    """The CoffObject of the file at path; CheckError if it is not one."""
    with open(path, "rb") as handle:
        data = handle.read()
    try:
        return parse_coff64(path, data)
    except (struct.error, ValueError, IndexError) as error:
        raise CheckError("%s: truncated or malformed COFF object (%s)" % (path, error))


def parse_coff64(path, data):
    machine, section_count, _, symbols_at, symbol_count, optional_size, _ = \
        struct.unpack_from(FILE_HEADER_FIELDS, data, 0)
    if machine != IMAGE_FILE_MACHINE_AMD64 or optional_size != 0:
        raise CheckError("%s: expected an x86-64 COFF object (IMAGE_FILE_MACHINE_AMD64, no optional header)" % path)
    strings_at = symbols_at + symbol_count * SYMBOL_SIZE
    strings_size, = struct.unpack_from("<I", data, strings_at)
    strings = data[strings_at:strings_at + strings_size]
    if len(strings) != strings_size:
        raise ValueError("the string table runs past the end of the file")
    sections = [read_section(data, FILE_HEADER_SIZE + index * SECTION_SIZE, strings)
                for index in range(section_count)]
    symbols = {}
    index = 0
    while index < symbol_count:
        raw_name, value, section, _, storage_class, aux = struct.unpack_from(SYMBOL_FIELDS, data,
                                                                             symbols_at + index * SYMBOL_SIZE)
        symbols[index] = CoffSymbol(symbol_name(raw_name, strings), value, section, storage_class, aux)
        index += 1 + aux
    return CoffObject(path, sections, symbols)


def string_at(strings, offset):
    return strings[offset:strings.index(b"\0", offset)].decode()


def symbol_name(raw, strings):
    """A symbol's name: the 8 bytes, or (first four zero) an offset into the string table."""
    if raw[:4] == b"\0\0\0\0":
        return string_at(strings, struct.unpack_from("<I", raw, 4)[0])
    return raw.rstrip(b"\0").decode()


def read_section(data, at, strings):
    (raw_name, _, _, size, contents_at, relocations_at, _, relocation_count, _,
     characteristics) = struct.unpack_from(SECTION_FIELDS, data, at)
    name = raw_name.rstrip(b"\0")
    name = string_at(strings, int(name[1:])) if name.startswith(LONG_NAME_PREFIX) else name.decode()
    if characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA:
        contents = b""
    else:
        contents = data[contents_at:contents_at + size]
        if len(contents) != size:
            raise ValueError("section %s runs past the end of the file" % name)
    first = 0
    if characteristics & IMAGE_SCN_LNK_NRELOC_OVFL and relocation_count == RELOCATION_COUNT_OVERFLOW:
        relocation_count = struct.unpack_from(RELOCATION_FIELDS, data, relocations_at)[0]
        first = 1
    relocations = [CoffRelocation(*struct.unpack_from(RELOCATION_FIELDS, data,
                                                      relocations_at + number * RELOCATION_SIZE))
                   for number in range(first, relocation_count)]
    alignment_code = characteristics >> IMAGE_SCN_ALIGN_SHIFT & IMAGE_SCN_ALIGN_MASK
    alignment = 1 << (alignment_code - 1) if alignment_code else 1
    return CoffSection(name, characteristics, alignment, size, contents, relocations)


# --------------------------------------------------------------------------
# Self-test: small objects made by clang

CLANG_TARGET = "x86_64-w64-windows-gnu"
SELF_TEST_SOURCE = """
	.section .rdata,"dr"
	.p2align 3
	.globl head
head:
	.quad target + 4
	.long local - 2
local:
	.byte 1, 2
	.section a_long_section_name,"dw"
	.byte 3
	.long head
	.section .bss,"bw"
	.p2align 4
buffer:
	.space 16
	.set VALUE, 5
	.globl GLOBAL_VALUE
	.set GLOBAL_VALUE, -1
	.globl a_symbol_name_longer_than_eight
	.set a_symbol_name_longer_than_eight, 7
"""


def expect(failures, name, actual, wanted):
    if actual != wanted:
        failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))


def assemble(directory, source, name="a.o"):
    path = os.path.join(directory, name)
    subprocess.run(["clang", "-target", CLANG_TARGET, "-c", "-x", "assembler", "-o", path, "-"],
                   input=source, universal_newlines=True, check=True)
    return path


def self_test_parse(failures, directory):
    path = assemble(directory, SELF_TEST_SOURCE)
    obj = read_coff64(path)
    named = {section.name: section for section in obj.sections}
    rdata, long_name, bss = named[".rdata"], named["a_long_section_name"], named[".bss"]
    number = {section.name: index + 1 for index, section in enumerate(obj.sections)}
    expect(failures, "section fields", [(s.alignment, s.size) for s in (rdata, long_name, bss)],
           [(8, 14), (1, 5), (16, 16)])
    expect(failures, "characteristics", [bool(s.characteristics & IMAGE_SCN_MEM_WRITE) for s in (rdata, long_name)],
           [False, True])
    expect(failures, "contents (an addend is in place)", rdata.contents,
           b"\x04\0\0\0\0\0\0\0\xfe\xff\xff\xff\x01\x02")
    expect(failures, "uninitialised data has no bytes", bss.contents, b"")
    plain = {s.name: (s.value, s.section, s.storage_class) for s in obj.symbols.values() if not s.aux}
    expect(failures, "symbols", plain,
           {"head": (0, number[".rdata"], IMAGE_SYM_CLASS_EXTERNAL),
            "local": (12, number[".rdata"], IMAGE_SYM_CLASS_STATIC),
            "buffer": (0, number[".bss"], IMAGE_SYM_CLASS_STATIC),
            "VALUE": (5, IMAGE_SYM_ABSOLUTE, IMAGE_SYM_CLASS_STATIC),
            "GLOBAL_VALUE": (0xFFFFFFFF, IMAGE_SYM_ABSOLUTE, IMAGE_SYM_CLASS_EXTERNAL),
            "a_symbol_name_longer_than_eight": (7, IMAGE_SYM_ABSOLUTE, IMAGE_SYM_CLASS_EXTERNAL),
            "target": (0, IMAGE_SYM_UNDEFINED, IMAGE_SYM_CLASS_EXTERNAL)})
    expect(failures, "a section's own symbol has an auxiliary record",
           sorted(s.name for s in obj.symbols.values() if s.aux),
           sorted(section.name for section in obj.sections))
    expect(failures, "relocations",
           [(r.offset, obj.symbols[r.symbol].name, r.type) for r in rdata.relocations + long_name.relocations],
           [(0, "target", IMAGE_REL_AMD64_ADDR64), (8, "local", IMAGE_REL_AMD64_ADDR32),
            (1, "head", IMAGE_REL_AMD64_ADDR32)])
    return path


def self_test_overflow(failures, directory):
    """65,536 relocations: more than NumberOfRelocations holds."""
    count = RELOCATION_COUNT_OVERFLOW + 1
    path = assemble(directory, "\t.section .rdata,\"dr\"\n" + "\t.long target\n" * count, "many.o")
    section = next(s for s in read_coff64(path).sections if s.name == ".rdata")
    expect(failures, "relocation count overflow", (len(section.relocations), section.relocations[-1].offset,
                                                   bool(section.characteristics & IMAGE_SCN_LNK_NRELOC_OVFL)),
           (count, 4 * (count - 1), True))
    expect(failures, "the first real relocation", section.relocations[0].offset, 0)


def self_test_refusals(failures, directory, good_path):
    with open(good_path, "rb") as handle:
        good = handle.read()
    i386 = struct.pack("<H", 0x14C) + good[2:]
    with_optional_header = good[:16] + struct.pack("<H", 240) + good[18:]
    for name, data, wanted in (
            ("not COFF", b"hello" * 20, "expected an x86-64 COFF object"),
            ("i386", i386, "expected an x86-64 COFF object"),
            ("an image", with_optional_header, "expected an x86-64 COFF object"),
            ("truncated header", good[:10], "truncated or malformed"),
            ("truncated strings", good[:len(good) - 8], "truncated or malformed")):
        path = os.path.join(directory, "bad.o")
        with open(path, "wb") as handle:
            handle.write(data)
        try:
            read_coff64(path)
            failures.append("%s: accepted" % name)
        except CheckError as error:
            if wanted not in str(error) or path not in str(error):
                failures.append("%s: the message %r does not name the file and %r" % (name, str(error), wanted))


def self_test():
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        good_path = self_test_parse(failures, directory)
        self_test_overflow(failures, directory)
        self_test_refusals(failures, directory, good_path)
    for failure in failures:
        sys.stderr.write("coff_object self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
