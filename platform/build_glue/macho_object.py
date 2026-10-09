"""Read the sections, symbols and relocations of a Mach-O x86-64 object.

Stands in for nothing on the GBA: it is build glue. check_flat.py compares
the data the converter wrote for macOS (elf_to_flat.py, assembled by clang)
with the ELF object it came from; this is the one place that reads the
Mach-O file. It follows Apple's headers <mach-o/loader.h> (mach_header_64,
segment_command_64, section_64, symtab_command), <mach-o/nlist.h>
(nlist_64) and <mach-o/reloc.h> with <mach-o/x86_64/reloc.h>
(relocation_info), as published in Apple's open source cctools and xnu
(EXTERNAL_HEADERS/mach-o/), and the "OS X ABI Mach-O File Format
Reference". Only a little-endian 64-bit relocatable object (MH_OBJECT) for
x86-64 is accepted: what clang writes for -target x86_64-apple-macos*.

A MachOObject holds
  sections  [MachOSection(segment, name, address, size, alignment, type,
            contents, relocations)] in file order; the section ordinal that
            symbols and relocations use is the index plus one. address is
            the section's address inside the object (symbol values are
            addresses), alignment a power of two in bytes, type the
            section_64 flags' SECTION_TYPE (S_ZEROFILL: no bytes in the
            file, contents b""), relocations [MachORelocation(offset,
            symbol, pcrel, length, extern, type)] with length in bytes and
            symbol a symbol-table index (extern) or a section ordinal
  symbols   {index: MachOSymbol(name, kind, external, section, value)} by
            symbol-table index (what an extern relocation names), the
            debugging (stab) entries left out: kind is N_UNDF, N_ABS or
            N_SECT, section an ordinal (0 for none)

'python3 -I macho_object.py --self-test' assembles small objects with clang
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

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_X86_64 = 0x01000007
MH_OBJECT = 1
HEADER_FIELDS = "<IIIIIIII"          # magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved
HEADER_SIZE = 32
LOAD_COMMAND_FIELDS = "<II"          # cmd, cmdsize
LC_SYMTAB = 0x2
LC_SEGMENT_64 = 0x19
SEGMENT_FIELDS = "<II16sQQQQiiII"     # cmd, cmdsize, segname, vmaddr, vmsize, fileoff, filesize, maxprot, initprot,
SEGMENT_SIZE = 72                    # nsects, flags
SEGMENT_SECTION_COUNT = 9
SECTION_FIELDS = "<16s16sQQIIIIIIII"  # sectname, segname, addr, size, offset, align, reloff, nreloc, flags,
SECTION_SIZE = 80                    # reserved1-3
SYMTAB_FIELDS = "<IIIIII"            # cmd, cmdsize, symoff, nsyms, stroff, strsize
NLIST_FIELDS = "<IBBHQ"              # n_strx, n_type, n_sect, n_desc, n_value
NLIST_SIZE = 16
RELOCATION_FIELDS = "<II"            # r_address, then r_symbolnum:24 r_pcrel:1 r_length:2 r_extern:1 r_type:4
RELOCATION_SIZE = 8
R_SCATTERED = 0x80000000             # in r_address: a scattered relocation (never on x86-64)
SYMBOLNUM_MASK = 0xFFFFFF
PCREL_SHIFT, LENGTH_SHIFT, EXTERN_SHIFT, TYPE_SHIFT = 24, 25, 27, 28
LENGTH_MASK, TYPE_MASK = 0x3, 0xF
SECTION_TYPE = 0x000000FF            # in section_64 flags
S_REGULAR, S_ZEROFILL = 0x0, 0x1
N_STAB, N_TYPE, N_EXT = 0xE0, 0x0E, 0x01
N_UNDF, N_ABS, N_SECT = 0x0, 0x2, 0xE
X86_64_RELOC_UNSIGNED = 0            # <mach-o/x86_64/reloc.h>: an absolute address
X86_64_RELOC_SIGNED = 1              # a PC-relative 32-bit displacement
X86_64_RELOC_SUBTRACTOR = 5          # the first half of a difference

MachOSection = collections.namedtuple("MachOSection", "segment name address size alignment type contents relocations")
MachORelocation = collections.namedtuple("MachORelocation", "offset symbol pcrel length extern type")
MachOSymbol = collections.namedtuple("MachOSymbol", "name kind external section value")


class MachOObject:
    """One relocatable object; path is only for messages."""

    def __init__(self, path, sections, symbols):
        self.path = path
        self.sections = sections
        self.symbols = symbols


def read_macho64(path):
    """The MachOObject of the file at path; CheckError if it is not one."""
    with open(path, "rb") as handle:
        data = handle.read()
    try:
        return parse_macho64(path, data)
    except (struct.error, ValueError, IndexError) as error:
        raise CheckError("%s: truncated or malformed Mach-O object (%s)" % (path, error))


def parse_macho64(path, data):
    magic, cpu, _, file_type, command_count, _, _, _ = struct.unpack_from(HEADER_FIELDS, data, 0)
    if magic != MH_MAGIC_64 or cpu != CPU_TYPE_X86_64 or file_type != MH_OBJECT:
        raise CheckError("%s: expected a 64-bit x86-64 Mach-O object (MH_OBJECT)" % path)
    sections, symbols = [], {}
    at = HEADER_SIZE
    for _ in range(command_count):
        command, size = struct.unpack_from(LOAD_COMMAND_FIELDS, data, at)
        if command == LC_SEGMENT_64:
            sections.extend(read_sections(path, data, at))
        elif command == LC_SYMTAB:
            symbols = read_symbols(data, at)
        if size <= 0:
            raise ValueError("load command of size %d" % size)
        at += size
    return MachOObject(path, sections, symbols)


def read_sections(path, data, at):
    """The sections of the LC_SEGMENT_64 command at `at`."""
    count = struct.unpack_from(SEGMENT_FIELDS, data, at)[SEGMENT_SECTION_COUNT]
    sections = []
    for index in range(count):
        (name, segment, address, size, offset, alignment, relocations_at, relocation_count, flags,
         _, _, _) = struct.unpack_from(SECTION_FIELDS, data, at + SEGMENT_SIZE + index * SECTION_SIZE)
        kind = flags & SECTION_TYPE
        contents = b"" if kind == S_ZEROFILL else data[offset:offset + size]
        if len(contents) != (0 if kind == S_ZEROFILL else size):
            raise ValueError("section %s runs past the end of the file" % name.rstrip(b"\0").decode())
        relocations = [read_relocation(path, data, relocations_at + number * RELOCATION_SIZE)
                       for number in range(relocation_count)]
        sections.append(MachOSection(segment.rstrip(b"\0").decode(), name.rstrip(b"\0").decode(), address, size,
                                     1 << alignment, kind, contents, relocations))
    return sections


def read_relocation(path, data, at):
    address, packed = struct.unpack_from(RELOCATION_FIELDS, data, at)
    if address & R_SCATTERED:
        raise CheckError("%s: a scattered relocation at 0x%x (x86-64 has none)" % (path, address & ~R_SCATTERED))
    return MachORelocation(address, packed & SYMBOLNUM_MASK, bool(packed >> PCREL_SHIFT & 1),
                           1 << (packed >> LENGTH_SHIFT & LENGTH_MASK), bool(packed >> EXTERN_SHIFT & 1),
                           packed >> TYPE_SHIFT & TYPE_MASK)


def read_symbols(data, at):
    """{index: symbol} of the LC_SYMTAB command at `at`, stab entries left out."""
    _, _, symbols_at, count, strings_at, strings_size = struct.unpack_from(SYMTAB_FIELDS, data, at)
    strings = data[strings_at:strings_at + strings_size]
    if len(strings) != strings_size:
        raise ValueError("the string table runs past the end of the file")
    symbols = {}
    for index in range(count):
        name_at, kind, section, _, value = struct.unpack_from(NLIST_FIELDS, data, symbols_at + index * NLIST_SIZE)
        if kind & N_STAB:
            continue
        name = strings[name_at:strings.index(b"\0", name_at)].decode()
        symbols[index] = MachOSymbol(name, kind & N_TYPE, bool(kind & N_EXT), section, value)
    return symbols


# --------------------------------------------------------------------------
# Self-test: small objects made by clang

CLANG_TARGET = "x86_64-apple-macos11"
SELF_TEST_SOURCE = """
	.section __DATA,__const
	.p2align 3
	.globl _head
