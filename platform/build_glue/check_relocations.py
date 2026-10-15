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
"Relocation Types"; elf_object.py reads the objects). Each relocation belongs to the symbol at or before its
offset in its section (the label of the table or script that holds it),
and the list classifies that symbol. The check fails unless:

  * every symbol that holds an address word is classified by the list, and
    every rule in the list names at least one symbol (a renamed table is
    caught rather than falling through to a catch-all);
  * a "pointer" symbol holds only R_X86_64_64, each at an 8-byte aligned
    offset in a section aligned to 8 (gcc copies such structs with aligned
    SSE moves, and UBSan checks the alignment of every load);
  * a "table" symbol is a "pointer" symbol that is an array of pointers and
    nothing else: its words fill it up to the next symbol (or the end of
    its section), so a table of bytecode that follows it without a label
    of its own cannot be widened with it unnoticed;
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
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError, capped, gnu_as, matching_rule, parse_options, problems_match  # noqa: E402
from elf_object import (ElfObject, R_X86_64_16, R_X86_64_32, R_X86_64_32S, R_X86_64_64, Section, Symbol,  # noqa: E402
                        read_elf64, relocation_name)

ADDRESS_8 = R_X86_64_64
ADDRESS_4 = R_X86_64_32
ADDRESS_TYPES = (ADDRESS_8, ADDRESS_4, R_X86_64_32S)

POINTER_SIZE = 8
TONE_DATA_SIZE = 24            # sizeof(struct ToneData) at 64 bits
TONE_WAV_OFFSET = 8            # offsetof(struct ToneData, wav)
TONE_KEY_SPLIT_OFFSET = 16     # offsetof(struct ToneData, attack)

CLASSES = ("pointer", "table", "packed", "operands", "tone", "bytecode", "label")
LABEL = "label"
TABLE = "table"
MAX_REPORTS = 20

Relocation = collections.namedtuple("Relocation", "section offset type")
# object glob, symbol glob, class, the line of the list it came from
Rule = collections.namedtuple("Rule", "object symbol kind line")


def read_objects(directory, names):
    """ElfObject for each name (a path relative to directory)."""
    return {name: read_elf64(os.path.join(directory, name)) for name in names}


def read_rules(path):
    """The list: the Rules, in order."""
    rules = []
    with open(path) as handle:
        for number, line in enumerate(handle, 1):
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            if len(fields) != 3 or fields[2] not in CLASSES:
                raise CheckError("%s:%d: expected 'object symbol class' with class one of %s, got %r"
                                 % (path, number, ", ".join(CLASSES), line.strip()))
            rules.append(Rule(fields[0], fields[1], fields[2], number))
    return rules


def classify_symbols(rules, object_name, obj, used):
    """The class of each of obj's symbols (a list parallel to obj.symbols)
    by the first rule matching it, or None; marks the rules used."""
    kinds = []
    for symbol in obj.symbols:
        index = matching_rule(rules, object_name, symbol.name)
        if index is None:
            kinds.append(None)
        else:
            used.add(index)
            kinds.append(rules[index].kind)
    return kinds


def address_relocations(obj):
    """The Relocations of obj that are address words."""
    return [Relocation(section, offset, kind)
            for section, table in obj.relocations.items()
            for offset, kind in table.items() if kind in ADDRESS_TYPES]


def owners(obj, kinds):
    """For each address relocation: (relocation, owning Symbol or None, class).

    The owner is the last symbol at or before the relocation's offset; of
    several at one address, the last in the symbol table. A "label" symbol
    is never an owner."""
    by_section = collections.defaultdict(list)
    for order, (symbol, kind) in enumerate(zip(obj.symbols, kinds)):
        if kind != LABEL:
            by_section[symbol.section].append((symbol.value, order, symbol, kind))
    values = {}
    for section, entries in by_section.items():
        entries.sort(key=lambda entry: entry[:2])
        values[section] = [entry[0] for entry in entries]
    result = []
    for relocation in address_relocations(obj):
        index = bisect.bisect_right(values.get(relocation.section, []), relocation.offset) - 1
        if index < 0:
            result.append((relocation, None, None))
        else:
            _, _, symbol, kind = by_section[relocation.section][index]
            result.append((relocation, symbol, kind))
    return result


def placement_problem(obj, relocation, owner, kind):
    """Why this relocation's type or place does not fit the class, or None."""
    aligned = (relocation.offset % POINTER_SIZE == 0
               and obj.sections[relocation.section].alignment >= POINTER_SIZE)
    if kind == "bytecode":
        return None if relocation.type == ADDRESS_4 else "a bytecode address word must stay 4 bytes"
    if kind in ("packed", "operands"):
        return None if relocation.type == ADDRESS_8 else "a pointer must be 8 bytes"
    if kind in ("pointer", TABLE):
        if relocation.type != ADDRESS_8:
            return "a pointer must be 8 bytes"
        return None if aligned else "a pointer must be 8-byte aligned"
    slot = (relocation.offset - owner.value) % TONE_DATA_SIZE
    if slot == TONE_WAV_OFFSET and relocation.type == ADDRESS_8:
        return None if aligned else "a ToneData wav pointer must be 8-byte aligned"
    if slot == TONE_KEY_SPLIT_OFFSET and relocation.type == ADDRESS_4:
        return None
    return "a ToneData address word must be the 8-byte wav at offset 8 or the 4-byte key split at 16"


