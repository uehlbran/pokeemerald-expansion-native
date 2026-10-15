"""Check that a linked macOS executable has the layout the GBA memory model needs.

Stands in for nothing on the GBA: it is build glue. On Linux the linker
scripts place the regions and assert what must hold (gba_regions.ld,
gba_game.ld); ld64 has no scripts, so the macOS link places them by its flags
and the order it meets the sections (platform/target.mk,
platform/gba_regions.macho.s), and this reads the result from the file:

    python3 -I check_macho_layout.py pokeemerald-native

reads the Mach-O load commands and exits 1, printing one line per problem,
unless:

  * the file is a 64-bit x86-64 executable (MH_EXECUTE), not position
    independent (no MH_PIE) and without chained fixups (no
    LC_DYLD_CHAINED_FIXUPS), with which ld64 ignores the image base
    (research/probes/README.md, probe M0);
  * __PAGEZERO is one 4 KiB page, so that the GBA's addresses are free, and
    __TEXT starts at 0x40000000; every section that holds instructions
    (S_ATTR_PURE_INSTRUCTIONS or S_ATTR_SOME_INSTRUCTIONS: __text, __stubs)
    ends below 0x42000000, so that bit 25 of every code address is clear for
    platform/script_tag.h's tag (gba_regions.ld asserts the same of etext).
    The read-only data after the code in __TEXT may reach past it, as
    .rodata does on Linux;
  * no section of a writable segment of the image (__DATA and any other
    above 0x40000000 mapped writable) lies where an address's bits 24-27 read
    as EWRAM's, 0x42000000-0x42FFFFFF and the like (check_support.ewram_alias):
    src/librfu_rfu.c would take a buffer there for EWRAM. The sanitized
    builds' __TEXT passes 0x42000000, so target.mk puts their __DATA at
    0x43000000;
  * every GBA region, and the battle runner's stack, is a segment at its GBA
    address, readable and writable, of the region's size in whole pages;
    EWRAM's marker section (__ewram_end, gba_regions_end.macho.s) starts at
    EWRAM's end, 0x02040000, after EWRAM_INIT's and EWRAM_DATA's sections and
    the byte that keeps EWRAM from being empty, so that they fit EWRAM
    (gba_regions.ld's ASSERT); IWRAM's 0x7F00 bytes come
    first and the test runner's persistent state, if linked, at 0x03007F00;
  * in __DATA the game's zero-initialised data (__game_bss) comes right
    before COMMON_DATA's (__game_common), nothing between them, so that
    game_bss_start to game_bss_end holds both and keeps the GBA's order
    (gba_game.ld says why);
  * in __DATA no writable data starts below __data: host_macos.c's
    host_image_writable is __data's start to the segment's end, so the
    game's __game_data, __game_bss and __game_common, and the program's
    __bss and __common, must lie past __data's start (ld64 orders the
    names it knows by its own table, and probe M2 saw only the order among
    the branch's own sections).

The addresses and sizes are GBATEK's ("GBA Memory Map"), written out again on
purpose: this checks the link, not gba_regions.macho.s against itself.
'python3 -I check_macho_layout.py --self-test' runs the checks on headers it
builds.
"""

import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, ewram_alias, problems_match   # noqa: E402
from macho_object import (CPU_TYPE_X86_64, HEADER_FIELDS, HEADER_SIZE, LC_SEGMENT_64, LOAD_COMMAND_FIELDS,   # noqa: E402
                          MH_MAGIC_64, SECTION_FIELDS, SECTION_SIZE, SEGMENT_FIELDS, SEGMENT_SIZE)

# <mach-o/loader.h>.
MH_EXECUTE = 2
MH_PIE = 0x200000
LC_DYLD_CHAINED_FIXUPS = 0x80000034
VM_PROT_READ, VM_PROT_WRITE = 0x1, 0x2
PAGE = 0x1000
PAGEZERO_SIZE = PAGE
IMAGE_BASE = 0x40000000
CODE_LIMIT = 0x42000000            # platform/script_tag.h: bit 25 clear in every code address
S_ATTR_PURE_INSTRUCTIONS, S_ATTR_SOME_INSTRUCTIONS = 0x80000000, 0x400
MAX_PROBLEMS = 20

