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
    tools/preproc does.
  * '.align N' aligns to 2**N bytes on ARM and to N bytes on x86.
    '.p2align N' means 2**N on both.
  * '.word' emits 4 bytes on ARM and 2 bytes on x86. '.4byte' emits 4 on
    both. The songs' GOTO and PATT targets are '.word'.

Everything else the data uses ('%progbits' section types, '.type ...,
%function', '.size', '.4byte', '.2byte', '.hword') means the same to x86
as. With these rewrites the 32-bit objects' sections are byte-identical to
the GBA build's.

Reads stdin, writes stdout. Run as 'python3 -I asm_filter.py'.
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


def strip_comment(line):
    """Return the line without an ARM '@' comment, honouring quoted strings."""
    quote = None
    escaped = False
    for index, char in enumerate(line):
        if quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char == "@" and not (index > 0 and line[index - 1] == "\\"):
            return line[:index].rstrip() + "\n"
    return line


def split_statements(line):
    """Split a line on ';' statement separators outside quoted strings."""
    parts = []
    quote = None
    escaped = False
    start = 0
    for index, char in enumerate(line):
        if quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char == ";":
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


def main():
    for line in sys.stdin:
        sys.stdout.write(filter_line(line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