_head:
	.quad _target + 4
	.long _local - 2
_local:
	.byte 1, 2
	.section __DATA,__script_data
	.byte 3
	.long Ltemporary
Ltemporary:
	.long . - _head
	.section __DATA,__bss,zerofill
	.p2align 4
_buffer:
	.space 16
	.set _VALUE, 5
	.globl _GLOBAL_VALUE
	.set _GLOBAL_VALUE, -1
"""


def expect(failures, name, actual, wanted):
    if actual != wanted:
        failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))


def assemble(directory, source):
    path = os.path.join(directory, "a.o")
    subprocess.run(["clang", "-target", CLANG_TARGET, "-c", "-x", "assembler", "-o", path, "-"],
                   input=source, universal_newlines=True, check=True)
    return path


def self_test_parse(failures, directory):
    path = assemble(directory, SELF_TEST_SOURCE)
    obj = read_macho64(path)
    named = {(section.segment, section.name): section for section in obj.sections}
    const, script, bss = named["__DATA", "__const"], named["__DATA", "__script_data"], named["__DATA", "__bss"]
    ordinal = {key: index + 1 for index, key in enumerate((s.segment, s.name) for s in obj.sections)}
    expect(failures, "section fields", [(s.size, s.alignment, s.type) for s in (const, script, bss)],
           [(14, 8, S_REGULAR), (9, 1, S_REGULAR), (16, 16, S_ZEROFILL)])
    expect(failures, "contents (an addend is in place)", const.contents,
           b"\x04\0\0\0\0\0\0\0\xfe\xff\xff\xff\x01\x02")
    expect(failures, "a zero-fill section has no bytes", bss.contents, b"")
    expect(failures, "addresses follow each other", (const.address, script.address - const.address), (0, 14))
    names = {index: symbol.name for index, symbol in obj.symbols.items()}
    expect(failures, "symbols",
           sorted((s.name, s.kind, s.external, s.section, s.value) for s in obj.symbols.values()),
           sorted([("_head", N_SECT, True, ordinal["__DATA", "__const"], 0),
                   ("_local", N_SECT, False, ordinal["__DATA", "__const"], 12),
                   ("_buffer", N_SECT, False, ordinal["__DATA", "__bss"], bss.address),
                   ("_VALUE", N_ABS, False, 0, 5), ("_GLOBAL_VALUE", N_ABS, True, 0, (1 << 64) - 1),
                   ("_target", N_UNDF, True, 0, 0)]))
    def target(relocation):
        return names[relocation.symbol] if relocation.extern else "section %d" % relocation.symbol

    expect(failures, "relocations (in file order): extern, symbol index, length in bytes",
           [(r.offset, target(r), r.pcrel, r.length, r.type) for r in const.relocations],
           [(8, "_local", False, 4, X86_64_RELOC_UNSIGNED), (0, "_target", False, 8, X86_64_RELOC_UNSIGNED)])
    expect(failures, "a temporary label's relocation names its section; a difference is two relocations",
           [(r.offset, target(r), r.type) for r in script.relocations],
           [(5, "_head", X86_64_RELOC_SUBTRACTOR), (5, "section %d" % ordinal["__DATA", "__script_data"],
                                                    X86_64_RELOC_UNSIGNED),
            (1, "section %d" % ordinal["__DATA", "__script_data"], X86_64_RELOC_UNSIGNED)])
    expect(failures, "a non-extern relocation holds the address in place", script.contents[1:5],
           struct.pack("<I", script.address + 5))
    return path


def self_test_refusals(failures, directory, good_path):
    with open(good_path, "rb") as handle:
        good = handle.read()
    other_cpu = good[:4] + struct.pack("<I", 0x0100000C) + good[8:]   # CPU_TYPE_ARM64
    executable = good[:12] + struct.pack("<I", 2) + good[16:]         # MH_EXECUTE
    scattered = bytearray(good)
    obj = read_macho64(good_path)
    first = next(section for section in obj.sections if section.relocations)
    relocations_at = struct.unpack_from(SECTION_FIELDS, good, HEADER_SIZE + SEGMENT_SIZE
                                        + obj.sections.index(first) * SECTION_SIZE)[6]
    struct.pack_into("<I", scattered, relocations_at, R_SCATTERED | 4)
    for name, data, wanted in (
            ("not Mach-O", b"hello" * 20, "expected a 64-bit x86-64 Mach-O object"),
            ("ELF", b"\x7fELF" + good[4:], "expected a 64-bit x86-64 Mach-O object"),
            ("arm64", other_cpu, "expected a 64-bit x86-64 Mach-O object"),
            ("not an object", executable, "expected a 64-bit x86-64 Mach-O object"),
            ("scattered relocation", bytes(scattered), "a scattered relocation at 0x4"),
            ("truncated header", good[:20], "truncated or malformed"),
            ("truncated sections", good[:400], "truncated or malformed"),
            ("truncated symbols", good[:len(good) - 60], "truncated or malformed")):
        path = os.path.join(directory, "bad.o")
        with open(path, "wb") as handle:
            handle.write(data)
        try:
            read_macho64(path)
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
        sys.stderr.write("macho_object self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
