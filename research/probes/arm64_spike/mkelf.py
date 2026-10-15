"""mkelf.py RAW OUT: wrap a raw AArch64 code blob (entry at offset 0, no
relocations) in a static ELF64 executable loaded at 0x400000, for qemu-aarch64."""
import struct, sys
code = open(sys.argv[1], "rb").read()
base, hdr = 0x400000, 64 + 56
ehdr = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + b"\0" * 9, 2, 183, 1,
                   base + hdr, 64, 0, 0, 64, 56, 1, 0, 0, 0)
phdr = struct.pack("<IIQQQQQQ", 1, 7, 0, base, base, hdr + len(code), hdr + len(code), 0x10000)
open(sys.argv[2], "wb").write(ehdr + phdr + code)
