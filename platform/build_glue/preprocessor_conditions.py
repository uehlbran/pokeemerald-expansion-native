"""Find the value a C file gives a constant, under its preprocessor conditions.

Stands in for nothing on the GBA: it is build glue. check_battle_script_
operands.py needs one number that C code defines per build configuration
(Z_EFFECT_BS_LENGTH in src/battle_z_move.c, inside '#if PLATFORM_NATIVE' and
'#if __SIZEOF_POINTER__ ...'), and the C preprocessor is not run on that
file for the check. This module reads the '#if', '#ifdef', '#ifndef', '#elif',
'#else', '#endif' and '#define' lines of a file and finds the '#define'
that applies when the names in `values` are defined as given (ISO C11
6.10.1, "Conditional inclusion": the controlling expression is integer
constants, defined(), the operators ! < > <= >= == != && || and
parentheses). A condition that uses any other name, an operator not listed
or a malformed expression is an error, not a guess.

'python3 -I preprocessor_conditions.py --self-test' checks each rule and
refusal and exits non-zero on a failure.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I leaves the script's directory off
from check_support import CheckError  # noqa: E402

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif|define)\b\s*(.*?)\s*(?://.*)?$")
CONDITION_TOKEN = re.compile(r"\s*(defined\s*\(\s*\w+\s*\)|defined\s+\w+|\w+|&&|\|\||==|!=|<=|>=|[()!<>])")
DEFINED = re.compile(r"defined|[\s()]")
NUMBER = re.compile(r"^\d+$")
NAME = re.compile(r"^\w+$")


def tokens_of(condition, values):
    """The condition as a list of integers (names replaced by their values,
    'defined' by 0 or 1) and operator strings."""
    tokens = []
    position = 0
    while position < len(condition.rstrip()):
        match = CONDITION_TOKEN.match(condition, position)
        if not match:
            raise CheckError("cannot read the condition %r at %r (expected integers, names, defined(), "
                             "! < > <= >= == != && || and parentheses)" % (condition, condition[position:].strip()))
        token = match.group(1)
        position = match.end()
        if token.startswith("defined"):
            tokens.append(1 if DEFINED.sub("", token) in values else 0)
        elif NUMBER.match(token):
            tokens.append(int(token))
        elif NAME.match(token):
            if token not in values:
                raise CheckError("the condition %r depends on %s, which is not one of %s"
                                 % (condition, token, ", ".join(sorted(values))))
            tokens.append(values[token])
        else:
            tokens.append(token)
    return tokens


class Expression:
    """A recursive-descent reader of a token list, lowest precedence first:
    ||, &&, == !=, < > <= >=, !, parentheses."""

    COMPARISONS = {"==": lambda a, b: a == b, "!=": lambda a, b: a != b, "<": lambda a, b: a < b,
                   ">": lambda a, b: a > b, "<=": lambda a, b: a <= b, ">=": lambda a, b: a >= b}

    def __init__(self, condition, tokens):
        self.condition, self.tokens, self.position = condition, tokens, 0

    def value(self):
        result = self.logical_or()
        if self.position != len(self.tokens):
            raise self.unreadable("an operator or the end")
        return result

    def unreadable(self, expected):
        return CheckError("cannot read the condition %r: expected %s at token %d" % (self.condition, expected, self.position))

    def accept(self, operators):
        """The operator at the current token if it is one of operators, consumed."""
        if self.position < len(self.tokens) and self.tokens[self.position] in operators:
            self.position += 1
            return self.tokens[self.position - 1]
        return None

    def logical_or(self):
        result = self.logical_and()
        while self.accept(("||",)):
            right = self.logical_and()
            result = int(bool(result) or bool(right))
        return result

    def logical_and(self):
        result = self.comparison(("==", "!="), self.relation)
        while self.accept(("&&",)):
            right = self.comparison(("==", "!="), self.relation)
            result = int(bool(result) and bool(right))
        return result

    def relation(self):
        return self.comparison(("<", ">", "<=", ">="), self.negation)

    def comparison(self, operators, operand):
        result = operand()
        operator = self.accept(operators)
        while operator:
            result = int(self.COMPARISONS[operator](result, operand()))
            operator = self.accept(operators)
        return result

    def negation(self):
        if self.accept(("!",)):
            return int(not self.negation())
        if self.accept(("(",)):
            result = self.logical_or()
            if not self.accept((")",)):
                raise self.unreadable("')'")
            return result
        if self.position < len(self.tokens) and isinstance(self.tokens[self.position], int):
            self.position += 1
            return self.tokens[self.position - 1]
        raise self.unreadable("a number, a name, '!' or '('")


def evaluate(condition, values):
    """A preprocessor condition over the names in values (others: error)."""
    return bool(Expression(condition, tokens_of(condition, values)).value())


def branch_taken(branch, values):
    kind, condition = branch
    if kind in ("if", "elif"):
        return evaluate(condition, values)
    if kind in ("ifdef", "ifndef"):
        return (condition in values) == (kind == "ifdef")
    return True   # else


def definitions(text, name):
    """[(value, enclosing conditions)] for each '#define name value'; the
    conditions are, per open #if, its branches up to the current one."""
    chains = []
    found = []
    for line in text.splitlines():
        match = DIRECTIVE.match(line)
        if not match:
            continue
        word, rest = match.groups()
        if word in ("if", "ifdef", "ifndef"):
            chains.append([(word, rest)])
        elif word in ("elif", "else"):
            chains[-1].append((word, rest))
        elif word == "endif":
            chains.pop()
        elif word == "define" and rest.split()[:1] == [name]:
            found.append((int(rest.split()[1], 0), [list(chain) for chain in chains]))
    return found