def placement_problems(name, obj, kinds):
    """Problems with the type and place of each address word of obj."""
    problems = []
    for relocation, owner, kind in owners(obj, kinds):
        where = "%s: %s+0x%x" % (name, obj.sections[relocation.section].name, relocation.offset)
        if owner is None:
            problems.append("%s: address word before any symbol" % where)
        elif kind is None:
            problems.append("%s (%s): symbol not in the list" % (where, owner.name))
        else:
            problem = placement_problem(obj, relocation, owner, kind)
            if problem:
                problems.append("%s (%s, %s, %s): %s"
                                % (where, owner.name, kind, relocation_name(relocation.type), problem))
    return problems


def word_counts(obj, kinds):
    """Symbol name (None: before any symbol) -> its number of address words."""
    return collections.Counter(owner.name if owner else None for _, owner, _ in owners(obj, kinds))


def reference_problems(name, reference, kinds):
    """Problems with the object assembled with 4-byte pointers: it must
    hold only 4-byte address words."""
    return ["%s: reference %s+0x%x is %s, not a 4-byte word"
            % (name, reference.sections[relocation.section].name, relocation.offset, relocation_name(relocation.type))
            for relocation, _, _ in owners(reference, kinds) if relocation.type != ADDRESS_4]


def count_problems(name, counts, expected):
    """Problems with the number of address words each symbol holds, against
    the number it holds in the reference."""
    return ["%s: %s holds %d address words, %d with 4-byte pointers" % (name, symbol, counts[symbol], expected[symbol])
            for symbol in sorted(set(counts) | set(expected), key=str) if counts[symbol] != expected[symbol]]


def table_problems(name, obj, kinds, counts):
    """Problems with each "table" symbol: its words fill it up to the next
    symbol."""
    kind_of = dict(zip(obj.symbols, kinds))
    extents = obj.extents(lambda symbol: kind_of[symbol] != LABEL)
    return ["%s: table %s holds %d pointers (%d bytes) but %d bytes lie before the next symbol"
            % (name, symbol.name, counts[symbol.name], counts[symbol.name] * POINTER_SIZE, extent)
            for symbol, kind, extent in zip(obj.symbols, kinds, extents)
            if kind == TABLE and counts[symbol.name] * POINTER_SIZE != extent]


def check_object(rules, name, obj, reference, used):
    """Problems in one object, compared with its 4-byte-pointer reference."""
    kinds = classify_symbols(rules, name, obj, used)
    reference_kinds = classify_symbols(rules, name, reference, set())
    counts = word_counts(obj, kinds)
    counts.pop(None, None)   # a word before any symbol is reported by placement_problems, once
    return (placement_problems(name, obj, kinds)
            + reference_problems(name, reference, reference_kinds)
            + count_problems(name, counts, word_counts(reference, reference_kinds))
            + table_problems(name, obj, kinds, counts))


def check(rules, objects, references, list_path):
    """Every problem in the objects, then every rule that named no symbol."""
    problems = []
    used = set()
    for name in sorted(objects):
        problems.extend(capped(check_object(rules, name, objects[name], references[name], used), MAX_REPORTS, name))
    for index, rule in enumerate(rules):
        if index not in used:
            problems.append("%s:%d: '%s %s %s' names no symbol" % (list_path, rule.line, rule.object, rule.symbol, rule.kind))
    return problems


# --------------------------------------------------------------------------
# Self-test

def make_object(alignments, symbols, relocations, sizes=None):
    """An ElfObject with sections keyed by their names, as the cases name them."""
    sizes = sizes or {}
    sections = {name: Section(name, sizes.get(name, 0), alignment, b"") for name, alignment in alignments.items()}
    table = {}
    for section, offset, kind in relocations:
        table.setdefault(section, {})[offset] = kind
    return ElfObject("self-test", sections, [Symbol(*symbol) for symbol in symbols], table)