# (segment, address, size): GBATEK "GBA Memory Map"; I/O, the mGBA debug page,
# palette and OAM are the page that holds them; the battle runner's stack is
# gba_regions.ld's. EWRAM's segment also maps the marker's page past it.
Region = collections.namedtuple("Region", "segment address size")
REGIONS = (
    Region("__GBA_CALLSTK", 0x01800000, 0x100000),
    Region("__GBA_EWRAM", 0x02000000, 0x40000),
    Region("__GBA_IWRAM", 0x03000000, 0x8000),
    Region("__GBA_IO", 0x04000000, PAGE),
    Region("__GBA_DEBUG", 0x04FFF000, PAGE),
    Region("__GBA_PALETTE", 0x05000000, PAGE),
    Region("__GBA_VRAM", 0x06000000, 0x18000),
    Region("__GBA_OAM", 0x07000000, PAGE),
    Region("__GBA_SRAM", 0x0E000000, 0x20000),
)
EWRAM_MARKER = "__ewram_end"
EWRAM_END = 0x02040000
IWRAM_BELOW_PERSISTENT = "__iwram"
IWRAM_PERSISTENT = "__persistent"
PERSISTENT_ADDRESS = 0x03007F00

Segment = collections.namedtuple("Segment", "name address size initprot maxprot sections")
Section = collections.namedtuple("Section", "name address size flags")


def name_of(field):
    return field.rstrip(b"\0").decode()


def read_executable(path, data):
    """(flags, [Segment] in load-command order, [load command numbers]);
    CheckError if the file is not a 64-bit x86-64 Mach-O executable."""
    try:
        magic, cpu, _, file_type, count, _, flags, _ = struct.unpack_from(HEADER_FIELDS, data, 0)
        if magic != MH_MAGIC_64 or cpu != CPU_TYPE_X86_64 or file_type != MH_EXECUTE:
            raise CheckError("%s: not a 64-bit x86-64 Mach-O executable (MH_EXECUTE)" % path)
        segments, commands = [], []
        at = HEADER_SIZE
        for _ in range(count):
            command, size = struct.unpack_from(LOAD_COMMAND_FIELDS, data, at)
            commands.append(command)
            if command == LC_SEGMENT_64:
                segments.append(read_segment(data, at))
            if size <= 0:
                raise ValueError("load command of size %d" % size)
            at += size
        return flags, segments, commands
    except (struct.error, ValueError) as error:
        raise CheckError("%s: truncated or malformed Mach-O executable (%s)" % (path, error))


def read_segment(data, at):
    (_, _, name, address, size, _, _, maxprot, initprot, count, _) = struct.unpack_from(SEGMENT_FIELDS, data, at)
    sections = []
    for index in range(count):
        fields = struct.unpack_from(SECTION_FIELDS, data, at + SEGMENT_SIZE + index * SECTION_SIZE)
        sections.append(Section(name_of(fields[0]), fields[2], fields[3], fields[8]))
    return Segment(name_of(name), address, size, initprot, maxprot, sections)


def check_image(flags, segments, commands):
    problems = []
    if flags & MH_PIE:
        problems.append("the executable is position independent (MH_PIE): link with -no_pie")
    if LC_DYLD_CHAINED_FIXUPS in commands:
        problems.append("the executable has chained fixups, with which ld64 ignores -image_base: link with "
                        "-no_fixup_chains")
    by_name = {segment.name: segment for segment in segments}
    zero = by_name.get("__PAGEZERO")
    if zero is None or zero.address != 0 or zero.size != PAGEZERO_SIZE:
        problems.append("__PAGEZERO is not one page at 0 (-pagezero_size 0x1000): %s" % describe(zero))
    text = by_name.get("__TEXT")
    if text is None or text.address != IMAGE_BASE:
        problems.append("__TEXT is not at 0x%08x (-image_base): %s" % (IMAGE_BASE, describe(text)))
    for segment in segments:
        for section in segment.sections:
            if section.flags & (S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS) \
                    and section.address + section.size > CODE_LIMIT:
                problems.append("%s,%s ends at 0x%x, past 0x%08x, where platform/script_tag.h's tag bit would be "
                                "ambiguous" % (segment.name, section.name, section.address + section.size, CODE_LIMIT))
    for segment in segments:
        if not segment.initprot & VM_PROT_WRITE:
            continue
        for section in segment.sections:
            alias = ewram_alias(section.address, section.address + section.size)
            if alias is not None:
                problems.append("%s,%s is writable and reaches 0x%x, whose bits 24-27 read as EWRAM's: "
                                "src/librfu_rfu.c would take a buffer there for EWRAM" % (segment.name, section.name,
                                                                                          alias))
    return problems


