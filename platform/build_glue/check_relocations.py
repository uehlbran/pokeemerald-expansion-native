"""Check the address words of the 64-bit build's assembled data.

Stands in for nothing on the GBA: it is build glue. On the GBA, and in the
32-bit native build, every address the data holds is a 4-byte word. At 64
bits the C code reads some of those words as struct or array members whose
pointers are 8 bytes (struct MapHeader, struct ToneData, the script command
tables, ...); the assembler macros and asm_filter.py's --pointer-size 8
rules widen those, and must leave every other address word (script and
song bytecode, read a byte at a time with T1_READ_PTR, ScriptReadWord and
the like) at 4 bytes. This script checks the result against the list
data_address_words.txt, whose header explains the classes.

Each address word in an object is a relocation: R_X86_64_64 for an 8-byte
word, R_X86_64_32 for a 4-byte one (the System V x86-64 psABI,
"Relocation Types"). Each relocation belongs to the symbol at or before its
offset in its section (the label of the table or script that holds it),
and the list classifies that symbol. The check fails unless:

  * every symbol that holds an address word is classified by the list, and
    every rule in the list names at least one symbol (a renamed table is
    caught rather than falling through to a catch-all);
  * a "pointer" symbol holds only R_X86_64_64, each at an 8-byte aligned
    offset in a section aligned to 8 (gcc copies such structs with aligned
    SSE moves, and UBSan checks the alignment of every load);
  * a "packed" symbol holds only R_X86_64_64 (struct ObjectEventTemplate is
    packed, aligned(4): its script pointer may sit at offset 4 mod 8);
  * an "operands" symbol (battle script bytecode, which
    src/battle_script_commands.c reads through CMD_ARGS, packed structs
    over the bytes) holds only R_X86_64_64, at any alignment;
  * a "tone" symbol (an array of struct ToneData, 24 bytes at 64) holds
    R_X86_64_64 at offset 8 mod 24 (the wav slot) and R_X86_64_32 at offset
    16 mod 24 only (voice_keysplit's key-split table address, which
    platform/m4a/m4a_note.c reads as four u8 from attack..release);
  * a "bytecode" symbol holds only R_X86_64_32;
  * every symbol holds as many address words as it does in the reference
    object, the same source assembled with 4-byte pointers (Makefile.native
    builds it with DATA_POINTER_SIZE=4), where each is an R_X86_64_32.

Usage (paths of OBJECTs relative to both directories):
  python3 -I check_relocations.py --list LIST --objdir DIR --reference DIR OBJECT...
  python3 -I check_relocations.py --self-test
Exit 0 and no output when the data is consistent; otherwise one line per
problem (at most MAX_REPORTS per object) and exit 1.
"""

import bisect
import collections
import fnmatch
import os
import re
import subprocess
import sys

ADDRESS_8 = "R_X86_64_64"
ADDRESS_4 = "R_X86_64_32"
ADDRESS_TYPES = (ADDRESS_8, ADDRESS_4, "R_X86_64_32S")

POINTER_SIZE = 8
TONE_DATA_SIZE = 24            # sizeof(struct ToneData) at 64 bits
TONE_WAV_OFFSET = 8            # offsetof(struct ToneData, wav)
TONE_KEY_SPLIT_OFFSET = 16     # offsetof(struct ToneData, attack)

CLASSES = ("pointer", "packed", "operands", "tone", "bytecode", "label")
MAX_REPORTS = 20
READELF_BATCH = 64

Symbol = collections.namedtuple("Symbol", "name section value")
Relocation = collections.namedtuple("Relocation", "section offset type")


class ObjectFile:
    """What the check needs from one relocatable object."""

    def __init__(self):
        self.alignments = {}   # section name -> sh_addralign
        self.symbols = []      # Symbol, in symbol-table order, defined in a section
        self.relocations = []  # Relocation


