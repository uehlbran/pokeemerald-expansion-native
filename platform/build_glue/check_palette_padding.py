#!/usr/bin/env python3
"""check_palette_padding.py: guards the zero-padding of .gbapal files (gbagfx_pad.sh).

The native build pads every INCGFX ".gbapal" that is not a whole number of 32-byte banks
(many species palettes have 11 to 15 colours; LoadSpritePalette copies a whole bank).
That is only right where the game does not take the array's size. For every INCGFX
".gbapal" in src/, include/ and test/ whose palette file is short (its size is not a
multiple of 32 bytes):

  * if the symbol's size is used, i.e. its declaration has an explicit size
    (`name[16] = INCGFX...`), the code applies sizeof or ARRAY_COUNT to it, or it sits in
    an initializer without a declaration of its own, the file must be listed in
    unpadded_palettes.txt;
  * and every listed file must exist, be short, and be used that way.

Exit 0 and no output when consistent; otherwise one line per problem with the file and the
symbol, exit 1. Usage: check_palette_padding.py [--root DIR] | --self-test
"""
import os
import re
import struct
import sys
import tempfile

BANK = 32                  # bytes: 16 colours, LoadSpritePalette's unit
GBA_BYTES_PER_COLOUR = 2   # BGR555
PNG_BYTES_PER_COLOUR = 3   # RGB in a PLTE chunk
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHUNK_HEADER = 8       # 4-byte length and 4-byte type
PNG_CHUNK_CRC = 4
SOURCE_DIRS = ("src", "include", "test")
LIST_NAME = "unpadded_palettes.txt"

INCGFX = re.compile(r'INCGFX(?:_[A-Z0-9]+)?\s*\(\s*"([^"]+\.(?:pal|png))"\s*,\s*"\.gbapal"\s*[,)]')
DECL = re.compile(r'(\w+)\s*(\[[^\]]*\])+\s*=\s*$')


class PaletteError(Exception):
    """A palette source file gbagfx would reject; the build must say which."""


def palette_bytes(root, path):
    """Size of the .gbapal gbagfx makes from `path` (a JASC .pal or an indexed .png), or None
    if the file does not exist (it may be generated later). A file that exists but is not
    a palette raises PaletteError."""
    full = os.path.join(root, path)
    if not os.path.exists(full):
        return None
    if path.endswith(".pal"):
        with open(full, "r", errors="replace") as handle:
            lines = handle.read().split("\n")
        try:
            return int(lines[2].strip()) * GBA_BYTES_PER_COLOUR  # JASC-PAL, 0100, then the colour count
        except (IndexError, ValueError):
            raise PaletteError("%s: not a JASC palette (line 3 should be the colour count)" % path)
    with open(full, "rb") as handle:
        data = handle.read()
    if data[:len(PNG_SIGNATURE)] != PNG_SIGNATURE:
        raise PaletteError("%s: not a PNG file" % path)
    position = len(PNG_SIGNATURE)
    while position + PNG_CHUNK_HEADER <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + PNG_CHUNK_HEADER])
        if kind == b"PLTE":
            return length // PNG_BYTES_PER_COLOUR * GBA_BYTES_PER_COLOUR
        position += PNG_CHUNK_HEADER + PNG_CHUNK_CRC + length
    raise PaletteError("%s: PNG has no PLTE chunk (not an indexed image)" % path)


def read_list(root):
    entries = {}
    path = os.path.join(root, "platform", "build_glue", LIST_NAME)
    with open(path, "r") as handle:
        for line in handle:
            text = line.split("#", 1)[0].strip()
            if text:
                entries[text] = line.strip()
    return entries


def source_files(root):
    for directory in SOURCE_DIRS:
        for base, _, names in os.walk(os.path.join(root, directory)):
            for name in names:
                if name.endswith((".c", ".h")):
                    yield os.path.join(base, name)


def scan(root):
    """[(source path, palette path, symbol or None, explicit_size)] and the concatenated code."""
    found = []
    texts = []
    for file in source_files(root):
        with open(file, "r", errors="replace") as handle:
            text = handle.read()
        texts.append(text)
        for line in text.split("\n"):
            stripped = line.lstrip()
            if stripped.startswith("//") or "INCGFX" not in line:
                continue
            for match in INCGFX.finditer(line):
                before = line[:match.start()].rstrip()
                declaration = DECL.search(before)
                symbol = declaration.group(1) if declaration else None
                explicit = False
                if declaration:
                    brackets = re.search(r'((?:\[[^\]]*\])+)\s*=\s*$', before).group(1)
                    explicit = any(b.strip("[]").strip() for b in re.findall(r'\[[^\]]*\]', brackets))
                found.append((os.path.relpath(file, root), match.group(1), symbol, explicit))
    return found, "\n".join(texts)


SIZE_TAKEN = re.compile(r'(?:sizeof\s*\(?\s*|ARRAY_COUNT\s*\(\s*)(\w+)')


def sized_symbols(code):
    """Every identifier the code applies sizeof or ARRAY_COUNT to."""
    return set(SIZE_TAKEN.findall(code))