def describe(segment):
    return "missing" if segment is None else "0x%x, 0x%x bytes" % (segment.address, segment.size)


def page_rounded(size):
    return (size + PAGE - 1) // PAGE * PAGE


def check_regions(segments):
    problems = []
    by_name = {segment.name: segment for segment in segments}
    for region in REGIONS:
        segment = by_name.get(region.segment)
        if segment is None:
            problems.append("%s: no such segment (platform/gba_regions.macho.s, target.mk's -segaddr)" % region.segment)
            continue
        size = region.size + PAGE if region.segment == "__GBA_EWRAM" else region.size
        if segment.address != region.address or segment.size != size:
            problems.append("%s is at 0x%x, 0x%x bytes, not at 0x%x, 0x%x bytes"
                            % (region.segment, segment.address, segment.size, region.address, size))
        wanted = VM_PROT_READ | VM_PROT_WRITE
        if segment.initprot & wanted != wanted:
            problems.append("%s is not mapped readable and writable (initprot %d)" % (region.segment, segment.initprot))
    problems.extend(check_ewram(by_name.get("__GBA_EWRAM")))
    problems.extend(check_iwram(by_name.get("__GBA_IWRAM")))
    return problems


def check_ewram(segment):
    if segment is None:
        return []
    names = [section.name for section in segment.sections]
    if EWRAM_MARKER not in names:
        return ["__GBA_EWRAM has no %s section (gba_regions_end.macho.s, linked last)" % EWRAM_MARKER]
    marker = segment.sections[names.index(EWRAM_MARKER)]
    problems = []
    if names.index(EWRAM_MARKER) != len(names) - 1:
        problems.append("__GBA_EWRAM's %s is not its last section (%s): gba_regions_end.o must be linked last"
                        % (EWRAM_MARKER, ", ".join(names)))
    if marker.address != EWRAM_END:
        problems.append("__GBA_EWRAM's %s is at 0x%x, not 0x%x: EWRAM_INIT and EWRAM_DATA are larger than the "
                        "256 KiB of EWRAM" % (EWRAM_MARKER, marker.address, EWRAM_END))
    return problems


def check_iwram(segment):
    if segment is None:
        return []
    names = [section.name for section in segment.sections]
    problems = []
    if not names or names[0] != IWRAM_BELOW_PERSISTENT:
        problems.append("__GBA_IWRAM does not start with %s (%s): gba_regions.o must be linked first"
                        % (IWRAM_BELOW_PERSISTENT, ", ".join(names)))
    for section in segment.sections:
        if section.name == IWRAM_PERSISTENT and section.size and section.address != PERSISTENT_ADDRESS:
            problems.append("__GBA_IWRAM's %s is at 0x%x, not 0x%x" % (IWRAM_PERSISTENT, section.address,
                                                                         PERSISTENT_ADDRESS))
    return problems


# The writable sections of __DATA that host_image_writable's span, __data's
# start to the segment's end, must hold.
WRITABLE_PAST_DATA = ("__game_data", "__game_bss", "__game_common", "__bss", "__common")


def check_game_data(segments):
    data = next((segment for segment in segments if segment.name == "__DATA"), None)
    if data is None:
        return ["no __DATA segment"]
    names = [section.name for section in data.sections]
    if "__game_bss" not in names or "__game_common" not in names:
        return []
    problems = []
    bss = names.index("__game_bss")
    if names.index("__game_common") != bss + 1:
        problems.append("__DATA's __game_common does not follow __game_bss (%s): gba_game.o must be linked first"
                        % ", ".join(names))
    start = next((section.address for section in data.sections if section.name == "__data"), None)
    if start is None:
        problems.append("__DATA has no __data, where host_image_writable's span starts (host_macos.c)")
        return problems
    for section in data.sections:
        if section.name in WRITABLE_PAST_DATA and section.address < start:
            problems.append("__DATA,%s starts at 0x%x, below __data's 0x%x: host_image_writable (host_macos.c) "
                            "would leave it out" % (section.name, section.address, start))
    return problems