SECTION_LINE = re.compile(
    r"^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+\s+[0-9a-f]+"
    r"\s+[A-Za-z]*\s*\d+\s+\d+\s+(\d+)\s*$")
SYMBOL_LINE = re.compile(
    r"^\s*\d+:\s+([0-9a-f]+)\s+\S+\s+(\w+)\s+\w+\s+\w+\s+(\w+)\s+(\S+)\s*$")
RELOCATION_SECTION = re.compile(r"^Relocation section '\.rela?(\S+)' at offset")
RELOCATION_LINE = re.compile(r"^([0-9a-f]+)\s+[0-9a-f]+\s+(R_\w+)")
FILE_LINE = re.compile(r"^File: (.+)$")


def parse_readelf(text, first_name):
    """Parse `readelf -SsrW` output for one or more files into ObjectFiles.

    readelf names each file in a "File:" line only when given several."""
    objects = {}
    current = objects.setdefault(first_name, ObjectFile())
    section_names = {}
    pending_symbols = []
    relocation_section = None

    def finish():
        for name, index, value in pending_symbols:
            if index in section_names:
                current.symbols.append(Symbol(name, section_names[index], value))
        pending_symbols.clear()

    for line in text.splitlines():
        match = FILE_LINE.match(line)
        if match:
            finish()
            current = objects.setdefault(match.group(1), ObjectFile())
            section_names = {}
            relocation_section = None
            continue
        match = SECTION_LINE.match(line)
        if match:
            section_names[match.group(1)] = match.group(2)
            current.alignments[match.group(2)] = int(match.group(3))
            continue
        match = RELOCATION_SECTION.match(line)
        if match:
            relocation_section = match.group(1)
            continue
        match = RELOCATION_LINE.match(line)
        if match and relocation_section is not None:
            current.relocations.append(Relocation(relocation_section, int(match.group(1), 16), match.group(2)))
            continue
        match = SYMBOL_LINE.match(line)
        if match:
            relocation_section = None
            kind, index, name = match.group(2), match.group(3), match.group(4)
            if kind not in ("SECTION", "FILE"):
                pending_symbols.append((name, index, int(match.group(1), 16)))
    finish()
    return objects


def read_objects(directory, names):
    """ObjectFile for each name (a path relative to directory)."""
    objects = {}
    for start in range(0, len(names), READELF_BATCH):
        batch = names[start:start + READELF_BATCH]
        paths = [os.path.join(directory, name) for name in batch]
        text = subprocess.run(["readelf", "-SsrW"] + paths, check=True,
                              stdout=subprocess.PIPE, universal_newlines=True).stdout
        parsed = parse_readelf(text, paths[0])
        for name, path in zip(batch, paths):
            if path not in parsed:
                raise RuntimeError("readelf printed nothing for %s" % path)
            objects[name] = parsed[path]
    return objects


def read_rules(path):
    """The list: (object glob, symbol glob, class, line number), in order."""
    rules = []
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            if len(fields) != 3 or fields[2] not in CLASSES:
                raise ValueError("%s:%d: expected 'object symbol class' with class one of %s"
                                 % (path, number, ", ".join(CLASSES)))
            rules.append((fields[0], fields[1], fields[2], number))
    return rules


def classify(rules, object_name, symbol_name, used):
    """The class of the first rule matching the symbol ('{stem}' is the
    object's file name without '.o'), or None; marks the rule used."""
    stem = os.path.splitext(os.path.basename(object_name))[0]
    for index, (object_glob, symbol_glob, kind, _) in enumerate(rules):
        if (fnmatch.fnmatchcase(object_name, object_glob)
                and fnmatch.fnmatchcase(symbol_name, symbol_glob.replace("{stem}", stem))):
            used.add(index)
            return kind
    return None


