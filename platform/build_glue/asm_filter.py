"""Rewrite expansion's ARM GNU assembler syntax for the host (x86) assembler.

Stands in for nothing on the GBA: it is build glue. Expansion's data
(data/*.s after cpp and preproc, the mid2agb song files, sound/MPlayDef.s,
the event script macros the tests use) is written for arm-none-eabi-as.
Three of its conventions mean something different to the x86 GNU
assembler (the GNU as manual, "Machine Dependencies" and "Assembler
Directives"):

  * '@' starts an end-of-line comment on ARM; on x86 it is an ordinary
    character, so a comment becomes a syntax error. '\\@' (the macro
    execution counter) is not a comment on either and is kept, as
    tools/preproc does. An '@' inside a string ("a@b") or a character
    constant ('@ in GAS syntax, or '@' in C style) is kept too.
  * '.align N' aligns to 2**N bytes on ARM and to N bytes on x86.
    '.p2align N' means 2**N on both.
  * '.word' emits 4 bytes on ARM and 2 bytes on x86. '.4byte' emits 4 on
    both. The songs' GOTO and PATT targets are '.word'.

Everything else the data uses ('%progbits' section types, '.type ...,
%function', '.size', '.4byte', '.2byte', '.hword') means the same to x86
as. With these rewrites the 32-bit objects' sections are byte-identical to
the GBA build's.

Malformed input fails loudly rather than being corrupted: after an
unterminated string nothing is stripped or renamed, so an '@' comment that
follows reaches the assembler, which rejects it.

With '--pointer-size 8', which Makefile.native passes for the 64-bit build
only, the filter also lays the data out for C structs whose pointers are 8
bytes (rules below, per kind of input). Without it, or with
'--pointer-size 4', the output is the same as before the option existed.

Reads stdin, writes stdout. Run as
'python3 -I asm_filter.py [--pointer-size 4|8]'.
'python3 -I asm_filter.py --self-test' checks the rules above and exits
non-zero on a failure.
"""

import re
import sys

# ARM directive -> the x86 directive with the same meaning.
DIRECTIVE_RENAMES = {
    ".align": ".p2align",
    ".word": ".4byte",
}

# A directive at the start of a statement, after any labels (which may
# contain macro arguments and '\@', and end in ':' or '::').
DIRECTIVE = re.compile(
    r"^(\s*(?:[^\s:;\"']+::?\s*)*)(" +
    "|".join(re.escape(name) for name in DIRECTIVE_RENAMES) +
    r")\b")


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


def rename_directive(statement):
    """Rename the statement's directive if ARM and x86 read it differently."""
    return DIRECTIVE.sub(
        lambda match: match.group(1) + DIRECTIVE_RENAMES[match.group(2)],
        statement)


def filter_line(line):
    if "@" not in line and not any(name in line for name in DIRECTIVE_RENAMES):
        return line
    line = strip_comment(line)
    return ";".join(rename_directive(part) for part in split_statements(line))


SELF_TEST_CASES = (
    ("\t.align 2 @ comment\n", "\t.p2align 2\n"),
    ("label: .align 2, 0\n", "label: .p2align 2, 0\n"),
    ("\t.ifdef x ; .align 1 ; .endif\n", "\t.ifdef x ; .p2align 1 ; .endif\n"),
    ("\t.word target @ comment\n", "\t.4byte target\n"),
    (".Lx_\\@_1: .word 3\n", ".Lx_\\@_1: .4byte 3\n"),
    ("gLabel::\t.word 4\n", "gLabel::\t.4byte 4\n"),
    ("\t.byte 1 ; .word 2\n", "\t.byte 1 ; .4byte 2\n"),
    ("\t.hword 1\n", "\t.hword 1\n"),
    ("\t.words 1\n", "\t.words 1\n"),
    ("\t.balign 4 @ kept as is\n", "\t.balign 4\n"),
    ("\t.byte (.L_\\@_2 - .L_\\@_1)\n", "\t.byte (.L_\\@_2 - .L_\\@_1)\n"),
    ('\t.string "a@b" @ comment\n', '\t.string "a@b"\n'),
    ('\t.string "it\'s \\"@\\"" @ comment\n', '\t.string "it\'s \\"@\\""\n'),
    ("\t.byte '@ @ comment\n", "\t.byte '@\n"),
    ("\t.byte '@', 'x' @ comment\n", "\t.byte '@', 'x'\n"),
    ("\t.byte '\\n, ';' ; .word 1 @ comment\n", "\t.byte '\\n, ';' ; .4byte 1\n"),
    # An unterminated string: nothing after it is touched.
    ('\t.ascii "open @ text\n', '\t.ascii "open @ text\n'),
)


def self_test():
    failures = 0
    for given, expected in SELF_TEST_CASES:
        actual = filter_line(given)
        if actual != expected:
            failures += 1
            sys.stderr.write("asm_filter self-test: %r\n  expected %r\n  got      %r\n"
                             % (given, expected, actual))
    if failures:
        sys.stderr.write("asm_filter self-test: %d of %d failed\n"
                         % (failures, len(SELF_TEST_CASES)))
        return 1
    return 0


POINTER_SIZES = ("4", "8")
USAGE = "usage: asm_filter.py [--pointer-size 4|8] < input > output | --self-test\n"


def filter_program(lines, pointer_size):
    """Yield the filtered lines of a whole input."""
    for line in lines:
        yield filter_line(line)


def main(argv):
    args = argv[1:]
    if args == ["--self-test"]:
        return self_test()
    pointer_size = "4"
    if len(args) == 2 and args[0] == "--pointer-size" and args[1] in POINTER_SIZES:
        pointer_size = args[1]
    elif args:
        sys.stderr.write(USAGE)
        return 2
    for line in filter_program(sys.stdin, int(pointer_size)):
        sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
