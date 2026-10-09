"""Check the battle script macros against the C that reads their operands.

Stands in for nothing on the GBA: it is build glue. A battle script is
bytecode that asm/macros/battle_script.inc writes and
src/battle_script_commands.c reads, one command at a time, through
CMD_ARGS: a packed struct laid over the bytes at gBattlescriptCurrInstr,
whose members are the command's operands (NATIVE_ARGS is CMD_ARGS with the
function pointer of a callnative first). The bytes must therefore have the
struct's layout, and the struct's pointer members are 8 bytes at 64 bits.
The macros write a pointer operand with '.4byte' and, under
'.ifdef NATIVE_POINTER_SIZE_8', '.8byte'.

For each macro this script expands the macros it calls, splits what it
emits into commands (each starts with '.byte B_SCR_OP_*'), finds the C
function that reads the command (gBattleScriptingCommandsTable, or the
callnative's function) and compares the operand widths, in order, with the
CMD_ARGS members: with 4-byte pointers (NATIVE_POINTER_SIZE_8 undefined, as
on the GBA and the 32-bit build) and with 8-byte pointers. It fails on any
difference, on a macro that writes operands but no command, and on a C
member type it does not know (MEMBER_SIZES).

Only the widths are compared, in order: two operands of the same width
swapped by name (jumpifword's wordPtr and jumpInstr, both pointers) pass.

A command the C file does not implement is skipped: a callnative of an
undefined function, an opcode missing from the table, or a function with
an empty body (upstream keeps a few such macros; no script uses them, and
a use would fail the build or never advance).

One command length is written in C: src/battle_z_move.c skips the
setzeffect command with Z_EFFECT_BS_LENGTH, which the replays and the
suite do not reach. Given that file, the script evaluates the
preprocessor conditions around the constant's definitions (for the native
build, at each pointer size) and requires it to equal the setzeffect
macro's length.

Usage:
  python3 -I check_battle_script_operands.py MACROS C_FILE Z_MOVE_FILE          check
  python3 -I check_battle_script_operands.py --table MACROS C_FILE Z_MOVE_FILE  print the operand table
  python3 -I check_battle_script_operands.py --self-test
Exit 0 and no output when consistent; otherwise one line per problem, exit 1.
"""

import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from asm_syntax import split_statements, strip_comment  # noqa: E402
from check_support import CheckError, problems_match  # noqa: E402
from preprocessor_conditions import constant_value  # noqa: E402

POINTER_SIZES = (4, 8)
POINTER_SYMBOL = "NATIVE_POINTER_SIZE_8"
OPCODE_PREFIX = "B_SCR_OP_"
CALLNATIVE = "B_SCR_OP_CALLNATIVE"
WIDTHS = {".byte": 1, ".2byte": 2, ".4byte": 4, ".8byte": 8}
# Sizes of the non-pointer member types CMD_ARGS uses; the two enums are
# __attribute__((packed)) (include/constants/abilities.h, species.h) and
# their values need 16 bits.
MEMBER_SIZES = {
    "u8": 1, "s8": 1, "bool8": 1,
    "u16": 2, "s16": 2, "bool16": 2,
    "u32": 4, "s32": 4, "bool32": 4,
    "enum Ability": 2, "enum Species": 2,
}
NATIVE_FUNCTION_MEMBER = "void (*func)(void)"
MAX_MACRO_DEPTH = 20
# Where a pointer size's width goes in a table row.
POINTER_COLUMN = {4: 1, 8: 2}

MACRO_START = re.compile(r"^\s*\.macro\s+(\w+)\s*,?\s*(.*)$")
MACRO_END = re.compile(r"^\s*\.endm\b")
PARAMETER = re.compile(r"\\(\w+)(\(\))?")
LABEL = re.compile(r"^\s*(?:[A-Za-z_.$0-9][\w.$]*:)+")
FUNCTION = re.compile(r"^(?:static\s+)?void\s+(\w+)\s*\(void\)\s*\n\{(.*?)^\}", re.S | re.M)
ARGS_USE = re.compile(r"\b(CMD_ARGS|NATIVE_ARGS)\s*\(")
TABLE_ENTRY = re.compile(r"\[\s*(B_SCR_OP_\w+)\s*\]\s*=\s*(\w+)")


# A file this check reads: the path, for messages, and its text.
Source = collections.namedtuple("Source", "path text")