def check(path, data):
    flags, segments, commands = read_executable(path, data)
    problems = check_image(flags, segments, commands) + check_regions(segments) + check_game_data(segments)
    return problems[:MAX_PROBLEMS]


# --------------------------------------------------------------------------
# The self-test: executables' headers built here, good and bad.

def pack_segment(name, address, size, sections, initprot=VM_PROT_READ | VM_PROT_WRITE):
    command = struct.pack(SEGMENT_FIELDS, LC_SEGMENT_64, SEGMENT_SIZE + SECTION_SIZE * len(sections),
                          name.encode(), address, size, 0, 0, initprot, initprot, len(sections), 0)
    for section in sections:
        section_name, section_address, section_size = section[:3]
        flags = section[3] if len(section) > 3 else 0
        command += struct.pack(SECTION_FIELDS, section_name.encode(), name.encode(), section_address, section_size,
                               0, 0, 0, 0, flags, 0, 0, 0)
    return command


def pack_executable(segments, flags=0, extra_commands=()):
    commands = [pack_segment(*segment) for segment in segments]
    commands += [struct.pack(LOAD_COMMAND_FIELDS, command, 8) for command in extra_commands]
    body = b"".join(commands)
    return struct.pack(HEADER_FIELDS, MH_MAGIC_64, CPU_TYPE_X86_64, 3, MH_EXECUTE, len(commands), len(body),
                       flags, 0) + body


def good_segments():
    segments = [("__PAGEZERO", 0, PAGEZERO_SIZE, [], 0),
                ("__TEXT", IMAGE_BASE, 0x31C000, [("__text", IMAGE_BASE + 0x1000, 0x300000, S_ATTR_PURE_INSTRUCTIONS),
                                                  ("__const", IMAGE_BASE + 0x301000, 0x1B000, 0)], VM_PROT_READ),
                ("__DATA", IMAGE_BASE + 0x31C000, 0x40000,
                 [("__data", IMAGE_BASE + 0x31C000, 0x10), ("__game_data", IMAGE_BASE + 0x31C010, 0x20),
                  ("__game_bss", IMAGE_BASE + 0x31D000, 0x100), ("__game_common", IMAGE_BASE + 0x31D100, 0x40),
                  ("__bss", IMAGE_BASE + 0x31D140, 0x10)])]
    for region in REGIONS:
        if region.segment == "__GBA_EWRAM":
            sections = [("__ewram_init", region.address, 4), ("__ewram_data", region.address + 0x10, 0x38000),
                        ("__ewram_pad", region.address + 0x38010, 1), (EWRAM_MARKER, EWRAM_END, 1)]
            segments.append((region.segment, region.address, region.size + PAGE, sections))
        elif region.segment == "__GBA_IWRAM":
            sections = [(IWRAM_BELOW_PERSISTENT, region.address, 0x7F00), (IWRAM_PERSISTENT, PERSISTENT_ADDRESS, 0x8)]
            segments.append((region.segment, region.address, region.size, sections))
        else:
            segments.append((region.segment, region.address, region.size, [("__x", region.address, region.size)]))
    return segments


def replaced(segments, name, **changes):
    """segments with the one named name changed: address, size, sections."""
    fields = ("name", "address", "size", "sections")
    result = []
    for segment in segments:
        if segment[0] == name:
            values = dict(zip(fields, segment))
            values.update(changes)
            segment = tuple(values[field] for field in fields) + tuple(segment[4:])
        result.append(segment)
    return result