def owners(rules, object_name, obj, used):
    """For each address relocation: (relocation, owning Symbol or None, class).

    The owner is the last symbol at or before the relocation's offset; of
    several at one address, the last in the symbol table. A "label" symbol
    is never an owner."""
    by_section = collections.defaultdict(list)
    for order, symbol in enumerate(obj.symbols):
        kind = classify(rules, object_name, symbol.name, used)
        if kind != "label":
            by_section[symbol.section].append((symbol.value, order, symbol, kind))
    values = {}
    for section, entries in by_section.items():
        entries.sort(key=lambda entry: entry[:2])
        values[section] = [entry[0] for entry in entries]
    result = []
    for relocation in obj.relocations:
        if relocation.type not in ADDRESS_TYPES:
            continue
        index = bisect.bisect_right(values.get(relocation.section, []), relocation.offset) - 1
        if index < 0:
            result.append((relocation, None, None))
        else:
            _, _, symbol, kind = by_section[relocation.section][index]
            result.append((relocation, symbol, kind))
    return result


def placement_problem(obj, relocation, owner, kind):
    """Why this relocation's type or place does not fit the class, or None."""
    aligned = relocation.offset % POINTER_SIZE == 0 and obj.alignments.get(relocation.section, 0) >= POINTER_SIZE
    if kind == "bytecode":
        return None if relocation.type == ADDRESS_4 else "a bytecode address word must stay 4 bytes"
    if kind in ("packed", "operands"):
        return None if relocation.type == ADDRESS_8 else "a pointer must be 8 bytes"
    if kind == "pointer":
        if relocation.type != ADDRESS_8:
            return "a pointer must be 8 bytes"
        return None if aligned else "a pointer must be 8-byte aligned"
    slot = (relocation.offset - owner.value) % TONE_DATA_SIZE
    if slot == TONE_WAV_OFFSET and relocation.type == ADDRESS_8:
        return None if aligned else "a ToneData wav pointer must be 8-byte aligned"
    if slot == TONE_KEY_SPLIT_OFFSET and relocation.type == ADDRESS_4:
        return None
    return "a ToneData address word must be the 8-byte wav at offset 8 or the 4-byte key split at 16"


def check_object(rules, name, obj, reference, used):
    """Problems in one object, compared with its 4-byte-pointer reference."""
    problems = []
    counts = collections.Counter()
    for relocation, owner, kind in owners(rules, name, obj, used):
        where = "%s: %s+0x%x" % (name, relocation.section, relocation.offset)
        if owner is None:
            problems.append("%s: address word before any symbol" % where)
            continue
        counts[owner.name] += 1
        if kind is None:
            problems.append("%s (%s): symbol not in the list" % (where, owner.name))
            continue
        problem = placement_problem(obj, relocation, owner, kind)
        if problem:
            problems.append("%s (%s, %s, %s): %s" % (where, owner.name, kind, relocation.type, problem))
    expected = collections.Counter()
    for relocation, owner, _ in owners(rules, name, reference, set()):
        if relocation.type != ADDRESS_4:
            problems.append("%s: reference %s+0x%x is %s, not a 4-byte word"
                            % (name, relocation.section, relocation.offset, relocation.type))
        expected[owner.name if owner else None] += 1
    for symbol in sorted(set(counts) | set(expected), key=str):
        if counts[symbol] != expected[symbol]:
            problems.append("%s: %s holds %d address words, %d with 4-byte pointers"
                            % (name, symbol, counts[symbol], expected[symbol]))
    return problems


def check(rules, objects, references):
    """Every problem in the objects, then every rule that named no symbol."""
    problems = []
    used = set()
    for name in sorted(objects):
        found = check_object(rules, name, objects[name], references[name], used)
        problems.extend(found[:MAX_REPORTS])
        if len(found) > MAX_REPORTS:
            problems.append("%s: %d more problems" % (name, len(found) - MAX_REPORTS))
    for index, (object_glob, symbol_glob, kind, number) in enumerate(rules):
        if index not in used:
            problems.append("list line %d: '%s %s %s' names no symbol" % (number, object_glob, symbol_glob, kind))
    return problems