# --------------------------------------------------------------------------
# C side

def balanced(text, start):
    """The text inside the parentheses opening at text[start - 1]."""
    depth = 1
    index = start
    while depth:
        if index >= len(text):
            raise CheckError("unbalanced parentheses after %r" % text[max(start - 20, 0):start])
        depth += {"(": 1, ")": -1}.get(text[index], 0)
        index += 1
    return text[start:index - 1]


def split_top(text):
    """Split on the commas outside parentheses."""
    parts, depth, start = [], 0, 0
    for index, char in enumerate(text):
        depth += {"(": 1, ")": -1}.get(char, 0)
        if char == "," and depth == 0:
            parts.append(text[start:index])
            start = index + 1
    parts.append(text[start:])
    return [part.strip() for part in parts if part.strip()]


class Member:
    """One CMD_ARGS member: its declaration, and its size in bytes with
    pointers of the given size (None: a bitfield, sized by its group)."""

    def __init__(self, declaration):
        self.declaration = " ".join(declaration.split())
        text = re.sub(r"\bconst\b", " ", self.declaration)
        self.pointer = "*" in text
        self.bits = None
        bitfield = re.match(r"^(.*?)\b\w+\s*:\s*(\d+)$", text)
        if self.pointer:
            self.base = None
        elif bitfield:
            self.base = " ".join(bitfield.group(1).split())
            self.bits = int(bitfield.group(2))
        else:
            match = re.match(r"^(.*?)\b(\w+)\s*(?:\[\s*(\d+)\s*\])?$", text)
            if not match:
                raise CheckError("cannot read the member %r" % declaration)
            self.base = " ".join(match.group(1).split())
            self.count = int(match.group(3) or 1)
        if self.base is not None and self.base not in MEMBER_SIZES:
            raise CheckError("unknown member type %r in %r (add it to MEMBER_SIZES)" % (self.base, declaration))

    def size(self, pointer_size):
        if self.pointer:
            return pointer_size
        return MEMBER_SIZES[self.base] * self.count