def self_test_cases():
    """(name, rules, objects, references, expected substrings of the problems)."""
    pointer_rules = [("a.o", "gTable", "pointer", 1), ("a.o", "*", "bytecode", 2)]
    table = [("gTable", ".rodata", 0), ("Script", "script_data", 0)]
    good = make_object({".rodata": 8, "script_data": 1}, table,
                       [(".rodata", 0, ADDRESS_8), (".rodata", 8, ADDRESS_8), ("script_data", 3, ADDRESS_4),
                        (".rodata", 16, R_X86_64_16)])
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
         {"a.o": good}, {"a.o": reference}, ["list:3: 'a.o gRenamed pointer' names no symbol"]),
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
        ("table fills its extent", [("t.o", "gTable", "table", 1), ("t.o", "*", "bytecode", 2)],
         {"t.o": make_object({"script_data": 8}, [("gTable", "script_data", 0), ("Script", "script_data", 16)],
                             [("script_data", 0, ADDRESS_8), ("script_data", 8, ADDRESS_8),
                              ("script_data", 17, ADDRESS_4)], {"script_data": 24})},
         {"t.o": make_object({"script_data": 4}, [("gTable", "script_data", 0), ("Script", "script_data", 8)],
                             [("script_data", 0, ADDRESS_4), ("script_data", 4, ADDRESS_4),
                              ("script_data", 9, ADDRESS_4)], {"script_data": 16})},
         []),
        ("table followed by unlabelled words", [("t.o", "gTable", "table", 1)],
         {"t.o": make_object({"script_data": 8}, [("gTable", "script_data", 0)],
                             [("script_data", 0, ADDRESS_8), ("script_data", 8, ADDRESS_8)], {"script_data": 24})},
         {"t.o": make_object({"script_data": 4}, [("gTable", "script_data", 0)],
                             [("script_data", 0, ADDRESS_4), ("script_data", 4, ADDRESS_4)], {"script_data": 16})},
         ["table gTable holds 2 pointers (16 bytes) but 24 bytes lie before the next symbol"]),
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




def run_case(rules, objects, references):
    return check([Rule(*rule) for rule in rules], objects, references, "list")


END_TO_END_8 = """
	.section .rodata
	.balign 8
gTable:
	.8byte gTarget
	.8byte gTarget + 4
	.section script_data, "a"
Script:
	.byte 1
	.4byte gTarget
"""
END_TO_END_4 = """
	.section .rodata
	.balign 4
gTable:
	.4byte gTarget
	.4byte gTarget + 4
	.section script_data, "a"
Script:
	.byte 1
	.4byte gTarget
"""


def self_test_end_to_end(failures, directory):
    """Objects written by the host assembler, read back by read_objects."""
    for subdirectory, source in (("obj", END_TO_END_8), ("narrow", END_TO_END_8.replace(".8byte", ".4byte")),
                                 ("ref", END_TO_END_4)):
        os.makedirs(os.path.join(directory, subdirectory))
        subprocess.run(gnu_as() + ["--64", "-o", os.path.join(directory, subdirectory, "a.o")], input=source,
                       universal_newlines=True, check=True)
    rules = [Rule("a.o", "gTable", "pointer", 1), Rule("a.o", "Script", "bytecode", 2)]
    reference = read_objects(os.path.join(directory, "ref"), ["a.o"])
    for name, subdirectory, wanted in (
            ("assembled objects, consistent", "obj", []),
            ("assembled objects, the table left at 4 bytes", "narrow",
             ["a.o: .rodata+0x0 (gTable, pointer, R_X86_64_32): a pointer must be 8 bytes",
              "a.o: .rodata+0x4 (gTable, pointer, R_X86_64_32): a pointer must be 8 bytes"])):
        problems = check(rules, read_objects(os.path.join(directory, subdirectory), ["a.o"]), reference, "list")
        if not problems_match(wanted, problems):
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, problems))


def self_test_read_rules(failures, directory):
    path = os.path.join(directory, "words.txt")
    with open(path, "w") as handle:
        handle.write("# a comment\n\na.o gTable table  # trailing\n")
    if read_rules(path) != [Rule("a.o", "gTable", "table", 3)]:
        failures.append("read_rules: %r" % (read_rules(path),))
    for text in ("a.o gTable\n", "a.o gTable nonsense\n", "a.o gTable table extra\n"):
        with open(path, "w") as handle:
            handle.write(text)
        try:
            read_rules(path)
            failures.append("read_rules accepted %r" % text)
        except CheckError as error:
            if not str(error).startswith("%s:1: expected 'object symbol class'" % path):
                failures.append("read_rules message for %r: %s" % (text, error))


def self_test():
    failures = []
    for name, rules, objects, references, expected in self_test_cases():
        problems = run_case(rules, objects, references)
        if not problems_match(expected, problems):
            failures.append("%s\n  expected %r\n  got      %r" % (name, expected, problems))
    with tempfile.TemporaryDirectory() as directory:
        self_test_end_to_end(failures, directory)
        self_test_read_rules(failures, directory)
    if failures:
        for failure in failures:
            sys.stderr.write("check_relocations self-test: %s\n" % failure)
        return 1
    return 0


USAGE = ("usage: check_relocations.py --list LIST --objdir DIR --reference DIR OBJECT...\n"
         "       check_relocations.py --self-test\n")


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    options, names = parse_options(argv[1:], {"--list": None, "--objdir": None, "--reference": None})
    if len(options) != 3 or not names:
        sys.stderr.write(USAGE)
        return 2
    try:
        problems = check(read_rules(options["--list"]), read_objects(options["--objdir"], names),
                         read_objects(options["--reference"], names), options["--list"])
    except (CheckError, OSError) as error:   # an OSError names the file it could not open
        sys.stderr.write("check_relocations: %s\n" % error)
        return 1
    for problem in problems:
        sys.stderr.write("check_relocations: %s\n" % problem)
    if problems:
        sys.stderr.write("check_relocations: the 64-bit data does not match %s (%d problems)\n"
                         % (options["--list"], len(problems)))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