# --------------------------------------------------------------------------
# Self-test

SELF_TEST_READELF = """
File: a.o
Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .rodata           PROGBITS        0000000000000000 000040 000030 00   A  0   0  8
  [ 2] .rela.rodata      RELA            0000000000000000 0000a8 000048 18   I  4   1  8
  [ 3] script_data       PROGBITS        0000000000000000 000070 000010 00  WA  0   0  1

Relocation section '.rela.rodata' at offset 0xa8 contains 3 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000000  0000000500000001 R_X86_64_64            0000000000000000 gTarget + 0
0000000000000008  0000000500000001 R_X86_64_64            0000000000000000 gTarget + 4
0000000000000010  0000000500000002 R_X86_64_16            0000000000000000 SOME_CONSTANT + 0

Relocation section '.relascript_data' at offset 0xf0 contains 1 entry:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000003  0000000500000002 R_X86_64_32            0000000000000000 gTarget + 0

Symbol table '.symtab' contains 6 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000000     0 SECTION LOCAL  DEFAULT    1 .rodata
     2: 0000000000000001     0 NOTYPE  LOCAL  DEFAULT  ABS NATIVE_POINTER_SIZE_8
     3: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT    1 gTable
     4: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT    3 Script_Start
     5: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND gTarget

File: b.o
Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 1] .rodata           PROGBITS        0000000000000000 000040 000030 00   A  0   0  4

There are no relocations in this file.

Symbol table '.symtab' contains 2 entries:
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     0: 0000000000000000     0 NOTYPE  LOCAL  DEFAULT  UND
     1: 0000000000000004     0 NOTYPE  GLOBAL DEFAULT    1 gOther
"""


def make_object(alignments, symbols, relocations):
    obj = ObjectFile()
    obj.alignments = dict(alignments)
    obj.symbols = [Symbol(*symbol) for symbol in symbols]
    obj.relocations = [Relocation(*relocation) for relocation in relocations]
    return obj


