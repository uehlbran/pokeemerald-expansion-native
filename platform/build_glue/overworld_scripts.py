"""Move a test object's event scripts out of its C, for the macOS build.

Stands in for nothing on the GBA: it is build glue for the macOS TEST=1
objects. include/test/overworld_script.h (native branch) hands each
OVERWORLD_SCRIPT block's script to the assembler as inline assembly in GNU
as's dialect, under expansion's event script macros (Makefile.native,
OVERWORLD_SCRIPT_MACROS), and GNU ld resolves the data's constants the
scripts name (ITEM_POTION, SPECIAL_*) against the data objects. On macOS
neither holds: clang's assembler, the only one Apple's clang uses, refuses
the macros (space-separated macro arguments, expressions it does not take as
absolute), the scripts' C names (ScrCmd_createmon) would need Mach-O's
underscore, and the converted data writes its constants local
(elf_to_flat.py). So the scripts are assembled as the data is
(docs/DECISIONS.md, "The data on a Mac"): this script reads the test
object's preprocessed C (after the C filter) and

  * writes the two file-scope blocks overworld_script.h adds (its .set
    constants and the .include of the macros) and every OVERWORLD_SCRIPT
    block's script, in the C's order, to an assembler file for GNU as: the
    script in .rodata under a global label of its own, NAME_1, NAME_2, ...,
    between the same .include lines as the block's, and its end;
  * replaces each OVERWORLD_SCRIPT block in the C with the address of its
    label, through a declaration (the compiler gives the name Mach-O's
    underscore), and each file-scope block with as many empty lines, so
    that the C's line numbers stay;
  * rewrites the C file in place.

Makefile.native assembles the file with GNU as (x86_64-elf-as), converts the
object as it converts the data (elf_to_flat.py --absolutes, check_flat.py)
and joins it to the C object (ld -r). With no OVERWORLD_SCRIPT block the
assembler file is empty and the file-scope blocks are still removed, since
clang cannot assemble the .include. The script text goes to GNU as as GCC
hands it over on Linux: an extended asm template turns %% into %, and
treats any other %, and {, | and }, as its own syntax, so a script holding
one is refused rather than guessed at. A block that has the marker
(.rodata.overworld_script) but not the form is refused too, naming its
line; nothing is dropped silently.

Usage:
  python3 -I overworld_scripts.py --name NAME C_FILE ASM_FILE
  python3 -I overworld_scripts.py --self-test
Exit 0 with both files written; otherwise one line per problem and exit 1,
with neither written.
"""

import os
import re
import sys
import tempfile

STRING = r'"(?:[^"\\\n]|\\.)*"'
# An asm statement whose template is string literals only, and the
# OVERWORLD_SCRIPT block's one output, as the preprocessor leaves them.
ASM_STATEMENT = re.compile(r'\b(?:__asm__|asm)\s*\(\s*((?:' + STRING + r'\s*)+)'
                           r'(:\s*"=r"\s*\(\s*_script\s*\)\s*)?\)\s*;')
MARKER = ".rodata.overworld_script"
# include/test/overworld_script.h's native OVERWORLD_SCRIPT, around the script.
SCRIPT_START = ('.pushsection .rodata.overworld_script, "a"\n'
                '.include "overworld_script_x86_macros.inc"\n'
                '1:\n')
SCRIPT_END = ('\n'
              'end\n'
              '.include "overworld_script_x86_purge.inc"\n'
              '.popsection\n'
              'mov $1b, %0\n')
# Its two file-scope blocks: the first starts so, the second is exactly so.
CONSTANTS_START = ".set FALSE, 0\n"
MACROS_INCLUDE = '.include "overworld_script_macros.inc"\n'
TEMPLATE_SYNTAX = re.compile(r"%%|[%{|}]")   # %% is a percent sign; the rest is the template's
NAME_PATTERN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
SIMPLE_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "a": "\a", "b": "\b", "f": "\f", "v": "\v",
                  "\\": "\\", "'": "'", '"': '"', "?": "?"}
ESCAPE = re.compile(r"\\(?:([0-7]{1,3})|x([0-9A-Fa-f]+)|(.))", re.DOTALL)