def operand_sizes(members, pointer_size):
    """[(size, members)] in order; consecutive bitfields share whole bytes."""
    sizes = []
    bits, group = 0, []
    for member in members + [None]:
        if member is not None and member.bits is not None:
            bits += member.bits
            group.append(member)
            continue
        if group:
            sizes.append(((bits + 7) // 8, group))
            bits, group = 0, []
        if member is not None:
            sizes.append((member.size(pointer_size), [member]))
    return sizes


def read_table(source):
    """opcode -> function name, from gBattleScriptingCommandsTable."""
    table = {}
    start = source.text.find("gBattleScriptingCommandsTable[]")
    if start < 0:
        raise CheckError("%s: no gBattleScriptingCommandsTable (expected the table of the C functions that read "
                         "the commands)" % source.path)
    end = source.text.find("};", start)
    for match in TABLE_ENTRY.finditer(source.text, start, end):
        table[match.group(1)] = match.group(2)
    return table


def read_members(function, body):
    """The Members a function body declares with CMD_ARGS or NATIVE_ARGS:
    [] if it reads no operand, None for an empty stub (Cmd_tryKO: it never
    advances, so no script uses it)."""
    uses = list(ARGS_USE.finditer(body))
    if len(uses) > 1:
        raise CheckError("%s declares its arguments %d times (expected one CMD_ARGS or NATIVE_ARGS)"
                         % (function, len(uses)))
    if not uses:
        return [] if body.strip() else None
    declarations = split_top(balanced(body, uses[0].end()))
    if uses[0].group(1) == "NATIVE_ARGS":
        declarations.insert(0, NATIVE_FUNCTION_MEMBER)
    return [Member(declaration) for declaration in declarations]


def read_commands(source):
    """(opcode -> function name, function name -> [Member] or None)."""
    table = read_table(source)
    functions = {}
    for match in FUNCTION.finditer(source.text):
        try:
            functions[match.group(1)] = read_members(match.group(1), match.group(2))
        except CheckError as error:
            raise CheckError("%s: %s: %s" % (source.path, match.group(1), error))
    return table, functions


# --------------------------------------------------------------------------
# Macro side

class Macro:
    def __init__(self, path, name, parameters, first_line):
        self.path = path
        self.name = name
        self.parameters = parameters   # [(name, default or None)]
        self.body = []                 # [(line number, text)]
        self.first_line = first_line

    def where(self, line):
        return "%s:%d: macro %s" % (self.path, line, self.name)


def read_macros(source):
    macros = {}
    current = None
    for number, line in enumerate(source.text.splitlines(), 1):
        code = strip_comment(line)
        start = MACRO_START.match(code)
        if start:
            parameters = []
            for token in re.split(r"[\s,]+", start.group(2).strip()):
                if token:
                    name, _, default = token.partition("=")
                    parameters.append((name.split(":")[0], default if _ else None))
            current = Macro(source.path, start.group(1), parameters, number)
        elif MACRO_END.match(code):
            if current is None:
                raise CheckError("%s:%d: .endm without .macro (expected the end of a macro that was started)"
                                 % (source.path, number))
            macros[current.name] = current
            current = None
        elif current is not None:
            for statement in split_statements(code):
                if statement.strip():
                    current.body.append((number, statement.strip()))
    return macros


def bind(macro, arguments, line):
    """Parameter -> value for a call of macro (at line of its caller) with the argument texts."""
    values = {name: default for name, default in macro.parameters}
    names = [name for name, _ in macro.parameters]
    position = 0
    for argument in arguments:
        keyword = re.match(r"^(\w+)\s*=(.*)$", argument)
        if keyword and keyword.group(1) in values:
            values[keyword.group(1)] = keyword.group(2).strip()
        else:
            if position >= len(names):
                raise CheckError("%s:%d: too many arguments to %s: %r (it takes %d)"
                                 % (macro.path, line, macro.name, arguments, len(names)))
            values[names[position]] = argument
            position += 1
    return values


def substitute(text, values):
    def replace(match):
        if match.group(1) in values and values[match.group(1)] is not None:
            return values[match.group(1)]
        return match.group(0)
    return PARAMETER.sub(replace, text)


class Item:
    """One emitted operand: its width, its expression and where it was written."""

    def __init__(self, width, expression, where):
        self.width, self.expression, self.where = width, expression, where


def expand(macros, macro, values, pointer_size, depth=0):
    """The Items a call of macro emits, with NATIVE_POINTER_SIZE_8 defined
    when pointer_size is 8. A condition that cannot be evaluated here
    ('.if' on a parameter) contributes both branches."""
    if depth > MAX_MACRO_DEPTH:
        raise CheckError("%s: macros nested more than %d deep (expected a macro that does not call itself)"
                         % (macro.where(macro.first_line), MAX_MACRO_DEPTH))
    items = []
    conditions = []   # True/False: a decided .ifdef; None: undecided
    for number, statement in macro.body:
        statement = LABEL.sub("", statement).strip()
        if not statement:
            continue
        word = statement.split(None, 1)[0]
        rest = statement[len(word):].strip()
        if word in (".ifdef", ".ifndef"):
            if rest != POINTER_SYMBOL:
                raise CheckError("%s: .ifdef %s (expected %s, the only symbol the macros test)"
                                 % (macro.where(number), rest, POINTER_SYMBOL))
            defined = pointer_size == 8
            conditions.append(defined if word == ".ifdef" else not defined)
        elif word.startswith(".if"):
            conditions.append(None)
        elif word == ".else":
            if conditions and conditions[-1] is not None:
                conditions[-1] = not conditions[-1]
        elif word == ".endif":
            conditions.pop()
        elif False in conditions:
            continue
        elif word in WIDTHS:
            for expression in split_top(substitute(rest, values)):
                items.append(Item(WIDTHS[word], expression, "%s:%d" % (macro.path, number)))
        elif word in macros:
            callee = macros[word]
            arguments = split_top(substitute(rest, values))
            items.extend(expand(macros, callee, bind(callee, arguments, number), pointer_size, depth + 1))
        else:
            raise CheckError("%s: cannot expand %r (expected a data directive or a macro of this file)"
                             % (macro.where(number), statement))
    return items


def commands(items):
    """Split Items into [(opcode, [operand Items])]."""
    result = []
    for item in items:
        if item.width == 1 and item.expression.startswith(OPCODE_PREFIX):
            result.append((item.expression, []))
        elif not result:
            raise CheckError("%s: an operand before any command (expected '.byte %s*' first)"
                             % (item.where, OPCODE_PREFIX))
        else:
            result[-1][1].append(item)
    return result


# --------------------------------------------------------------------------
# The check

def reader(opcode, operands, table, functions, c_path):
    """(C function name, its Members) for a command, or (name, None) when
    the command's function is not known (an undefined callnative), or
    (None, None) for a callnative whose function is a parameter."""
    if opcode == CALLNATIVE:
        if not operands or not re.match(r"^\w+$", operands[0].expression):
            return None, None
        name = operands[0].expression
        return name, functions.get(name)
    if opcode not in table:
        return None, None
    name = table[opcode]
    if name not in functions:
        raise CheckError("%s: %s (%s) is not defined (expected a function of that name that reads the command)"
                         % (c_path, name, opcode))
    return name, functions[name]   # None for a stub


def expected_operands(opcode, operands, members, pointer_size):
    """([(width, [Member])] the C reads, the operands to compare with it),
    or None for a command the C file does not implement. A callnative of an
    undefined function is checked for its function pointer only."""
    if members is not None:
        return operand_sizes(members, pointer_size), operands
    if opcode == CALLNATIVE:
        return [(pointer_size, [Member(NATIVE_FUNCTION_MEMBER)])], operands[:1]
    return None


class Findings:
    """What the check learns: its problems, the table rows (macro, opcode,
    C function) -> [[declaration, width with 4-byte pointers, with 8-byte]],
    and, per macro line that writes a '.4byte' or '.8byte', whether it is a
    pointer ({True}), an integer ({False}) or, wrongly, both. (A line has
    one meaning: a macro that writes operands but no command, the only way
    to share a line between commands, is rejected by commands().)"""

    def __init__(self):
        self.problems = []
        self.rows = {}
        self.word_kinds = {}

    def add_row(self, key, expected, pointer_size):
        row = self.rows.setdefault(key, [[", ".join(m.declaration for m in group), None, None]
                                         for _, group in expected])
        for entry, (size, _) in zip(row, expected):
            entry[POINTER_COLUMN[pointer_size]] = size

    def add_command(self, macro, opcode, name, operands, expected, pointer_size):
        self.add_row((macro.name, opcode, name), expected, pointer_size)
        widths = [item.width for item in operands]
        sizes = [size for size, _ in expected]
        if widths != sizes:
            self.problems.append("%s:%d: %s (%s, %s), %d-byte pointers: the macro writes %s, the C reads %s"
                                 % (macro.path, macro.first_line, macro.name, opcode, name, pointer_size, widths, sizes))
            return
        for item, (_, group) in zip(operands, expected):
            if item.width in (4, 8):
                self.word_kinds.setdefault(item.where, set()).add(group[0].pointer)

    def pointer_lines(self):
        return sorted(where for where, seen in self.word_kinds.items() if seen == {True})


def check_macro(macros, macro, table, functions, c_path, findings):
    """Compare each command the macro emits, with each pointer size, with the C that reads it."""
    unbound = {name: None for name, _ in macro.parameters}
    for pointer_size in POINTER_SIZES:
        for opcode, operands in commands(expand(macros, macro, unbound, pointer_size)):
            name, members = reader(opcode, operands, table, functions, c_path)
            wanted = expected_operands(opcode, operands, members, pointer_size)
            if wanted is not None:
                findings.add_command(macro, opcode, name, wanted[1], wanted[0], pointer_size)


def check(macro_source, c_source, z_source=None):
    """(problems, table rows, pointer lines), each Source a file's path and
    text. With z_source (src/battle_z_move.c), its command length is
    checked too."""
    macros = read_macros(macro_source)
    table, functions = read_commands(c_source)
    findings = Findings()
    for macro in macros.values():
        check_macro(macros, macro, table, functions, c_source.path, findings)
    if z_source is not None:
        findings.problems.extend(check_skipped_length(macros, macro_source.path, z_source))
    return findings.problems, findings.rows, findings.pointer_lines()


def print_table(rows, pointer_lines):
    sys.stdout.write("# macro | opcode | C reader | operands: declaration (bytes at 32/64)\n")
    for (macro, opcode, name), row in sorted(rows.items()):
        operands = "; ".join("%s (%s/%s)" % (declaration, a, b) for declaration, a, b in row)
        sys.stdout.write("%s | %s | %s | %s\n" % (macro, opcode, name or "-", operands or "none"))
    sys.stdout.write("# pointer operand lines in the macro file: %d\n" % len(pointer_lines))


# --------------------------------------------------------------------------
# The command length in src/battle_z_move.c

SKIPPED_COMMAND = ("Z_EFFECT_BS_LENGTH", "setzeffect")


def check_skipped_length(macros, macro_path, z_source):
    """Problems with the length src/battle_z_move.c gives the setzeffect command."""
    name, macro_name = SKIPPED_COMMAND
    if macro_name not in macros:
        raise CheckError("%s: no %s macro (expected the command whose length %s is)" % (macro_path, macro_name, name))
    problems = []
    for pointer_size in POINTER_SIZES:
        items = expand(macros, macros[macro_name], {}, pointer_size)
        if len(commands(items)) != 1:
            raise CheckError("%s: the %s macro is not one command (expected one, the length of %s)"
                             % (macro_path, macro_name, name))
        length = sum(item.width for item in items)
        try:
            value = constant_value(z_source.text, name, {"PLATFORM_NATIVE": 1, "__SIZEOF_POINTER__": pointer_size})
        except CheckError as error:
            raise CheckError("%s: %s" % (z_source.path, error))
        if value is None:
            raise CheckError("%s: no definition of %s for %d-byte pointers (expected %d)"
                             % (z_source.path, name, pointer_size, length))
        if value != length:
            problems.append("%s: %s is %d with %d-byte pointers; the %s command is %d bytes"
                            % (z_source.path, name, value, pointer_size, macro_name, length))
    return problems


# --------------------------------------------------------------------------
# Self-test

SELF_TEST_C = """
void (*const gBattleScriptingCommandsTable[])(void) =
{
    [B_SCR_OP_JUMPIFBYTE] = Cmd_jumpifbyte,
    [B_SCR_OP_CALLNATIVE] = Cmd_callnative,
    [B_SCR_OP_SWITCH] = Cmd_switch,
    [B_SCR_OP_STUB] = Cmd_stub,
};

static void Cmd_jumpifbyte(void)
{
    CMD_ARGS(u8 comparison, const u8 *bytePtr, u8 value, const u8 *jumpInstr);
}

static void Cmd_callnative(void)
{
    CMD_ARGS(void (*func)(void));
}

static void Cmd_stub(void)
{
}

static void Cmd_switch(void)
{
    CMD_ARGS(u8 battler:7, u8 optional:1, enum Ability ability, u32 flags);
}

void BS_JumpIfX(void)
{
    NATIVE_ARGS(u8 battler, const u16 *table, const u8 *jumpInstr);
}
"""

SELF_TEST_MACROS = """
	.macro jumpifbyte comparison:req, bytePtr:req, value:req, jumpInstr:req
	.byte B_SCR_OP_JUMPIFBYTE
	.byte \\comparison
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \\bytePtr
	.else
	.4byte \\bytePtr
	.endif
	.byte \\value
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \\jumpInstr
	.else
	.4byte \\jumpInstr
	.endif
	.endm

	.macro callnative func:req
	.byte B_SCR_OP_CALLNATIVE
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \\func
	.else
	.4byte \\func
	.endif
	.endm

	.macro switch battler:req optional=0, flags:req
	.byte B_SCR_OP_SWITCH
	.byte \\battler | (\\optional << 7) @ two bitfields
	.2byte ABILITY_NONE
	.4byte \\flags
	.endm

	.macro jumpifx battler:req, table:req, jumpInstr:req, flag=0
	.if \\flag != 0
	switch \\battler, flags=\\flag
	.endif
	callnative BS_JumpIfX
	.byte \\battler
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \\table
	.else
	.4byte \\table
	.endif
	.ifdef NATIVE_POINTER_SIZE_8
	.8byte \\jumpInstr
	.else
	.4byte \\jumpInstr
	.endif
	.endm

	.macro jumpifnotx battler:req, jumpInstr:req
	jumpifx \\battler, sTable, 1f
	jumpifbyte CMP_EQUAL, sZero, 0, \\jumpInstr
1:
	.endm

	.macro unused
	callnative BS_NotDefined
	.endm

	.macro stub instr:req
	.byte B_SCR_OP_STUB
	.4byte \\instr
	.endm
"""


def self_test():
    failures = []

    def expect(name, macro_text, c_text, wanted, z_text=None):
        try:
            problems = check(Source("macros", macro_text), Source("c", c_text),
                             None if z_text is None else Source("z.c", z_text))[0]
        except CheckError as error:
            problems = ["CheckError: %s" % error]
        if not problems_match(wanted, problems):
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, problems))

    expect("consistent", SELF_TEST_MACROS, SELF_TEST_C, [])
    # Five pointer operands, each on a '.8byte' and a '.4byte' line; the
    # switch's flags is the one integer word.
    lines = check(Source("macros", SELF_TEST_MACROS), Source("c", SELF_TEST_C))[2]
    if len(lines) != 10:
        failures.append("pointer lines: expected 10, got %r" % (lines,))
    unguarded = SELF_TEST_MACROS.replace(
        ".ifdef NATIVE_POINTER_SIZE_8\n\t.8byte \\jumpInstr\n\t.else\n\t.4byte \\jumpInstr\n\t.endif\n\t.endm\n\n\t.macro callnative",
        ".4byte \\jumpInstr\n\t.endm\n\n\t.macro callnative", 1)
    expect("pointer left at 4 bytes", unguarded, SELF_TEST_C,
           ["jumpifbyte (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 8-byte pointers: the macro writes [1, 8, 1, 4]",
            "jumpifnotx (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 8-byte pointers"])
    expect("integer widened", SELF_TEST_MACROS.replace("\t.4byte \\flags", "\t.8byte \\flags"), SELF_TEST_C,
           # the two bitfields are one byte
           ["switch (B_SCR_OP_SWITCH, Cmd_switch), 4-byte pointers: the macro writes [1, 2, 8], the C reads [1, 2, 4]",
            "switch (B_SCR_OP_SWITCH, Cmd_switch), 8-byte pointers",
            "jumpifx (B_SCR_OP_SWITCH, Cmd_switch), 4-byte pointers",
            "jumpifx (B_SCR_OP_SWITCH, Cmd_switch), 8-byte pointers",
            "jumpifnotx (B_SCR_OP_SWITCH, Cmd_switch), 4-byte pointers",
            "jumpifnotx (B_SCR_OP_SWITCH, Cmd_switch), 8-byte pointers"])
    expect("operand missing", SELF_TEST_MACROS.replace("\t.byte \\value\n", ""), SELF_TEST_C,
           ["jumpifbyte (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 4-byte pointers: the macro writes [1, 4, 4]",
            "jumpifbyte (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 8-byte pointers",
            "jumpifnotx (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 4-byte pointers",
            "jumpifnotx (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 8-byte pointers"])
    expect("C member changed", SELF_TEST_MACROS, SELF_TEST_C.replace("u8 value, const u8 *jumpInstr", "u16 value, const u8 *jumpInstr"),
           ["jumpifbyte (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 4-byte pointers: the macro writes [1, 4, 1, 4], the C reads [1, 4, 2, 4]",
            "jumpifbyte (B_SCR_OP_JUMPIFBYTE, Cmd_jumpifbyte), 8-byte pointers",
            "jumpifnotx", "jumpifnotx"])
    expect("unknown type", SELF_TEST_MACROS, SELF_TEST_C.replace("u32 flags", "enum Weather flags"),
           ["CheckError: c: Cmd_switch: unknown member type 'enum Weather'"])
    expect("opcode not in the table: skipped", SELF_TEST_MACROS.replace("B_SCR_OP_SWITCH\n\t.byte", "B_SCR_OP_SWAP\n\t.byte")
           .replace("\t.4byte \\flags", "\t.8byte \\flags"), SELF_TEST_C, [])
    # A macro that writes operands but no command cannot be checked alone.
    expect("operands without a command", SELF_TEST_MACROS + "\n\t.macro word v\n\t.4byte \\v\n\t.endm\n",
           SELF_TEST_C, ["CheckError: macros:68: an operand before any command"])
    # A keyword argument can name the callnative's function.
    keyword = ("\n\t.macro callnativeof extra=0, func=BS_NotDefined\n\tcallnative \\func\n\t.endm\n"
               "\n\t.macro jumpifxkw battler:req\n\tcallnativeof func=BS_JumpIfX\n\t.byte \\battler\n"
               "\t.4byte sTable\n\t.4byte sTarget\n\t.endm\n")
    expect("keyword argument", SELF_TEST_MACROS + keyword, SELF_TEST_C,
           ["jumpifxkw (B_SCR_OP_CALLNATIVE, BS_JumpIfX), 8-byte pointers: the macro writes [8, 1, 4, 4]"])
    # The operands after a callnative of an undefined function are not checked.
    expect("undefined native", SELF_TEST_MACROS.replace("\tcallnative BS_NotDefined\n", "\tcallnative BS_NotDefined\n\t.byte 1\n"),
           SELF_TEST_C, [])
    expect("unknown statement", SELF_TEST_MACROS.replace("1:\n", "\tnosuchmacro\n"), SELF_TEST_C,
           ["CheckError: macros:55: macro jumpifnotx: cannot expand 'nosuchmacro'"])
    # Z_EFFECT_BS_LENGTH: the setzeffect callnative is 5 and 9 bytes.
    z_macros = SELF_TEST_MACROS + "\n\t.macro setzeffect\n\tcallnative BS_SetZEffect\n\t.endm\n"
    z_c = SELF_TEST_C + "\nvoid BS_SetZEffect(void)\n{\n    NATIVE_ARGS();\n}\n"
    z_file = ("#if B_SOME_CONFIG\n#define OTHER 1\n#endif\n"
              "#if PLATFORM_NATIVE\n#if __SIZEOF_POINTER__ != 8\n#define Z_EFFECT_BS_LENGTH  5\n"
              "#else\n#define Z_EFFECT_BS_LENGTH  9 // the 64-bit length\n#endif\n"
              "#else\n#define Z_EFFECT_BS_LENGTH  5\n#endif\n")
    for name, text, wanted in (
            ("Z_EFFECT_BS_LENGTH right", z_file, []),
            ("Z_EFFECT_BS_LENGTH 5 at 64", z_file.replace("LENGTH  9", "LENGTH  5"),
             ["Z_EFFECT_BS_LENGTH is 5 with 8-byte pointers; the setzeffect command is 9 bytes"]),
            ("Z_EFFECT_BS_LENGTH unguarded", "#define Z_EFFECT_BS_LENGTH  5\n",
             ["Z_EFFECT_BS_LENGTH is 5 with 8-byte pointers; the setzeffect command is 9 bytes"]),
            ("Z_EFFECT_BS_LENGTH 9 on the GBA side only", z_file.replace("!= 8", "== 8"),
             ["Z_EFFECT_BS_LENGTH is 9 with 4-byte pointers; the setzeffect command is 5 bytes",
              "Z_EFFECT_BS_LENGTH is 5 with 8-byte pointers; the setzeffect command is 9 bytes"]),
            ("Z_EFFECT_BS_LENGTH with #ifndef, #elif and logic",
             "#ifndef PLATFORM_NATIVE\n#define Z_EFFECT_BS_LENGTH 7\n"
             "#elif PLATFORM_NATIVE && __SIZEOF_POINTER__ == 8\n#define Z_EFFECT_BS_LENGTH 9\n"
             "#elif !(__SIZEOF_POINTER__ == 8) || 0\n#define Z_EFFECT_BS_LENGTH 5\n#endif\n", []),
            ("Z_EFFECT_BS_LENGTH missing", "#define OTHER 5\n", ["CheckError: z.c: no definition of Z_EFFECT_BS_LENGTH for 4-byte pointers (expected 5)"]),
            ("Z_EFFECT_BS_LENGTH under an unknown condition", "#if B_SOME_CONFIG\n#define Z_EFFECT_BS_LENGTH 5\n#endif\n",
             ["CheckError: z.c: the condition 'B_SOME_CONFIG' depends on B_SOME_CONFIG"])):
        expect(name, z_macros, z_c, wanted, text)
    if failures:
        for failure in failures:
            sys.stderr.write("check_battle_script_operands self-test: %s\n" % failure)
        return 1
    return 0


USAGE = "usage: check_battle_script_operands.py [--table] MACROS C_FILE Z_MOVE_FILE | --self-test\n"


def main(argv):
    args = argv[1:]
    if args == ["--self-test"]:
        return self_test()
    printing = args[:1] == ["--table"]
    if printing:
        args = args[1:]
    if len(args) != 3:
        sys.stderr.write(USAGE)
        return 2
    try:
        sources = []
        for path in args:
            with open(path) as handle:
                sources.append(Source(path, handle.read()))
        problems, rows, pointer_lines = check(*sources)
    except (CheckError, OSError) as error:   # an OSError names the file it could not open
        sys.stderr.write("check_battle_script_operands: %s\n" % error)
        return 1
    if printing:
        print_table(rows, pointer_lines)
    for problem in problems:
        sys.stderr.write("check_battle_script_operands: %s\n" % problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
