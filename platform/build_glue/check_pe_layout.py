"""Check that a linked Windows executable has the layout the GBA memory model needs.

Stands in for nothing on the GBA: it is build glue. The Windows build cannot
be run where it is made (no Windows, and Wine is not part of the gate), so the
link's result is checked from the file: usage

    python3 -I check_pe_layout.py --gui pokeemerald-native.exe

reads the PE headers and exits 1, printing one line per problem, unless:

  * the subsystem is what the flag says: --gui for the game (Windows GUI, so
    that Explorer opens no console window beside it), --console for the unit
    tests, whose output the runner shows;
  * the image is a PE32+ x86-64 executable at the base gba_regions.pe.ld
    assumes (0x01800000), with no base relocations, so that the loader maps
    it there or refuses it: no DYNAMIC_BASE and no HIGH_ENTROPY_VA in the
    DllCharacteristics, the relocations-stripped flag set, an empty base
    relocation directory and no .reloc section;
  * its sections are in ascending order and adjacent, each at a multiple of
    the section alignment, which the loader requires (research/probes/README.md,
    probe W1: "as_linked" was refused, "filled_gaps" started 100 times in 100);
  * every GBA region, and the battle runner's stack, is a section at its GBA
    address, of exactly its size, writable, and the gaps between them are
    read-only (a writable gap would be charged to the process as memory);
  * .text is at 0x40001000;
  * above it, the sections the loader keeps (not the discardable debug
    sections) are read-only first and writable after (gba_game.pe.ld moves
    the initialised data there), so that the program's mutable data is one
    stretch, which host_windows.c's host_image_writable gives;
  * its resources hold the application manifest (type RT_MANIFEST, id 1) and
    the manifest sets the active code page to UTF-8 (build_glue/
    windows_manifest.xml, linked through target.mk), without which the
    narrow strings the C runtime gives and takes are not UTF-8.

The addresses and sizes are those of gba_regions.pe.ld (GBATEK "GBA Memory
Map"), written out again on purpose: this checks the link, not the script
against itself. 'python3 -I check_pe_layout.py --self-test' runs the checks on
headers it builds.
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, problems_match   # noqa: E402

IMAGE_BASE = 0x01800000
CODE_ADDRESS = 0x40001000
MACHINE_AMD64 = 0x8664
PE32_PLUS_MAGIC = 0x20B
# Microsoft PE format, "COFF File Header (Object and Image)" and "DLL Characteristics".
IMAGE_SUBSYSTEM_WINDOWS_GUI = 2
IMAGE_SUBSYSTEM_WINDOWS_CUI = 3
IMAGE_FILE_RELOCS_STRIPPED = 0x0001
IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA = 0x0020
IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE = 0x0040
# "Section Flags".
IMAGE_SCN_MEM_DISCARDABLE = 0x02000000
IMAGE_SCN_MEM_WRITE = 0x80000000
BASE_RELOCATION_DIRECTORY = 5
MAX_PROBLEMS = 20
# "The .rsrc Section": the resource directory, its entries (an id, and the
# offset from the directory's start of a subdirectory, high bit set, or of a
# data entry: the data's RVA and size), three levels deep (type, name,
# language). The manifest the loader reads at process creation is type
# RT_MANIFEST, id CREATEPROCESS_MANIFEST_RESOURCE_ID (winuser.h).
RESOURCE_DIRECTORY = 2
RESOURCE_DIRECTORY_HEADER = 16
RESOURCE_ENTRY_SIZE = 8
RESOURCE_SUBDIRECTORY = 0x80000000
RT_MANIFEST = 24
CREATEPROCESS_MANIFEST_RESOURCE_ID = 1
# Microsoft, "Use UTF-8 code pages in Windows apps": the element and its namespace.
UTF8_CODE_PAGE = re.compile(rb'<activeCodePage\s+xmlns="http://schemas\.microsoft\.com/SMI/2019/WindowsSettings"\s*>'
                            rb'\s*UTF-8\s*</activeCodePage>')

# (what, address, size): gba_regions.pe.ld.
REGIONS = (
    ("the battle runner's stack", 0x01801000, 0x100000),
    ("EWRAM", 0x02000000, 0x40000),
    ("IWRAM", 0x03000000, 0x8000),
    ("I/O", 0x04000000, 0x1000),
    ("mGBA debug", 0x04FFF000, 0x1000),
    ("palette RAM", 0x05000000, 0x1000),
    ("VRAM", 0x06000000, 0x18000),
    ("OAM", 0x07000000, 0x1000),
    ("SRAM", 0x0E000000, 0x20000),
)

# Offsets in the headers (Microsoft PE format): the DOS header's pointer to the PE
# signature; the optional header's fields (PE32+: "Optional Header Windows-Specific
# Fields (Image Only)" and "Optional Header Data Directories").
DOS_POINTER_OFFSET = 0x3C
FILE_HEADER_SIZE = 20
OPTIONAL_IMAGE_BASE = 24
OPTIONAL_SECTION_ALIGNMENT = 32
OPTIONAL_SUBSYSTEM = 68
OPTIONAL_DLL_CHARACTERISTICS = 70
OPTIONAL_DIRECTORY_COUNT = 108
OPTIONAL_DIRECTORIES = 112
# name, virtual size, rva, raw size, raw pointer, relocations, line numbers, their counts, flags
SECTION_HEADER = struct.Struct("<8sIIIIIIHHI")
SECTION_HEADER_SIZE = SECTION_HEADER.size
DATA_DIRECTORY_SIZE = 8


class Section:
    def __init__(self, name, address, size, flags, raw_size=0, raw_pointer=0):
        self.name, self.address, self.size, self.flags, self.raw_size = name, address, size, flags, raw_size
        self.raw_pointer = raw_pointer

    @property
    def writable(self):
        return bool(self.flags & IMAGE_SCN_MEM_WRITE)

    @property
    def discardable(self):
        return bool(self.flags & IMAGE_SCN_MEM_DISCARDABLE)

    @property
    def stored(self):
        return self.raw_size != 0


class Image:
    def __init__(self, base, section_alignment, characteristics, dll_characteristics, relocation_directory_size,
                 sections, manifest=None, subsystem=IMAGE_SUBSYSTEM_WINDOWS_GUI):
        self.subsystem = subsystem
        self.base, self.section_alignment = base, section_alignment
        self.characteristics, self.dll_characteristics = characteristics, dll_characteristics
        self.relocation_directory_size, self.sections = relocation_directory_size, sections
        self.manifest = manifest   # the application manifest's bytes, or None


def read_manifest(data, base, sections, directory_rva):
    """The bytes of the application manifest in the resources at directory_rva (0: none), or None."""
    def offset_of(rva, size=1):
        """The file offset of the size bytes at rva, which must lie in one section's stored bytes."""
        for section in sections:
            start = section.address - base
            if start <= rva and rva + size <= start + section.raw_size:
                return section.raw_pointer + rva - start
        raise CheckError("the resources' %#x bytes at RVA %#x are not in the file" % (size, rva))

    if directory_rva == 0:
        return None
    root = offset_of(directory_rva)

    def target(directory, wanted):
        """The target of directory's entry with id wanted (None: its first entry), or None."""
        named, ids = struct.unpack_from("<HH", data, directory + 12)
        for index in range(named + ids):
            name, entry_target = struct.unpack_from("<II", data,
                                                    directory + RESOURCE_DIRECTORY_HEADER + RESOURCE_ENTRY_SIZE * index)
            if wanted is None or name == wanted:
                return entry_target
        return None

    directory = root
    for wanted in (RT_MANIFEST, CREATEPROCESS_MANIFEST_RESOURCE_ID, None):
        found = target(directory, wanted)
        if found is None:
            return None
        if wanted is not None and not found & RESOURCE_SUBDIRECTORY:
            raise CheckError("the resources' directory is malformed")
        directory = root + (found & ~RESOURCE_SUBDIRECTORY)
    if found & RESOURCE_SUBDIRECTORY:
        raise CheckError("the resources' directory is malformed")
    data_rva, size = struct.unpack_from("<II", data, directory)
    start = offset_of(data_rva, size)
    if start + size > len(data):
        raise CheckError("the manifest runs past the end of the file")
    return bytes(data[start:start + size])


