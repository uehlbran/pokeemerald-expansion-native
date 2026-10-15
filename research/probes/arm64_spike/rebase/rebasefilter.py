"""rebasefilter.py IN.ll OUT.ll: the window model's pointer rule, as a rewrite
of clang's IR before optimisation (as divfilter.py does for division).

The window: the GBA regions and the image at WINDOW_BASE + their Linux
addresses (WINDOW_BASE = 2^32 here, the arm64 page zero's end). In the game's
code every integer made from a pointer is that pointer's offset in the window
(today's Linux address) and every pointer made from an integer below 2^32 is
WINDOW_BASE plus it; integers of 2^32 and above (no valid pointer on arm64
macOS is below 2^32) pass unchanged, so pointers outside the window survive a
round trip through uintptr_t. With WINDOW_BASE = 2^32 a pointer's low 32 bits
are already its offset, so ptrtoint to i32 or narrower needs no rewrite.
  %x = ptrtoint ptr %p to i64  ->  offset if p is in the window, else p
  %x = inttoptr iN %v to ptr   ->  zext to i64; +WINDOW_BASE if 1 .. 2^32-1 (0 is null)
  inttoptr (i64 C to ptr)      ->  inttoptr (i64 C+WINDOW_BASE to ptr), C < 2^32
  ptrtoint (ptr @g to i64)     ->  sub (... , WINDOW_BASE)   (a constant)
Vector forms are refused (exit 1). Counts per kind go to stderr."""
import re
import sys

BASE = 1 << 32
P2I = re.compile(r"^(\s*)(%[-\w.$]+)\s*=\s*ptrtoint\s+ptr\s+(%[-\w.$]+|@[-\w.$]+|null)\s+to\s+i64(.*)$")
I2P = re.compile(r"^(\s*)(%[-\w.$]+)\s*=\s*inttoptr\s+(i\d+)\s+(%[-\w.$]+|-?\d+)\s+to\s+ptr(.*)$")
CONST_I2P = re.compile(r"inttoptr \(i64 (\d+) to ptr\)")
CONST_P2I64 = re.compile(r"ptrtoint \(ptr (@[-\w.$]+) to i64\)")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    counts = {}
    out = []
    serial = [0]

    def fresh():
        serial[0] += 1
        return f"%pen.{serial[0]}"

    def count(kind):
        counts[kind] = counts.get(kind, 0) + 1

    for n, line in enumerate(open(src), 1):
        if re.search(r"(ptrtoint|inttoptr)\s+<", line):
            sys.stderr.write(f"rebasefilter: {src}:{n}: vector form refused: {line}")
            sys.exit(1)

        def fix_const(m):
            c = int(m.group(1))
            if c < BASE:
                count("inttoptr constant")
                return f"inttoptr (i64 {c + BASE} to ptr)"
            return m.group(0)
        line = CONST_I2P.sub(fix_const, line)

        def fix_p2i(m):
            count("ptrtoint constant to i64")
            return f"sub (i64 {m.group(0)}, i64 {BASE})"
        line = CONST_P2I64.sub(fix_p2i, line)

        m = P2I.match(line)
        if m:
            ind, x, p, rest = m.groups()
            count("ptrtoint to i64")
            t = fresh()
            out.append(f"{ind}{t}.t = ptrtoint ptr {p} to i64{rest}\n")
            out.append(f"{ind}{t}.o = sub i64 {t}.t, {BASE}\n")
            out.append(f"{ind}{t}.w = icmp ult i64 {t}.o, {BASE}\n")
            out.append(f"{ind}{x} = select i1 {t}.w, i64 {t}.o, i64 {t}.t\n")
            continue
        m = I2P.match(line)
        if m:
            ind, x, ty, v, rest = m.groups()
            count(f"inttoptr from {ty}")
            t = fresh()
            wide = v
            if ty != "i64":
                out.append(f"{ind}{t}.z = zext {ty} {v} to i64\n")
                wide = f"{t}.z"
            # 0 stays the null pointer; 1 .. 2^32-1 are offsets in the window.
            out.append(f"{ind}{t}.m = add i64 {wide}, -1\n")
            out.append(f"{ind}{t}.w = icmp ult i64 {t}.m, {BASE - 1}\n")
            out.append(f"{ind}{t}.a = add i64 {wide}, {BASE}\n")
            out.append(f"{ind}{t}.s = select i1 {t}.w, i64 {t}.a, i64 {wide}\n")
            out.append(f"{ind}{x} = inttoptr i64 {t}.s to ptr{rest}\n")
            continue
        out.append(line)
    open(dst, "w").writelines(out)
    for k in sorted(counts):
        sys.stderr.write(f"rebasefilter {src}: {k}: {counts[k]}\n")


main()
