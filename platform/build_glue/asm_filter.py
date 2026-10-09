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
bytes (the rules are below, per kind of input, '--input'). Without it, or
with '--pointer-size 4', the output is the same as before the option
existed.

Reads stdin, writes stdout. Run as
'python3 -I asm_filter.py [--pointer-size 4|8] [--input KIND]'.
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


# --------------------------------------------------------------------------
# 8-byte pointers (--pointer-size 8)
#
# At 64 bits the C structs and arrays that read the data have 8-byte,
# 8-aligned pointers. The macros that emit such structs have guarded 64-bit
# variants (asm/macros/m4a.inc, music_voice.inc, map.inc, data/specials.inc,
# data/script_cmd_table.inc); these rules lay out the words no macro emits,
# per kind of input (--input). They act only on statements outside macro
# definitions and only on the '.4byte's the rules above have produced; every
# other '.4byte' (script and song bytecode, read a byte at a time) stays.
#
#   data     Hand-written tables of C pointers, named in WIDENED_TABLES:
#            aligned to 8, and each '.4byte' up to the next label is
#            '.8byte'.
#   mapjson  mapjson's output (data/maps.s, data/map_events.s): every label
#            is aligned to 8, which also gives struct MapHeader and struct
#            MapLayout their tail padding, and at the end. Every '.4byte' is
#            a pointer ('.8byte', NULL included) except the first two after
#            a '*_Layout' label (width, height) and the first after a
#            '*_MapConnections' label (count, then 4 bytes of padding).
#   song     A mid2agb song: after the song's last label comes its header,
#            struct SongHeader: four bytes, then the voicegroup and one word
#            per track. Aligned to 8, four bytes of padding after the four
#            bytes, and 8-byte words. Songs have no macros.

INPUT_KINDS = ("data", "mapjson", "song")
WIDENED_TABLES = frozenset((
    "gSpecialVars",                     # data/event_scripts.s
    "gStdScripts",                      # data/event_scripts.s
    "gBattlescriptsForUsingItem",       # data/battle_scripts_2.s
    "gBattlescriptsForSafariActions",   # data/battle_scripts_2.s
    "gContestAI_ScriptsTable",          # data/contest_ai_scripts.s
    "gFieldEffectScriptPointers",       # data/field_effect_scripts.s
    "gMysteryEventScriptCmdTable",      # data/mystery_event_script_cmd_table.s
))
# mapjson labels whose first words are not pointers: (label suffix, how
# many 4-byte words, the padding before the first pointer).
MAPJSON_SCALAR_WORDS = (
    ("_Layout", 2, None),                   # struct MapLayout: s32 width, height
    ("_MapConnections", 1, ".space 4"),     # struct MapConnections: s32 count
)
SONG_HEADER_BYTES = 4      # trackCount, blockCount, priority, reverb
POINTER_ALIGN = ".balign 8"
POINTER_WORD = ".8byte"
SONG_HEADER_PADDING = ".space 4"

LABEL = re.compile(r"\s*([A-Za-z_.$][\w.$]*)(::?)")
DIRECTIVE_NAME = re.compile(r"\s*(\.[\w.]+)")


class FilterError(Exception):
    """Input the 8-byte rules do not recognise; the build must stop."""


def parse_statement(statement):
    """(labels defined, directive or None, start and end of the directive)."""
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
        return labels, None, position, position
    return labels, match.group(1), match.start(1), match.end(1)


def operand_count(statement, end):
    """Number of comma-separated operands after the directive ending at end."""
    return len([operand for operand in statement[end:].split(",") if operand.strip()])


def with_directive(statement, start, end, directive):
    """The statement with the directive at start..end replaced."""
    return statement[:start] + directive + statement[end:]


def with_prefix(statements, index, directive):
    """Put directive before statement index: on a line of its own if the
    statement starts the line, else as a statement before it."""
    if index == 0:
        statements[0] = "\t%s\n%s" % (directive, statements[0])
    else:
        statements[index] = "\t%s ;%s" % (directive, statements[index])


def rewrite_statements(lines, visit):
    """Yield the lines, with visit(labels, directive, statement, start, end)
    applied to each statement outside a macro definition; it returns the
    statement, possibly rewritten, and a directive to put before it or None.
    A line with no label, '.4byte' or macro boundary passes unparsed."""
    depth = 0
    for line in lines:
        if ":" not in line and ".4byte" not in line and "macro" not in line and "endm" not in line:
            yield line
            continue
        statements = split_statements(line)
        prefixes = {}
        for index, statement in enumerate(statements):
            labels, directive, start, end = parse_statement(statement)
            if directive == ".macro":
                depth += 1
            elif directive == ".endm":
                depth = max(depth - 1, 0)
            elif depth == 0:
                statements[index], prefix = visit(labels, directive, statement, start, end)
                if prefix:
                    prefixes[index] = prefix
        for index, prefix in prefixes.items():
            with_prefix(statements, index, prefix)
        yield ";".join(statements)