def read_image(data):
    """The Image the PE headers in data describe."""
    try:
        pe = struct.unpack_from("<I", data, DOS_POINTER_OFFSET)[0]
        if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
            raise CheckError("not a PE file")
        machine, section_count, _, _, _, optional_size, characteristics = struct.unpack_from("<HHIIIHH", data, pe + 4)
        optional = pe + 4 + FILE_HEADER_SIZE
        if machine != MACHINE_AMD64 or struct.unpack_from("<H", data, optional)[0] != PE32_PLUS_MAGIC:
            raise CheckError("not a PE32+ x86-64 file")
        base = struct.unpack_from("<Q", data, optional + OPTIONAL_IMAGE_BASE)[0]
        section_alignment = struct.unpack_from("<I", data, optional + OPTIONAL_SECTION_ALIGNMENT)[0]
        subsystem = struct.unpack_from("<H", data, optional + OPTIONAL_SUBSYSTEM)[0]
        dll_characteristics = struct.unpack_from("<H", data, optional + OPTIONAL_DLL_CHARACTERISTICS)[0]
        directory_count = struct.unpack_from("<I", data, optional + OPTIONAL_DIRECTORY_COUNT)[0]
        directory = optional + OPTIONAL_DIRECTORIES
        relocation_size = 0
        if directory_count > BASE_RELOCATION_DIRECTORY:
            relocation_entry = directory + DATA_DIRECTORY_SIZE * BASE_RELOCATION_DIRECTORY
            relocation_size = struct.unpack_from("<II", data, relocation_entry)[1]
        resource_rva = 0
        if directory_count > RESOURCE_DIRECTORY:
            resource_rva = struct.unpack_from("<I", data, directory + DATA_DIRECTORY_SIZE * RESOURCE_DIRECTORY)[0]
        sections = []
        for index in range(section_count):
            fields = SECTION_HEADER.unpack_from(data, optional + optional_size + SECTION_HEADER_SIZE * index)
            sections.append(Section(fields[0].rstrip(b"\0").decode("ascii", "replace"), base + fields[2], fields[1],
                                    fields[9], fields[3], fields[4]))
        manifest = read_manifest(data, base, sections, resource_rva)
        return Image(base, section_alignment, characteristics, dll_characteristics, relocation_size, sections,
                     manifest, subsystem)
    except struct.error:
        raise CheckError("the PE headers are cut short")


