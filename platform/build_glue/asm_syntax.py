"""Read the lines and statements of ARM GNU assembler source.

Stands in for nothing on the GBA: it is build glue. asm_filter.py rewrites
expansion's ARM assembler text for the host assembler, and
check_battle_script_operands.py reads the battle script macros written in the
same syntax. Both need the same few facts about it (the GNU as manual,
"Syntax"):

  * '@' starts an end-of-line comment on ARM. '\\@' (the macro execution
    counter) is not a comment and is kept. An '@' inside a string ("a@b")
    or a character constant ('@ in GAS syntax, or '@' in C style) is not
    one either.
  * ';' separates statements on a line, outside strings and character
    constants.
  * A statement is any number of labels ('name:' or 'name::'), then a
    directive or an instruction.

Nothing is rejected: after an unterminated string the rest of the line is
inside the string, so nothing in it is stripped or split, and the assembler
is left to reject the line.

'python3 -I asm_syntax.py --self-test' checks each rule and exits non-zero on
a failure.
"""

import collections
import re
import sys

LABEL = re.compile(r"\s*([A-Za-z_.$][\w.$]*)(::?)")
DIRECTIVE_NAME = re.compile(r"\s*(\.[\w.]+)")

# labels: the names defined; directive: its name or None; start, end: its
# place in the statement (both the end of the labels if there is none).
Statement = collections.namedtuple("Statement", "labels directive start end")


def end_of_string(line, start):
    """Index just past the string literal opening at line[start] ('"').

    An unterminated string runs to the end of the line."""
    index = start + 1
    while index < len(line):
        if line[index] == "\\":
            index += 2
        elif line[index] == '"':
            return index + 1
        else:
            index += 1
    return len(line)


def end_of_char_constant(line, start):
    """Index just past the character constant opening at line[start] (').

    GAS writes a character constant as 'c or '\\c; a closing quote, as in
    C's 'c', is accepted and skipped."""
    index = start + 1
    if index < len(line) and line[index] == "\\":
        index += 1
    index += 1
    if index < len(line) and line[index] == "'":
        index += 1
    return min(index, len(line))


def code_positions(line):
    """Yield the index of each character outside string and character literals."""
    index = 0
    while index < len(line):
        char = line[index]
        if char == '"':
            index = end_of_string(line, index)
        elif char == "'":
            index = end_of_char_constant(line, index)
        else:
            yield index
            index += 1


def strip_comment(line):
    """Return the line without an ARM '@' comment."""
    for index in code_positions(line):
        if line[index] == "@" and not (index > 0 and line[index - 1] == "\\"):
            return line[:index].rstrip() + "\n"
    return line


def split_statements(line):
    """Split a line on the ';' statement separators outside literals."""
    parts = []
    start = 0
    for index in code_positions(line):
        if line[index] == ";":
            parts.append(line[start:index])
            start = index + 1
    parts.append(line[start:])
    return parts


def parse_statement(statement):
    """The Statement: its labels and its directive."""
    labels = []
    position = 0
    while True:
        match = LABEL.match(statement, position)
        if not match:
            break
        labels.append(match.group(1))
        position = match.end()
    match = DIRECTIVE_NAME.match(statement, position)
    if not match:
        return Statement(labels, None, position, position)
    return Statement(labels, match.group(1), match.start(1), match.end(1))


def operand_count(statement, end):
    """Number of comma-separated operands after the directive ending at end."""
    return len([operand for operand in statement[end:].split(",") if operand.strip()])


# Each case: (function, argument, expected result).
SELF_TEST_CASES = (
    (strip_comment, "\t.byte 1 @ comment\n", "\t.byte 1\n"),
    (strip_comment, "\t.byte 1\n", "\t.byte 1\n"),
    (strip_comment, "@ all comment\n", "\n"),
    (strip_comment, ".L_\\@_1: .byte 2 @ c\n", ".L_\\@_1: .byte 2\n"),
    (strip_comment, '\t.string "a@b" @ c\n', '\t.string "a@b"\n'),
    (strip_comment, '\t.string "q\\"@" @ c\n', '\t.string "q\\"@"\n'),
    (strip_comment, "\t.byte '@ @ c\n", "\t.byte '@\n"),
    (strip_comment, "\t.byte '@', 'x' @ c\n", "\t.byte '@', 'x'\n"),
    # An unterminated string runs to the end of the line: nothing in it is a comment.
    (strip_comment, '\t.ascii "open @ text\n', '\t.ascii "open @ text\n'),
    (split_statements, "\t.byte 1\n", ["\t.byte 1\n"]),
    (split_statements, "a ; b ;c", ["a ", " b ", "c"]),
    (split_statements, '.string "a;b" ; x', ['.string "a;b" ', " x"]),
    (split_statements, ".byte ';', 1 ; x", [".byte ';', 1 ", " x"]),
    (split_statements, '.ascii "open ; text', ['.ascii "open ; text']),
    (lambda statement: parse_statement(statement), "\t.4byte x",
     Statement([], ".4byte", 1, 7)),
    (lambda statement: parse_statement(statement), "gA:: gB: .space 4",
     Statement(["gA", "gB"], ".space", 9, 15)),
    (lambda statement: parse_statement(statement), "gA:",
     Statement(["gA"], None, 3, 3)),
    (lambda statement: parse_statement(statement), "\tmov r0, r1",
     Statement([], None, 0, 0)),
    (lambda statement: parse_statement(statement), "1: .byte 1",
     Statement([], None, 0, 0)),
    (lambda statement: operand_count(statement, 0), " 1, 2 ,3", 3),
    (lambda statement: operand_count(statement, 0), "  ", 0),
    (lambda statement: operand_count(statement, 0), " 1,,2", 2),
)


def self_test():
    failures = 0
    for function, given, expected in SELF_TEST_CASES:
        actual = function(given)
        if actual != expected:
            failures += 1
            sys.stderr.write("asm_syntax self-test: %r\n  expected %r\n  got      %r\n" % (given, expected, actual))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
