"""What the 64-bit data checkers share: errors, reports, options and rules.

Stands in for nothing on the GBA: it is build glue. check_relocations.py,
check_data_layout.py and check_battle_script_operands.py each read some
input, collect a list of problems (one line each) and exit 1 if there is
one. This module holds the parts that are the same in all three:

  * CheckError: input a checker cannot read at all. The build must stop,
    and the message names the file and what was expected.
  * capped(): at most N problems per object, then "N more problems".
  * problems_match(): the self-tests' comparison of the problems found with
    the substrings expected, in order.
  * parse_options(): the "--name value" options before the positional
    arguments.
  * matching_rule(): which "object-glob symbol-glob ..." rule applies to a
    symbol, with '{stem}' standing for the object's name without '.o'
    (the lists data_address_words.txt and check_data_layout.RULES).
  * ewram_alias(): where writable data in the executable would be taken for
    EWRAM by a test of an address's bits 24-27 (check_pe_layout.py and
    check_macho_layout.py refuse it; gba_regions.ld asserts the same).
  * gnu_as(): the command of the GNU assembler that the self-tests of the
    object readers, the converter and the checkers assemble their ELF
    fixtures with: $GNU_AS if it is set, else `as`. On macOS `as` is
    Apple's, which writes Mach-O and takes no GNU options; platform/target.mk
    sets GNU_AS to Homebrew's x86_64-elf-as there.

'python3 -I check_support.py --self-test' checks each of them, the refusals
included, and exits non-zero on a failure.
"""

import fnmatch
import os
import shlex
import sys

STEM = "{stem}"


class CheckError(Exception):
    """Input a checker cannot read; the build must stop."""


def capped(problems, limit, where):
    """The first `limit` problems, then one line counting the rest."""
    if len(problems) <= limit:
        return list(problems)
    return list(problems[:limit]) + ["%s: %d more problems" % (where, len(problems) - limit)]


def problems_match(expected, found):
    """True if found has one problem per expected substring, in order."""
    return len(found) == len(expected) and all(wanted in problem for wanted, problem in zip(expected, found))


def parse_options(args, allowed):
    """(options, the arguments after them): the leading '--name value' pairs
    of args. allowed maps each option name to the tuple of values it takes,
    or to None for any value. Parsing stops at the first argument that is
    not an allowed option with an allowed value, so the caller's check that
    nothing (or the right number of things) is left rejects the rest."""
    options = {}
    while len(args) >= 2 and args[0] in allowed and (allowed[args[0]] is None or args[1] in allowed[args[0]]):
        options[args[0]] = args[1]
        args = args[2:]
    return options, args


def matching_rule(rules, object_name, symbol_name):
    """Index of the first rule whose object glob and symbol glob (its first
    two fields) match, or None."""
    stem = os.path.splitext(os.path.basename(object_name))[0]
    for index, rule in enumerate(rules):
        if fnmatch.fnmatchcase(object_name, rule[0]) and fnmatch.fnmatchcase(symbol_name, rule[1].replace(STEM, stem)):
            return index
    return None


# GBATEK "GBA Memory Map": 02000000-02FFFFFF is EWRAM and its mirrors, so
# expansion tests an address's bits 24-27 for 2 to ask whether it is in EWRAM
# (src/librfu_rfu.c rfu_initializeAPI refuses an API buffer there). In the
# native executable, linked from 0x40000000 up, the addresses with those bits
# 2 are 0x42000000-0x42FFFFFF, then 0x52000000-0x52FFFFFF and so on; a
# variable there would be taken for EWRAM.
IMAGE_START = 0x40000000
NIBBLE_BLOCK = 0x10000000
EWRAM_NIBBLE_OFFSET, EWRAM_NIBBLE_SIZE = 0x02000000, 0x01000000


def ewram_alias(start, end):
    """The first address of [start, end) at or above IMAGE_START whose bits
    24-27 are 2, as EWRAM's are, or None."""
    address = max(start, IMAGE_START)
    while address < end:
        window = address - address % NIBBLE_BLOCK + EWRAM_NIBBLE_OFFSET
        if window <= address < window + EWRAM_NIBBLE_SIZE:
            return address
        if address < window:
            return window if window < end else None
        address = window - EWRAM_NIBBLE_OFFSET + NIBBLE_BLOCK
    return None