def self_test_cases():
    """(name, rules, objects, references, expected substrings of the problems)."""
    pointer_rules = [("a.o", "gTable", "pointer", 1), ("a.o", "*", "bytecode", 2)]
    table = [("gTable", ".rodata", 0), ("Script", "script_data", 0)]
    good = make_object({".rodata": 8, "script_data": 1}, table,
                       [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_8), ("script_data", 3, ADDRESS_4),
                        (".rodata", 16, "R_X86_64_16")])
    reference = make_object({".rodata": 4, "script_data": 1}, table,
                            [(".rodata", 0, ADDRESS_4), (".rodata", 4, ADDRESS_4), ("script_data", 3, ADDRESS_4)])
    tone_rules = [("s.o", "keysplit_*", "label", 1), ("s.o", "voicegroup_*", "tone", 2)]
    tones = [("voicegroup_a", ".rodata", 0), ("keysplit_x", ".rodata", 28)]
    return [
        ("consistent", pointer_rules, {"a.o": good}, {"a.o": reference}, []),
        ("pointer left at 4 bytes", pointer_rules,
         {"a.o": make_object({".rodata": 8, "script_data": 1}, table,
                             [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_4), ("script_data", 3, ADDRESS_4)])},
         {"a.o": reference}, ["gTable, pointer, R_X86_64_32): a pointer must be 8 bytes"]),
        ("misaligned pointer", pointer_rules,
         {"a.o": make_object({".rodata": 8, "script_data": 1}, table,
                             [(".rodata", 0, ADDRESS_8), (".rodata", 12, ADDRESS_8), ("script_data", 3, ADDRESS_4)])},
         {"a.o": reference}, ["a pointer must be 8-byte aligned"]),
        ("section aligned to 4", pointer_rules,
         {"a.o": make_object({".rodata": 4, "script_data": 1}, table,
                             [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_8), ("script_data", 3, ADDRESS_4)])},
         {"a.o": reference}, ["a pointer must be 8-byte aligned", "a pointer must be 8-byte aligned"]),
        ("bytecode widened", pointer_rules,
         {"a.o": make_object({".rodata": 8, "script_data": 1}, table,
                             [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_8), ("script_data", 3, ADDRESS_8)])},
         {"a.o": reference}, ["a bytecode address word must stay 4 bytes"]),
        ("pointer lost", pointer_rules,
         {"a.o": make_object({".rodata": 8, "script_data": 1}, table,
                             [(".rodata", 0, ADDRESS_8), ("script_data", 3, ADDRESS_4)])},
         {"a.o": reference}, ["gTable holds 1 address words, 2 with 4-byte pointers"]),
        ("symbol not listed", [("a.o", "gTable", "pointer", 1)], {"a.o": good}, {"a.o": reference},
         ["(Script): symbol not in the list"]),
        ("rule names nothing", pointer_rules + [("a.o", "gRenamed", "pointer", 3)],
         {"a.o": good}, {"a.o": reference}, ["list line 3: 'a.o gRenamed pointer' names no symbol"]),
        ("reference not 4-byte", pointer_rules, {"a.o": good}, {"a.o": good},
         ["reference .rodata+0x0 is R_X86_64_64", "reference .rodata+0x8 is R_X86_64_64"]),
        ("word before any symbol", pointer_rules,
         {"a.o": make_object({".rodata": 8, "script_data": 1}, [("gTable", ".rodata", 8), ("Script", "script_data", 0)],
                             [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_8), ("script_data", 3, ADDRESS_4)])},
         {"a.o": reference}, ["address word before any symbol", "gTable holds 1 address words, 2 with"]),
        ("tone slots; a label is no owner", tone_rules,
         {"s.o": make_object({".rodata": 8}, tones,
                             [(".rodata", 8, ADDRESS_8), (".rodata", 16, ADDRESS_4), (".rodata", 32, ADDRESS_8)])},
         {"s.o": make_object({".rodata": 4}, [("voicegroup_a", ".rodata", 0), ("keysplit_x", ".rodata", 14)],
                             [(".rodata", 4, ADDRESS_4), (".rodata", 8, ADDRESS_4), (".rodata", 16, ADDRESS_4)])},
         []),
        ("tone key split widened", tone_rules,
         {"s.o": make_object({".rodata": 8}, tones, [(".rodata", 16, ADDRESS_8)])},
         {"s.o": make_object({".rodata": 4}, tones, [(".rodata", 8, ADDRESS_4)])},
         ["the 8-byte wav at offset 8 or the 4-byte key split at 16"]),
        ("tone wav at 4 bytes", tone_rules,
         {"s.o": make_object({".rodata": 8}, tones, [(".rodata", 8, ADDRESS_4)])},
         {"s.o": make_object({".rodata": 4}, tones, [(".rodata", 4, ADDRESS_4)])},
         ["the 8-byte wav at offset 8 or the 4-byte key split at 16"]),
        ("song header by stem", [("midi/*.o", "{stem}", "pointer", 1), ("midi/*.o", "*", "bytecode", 2)],
         {"midi/mus_a.o": make_object({".rodata": 8}, [("mus_a_1", ".rodata", 0), ("mus_a", ".rodata", 16)],
                                      [(".rodata", 4, ADDRESS_4), (".rodata", 24, ADDRESS_8)])},
         {"midi/mus_a.o": make_object({".rodata": 4}, [("mus_a_1", ".rodata", 0), ("mus_a", ".rodata", 12)],
                                      [(".rodata", 4, ADDRESS_4), (".rodata", 16, ADDRESS_4)])},
         []),
        ("battle script operands at any alignment", [("b.o", "*", "operands", 1)],
         {"b.o": make_object({"script_data": 1}, [("BattleScript_A", "script_data", 0)],
                             [("script_data", 1, ADDRESS_8), ("script_data", 10, ADDRESS_8)])},
         {"b.o": make_object({"script_data": 1}, [("BattleScript_A", "script_data", 0)],
                             [("script_data", 1, ADDRESS_4), ("script_data", 6, ADDRESS_4)])},
         []),
        ("battle script operand left at 4 bytes", [("b.o", "*", "operands", 1)],
         {"b.o": make_object({"script_data": 1}, [("BattleScript_A", "script_data", 0)],
                             [("script_data", 1, ADDRESS_8), ("script_data", 10, ADDRESS_4)])},
         {"b.o": make_object({"script_data": 1}, [("BattleScript_A", "script_data", 0)],
                             [("script_data", 1, ADDRESS_4), ("script_data", 6, ADDRESS_4)])},
         ["(BattleScript_A, operands, R_X86_64_32): a pointer must be 8 bytes"]),
        ("packed pointer at 4 mod 8", [("m.o", "*_ObjectEvents", "packed", 1)],
         {"m.o": make_object({".rodata": 8}, [("X_ObjectEvents", ".rodata", 0)],
                             [(".rodata", 16, ADDRESS_8), (".rodata", 44, ADDRESS_8)])},
         {"m.o": make_object({".rodata": 4}, [("X_ObjectEvents", ".rodata", 0)],
                             [(".rodata", 16, ADDRESS_4), (".rodata", 40, ADDRESS_4)])},
         []),
    ]


