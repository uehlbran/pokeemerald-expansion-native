# Experiment: GBA address space mirrored in a native process

Date 2026-10-08. Question: can a native Linux process map the GBA memory
regions at their real addresses, so that expansion's `include/gba/io_reg.h`
(`REG_BASE 0x4000000`) and `include/gba/defines.h` (`EWRAM_START
0x02000000`, `VRAM 0x06000000`, ...) compile and run without modification?

Program: `mmap` with `MAP_FIXED_NOREPLACE` of EWRAM (0x02000000, 256 KiB),
IWRAM (0x03000000, 32 KiB), I/O (0x04000000, 4 KiB), the mGBA debug register
page (0x04FFF000, 4 KiB), PLTT (0x05000000, 1 KiB), VRAM (0x06000000,
96 KiB), OAM (0x07000000, 1 KiB) and flash (0x0E000000, 128 KiB), then a
volatile 16-bit write and read at 0x04000000, then `SDL_Init(0)`.

Build: `gcc -m32|-m64 -no-pie -Wl,-Ttext-segment=0x40000000`, with and
without `-fsanitize=address,undefined`, linked against SDL2.

| Build | Result |
|---|---|
| 32-bit | all regions mapped, read back 0x1234, `main` at 0x40001196 |
| 32-bit ASan+UBSan | same |
| 64-bit | same, `sizeof(void *) == 8` |
| 64-bit ASan+UBSan | same |
| 32-bit under Valgrind | Valgrind itself fails at startup ("a function redirection which is mandatory for this platform-tool combination"): the i386 libc debug symbols are not installed. Not needed, since the i386 sanitizer runtimes work. |

Why the text segment moves: a 32-bit non-PIE executable normally loads at
0x08048000, inside the GBA ROM window (0x08000000-0x09FFFFFF), and a
64-bit one at 0x00400000, which a 30 MiB data segment would extend into
EWRAM at 0x02000000. Linking at 0x40000000 keeps the native image clear of
every GBA region on both bit widths. On 32-bit, AddressSanitizer's shadow
occupies 0x20000000-0x3FFFFFFF and treats 0x00000000-0x1FFFFFFF as
application memory, so the GBA regions sit in application memory and the
image sits in high memory; on 64-bit the shadow starts at 0x00007FFF8000
and everything below is application memory.

Consequence for the design: see `../RESEARCH.md`, "Memory model".