def constant_value(text, name, values):
    """The value of '#define name' in text when the names in values are
    defined as given, or None if no definition applies. Only the conditions
    around the definitions are evaluated; CheckError if more than one
    definition applies."""
    active = [value for value, chains in definitions(text, name)
              if all(branch_taken(chain[-1], values) and not any(branch_taken(b, values) for b in chain[:-1])
                     for chain in chains)]
    if len(active) > 1:
        raise CheckError("%s is defined %d times when %s" % (name, len(active), values))
    return active[0] if active else None


# --------------------------------------------------------------------------
# Self-test

SELF_TEST_VALUES = {"A": 1, "B": 0, "N": 8}

SELF_TEST_CONDITIONS = (
    ("1", True), ("0", False), ("A", True), ("B", False), ("N == 8", True), ("N != 8", False),
    ("N < 9", True), ("N > 8", False), ("N <= 8", True), ("N >= 9", False),
    ("defined(A)", True), ("defined A", True), ("defined(C)", False), ("!defined(C)", True),
    ("!A", False), ("!!A", True), ("A && B", False), ("A || B", True), ("B || B", False), ("A && A", True),
    ("(A || B) && !B", True), ("A || B && B", True), ("(A || B) && B", False),
    ("N == 8 && A", True), ("N != 8 || B", False), ("A == 1 == 1", True), ("1 < 2 == 1", True),
)

SELF_TEST_REFUSALS = (
    ("C", "depends on C"),
    ("A &&", "cannot read the condition 'A &&'"),
    ("A B", "expected an operator or the end"),
    ("(A", "expected ')'"),
    ("A)", "expected an operator or the end"),
    ("A + 1", "cannot read the condition 'A + 1' at '+ 1'"),
    ("0x10", "depends on 0x10"),
    ("", "expected a number, a name"),
)

SELF_TEST_FILE = """\
#if B_SOME_CONFIG
#define OTHER 1
#endif
#if PLATFORM_NATIVE
#if __SIZEOF_POINTER__ != 8
#define LENGTH  5
#else
#define LENGTH  9 // the 64-bit length
#endif
#else
#define LENGTH  5
#endif
"""


def self_test():
    failures = []

    def expect(name, actual, wanted):
        if actual != wanted:
            failures.append("%s\n  expected %r\n  got      %r" % (name, wanted, actual))

    for condition, wanted in SELF_TEST_CONDITIONS:
        expect("evaluate %r" % condition, evaluate(condition, SELF_TEST_VALUES), wanted)
    for condition, wanted in SELF_TEST_REFUSALS:
        try:
            evaluate(condition, SELF_TEST_VALUES)
            failures.append("evaluate %r was accepted" % condition)
        except CheckError as error:
            if wanted not in str(error):
                failures.append("evaluate %r: the message is %r, not one with %r" % (condition, str(error), wanted))
    native = {"PLATFORM_NATIVE": 1, "__SIZEOF_POINTER__": 8}
    expect("the 64-bit definition", constant_value(SELF_TEST_FILE, "LENGTH", native), 9)
    expect("the 32-bit definition", constant_value(SELF_TEST_FILE, "LENGTH", dict(native, __SIZEOF_POINTER__=4)), 5)
    expect("#ifdef, #ifndef", (constant_value("#ifndef X\n#define V 7\n#endif\n", "V", {}),
                               constant_value("#ifndef X\n#define V 7\n#endif\n", "V", {"X": 0}),
                               constant_value("#ifdef X\n#define V 7\n#endif\n", "V", {"X": 0})), (7, None, 7))
    expect("no definition", constant_value(SELF_TEST_FILE, "MISSING", native), None)
    ifndef_elif = ("#ifndef PLATFORM_NATIVE\n#define LENGTH 7\n#elif PLATFORM_NATIVE && __SIZEOF_POINTER__ == 8\n"
                   "#define LENGTH 9\n#elif !(__SIZEOF_POINTER__ == 8) || 0\n#define LENGTH 5\n#endif\n")
    expect("#ifndef, #elif and logic", (constant_value(ifndef_elif, "LENGTH", native),
                                        constant_value(ifndef_elif, "LENGTH", dict(native, __SIZEOF_POINTER__=4))),
           (9, 5))
    try:
        constant_value("#define LENGTH 1\n#define LENGTH 2\n", "LENGTH", native)
        failures.append("two definitions were accepted")
    except CheckError as error:
        if "LENGTH is defined 2 times" not in str(error):
            failures.append("two definitions: %s" % error)
    for failure in failures:
        sys.stderr.write("preprocessor_conditions self-test: %s\n" % failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if sys.argv[1:] == ["--self-test"] else 2)
