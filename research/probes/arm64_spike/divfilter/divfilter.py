"""divfilter.py IN.ll OUT.ll: rewrites LLVM IR divisions into calls to helpers
with the GBA libgcc's results (pen_div.c), so that no hardware division sees
a zero divisor or INT_MIN / -1.

Input: clang -S -emit-llvm -O2 -Xclang -disable-llvm-passes (the IR before any
optimisation pass has seen a division whose divisor may be zero, which C calls
undefined and an optimiser may exploit). Every sdiv/udiv/srem/urem on i32 or
i64, with or without `exact`, becomes a call to @pen_{div,mod}_{s,u}{32,64},
except a division by a constant the hardware handles alike everywhere (any
non-zero unsigned constant; a signed constant other than 0 and -1), which is
kept so the compiler can still strength-reduce it (also when the dividend
is a constant expression). Any other division form (vector, i8/i16/i128, a
constant-expression divisor, an unparsed line) is refused:
exit status 1, naming the line. Counts per kind go to stderr."""
import re
import sys

OPS = {"sdiv": ("div", "s"), "srem": ("mod", "s"), "udiv": ("div", "u"), "urem": ("mod", "u")}
ANY_DIV = re.compile(r"\b(sdiv|udiv|srem|urem)\b")
INSN = re.compile(
    r"^(?P<lhs>\s*%[-\w.$]+\s*=\s*)(?P<op>sdiv|udiv|srem|urem)(?:\s+exact)?\s+"
    r"(?P<ty>i32|i64)\s+(?P<a>%[-\w.$]+|-?\d+|undef|poison),\s*(?P<b>%[-\w.$]+|-?\d+|undef|poison)"
    r"(?P<rest>(?:,\s*!.*)?)$")

CONST_DIVISOR = re.compile(
    r"^\s*%[-\w.$]+\s*=\s*(?P<op>sdiv|udiv|srem|urem)(?:\s+exact)?\s+(?P<ty>i32|i64)\s+.*,\s*(?P<b>-?\d+)(?:,\s*!.*)?$")


def keep(op, divisor):
    if not re.fullmatch(r"-?\d+", divisor):
        return False
    value = int(divisor)
    if value == 0:
        return False
    return not (op[0] == "s" and value == -1)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    out, used, counts, refused = [], set(), {}, []
    for number, line in enumerate(open(src), 1):
        text = line.rstrip("\n")
        if not ANY_DIV.search(text) or text.lstrip().startswith(";") or text.startswith("declare") \
                or text.startswith("define") or text.startswith("source_filename") or text.startswith("!"):
            out.append(text)
            continue
        m = INSN.match(text)
        c = None if m else CONST_DIVISOR.match(text)
        if c and keep(c["op"], c["b"]):
            # A constant-expression dividend over a safe constant divisor.
            key = f"{c['op']} {c['ty']} kept"
            counts[key] = counts.get(key, 0) + 1
            out.append(text)
            continue
        if not m:
            # A name or string merely containing the word is not a division.
            if re.search(r"=\s*(sdiv|udiv|srem|urem)\b|\b(sdiv|udiv|srem|urem)\s*\(|\b(sdiv|udiv|srem|urem)\s+(exact\s+)?<", text):
                refused.append(f"{src}:{number}: {text.strip()}")
            out.append(text)
            continue
        op, ty, b = m["op"], m["ty"], m["b"]
        key = f"{op} {ty}"
        if keep(op, b):
            counts[key + " kept"] = counts.get(key + " kept", 0) + 1
            out.append(text)
            continue
        kind, sign = OPS[op]
        name = f"pen_{kind}_{sign}{ty[1:]}"
        used.add((name, ty))
        counts[key + " rewritten"] = counts.get(key + " rewritten", 0) + 1
        out.append(f"{m['lhs']}call {ty} @{name}({ty} {m['a']}, {ty} {b}){m['rest']}")
    for name, ty in sorted(used):
        if not any(l.startswith(("declare", "define")) and f"@{name}(" in l for l in out):
            out.append(f"declare {ty} @{name}({ty}, {ty}) nounwind willreturn memory(none)")
    open(dst, "w").write("\n".join(out) + "\n")
    for key in sorted(counts):
        print(f"divfilter {src}: {key}: {counts[key]}", file=sys.stderr)
    if not counts:
        print(f"divfilter {src}: no divisions", file=sys.stderr)
    for r in refused:
        print(f"divfilter REFUSED {r}", file=sys.stderr)
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
