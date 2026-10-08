"""Rewrite expansion's ARM GNU assembler syntax for the host (x86) assembler.

Stands in for nothing on the GBA: it is build glue. Expansion's data
(data/*.s after cpp and preproc, the mid2agb song files, sound/MPlayDef.s)
is written for arm-none-eabi-as. Two of its conventions mean something
different, or nothing, to the x86 GNU assembler:

  * '@' starts an end-of-line comment on ARM; on x86 it is an ordinary
    character, so a comment becomes a syntax error. '\\@' (the macro
    execution counter) is not a comment on either and is kept, as
    tools/preproc does.
  * '.align N' aligns to 2**N bytes on ARM; on x86 it aligns to N bytes.
    '.p2align N' means 2**N on both, so every '.align' becomes '.p2align'
    (the GNU as manual, "Assembler Directives", .align and .p2align).

Everything else the data uses ('%progbits' section types, '.type ...,
%function', '.size', '.4byte', '.2byte') is accepted unchanged by x86 as.

Reads stdin, writes stdout. Run as 'python3 -I asm_filter.py'.
"""

import re
import sys

ALIGN_DIRECTIVE = re.compile(r"^(\s*(?:[A-Za-z_.$][\w.$]*:\s*)*)\.align\b")


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


def rewrite_statement(statement):
    """Rewrite one ';'-free statement: '.align' becomes '.p2align'."""
    return ALIGN_DIRECTIVE.sub(r"\1.p2align", statement)


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


def filter_line(line):
    if "@" not in line and ".align" not in line:
        return line
    line = strip_comment(line)
    return ";".join(rewrite_statement(part) for part in split_statements(line))


def main():
    for line in sys.stdin:
        sys.stdout.write(filter_line(line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
