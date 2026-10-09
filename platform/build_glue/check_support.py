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

'python3 -I check_support.py --self-test' checks each of them, the refusals
included, and exits non-zero on a failure.
"""

import fnmatch
import os
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
    expect("rule: none", matching_rule(rules, "b.o", "Other"), None)
    for failure in failures:
        sys.stderr.write("check_support self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
