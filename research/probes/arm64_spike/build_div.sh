#!/usr/bin/env bash
# build_div.sh: builds divprobe.c for AArch64 Linux with no libc and no linker
# (clang's object, its .text as a raw blob, wrapped by mkelf.py), runs it under
# qemu-aarch64, and shows the instructions it ran.
set -eu
cd "$(dirname "$0")"
mkdir -p out
clang --target=aarch64-linux-gnu -O1 -ffreestanding -fno-pic -fno-stack-protector -nostdlib \
    -fno-builtin -mgeneral-regs-only -c divprobe.c -o out/divprobe.o
if llvm-objdump-18 -r out/divprobe.o | grep -q R_AARCH64; then
    echo "divprobe.o has relocations; mkelf.py cannot wrap it" >&2; exit 1
fi
llvm-objcopy-18 -O binary --only-section=.text.start out/divprobe.o out/divprobe.bin
python3 -I mkelf.py out/divprobe.bin out/divprobe
chmod +x out/divprobe
llvm-objdump-18 -d out/divprobe.o | grep -E "sdiv|udiv|msub" | sort | uniq -c
qemu-aarch64 ./out/divprobe | tee out/divprobe.txt