def widen_tables(lines):
    """The 'data' rule."""
    widening = False

    def visit(labels, directive, statement, start, end):
        nonlocal widening
        prefix = None
        if labels:
            widening = labels[-1] in WIDENED_TABLES
            prefix = POINTER_ALIGN if widening else None
        if widening and directive == ".4byte":
            statement = with_directive(statement, start, end, POINTER_WORD)
        return statement, prefix

    return rewrite_statements(lines, visit)


def widen_mapjson(lines):
    """The 'mapjson' rule."""
    scalars = 0
    padding = None

    def visit(labels, directive, statement, start, end):
        nonlocal scalars, padding
        prefix = None
        if labels:
            prefix = POINTER_ALIGN
            scalars, padding = 0, None
            for suffix, count, pad in MAPJSON_SCALAR_WORDS:
                if labels[-1].endswith(suffix):
                    scalars, padding = count, pad
        if directive == ".4byte" and scalars:
            if operand_count(statement, end) != 1:
                raise FilterError("mapjson: one word per statement expected, got %r" % statement.strip())
            scalars -= 1
        elif directive == ".4byte":
            statement = with_directive(statement, start, end, POINTER_WORD)
            if padding and prefix is None:
                prefix, padding = padding, None
        return statement, prefix

    yield from rewrite_statements(lines, visit)
    yield "\t%s\n" % POINTER_ALIGN


def widen_song(lines):
    """The 'song' rule on a whole (filtered) song file."""
    lines = [split_statements(line) for line in lines]
    header = None
    for number, statements in enumerate(lines):
        if any(parse_statement(statement)[0] for statement in statements):
            header = number
    if header is None:
        raise FilterError("song: no label, so no header")
    for statements in lines[:header]:
        yield ";".join(statements)
    header_bytes = 0
    widened = False
    for number, statements in enumerate(lines[header:]):
        prefixes = {}
        for index, statement in enumerate(statements):
            labels, directive, start, end = parse_statement(statement)
            if labels and number == 0:
                prefixes[index] = POINTER_ALIGN
            elif labels:
                raise FilterError("song header: unexpected %r" % statement.strip())
            if directive == ".byte" and not widened:
                header_bytes += operand_count(statement, end)
            elif directive == ".4byte":
                statements[index] = with_directive(statement, start, end, POINTER_WORD)
                if not widened:
                    if header_bytes != SONG_HEADER_BYTES:
                        raise FilterError("song header: %d bytes before the first word, not %d"
                                          % (header_bytes, SONG_HEADER_BYTES))
                    prefixes[index] = SONG_HEADER_PADDING
                    widened = True
            elif directive not in (None, ".end"):
                raise FilterError("song header: unexpected %r" % statement.strip())
        for index, directive in prefixes.items():
            with_prefix(statements, index, directive)
        yield ";".join(statements)
    if not widened:
        raise FilterError("song header: no word after the song's last label")


WIDEN = {"data": widen_tables, "mapjson": widen_mapjson, "song": widen_song}


def filter_program(lines, pointer_size=4, input_kind="data"):
    """Yield the filtered lines of a whole input."""
    filtered = (filter_line(line) for line in lines)
    if pointer_size == 8:
        return WIDEN[input_kind](filtered)
    return filtered


SONG = """\
\t.align\t2
mus_x_1:
\t.byte\tVOICE , 1
\t.word\tmus_x_1_B1\t@ GOTO target, bytecode
mus_x_1_B1:
\t.byte\tFINE
\t.align\t2

mus_x:
\t.byte\t1\t@ NumTrks
\t.byte\t0\t@ NumBlks
\t.byte\tmus_x_pri\t@ Priority
\t.byte\tmus_x_rev\t@ Reverb.

\t.word\tmus_x_grp

\t.word\tmus_x_1

\t.end
"""

SONG_8 = """\
\t.p2align\t2
mus_x_1:
\t.byte\tVOICE , 1
\t.4byte\tmus_x_1_B1
mus_x_1_B1:
\t.byte\tFINE
\t.p2align\t2

\t.balign 8
mus_x:
\t.byte\t1
\t.byte\t0
\t.byte\tmus_x_pri
\t.byte\tmus_x_rev

\t.space 4
\t.8byte\tmus_x_grp

\t.8byte\tmus_x_1

\t.end
"""

TABLES = """\
	.macro def m
gStdScripts:: .4byte \\m
	.endm
	.align 2
gSpecialVars::
	.4byte gSpecialVar_0x8000
	.4byte gTrainerBattleParameter + 2 @ comment
	.align 2
gStdScripts::
	.4byte Std_ObtainItem
gStdScripts_End::
	.4byte Script_Word
Script:
	.byte 1 ; .word Target
"""

TABLES_8 = """\
	.macro def m
gStdScripts:: .4byte \\m
	.endm
	.p2align 2
	.balign 8
gSpecialVars::
	.8byte gSpecialVar_0x8000
	.8byte gTrainerBattleParameter + 2
	.p2align 2
	.balign 8
gStdScripts::
	.8byte Std_ObtainItem
gStdScripts_End::
	.4byte Script_Word
Script:
	.byte 1 ; .4byte Target
"""