def self_test():
    failures = []
    good = good_segments()

    def expect(name, data, wanted):
        found = check("self-test", data)
        if not problems_match(wanted, found):
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, found))

    expect("a good layout", pack_executable(good), [])
    expect("PIE", pack_executable(good, flags=MH_PIE), ["MH_PIE"])
    expect("chained fixups", pack_executable(good, extra_commands=(LC_DYLD_CHAINED_FIXUPS,)), ["chained fixups"])
    expect("a 4 GiB page zero", pack_executable(replaced(good, "__PAGEZERO", size=0x100000000)), ["__PAGEZERO"])
    expect("the image elsewhere", pack_executable(replaced(good, "__TEXT", address=0x100000000)), ["__TEXT is not"])
    code_past = [("__text", IMAGE_BASE + 0x1000, 0x2000000, S_ATTR_PURE_INSTRUCTIONS)]
    expect("code past the tag bit", pack_executable(replaced(good, "__TEXT", size=0x2002000, sections=code_past)),
           ["__TEXT,__text ends at 0x42001000, past 0x42000000, where platform/script_tag.h's tag bit"])
    stubs_past = [("__text", IMAGE_BASE + 0x1000, 0x1000, S_ATTR_PURE_INSTRUCTIONS),
                  ("__stubs", CODE_LIMIT - 4, 8, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS)]
    expect("stubs past the tag bit", pack_executable(replaced(good, "__TEXT", size=0x2002000, sections=stubs_past)),
           ["__TEXT,__stubs ends"])
    some_past = [("__text", IMAGE_BASE + 0x1000, 0x1000, S_ATTR_PURE_INSTRUCTIONS),
                 ("__some", CODE_LIMIT - 4, 8, S_ATTR_SOME_INSTRUCTIONS)]
    expect("a section with some instructions past the tag bit",
           pack_executable(replaced(good, "__TEXT", size=0x2002000, sections=some_past)), ["__TEXT,__some ends"])
    data_past = [("__text", IMAGE_BASE + 0x1000, 0x1000000, S_ATTR_PURE_INSTRUCTIONS),
                 ("__const", IMAGE_BASE + 0x1001000, 0x2000000, 0)]
    expect("read-only data past it, as .rodata on Linux",
           pack_executable(replaced(good, "__TEXT", size=0x3001000, sections=data_past)), [])
    sanitized_text = replaced(good, "__TEXT", size=0x2F00000, sections=data_past[:1] + [
        ("__const", IMAGE_BASE + 0x1001000, 0x1EFF000, 0)])
    data_at = lambda address, sections: replaced(sanitized_text, "__DATA", address=address, sections=sections)
    expect("the sanitized layout: __DATA from 0x43000000",
           pack_executable(data_at(0x43000000, [("__data", 0x43000000, 0x10), ("__game_bss", 0x43001000, 0x100),
                                                ("__game_common", 0x43001100, 0x40)])), [])
    expect("__DATA after a __TEXT that passes 0x42000000",
           pack_executable(data_at(0x42F00000, [("__data", 0x42F00000, 0x10), ("__game_bss", 0x42F01000, 0x100),
                                                ("__game_common", 0x42F01100, 0x40)])),
           ["__DATA,__data is writable and reaches 0x42f00000, whose bits 24-27 read as EWRAM's",
            "__DATA,__game_bss is writable and reaches 0x42f01000", "__DATA,__game_common is writable and reaches"])
    expect("zero-filled data reaching 0x42000000",
           pack_executable(replaced(good, "__DATA", sections=[("__data", IMAGE_BASE + 0x31C000, 0x10),
                                                              ("__game_bss", IMAGE_BASE + 0x31D000, 0x1CE3001),
                                                              ("__game_common", 0x42000001, 0x40)])),
           ["__DATA,__game_bss is writable and reaches 0x42000000", "__DATA,__game_common is writable"])
    expect("the next window", pack_executable(data_at(0x43000000, [("__data", 0x43000000, 0x10),
                                                                   ("__game_bss", 0x43001000, 0x0EFFF001),
                                                                   ("__game_common", 0x52000001, 0x40)])),
           ["__DATA,__game_bss is writable and reaches 0x52000000", "__DATA,__game_common is writable"])
    read_only_data = [segment if segment[0] != "__DATA" else
                      ("__DATA", 0x42000000, 0x2000, [("__data", 0x42000000, 0x10), ("__game_bss", 0x42001000, 0x100),
                                                      ("__game_common", 0x42001100, 0x40)], VM_PROT_READ)
                      for segment in good]
    expect("a read-only segment there is no matter", pack_executable(read_only_data), [])
    expect("a region moved", pack_executable(replaced(good, "__GBA_VRAM", address=0x06100000)), ["__GBA_VRAM is at"])
    expect("a region missing", pack_executable([segment for segment in good if segment[0] != "__GBA_OAM"]),
           ["__GBA_OAM: no such segment"])
    ewram = next(segment for segment in good if segment[0] == "__GBA_EWRAM")
    overflowing = [("__ewram_init", 0x02000000, 4), ("__ewram_data", 0x02000010, 0x40000), (EWRAM_MARKER, 0x02080000, 1)]
    expect("EWRAM_DATA past EWRAM", pack_executable(replaced(good, "__GBA_EWRAM", size=0x81000, sections=overflowing)),
           ["__GBA_EWRAM is at", "EWRAM_INIT and EWRAM_DATA are larger"])
    marker_first = [ewram[3][3], ewram[3][0], ewram[3][1], ewram[3][2]]
    expect("the marker first", pack_executable(replaced(good, "__GBA_EWRAM", sections=marker_first)),
           ["is not its last section"])
    expect("no marker", pack_executable(replaced(good, "__GBA_EWRAM", sections=ewram[3][:3])), ["no __ewram_end"])
    nothing_but_the_pad = [("__ewram_init", 0x02000000, 0), ("__ewram_data", 0x02000000, 0),
                           ("__ewram_pad", 0x02000000, 1), (EWRAM_MARKER, EWRAM_END, 1)]
    expect("no EWRAM variable at all", pack_executable(replaced(good, "__GBA_EWRAM", sections=nothing_but_the_pad)),
           [])
    persistent_first = [(IWRAM_PERSISTENT, 0x03000000, 0x8), (IWRAM_BELOW_PERSISTENT, 0x03001000, 0x7F00)]
    expect("the persistent state first", pack_executable(replaced(good, "__GBA_IWRAM", sections=persistent_first)),
           ["does not start with __iwram", "__persistent is at 0x3000000"])
    data = next(segment for segment in good if segment[0] == "__DATA")
    apart = [data[3][0], data[3][1], data[3][2], data[3][4], data[3][3]]
    expect("COMMON_DATA apart from the game's bss", pack_executable(replaced(good, "__DATA", sections=apart)),
           ["__game_common does not follow __game_bss"])
    game_data_first = [("__game_data", IMAGE_BASE + 0x31C000, 0x20), ("__data", IMAGE_BASE + 0x31C020, 0x10)] \
        + data[3][2:]
    expect("the game's initialised data before __data",
           pack_executable(replaced(good, "__DATA", sections=game_data_first)),
           ["__DATA,__game_data starts at 0x4031c000, below __data's 0x4031c020"])
    bss_first = [("__bss", IMAGE_BASE + 0x31C000, 0x10), ("__common", IMAGE_BASE + 0x31C010, 0x10)] \
        + [(name, address + 0x20, size) for name, address, size in data[3][:4]]
    expect("the program's zero-filled data before __data",
           pack_executable(replaced(good, "__DATA", sections=bss_first)),
           ["__DATA,__bss starts at 0x4031c000, below __data's", "__DATA,__common starts at 0x4031c010"])
    no_data = data[3][1:]
    expect("no __data", pack_executable(replaced(good, "__DATA", sections=no_data)),
           ["__DATA has no __data"])
    expect("no __DATA", pack_executable([segment for segment in good if segment[0] != "__DATA"]), ["no __DATA"])
    try:
        check("self-test", b"\x7fELF" + bytes(60))
        failures.append("an ELF file was accepted")
    except CheckError:
        pass
    for failure in failures:
        sys.stderr.write("check_macho_layout self-test: %s\n" % failure)
    return 1 if failures else 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) != 2:
        sys.stderr.write("usage: check_macho_layout.py EXECUTABLE | --self-test\n")
        return 2
    try:
        with open(argv[1], "rb") as handle:
            problems = check(argv[1], handle.read())
    except (CheckError, OSError) as error:
        sys.stderr.write("check_macho_layout: %s\n" % error)
        return 1
    for problem in problems:
        sys.stderr.write("check_macho_layout: %s: %s\n" % (argv[1], problem))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