def sized_use(symbol, explicit, sized):
    if symbol is None:
        return "in an initializer without a declaration of its own"
    if explicit:
        return "declared with an explicit size"
    if symbol in sized:
        return "its size is taken (sizeof or ARRAY_COUNT)"
    return None


def check(root):
    listed = read_list(root)
    found, code = scan(root)
    sized = sized_symbols(code)
    problems = []
    sizes = {}
    used = {}
    for source, palette, symbol, explicit in found:
        if palette not in sizes:
            sizes[palette] = palette_bytes(root, palette)
        size = sizes[palette]
        if size is None or size % BANK == 0:
            continue
        reason = sized_use(symbol, explicit, sized)
        if reason:
            used[palette] = (source, symbol, reason)
            if palette not in listed:
                problems.append("%s: %s (%s) is %d bytes and %s, but is not in %s" %
                                (palette, symbol or "(initializer)", source, size, reason, LIST_NAME))
    for palette in listed:
        size = palette_bytes(root, palette)
        if size is None:
            problems.append("%s: listed in %s but not found" % (palette, LIST_NAME))
        elif size % BANK == 0:
            problems.append("%s: listed in %s but is %d bytes, a whole number of banks" % (palette, LIST_NAME, size))
        elif palette not in used:
            problems.append("%s: listed in %s but no code takes its size" % (palette, LIST_NAME))
    return problems


def expect(condition, message):
    """A self-test check that still runs under python -O."""
    if not condition:
        raise AssertionError(message)


def self_test():
    cases = [
        ("static const u16 sPal[] = INCGFX_U16(\"a.pal\", \".gbapal\");", "sPal", False),
        ("static const u16 sPal[16] = INCGFX_U16(\"a.pal\", \".gbapal\");", "sPal", True),
        ("static const u16 sPal[][2] = INCGFX_U16(\"a.pal\", \".gbapal\");", "sPal", True),
        ("    INCGFX_U16(\"a.pal\", \".gbapal\"),", None, False),
    ]
    for line, symbol, explicit in cases:
        match = INCGFX.search(line)
        expect(match, line)
        before = line[:match.start()].rstrip()
        declaration = DECL.search(before)
        expect((declaration.group(1) if declaration else None) == symbol, line)
    expect(sized_use(None, False, set()) is not None, "an initializer without a declaration is a sized use")
    expect(sized_use("x", True, set()) is not None, "an explicit array size is a sized use")
    expect(sized_use("x", False, sized_symbols("ARRAY_COUNT(x)")) is not None, "ARRAY_COUNT(x) is a sized use")
    expect(sized_use("x", False, sized_symbols("sizeof(x)")) is not None, "sizeof(x) is a sized use")
    expect(sized_use("x", False, sized_symbols("sizeof x")) is not None, "sizeof x is a sized use")
    expect(sized_use("x", False, sized_symbols("sizeof(xy)")) is None, "sizeof(xy) is not a use of x")
    expect(sized_use("x", False, set()) is None, "a symbol nothing sizes is not a sized use")
    self_test_palette_files()


def self_test_palette_files():
    """palette_bytes on a good JASC file, and loud failures on malformed ones."""
    with tempfile.TemporaryDirectory() as root:
        def write(name, content):
            with open(os.path.join(root, name), "wb") as handle:
                handle.write(content)
        write("good.pal", b"JASC-PAL\r\n0100\r\n12\r\n0 0 0\r\n")
        write("short.pal", b"JASC-PAL\r\n")
        write("text.pal", b"JASC-PAL\r\n0100\r\nmany\r\n")
        write("fake.png", b"not a png at all")
        write("nopalette.png", PNG_SIGNATURE + struct.pack(">I4s", 0, b"IEND") + bytes(PNG_CHUNK_CRC))
        write("indexed.png", PNG_SIGNATURE + struct.pack(">I4s", 6, b"PLTE") + bytes(6 + PNG_CHUNK_CRC))
        expect(palette_bytes(root, "good.pal") == 24, "a 12-colour JASC palette is 24 bytes")
        expect(palette_bytes(root, "indexed.png") == 4, "a 2-colour PLTE is 4 bytes")
        expect(palette_bytes(root, "absent.pal") is None, "a missing file has no size")
        for name in ("short.pal", "text.pal", "fake.png", "nopalette.png"):
            try:
                palette_bytes(root, name)
            except PaletteError:
                continue
            raise AssertionError("%s was accepted as a palette" % name)


def main(argv):
    usage = "usage: check_palette_padding.py [--root DIR] | --self-test"
    if argv == ["--self-test"]:
        self_test()
        return 0
    root = "."
    if len(argv) == 2 and argv[0] == "--root":
        root = argv[1]
    elif argv:
        print(usage, file=sys.stderr)
        return 2
    try:
        problems = check(root)
    except (OSError, PaletteError) as error:
        print("check_palette_padding: " + str(error), file=sys.stderr)
        return 1
    for problem in problems:
        print("check_palette_padding: " + problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
