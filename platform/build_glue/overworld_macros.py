"""Keep the event script macros from replacing x86 instructions.

Stands in for nothing on the GBA: it is build glue for the TEST=1 objects.
include/test/overworld_script.h (native branch) gives the tests' inline
assembly expansion's event script macros through one top-level
`.include "overworld_script_macros.inc"` per object. A GNU as macro whose
name is also an instruction replaces the instruction for the rest of the
file (GNU as manual, "Macro": macros are looked up before instructions), and
event.inc defines `call`, `nop`, `lock` and (for its enums) `inc`: every
`call` gcc emitted after the include assembled as the script command
`call` (one byte 0x04 and a word), and the objects crashed when run.

So the flattened macro file is split. The macros whose names the host
assembler takes as instructions or prefixes are purged at the end of the
top-level include, and their definitions go to a second file that each
OVERWORLD_SCRIPT block includes before its script and purges after it (a
third file, the purges), so they exist only inside script blocks. Which
names conflict is asked of the host assembler itself: one line per macro
name, and a name that does not draw "no such instruction" is an instruction.

    python3 -I overworld_macros.py --as 'as --32' IN MACROS X86_MACROS X86_PURGE
    python3 -I overworld_macros.py --self-test
"""

import re
import shlex
import subprocess
import sys

MACRO_START = re.compile(r"^\s*\.macro\s+([A-Za-z_.$][A-Za-z0-9_.$]*)")
MACRO_END = re.compile(r"^\s*\.endm\b")
ASSEMBLER_LINE = re.compile(r"^\{standard input\}:(\d+): (Error|Warning): (.*)$")
NOT_AN_INSTRUCTION = "no such instruction"


def macro_names(lines):
    """Names of the top-level macros, in order of definition."""
    names = []
    depth = 0
    for line in lines:
        start = MACRO_START.match(line)
        if start:
            if depth == 0:
                names.append(start.group(1))
            depth += 1
        elif MACRO_END.match(line):
            depth -= 1
            if depth < 0:
                raise ValueError(".endm without .macro")
    if depth != 0:
        raise ValueError(".macro without .endm")
    return names


def macro_definitions(lines, wanted):
    """The text of the top-level macros named in `wanted`, .macro to .endm."""
    out = []
    depth = 0
    copying = False
    for line in lines:
        start = MACRO_START.match(line)
        if start and depth == 0:
            copying = start.group(1) in wanted
        if start:
            depth += 1
        if copying:
            out.append(line)
        if MACRO_END.match(line):
            depth -= 1
            if depth == 0:
                copying = False
    return out


def instruction_names(names, assembler):
    """The names the host assembler accepts as an instruction or a prefix."""
    source = "".join(name + "\n" for name in names)
    result = subprocess.run(
        shlex.split(assembler) + ["-o", "/dev/null", "-"],
        input=source, capture_output=True, text=True, check=False)
    not_instructions = set()
    for line in result.stderr.splitlines():
        match = ASSEMBLER_LINE.match(line)
        if match and NOT_AN_INSTRUCTION in match.group(3):
            not_instructions.add(int(match.group(1)))
    return [name for number, name in enumerate(names, 1) if number not in not_instructions]


def split(lines, conflicts):
    """(the macros file with the conflicts purged at its end, their definitions, their purges)"""
    purges = [".purgem %s\n" % name for name in conflicts]
    return lines + purges, macro_definitions(lines, set(conflicts)), purges


def self_test():
    sample = [
        ".macro call destination:req\n",
        "\t.byte 0x04\n",
        "\t.4byte \\destination\n",
        ".endm\n",
        ".macro outer\n",
        "\t.macro inner\n",
        "\t.endm\n",
        ".endm\n",
        ".macro lock\n",
        "\t.byte 0x6a\n",
        ".endm\n",
        "\t.set FOO, 1\n",
    ]
    assert macro_names(sample) == ["call", "outer", "lock"], macro_names(sample)
    definitions = macro_definitions(sample, {"call", "lock"})
    assert definitions == sample[0:4] + sample[8:11], definitions
    macros, x86, purges = split(sample, ["call", "lock"])
    assert macros[-2:] == [".purgem call\n", ".purgem lock\n"]
    assert x86 == definitions and purges == macros[-2:]
    for broken in ([".macro a\n"], [".endm\n"]):
        try:
            macro_names(broken)
        except ValueError:
            continue
        raise AssertionError("unbalanced macros accepted: %r" % broken)
    print("overworld_macros.py: self-test passed")


def main(argv):
    if argv[1:] == ["--self-test"]:
        self_test()
        return 0
    if len(argv) != 7 or argv[1] != "--as":
        sys.stderr.write(__doc__)
        return 2
    assembler, source, macros_path, x86_path, purge_path = argv[2:]
    with open(source, encoding="utf-8") as f:
        lines = f.readlines()
    conflicts = instruction_names(macro_names(lines), assembler)
    outputs = split(lines, conflicts)
    for path, content in zip((macros_path, x86_path, purge_path), outputs):
        with open(path, "w", encoding="utf-8") as f:
            f.writelines(content)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