def round_up(value, alignment):
    return (value + alignment - 1) // alignment * alignment


SUBSYSTEMS = {"--gui": (IMAGE_SUBSYSTEM_WINDOWS_GUI, "Windows GUI"),
              "--console": (IMAGE_SUBSYSTEM_WINDOWS_CUI, "Windows console")}


def check_image(image, subsystem_flag):
    """The problems of image, one line each; subsystem_flag is --gui or --console."""
    problems = []
    wanted, wanted_name = SUBSYSTEMS[subsystem_flag]
    if image.subsystem != wanted:
        problems.append("the subsystem is %d, not %d (%s)" % (image.subsystem, wanted, wanted_name))
    if image.base != IMAGE_BASE:
        problems.append("the image base is %#x, not %#x" % (image.base, IMAGE_BASE))
    if image.dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE:
        problems.append("DYNAMIC_BASE is set: the loader may move the image")
    if image.dll_characteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA:
        problems.append("HIGH_ENTROPY_VA is set")
    if not image.characteristics & IMAGE_FILE_RELOCS_STRIPPED:
        problems.append("the relocations-stripped flag is not set")
    if image.relocation_directory_size != 0:
        problems.append("the base relocation directory is not empty")
    if any(section.name == ".reloc" for section in image.sections):
        problems.append("there is a .reloc section")
    expected = image.base + image.section_alignment
    for section in image.sections:
        if section.address != expected:
            problems.append("%s is at %#x, not %#x where the previous section ends" % (section.name, section.address,
                                                                                       expected))
        expected = section.address + round_up(section.size, image.section_alignment)
    by_address = {section.address: section for section in image.sections}
    for what, address, size in REGIONS:
        section = by_address.get(address)
        if section is None:
            problems.append("%s: no section at %#x" % (what, address))
        elif section.size != size:
            problems.append("%s: %s is %#x bytes, not %#x" % (what, section.name, section.size, size))
        elif not section.writable:
            problems.append("%s: %s is not writable" % (what, section.name))
    region_addresses = {address for _, address, _ in REGIONS}
    for section in image.sections:
        if section.address < CODE_ADDRESS and section.address not in region_addresses:
            if section.writable:
                problems.append("%s, between the regions, is writable" % section.name)
            if section.stored:
                problems.append("%s, between the regions, is stored in the file (%#x bytes)" % (section.name,
                                                                                                 section.raw_size))
    code = by_address.get(CODE_ADDRESS)
    if code is None or code.name != ".text":
        problems.append(".text is not at %#x" % CODE_ADDRESS)
    first_writable = None
    for section in image.sections:
        if section.address < CODE_ADDRESS or section.discardable:
            continue
        if section.writable and first_writable is None:
            first_writable = section
        elif not section.writable and first_writable is not None:
            problems.append("%s is read-only but follows the writable %s: the writable sections must come last"
                            % (section.name, first_writable.name))
    if image.manifest is None:
        problems.append("there is no application manifest (resource type 24, id 1): target.mk links "
                        "windows_manifest.o into every executable")
    elif not UTF8_CODE_PAGE.search(image.manifest):
        problems.append("the application manifest does not set the active code page to UTF-8")
    return problems


