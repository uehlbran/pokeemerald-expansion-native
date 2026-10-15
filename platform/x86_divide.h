/*
 * x86_divide.h: whether the x86-64 instruction at an address is a DIV or an
 * IDIV, the instructions that raise a divide error.
 *
 * host_macos.c asks it about a SIGFPE's program counter: macOS gives a SIGFPE
 * that kill or raise sends the same si_code as a divide error
 * (docs/DECISIONS.md, "On macOS a SIGFPE sent by kill looks like a divide
 * error"). It is a pure function of the bytes, so the Linux gate tests it
 * (test/test_x86_divide.c, test/posix/test_x86_divide_posix.c) although only
 * the macOS build calls it. The encoding is x86-64's at every build width:
 * 40h-4Fh are REX prefixes here, as on the Mac, where they are INC and DEC in
 * 32-bit mode.
 */
#ifndef PLATFORM_X86_DIVIDE_H
#define PLATFORM_X86_DIVIDE_H

#include <stdbool.h>

/* True when the instruction at code is F6 or F7 (DIV or IDIV of r/m8, or of
 * r/m16/32/64) with 6 or 7 in its ModRM byte's reg field, after up to 14
 * legacy prefixes (segment overrides, operand and address size, LOCK, REPNE,
 * REP; an instruction is at most 15 bytes) and one REX prefix. Reads the
 * prefixes and the opcode, and the ModRM byte only after an F6 or F7, so a
 * one-byte instruction may end the mapped memory. code is not NULL. */
bool x86_divide_at(const unsigned char *code);

#endif /* PLATFORM_X86_DIVIDE_H */