MAPS = """\
	.macro connection direction, offset, map
	.byte \\direction ; .4byte \\offset
	.endm
X_Layout_Border::
	.incbin "border.bin"
	.align 2
X_Layout::
	.4byte 30
	.4byte 20
	.4byte X_Layout_Border
	.4byte NULL
	.byte FALSE
	.2byte 0
	.byte 0
X:
	.4byte X_Layout
	.byte 1
X_MapConnectionsList:
	connection up, 0, MAP_Y
X_MapConnections:
	.4byte 1
	.4byte X_MapConnectionsList
gMapGroups::
	.4byte gMapGroup_A
"""

MAPS_8 = """\
	.macro connection direction, offset, map
	.byte \\direction ; .4byte \\offset
	.endm
	.balign 8
X_Layout_Border::
	.incbin "border.bin"
	.p2align 2
	.balign 8
X_Layout::
	.4byte 30
	.4byte 20
	.8byte X_Layout_Border
	.8byte NULL
	.byte FALSE
	.2byte 0
	.byte 0
	.balign 8
X:
	.8byte X_Layout
	.byte 1
	.balign 8
X_MapConnectionsList:
	connection up, 0, MAP_Y
	.balign 8
X_MapConnections:
	.4byte 1
	.space 4
	.8byte X_MapConnectionsList
	.balign 8
gMapGroups::
	.8byte gMapGroup_A
	.balign 8
"""

# (pointer size, input kind, input, expected output or the FilterError's text)
SELF_TEST_PROGRAMS = (
    (4, "song", SONG, "".join(filter_line(line) for line in SONG.splitlines(True))),
    (8, "data", SONG, "".join(filter_line(line) for line in SONG.splitlines(True))),
    (8, "song", SONG, SONG_8),
    (8, "song", "mus_y: .byte 1, 2, 3, 4 ; .word a, b\n",
     "\t.balign 8\nmus_y: .byte 1, 2, 3, 4 ;\t.space 4 ; .8byte a, b\n"),
    (8, "song", SONG.replace("\t.byte\t0\t@ NumBlks\n", ""), FilterError("3 bytes before the first word, not 4")),
    (8, "song", SONG.replace("\t.end\n", "\t.byte 1\n"), FilterError("unexpected '.byte 1'")),
    (8, "song", SONG.replace("\t.end\n", "other:\n"), FilterError("no word after the song's last label")),
    (8, "song", "\t.byte 1\n", FilterError("no label")),
    (8, "data", TABLES, TABLES_8),
    (4, "data", TABLES, "".join(filter_line(line) for line in TABLES.splitlines(True))),
    (8, "mapjson", MAPS, MAPS_8),
    (8, "mapjson", "Y_MapConnections: .4byte 2 ; .word L\n",
     "\t.balign 8\nY_MapConnections: .4byte 2 ;\t.space 4 ; .8byte L\n\t.balign 8\n"),
    (8, "mapjson", "X_Layout::\n\t.4byte 30, 20\n", FilterError("one word per statement expected")),
)


def self_test():
    failures = 0
    for given, expected in SELF_TEST_CASES:
        actual = filter_line(given)
        if actual != expected:
            failures += 1
            sys.stderr.write("asm_filter self-test: %r\n  expected %r\n  got      %r\n"
                             % (given, expected, actual))
    for pointer_size, input_kind, given, expected in SELF_TEST_PROGRAMS:
        try:
            actual = "".join(filter_program(given.splitlines(True), pointer_size, input_kind))
        except FilterError as error:
            actual = error
        if isinstance(expected, FilterError):
            passed = isinstance(actual, FilterError) and str(expected) in str(actual)
        else:
            passed = actual == expected
        if not passed:
            failures += 1
            sys.stderr.write("asm_filter self-test (--pointer-size %d --input %s): %r\n  expected %r\n  got      %r\n"
                             % (pointer_size, input_kind, given, expected, actual))
    if failures:
        sys.stderr.write("asm_filter self-test: %d of %d failed\n"
                         % (failures, len(SELF_TEST_CASES) + len(SELF_TEST_PROGRAMS)))
        return 1
    return 0


USAGE = ("usage: asm_filter.py [--pointer-size 4|8] [--input %s] < input > output | --self-test\n"
         % "|".join(INPUT_KINDS))


def main(argv):
    args = argv[1:]
    if args == ["--self-test"]:
        return self_test()
    options = {"--pointer-size": "4", "--input": "data"}
    choices = {"--pointer-size": ("4", "8"), "--input": INPUT_KINDS}
    while len(args) >= 2 and args[0] in options and args[1] in choices[args[0]]:
        options[args[0]] = args[1]
        args = args[2:]
    if args:
        sys.stderr.write(USAGE)
        return 2
    try:
        for line in filter_program(sys.stdin, int(options["--pointer-size"]), options["--input"]):
            sys.stdout.write(line)
    except FilterError as error:
        sys.stderr.write("asm_filter: %s\n" % error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