def decode(literals):
    """The text of adjacent C string literals, escapes decoded."""
    def escape(match):
        octal, hexadecimal, simple = match.groups()
        if octal:
            return chr(int(octal, 8))
        if hexadecimal:
            return chr(int(hexadecimal, 16))
        if simple not in SIMPLE_ESCAPES:
            raise ValueError("unknown escape \\%s" % simple)
        return SIMPLE_ESCAPES[simple]
    return "".join(ESCAPE.sub(escape, literal[1:-1]) for literal in re.findall(STRING, literals))


def script_part(name, body):
    """The assembler text of one script under its label."""
    return ('\t.include "overworld_script_x86_macros.inc"\n'
            '\t.globl %s\n'
            '%s:\n'
            '%s\n'
            'end\n'
            '\t.include "overworld_script_x86_purge.inc"\n' % (name, name, body))


def split(c_text, name):
    """(new C text, assembler text, problems)."""
    if not NAME_PATTERN.match(name):
        return None, None, ["the name %r is not an identifier" % name]
    parts, problems, scripts = [], [], [0]

    def replace(match):
        line = c_text.count("\n", 0, match.start()) + 1
        newlines = "\n" * match.group(0).count("\n")
        try:
            text = decode(match.group(1))
        except ValueError as error:
            problems.append("line %d: %s" % (line, error))
            return match.group(0)
        if match.group(2):
            if MARKER not in text:
                return match.group(0)
            if not (text.startswith(SCRIPT_START) and text.endswith(SCRIPT_END)):
                problems.append("line %d: an OVERWORLD_SCRIPT block not of include/test/overworld_script.h's form"
                                % line)
                return match.group(0)
            body = text[len(SCRIPT_START):-len(SCRIPT_END)]
            syntax = [found.group(0) for found in TEMPLATE_SYNTAX.finditer(body) if found.group(0) != "%%"]
            if syntax:
                problems.append("line %d: the script holds %r, which an asm template would not pass on as it is"
                                % (line, syntax[0]))
                return match.group(0)
            scripts[0] += 1
            label = "%s_%d" % (name, scripts[0])
            parts.append(script_part(label, body.replace("%%", "%")))
            return ("{ extern const unsigned char %s[]; _script = %s; }" % (label, label)) + newlines
        if text.startswith(CONSTANTS_START) or text == MACROS_INCLUDE:
            parts.append(text)
            return newlines
        if MARKER in text:
            problems.append("line %d: a block naming %s without the script's output" % (line, MARKER))
        return match.group(0)

    new_c = ASM_STATEMENT.sub(replace, c_text)
    if not problems and MARKER in new_c:
        line = new_c.count("\n", 0, new_c.index(MARKER)) + 1
        problems.append("line %d: %s outside a block this script reads" % (line, MARKER))
    if problems:
        return None, None, problems
    assembly = "\t.section .rodata\n" + "".join(parts) if scripts[0] else ""
    return new_c, assembly, []


def run(name, c_path, asm_path):
    with open(c_path) as handle:
        new_c, assembly, problems = split(handle.read(), name)
    if problems:
        return ["%s: %s" % (c_path, problem) for problem in problems]
    with open(asm_path, "w") as handle:
        handle.write(assembly)
    with open(c_path, "w") as handle:
        handle.write(new_c)
    return []


# --------------------------------------------------------------------------
# Self-test

def block(script):
    """An OVERWORLD_SCRIPT block as the preprocessor leaves it."""
    literal = '"' + script.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return ('({ const u8 *_script; __asm__(".pushsection .rodata.overworld_script, \\"a\\"\\n" '
            '".include \\"overworld_script_x86_macros.inc\\"\\n" "1:\\n" %s "\\n" "end\\n" '
            '".include \\"overworld_script_x86_purge.inc\\"\\n" ".popsection\\n" "mov $1b, %%0\\n" '
            ': "=r" (_script)); _script; })' % literal)


SELF_TEST_C = """int before;
__asm__(".set FALSE, 0\\n"
    ".set TRUE, 1\\n");
__asm__(".include \\"overworld_script_macros.inc\\"\\n");
static void f(void)
{
    const u8 *a = BLOCK1;
    asm("nop");
    const u8 *b = BLOCK2;
}
"""
SELF_TEST_EXPECTED_C = """int before;



static void f(void)
{
    const u8 *a = ({ const u8 *_script; { extern const unsigned char S_1[]; _script = S_1; } _script; });
    asm("nop");
    const u8 *b = ({ const u8 *_script; { extern const unsigned char S_2[]; _script = S_2; } _script; });
}
"""
SELF_TEST_EXPECTED_ASM = """\t.section .rodata
.set FALSE, 0
.set TRUE, 1
.include "overworld_script_macros.inc"
\t.include "overworld_script_x86_macros.inc"
\t.globl S_1
S_1:
setvar 0x8000, 1; end;
end
\t.include "overworld_script_x86_purge.inc"
\t.include "overworld_script_x86_macros.inc"
\t.globl S_2
S_2:
message "a\\"b"; giveitem ITEM_POTION, 100%
end
\t.include "overworld_script_x86_purge.inc"
"""