def gnu_as():
    """The GNU assembler's command as a list: $GNU_AS split as a shell would, else ['as']."""
    return shlex.split(os.environ.get("GNU_AS") or "as")


def self_test():
    failures = []

    def expect(name, actual, wanted):
        if actual != wanted:
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))

    expect("capped under the limit", capped(["a", "b"], 2, "x.o"), ["a", "b"])
    expect("capped over the limit", capped(["a", "b", "c", "d"], 2, "x.o"), ["a", "b", "x.o: 2 more problems"])
    expect("capped none", capped([], 2, "x.o"), [])
    expect("problems_match substrings", problems_match(["b", "d"], ["abc", "cde"]), True)
    expect("problems_match too few", problems_match(["b"], []), False)
    expect("problems_match too many", problems_match([], ["abc"]), False)
    expect("problems_match order", problems_match(["d", "b"], ["abc", "cde"]), False)
    expect("problems_match substring absent", problems_match(["x"], ["abc"]), False)
    allowed = {"--list": None, "--width": ("4", "8")}
    expect("options", parse_options(["--list", "a", "--width", "8", "o.o"], allowed),
           ({"--list": "a", "--width": "8"}, ["o.o"]))
    expect("options: a value not allowed stops the parse", parse_options(["--width", "3", "o.o"], allowed),
           ({}, ["--width", "3", "o.o"]))
    expect("options: an unknown option stops the parse", parse_options(["--other", "1"], allowed),
           ({}, ["--other", "1"]))
    expect("options: a missing value stops the parse", parse_options(["--list"], allowed), ({}, ["--list"]))
    rules = [("a.o", "gTable", 1), ("m/*.o", STEM, 2), ("*.o", "g*", 3)]
    expect("rule: exact", matching_rule(rules, "a.o", "gTable"), 0)
    expect("rule: stem", matching_rule(rules, "m/mus_x.o", "mus_x"), 1)
    expect("rule: stem of another object", matching_rule(rules, "m/mus_x.o", "mus_y"), None)
    expect("rule: first match wins", matching_rule(rules, "a.o", "gOther"), 2)
    expect("rule: the symbol matches but the object does not", matching_rule(rules[:1], "b.o", "gTable"), None)
    expect("rule: none", matching_rule(rules, "b.o", "Other"), None)
    expect("ewram_alias: below the window", ewram_alias(0x41000000, 0x42000000), None)
    expect("ewram_alias: reaching it", ewram_alias(0x41000000, 0x42000001), 0x42000000)
    expect("ewram_alias: inside it", ewram_alias(0x429BDD08, 0x44C9E940), 0x429BDD08)
    expect("ewram_alias: from its first byte", ewram_alias(0x42000000, 0x42000001), 0x42000000)
    expect("ewram_alias: its last byte", ewram_alias(0x42FFFFFF, 0x43000000), 0x42FFFFFF)
    expect("ewram_alias: from its end", ewram_alias(0x43000000, 0x52000000), None)
    expect("ewram_alias: the next block's", ewram_alias(0x43000000, 0x52000010), 0x52000000)
    expect("ewram_alias: past the next block's", ewram_alias(0x53000000, 0x62000000), None)
    expect("ewram_alias: the GBA's EWRAM itself is not the image's", ewram_alias(0x02000000, 0x02040000), None)
    expect("ewram_alias: from below the image", ewram_alias(0x02000000, 0x42000001), 0x42000000)
    expect("ewram_alias: empty", ewram_alias(0x42000010, 0x42000010), None)
    saved = os.environ.pop("GNU_AS", None)
    try:
        expect("gnu_as: as by default", gnu_as(), ["as"])
        os.environ["GNU_AS"] = ""
        expect("gnu_as: as when empty", gnu_as(), ["as"])
        os.environ["GNU_AS"] = "x86_64-elf-as --warn"
        expect("gnu_as: the variable's words", gnu_as(), ["x86_64-elf-as", "--warn"])
    finally:
        os.environ.pop("GNU_AS", None)
        if saved is not None:
            os.environ["GNU_AS"] = saved
    for failure in failures:
        sys.stderr.write("check_support self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