def self_test():
    failures = []

    def expect(name, actual, wanted):
        if not problems_match(wanted, actual):
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))

    # Section flags written out from the specification, so a wrong constant above fails here:
    # uninitialised data (0x80) readable (0x40000000), and writable (0x80000000) for a region.
    # Initialised data is 0x40, and a debug section discardable (0x02000000).
    READ_ONLY_UNINITIALISED = 0x00000080 | 0x40000000
    READ_WRITE_UNINITIALISED = 0x00000080 | 0x40000000 | 0x80000000
    READ_ONLY_INITIALISED = 0x00000040 | 0x40000000
    READ_WRITE_INITIALISED = 0x00000040 | 0x40000000 | 0x80000000

    def layout():
        sections = []
        address = IMAGE_BASE + 0x1000
        for what, region_address, size in REGIONS:
            if region_address != address:
                sections.append(Section(".gap", address, region_address - address, READ_ONLY_UNINITIALISED))
            sections.append(Section(".gba", region_address, size, READ_WRITE_UNINITIALISED))
            address = region_address + round_up(size, 0x1000)
        sections.append(Section(".gap", address, CODE_ADDRESS - address, READ_ONLY_UNINITIALISED))
        sections.append(Section(".text", CODE_ADDRESS, 0x1234, 0x60000020))
        sections.append(Section(".rdata", CODE_ADDRESS + 0x2000, 0x10, READ_ONLY_INITIALISED))
        sections.append(Section(".data", CODE_ADDRESS + 0x3000, 0x10, READ_WRITE_INITIALISED))
        sections.append(Section(".bss", CODE_ADDRESS + 0x4000, 0x10, READ_WRITE_UNINITIALISED))
        sections.append(Section(".debug", CODE_ADDRESS + 0x5000, 0x10, READ_ONLY_INITIALISED | 0x02000000))
        return Image(IMAGE_BASE, 0x1000, 0x0001 | 0x0002, 0x0100, 0, sections, GOOD_MANIFEST)

    GOOD_MANIFEST = (b'<assembly><application><windowsSettings><activeCodePage '
                     b'xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">UTF-8</activeCodePage>'
                     b'</windowsSettings></application></assembly>')
    expect("a good layout", check_image(layout(), "--gui"), [])
    expect("a GUI image checked as a console one", check_image(layout(), "--console"), ["the subsystem is 2, not 3"])
    image = layout()
    image.subsystem = 3   # the specification's value for the console subsystem
    expect("a console image checked as a GUI one", check_image(image, "--gui"), ["the subsystem is 3, not 2"])
    expect("a console image", check_image(image, "--console"), [])
    image = layout()
    image.base = 0x400000
    expect("the base", check_image(image, "--gui"), ["image base is 0x400000", "is at 0x1801000, not 0x401000"])
    image = layout()
    image.dll_characteristics |= 0x0040 | 0x0020   # the specification's values, not the constants under test
    expect("randomisation", check_image(image, "--gui"), ["DYNAMIC_BASE", "HIGH_ENTROPY_VA"])
    image = layout()
    image.characteristics = 0x2
    image.relocation_directory_size = 8
    image.sections.append(Section(".reloc", CODE_ADDRESS + 0x6000, 0x10, 0x42000040))
    expect("relocations", check_image(image, "--gui"), ["flag is not set", "directory is not empty", "there is a .reloc"])
    image = layout()
    del image.sections[1]
    expect("a hole", check_image(image, "--gui"), ["is at 0x2000000, not 0x1901000"])
    image = layout()
    image.sections[2].size += 0x1000
    expect("a region of the wrong size", check_image(image, "--gui"),
           ["is at 0x2040000, not 0x2041000", "EWRAM: .gba is 0x41000 bytes"])
    image = layout()
    image.sections[1].flags = READ_WRITE_UNINITIALISED
    expect("a writable gap", check_image(image, "--gui"), [".gap, between the regions, is writable"])
    image = layout()
    image.sections[1].raw_size = 0x1000
    expect("a gap stored in the file", check_image(image, "--gui"),
           [".gap, between the regions, is stored in the file (0x1000 bytes)"])
    image = layout()
    image.sections[2].flags = READ_ONLY_UNINITIALISED
    expect("a read-only region", check_image(image, "--gui"), ["EWRAM: .gba is not writable"])
    image = layout()
    image.sections[-5].name = ".code"
    expect("the code", check_image(image, "--gui"), [".text is not at"])
    image = layout()
    image.sections[-4].flags, image.sections[-3].flags = image.sections[-3].flags, image.sections[-4].flags
    expect("data before read-only data", check_image(image, "--gui"),
           [".data is read-only but follows the writable .rdata"])
    image = layout()
    image.sections[-1].flags &= ~0x02000000
    expect("a kept read-only section after the data", check_image(image, "--gui"),
           [".debug is read-only but follows the writable .data"])
    image = layout()
    image.manifest = None
    expect("no manifest", check_image(image, "--gui"), ["there is no application manifest"])
    image = layout()
    image.manifest = GOOD_MANIFEST.replace(b"UTF-8", b"Legacy")
    expect("a manifest of another code page", check_image(image, "--gui"), ["does not set the active code page to UTF-8"])

    # A resource tree as windres writes one, at RVA 0x1000 of a section stored at file offset 0x200:
    # the root (0x00, room for two entries), type 24 -> the names (0x20), id 1 -> the languages
    # (0x38), 0x409 -> the data entry (0x50) -> the manifest's bytes (0x60). The root may
    # start with a named entry (the high bit set in its name: the offset of a string), as a
    # resource of a named type would; it never matches an id.
    def tree(type_id, name_id, named_first=False, name_target=0x80000038, manifest_size=None):
        def directory(entries):
            named = sum(1 for entry_id, _ in entries if entry_id & 0x80000000)
            return (struct.pack("<IIHHHH", 0, 0, 0, 0, named, len(entries) - named)
                    + b"".join(struct.pack("<II", entry_id, entry_target) for entry_id, entry_target in entries))
        root_entries = [(type_id, 0x80000020)]
        if named_first:
            # The name's offset, 0x18, has the manifest type's id in its low bits: a walk that
            # ignored the high bit would take this entry for type 24.
            root_entries.insert(0, (0x80000018, 0x80000020))
        root = directory(root_entries).ljust(0x20, b"\0")
        size = len(GOOD_MANIFEST) if manifest_size is None else manifest_size
        body = (root + directory([(name_id, name_target)]) + directory([(0x409, 0x50)])
                + struct.pack("<IIII", 0x1000 + 0x60, size, 0, 0) + GOOD_MANIFEST)
        return bytes(0x200) + body

    sections = [Section(".rsrc", IMAGE_BASE + 0x1000, 0x200, READ_ONLY_INITIALISED, 0x200, 0x200)]
    for name, data, rva, wanted in (("a manifest read", tree(24, 1), 0x1000, GOOD_MANIFEST),
                                    ("a manifest after a named entry", tree(24, 1, named_first=True), 0x1000,
                                     GOOD_MANIFEST),
                                    ("a resource of another type", tree(16, 1), 0x1000, None),
                                    ("only a named type", tree(16, 1, named_first=True), 0x1000, None),
                                    ("a manifest of another id", tree(24, 2), 0x1000, None),
                                    ("no resources", b"", 0, None)):
        found = read_manifest(data, IMAGE_BASE, sections, rva)
        if found != wanted:
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, found))
    for name, data in (("a data entry where a directory belongs", tree(24, 1, name_target=0x50)),
                       ("a manifest that runs past its section", tree(24, 1, manifest_size=0x400))):
        try:
            read_manifest(data, IMAGE_BASE, sections, 0x1000)
            failures.append("read_manifest accepted %s" % name)
        except CheckError:
            pass
    data = bytearray(1024)
    for message, damaged in (("a short file", b"MZ"), ("not a PE file", bytes(data))):
        try:
            read_image(damaged)
            failures.append("read_image accepted %s" % message)
        except CheckError:
            pass
    for failure in failures:
        sys.stderr.write("check_pe_layout self-test: %s\n" % failure)
    return 1 if failures else 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 2 or argv[0] not in SUBSYSTEMS:
        sys.stderr.write(__doc__.split("\n\n")[0] + "\nusage: check_pe_layout.py --gui|--console EXE | --self-test\n")
        return 2
    flag, path = argv
    try:
        with open(path, "rb") as handle:
            problems = check_image(read_image(handle.read()), flag)
    except (OSError, CheckError) as error:
        sys.stderr.write("check_pe_layout: %s: %s\n" % (path, error))
        return 1
    for problem in problems[:MAX_PROBLEMS]:
        sys.stderr.write("check_pe_layout: %s: %s\n" % (path, problem))
    if len(problems) > MAX_PROBLEMS:
        sys.stderr.write("check_pe_layout: %s: %d more problems\n" % (path, len(problems) - MAX_PROBLEMS))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