# (case, C text, the problems).
SELF_TEST_REFUSALS = (
    ("a block of another form", "x = " + block("end;").replace("mov $1b", "lea 1b"),
     ["line 1: an OVERWORLD_SCRIPT block not of include/test/overworld_script.h's form"]),
    ("an operand in the script", "x = " + block("setvar %1, 1;"),
     ["line 1: the script holds '%', which an asm template would not pass on as it is"]),
    ("a dialect brace in the script", "x = " + block("setvar {1}, 1;"),
     ["line 1: the script holds '{', which an asm template would not pass on as it is"]),
    ("the marker in a basic asm", '\n__asm__(".pushsection .rodata.overworld_script\\n");',
     ["line 2: a block naming .rodata.overworld_script without the script's output"]),
    ("the marker outside a block", 'char *s = ".rodata.overworld_script";',
     ["line 1: .rodata.overworld_script outside a block this script reads"]),
    ("an escape C does not have", 'x = ({ __asm__(".rodata.overworld_script\\q" : "=r" (_script)); });',
     ["line 1: unknown escape \\q"]),
)


def self_test():
    failures = []

    def expect(name, actual, wanted):
        if actual != wanted:
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))

    c_text = (SELF_TEST_C.replace("BLOCK1", block("setvar 0x8000, 1; end;"))
              .replace("BLOCK2", block('message "a\\"b"; giveitem ITEM_POTION, 100%%')))
    expect("split", split(c_text, "S"), (SELF_TEST_EXPECTED_C, SELF_TEST_EXPECTED_ASM, []))
    expect("no script: the file-scope blocks go, the assembler file is empty",
           split(SELF_TEST_C.replace("BLOCK1", "0").replace("BLOCK2", "0"), "S"),
           (SELF_TEST_EXPECTED_C.replace(SELF_TEST_EXPECTED_C.splitlines()[6], "    const u8 *a = 0;")
            .replace(SELF_TEST_EXPECTED_C.splitlines()[8], "    const u8 *b = 0;"), "", []))
    expect("decode", decode(r'"a\n" "\"\\" "\101\x42"'), 'a\n"\\AB')
    expect("a name that is not an identifier", split("", "a-b")[2], ["the name 'a-b' is not an identifier"])
    for name, text, wanted in SELF_TEST_REFUSALS:
        expect(name, split(text, "S"), (None, None, wanted))
    with tempfile.TemporaryDirectory() as directory:
        c_path, asm_path = os.path.join(directory, "t.i"), os.path.join(directory, "t.scripts.s")
        with open(c_path, "w") as handle:
            handle.write(c_text)
        expect("run", run("S", c_path, asm_path), [])
        expect("run: the C rewritten", open(c_path).read(), SELF_TEST_EXPECTED_C)
        expect("run: the assembler file", open(asm_path).read(), SELF_TEST_EXPECTED_ASM)
        with open(c_path, "w") as handle:
            handle.write(SELF_TEST_REFUSALS[0][1])
        os.remove(asm_path)
        expect("run: a refusal names the file", run("S", c_path, asm_path),
               ["%s: %s" % (c_path, SELF_TEST_REFUSALS[0][2][0])])
        expect("run: and writes nothing", (os.path.exists(asm_path), open(c_path).read()),
               (False, SELF_TEST_REFUSALS[0][1]))
    for failure in failures:
        sys.stderr.write("overworld_scripts self-test: %s\n" % failure)
    return 1 if failures else 0


USAGE = ("usage: overworld_scripts.py --name NAME C_FILE ASM_FILE\n"
         "       overworld_scripts.py --self-test\n")


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) != 5 or argv[1] != "--name":
        sys.stderr.write(USAGE)
        return 2
    try:
        problems = run(argv[2], argv[3], argv[4])
    except OSError as error:   # names the file it could not open
        problems = [str(error)]
    for problem in problems:
        sys.stderr.write("overworld_scripts: %s\n" % problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