def self_test():
    failures = 0
    parsed = parse_readelf(SELF_TEST_READELF, "unused")
    a, b = parsed.get("a.o"), parsed.get("b.o")
    expected_a = ({".rodata": 8, ".rela.rodata": 8, "script_data": 1},
                  [Symbol("gTable", ".rodata", 0), Symbol("Script_Start", "script_data", 0)],
                  [Relocation(".rodata", 0, ADDRESS_8), Relocation(".rodata", 8, ADDRESS_8),
                   Relocation(".rodata", 16, "R_X86_64_16"), Relocation("script_data", 3, ADDRESS_4)])
    if a is None or b is None or (a.alignments, a.symbols, a.relocations) != expected_a:
        failures += 1
        sys.stderr.write("check_relocations self-test: parse_readelf: got %r\n"
                         % ({k: (v.alignments, v.symbols, v.relocations) for k, v in parsed.items()},))
    elif (b.alignments, b.symbols, b.relocations) != ({".rodata": 4}, [Symbol("gOther", ".rodata", 4)], []):
        failures += 1
        sys.stderr.write("check_relocations self-test: parse_readelf b.o: got %r\n"
                         % ((b.alignments, b.symbols, b.relocations),))
    for name, rules, objects, references, expected in self_test_cases():
        problems = check(rules, objects, references)
        if len(problems) != len(expected) or any(e not in p for e, p in zip(expected, problems)):
            failures += 1
            sys.stderr.write("check_relocations self-test: %s\n  expected %r\n  got      %r\n"
                             % (name, expected, problems))
    if failures:
        sys.stderr.write("check_relocations self-test: %d failed\n" % failures)
        return 1
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    args = argv[1:]
    options = {}
    while len(args) >= 2 and args[0] in ("--list", "--objdir", "--reference"):
        options[args[0]] = args[1]
        args = args[2:]
    if len(options) != 3 or not args:
        sys.stderr.write("usage: check_relocations.py --list LIST --objdir DIR --reference DIR OBJECT...\n"
                         "       check_relocations.py --self-test\n")
        return 2
    rules = read_rules(options["--list"])
    objects = read_objects(options["--objdir"], args)
    references = read_objects(options["--reference"], args)
    problems = check(rules, objects, references)
    for problem in problems:
        sys.stderr.write("check_relocations: %s\n" % problem)
    if problems:
        sys.stderr.write("check_relocations: the 64-bit data does not match %s (%d problems)\n"
                         % (options["--list"], len(problems)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
