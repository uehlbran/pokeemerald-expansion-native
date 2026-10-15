/*
 * test_x86_divide.c: which x86-64 byte strings x86_divide_at takes for a DIV
 * or IDIV (x86_divide.h): F6 and F7 with /6 and /7 in every operand form,
 * after each legacy prefix and after REX, and not the other members of their
 * opcode group, nor an instruction that only looks like a prefix. The
 * encodings are the Intel SDM's (vol. 2, 2.1.1, 2.2.1, DIV, IDIV, MUL, NEG,
 * TEST, PUSH) and match GNU as's for the mnemonic in each comment. That a
 * one-byte instruction may end the mapped memory needs a page that ends
 * there: posix/test_x86_divide_posix.c.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "ptest.h"
#include "x86_divide.h"

/* The bytes of one instruction and what follows it, with room after it, so
 * that no case reads past its array whatever the decoder reads. */
#define CODE_SIZE 32u

static bool divide_at(const unsigned char *bytes, size_t length)
{
    unsigned char code[CODE_SIZE];
    memset(code, 0x90, sizeof code); /* NOP after the instruction */
    memcpy(code, bytes, length);
    return x86_divide_at(code);
}

#define DIVIDE_AT(...) divide_at((const unsigned char[]){__VA_ARGS__}, sizeof((const unsigned char[]){__VA_ARGS__}))

PTEST(div_and_idiv_of_every_width_are_divisions)
{
    PT_ASSERT(DIVIDE_AT(0xF6, 0xF1));             /* div cl */
    PT_ASSERT(DIVIDE_AT(0xF6, 0xF9));             /* idiv cl */
    PT_ASSERT(DIVIDE_AT(0x66, 0xF7, 0xF1));       /* div cx */
    PT_ASSERT(DIVIDE_AT(0xF7, 0xF1));             /* div ecx */
    PT_ASSERT(DIVIDE_AT(0xF7, 0xF9));             /* idiv ecx */
    PT_ASSERT(DIVIDE_AT(0x48, 0xF7, 0xF1));       /* div rcx */
    PT_ASSERT(DIVIDE_AT(0x49, 0xF7, 0xF8));       /* idiv r8 */
    PT_ASSERT(DIVIDE_AT(0x41, 0xF6, 0xF0));       /* div r8b */
    PT_ASSERT(DIVIDE_AT(0x4F, 0xF7, 0x3C, 0x24)); /* idiv qword [r12] (REX.WRXB) */
}

PTEST(memory_operands_are_divisions)
{
    PT_ASSERT(DIVIDE_AT(0xF7, 0x33));                         /* div dword [rbx] */
    PT_ASSERT(DIVIDE_AT(0xF7, 0x7D, 0x08));                   /* idiv dword [rbp+8] */
    PT_ASSERT(DIVIDE_AT(0xF7, 0x35, 0x10, 0x00, 0x00, 0x00)); /* div dword [rip+16] */
    PT_ASSERT(DIVIDE_AT(0xF7, 0x34, 0x8B));                   /* div dword [rbx+rcx*4] */
}

PTEST(each_legacy_prefix_is_skipped)
{
    static const unsigned char prefixes[] = {
        0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, /* ES CS SS DS FS GS */
        0x66, 0x67,                         /* operand size, address size */
        0xF0, 0xF2, 0xF3,                   /* LOCK, REPNE, REP */
    };
    for (size_t i = 0; i < sizeof prefixes; i++) {
        PT_ASSERT(DIVIDE_AT(prefixes[i], 0xF7, 0xF1));
        PT_ASSERT(DIVIDE_AT(prefixes[i], 0x48, 0xF7, 0xF9));
        PT_ASSERT(!DIVIDE_AT(prefixes[i], 0xF7, 0xE1)); /* mul */
    }
}

PTEST(prefixes_up_to_the_longest_instruction_are_skipped)
{
    unsigned char code[CODE_SIZE];
    memset(code, 0x90, sizeof code);
    memset(code, 0x66, 14u); /* 14 prefixes and the opcode and ModRM: 16 bytes */
    code[14] = 0xF7;
    code[15] = 0xF1;
    PT_ASSERT(x86_divide_at(code));
    memset(code, 0x90, sizeof code);
    memset(code, 0x66, 15u); /* a 15th prefix leaves no room for the opcode */
    code[15] = 0xF7;
    code[16] = 0xF1;
    PT_ASSERT(!x86_divide_at(code));
}

PTEST(the_rest_of_the_opcode_group_is_not_a_division)
{
    PT_ASSERT(!DIVIDE_AT(0xF7, 0xC1, 0x01, 0x00, 0x00, 0x00)); /* test ecx, 1 (/0) */
    PT_ASSERT(!DIVIDE_AT(0xF7, 0xD1));                         /* not ecx (/2) */
    PT_ASSERT(!DIVIDE_AT(0xF7, 0xD9));                         /* neg ecx (/3) */
    PT_ASSERT(!DIVIDE_AT(0xF7, 0xE1));                         /* mul ecx (/4) */
    PT_ASSERT(!DIVIDE_AT(0xF7, 0xE9));                         /* imul ecx (/5) */
    PT_ASSERT(!DIVIDE_AT(0xF6, 0xE1));                         /* mul cl */
    PT_ASSERT(!DIVIDE_AT(0x48, 0xF7, 0xE9));                   /* imul rcx */
}

PTEST(other_instructions_are_not_divisions)
{
    PT_ASSERT(!DIVIDE_AT(0x90));                   /* nop */
    PT_ASSERT(!DIVIDE_AT(0xC3));                   /* ret */
    PT_ASSERT(!DIVIDE_AT(0x0F, 0x0B));             /* ud2 */
    PT_ASSERT(!DIVIDE_AT(0xF5, 0xF1));             /* cmc, then a byte that would be /6 */
    PT_ASSERT(!DIVIDE_AT(0xF8, 0xF1));             /* clc, likewise */
    PT_ASSERT(!DIVIDE_AT(0xF3, 0x0F, 0x5E, 0xC1)); /* divss xmm0, xmm1 */
    PT_ASSERT(!DIVIDE_AT(0xF2, 0x0F, 0x5E, 0xC1)); /* divsd xmm0, xmm1 */
}

PTEST(only_40_to_4f_is_rex)
{
    /* 50h is push rax, a whole instruction: the division after it is the
     * next instruction, not the one at the address. */
    PT_ASSERT(!DIVIDE_AT(0x50, 0xF7, 0xF1));
    PT_ASSERT(!DIVIDE_AT(0x5F, 0xF7, 0xF1)); /* pop rdi */
    PT_ASSERT(!DIVIDE_AT(0x3F, 0xF7, 0xF1)); /* below 40h (invalid in 64-bit mode) */
    PT_ASSERT(DIVIDE_AT(0x40, 0xF7, 0xF1));
    PT_ASSERT(DIVIDE_AT(0x4F, 0xF7, 0xF1));
    /* REX comes after the legacy prefixes, once; a second REX is the opcode. */
    PT_ASSERT(!DIVIDE_AT(0x48, 0x48, 0xF7, 0xF1));
    PT_ASSERT(!DIVIDE_AT(0x48, 0x66, 0xF7, 0xF1));
}

PTEST_MAIN()
