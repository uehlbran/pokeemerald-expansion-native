/*
 * x86_divide.c: whether an x86-64 instruction is a DIV or an IDIV
 * (x86_divide.h).
 *
 * Followed: the Intel SDM, volume 2: 2.1.1 for the legacy prefixes, 2.2.1
 * for REX (0100WRXB, immediately before the opcode), and the DIV and IDIV
 * pages for F6 /6, F6 /7, F7 /6 and F7 /7.
 */
#include "x86_divide.h"

#include <stddef.h>

/* The legacy prefixes (2.1.1): segment overrides, operand and address size,
 * LOCK, REPNE and REP; then REX (2.2.1). */
#define PREFIX_SEGMENT_ES 0x26u
#define PREFIX_SEGMENT_CS 0x2Eu
#define PREFIX_SEGMENT_SS 0x36u
#define PREFIX_SEGMENT_DS 0x3Eu
#define PREFIX_SEGMENT_FS 0x64u
#define PREFIX_SEGMENT_GS 0x65u
#define PREFIX_OPERAND_SIZE 0x66u
#define PREFIX_ADDRESS_SIZE 0x67u
#define PREFIX_LOCK 0xF0u
#define PREFIX_REPNE 0xF2u
#define PREFIX_REP 0xF3u
#define REX_HIGH_MASK 0xF0u
#define REX_HIGH_BITS 0x40u
#define MAX_PREFIXES 14u /* an instruction is at most 15 bytes */
/* DIV and IDIV: F6 (r/m8) or F7 with /6 or /7, the ModRM byte's reg field. */
#define OPCODE_GROUP3_BYTE 0xF6u
#define OPCODE_GROUP3 0xF7u
#define MODRM_REG_SHIFT 3u
#define MODRM_FIELD_MASK 7u
#define MODRM_REG_DIV 6u
#define MODRM_REG_IDIV 7u

static bool is_legacy_prefix(unsigned char byte)
{
    switch (byte) {
    case PREFIX_SEGMENT_ES:
    case PREFIX_SEGMENT_CS:
    case PREFIX_SEGMENT_SS:
    case PREFIX_SEGMENT_DS:
    case PREFIX_SEGMENT_FS:
    case PREFIX_SEGMENT_GS:
    case PREFIX_OPERAND_SIZE:
    case PREFIX_ADDRESS_SIZE:
    case PREFIX_LOCK:
    case PREFIX_REPNE:
    case PREFIX_REP:
        return true;
    default:
        return false;
    }
}

static bool is_rex_prefix(unsigned char byte)
{
    return (byte & REX_HIGH_MASK) == REX_HIGH_BITS;
}

bool x86_divide_at(const unsigned char *code)
{
    size_t at = 0;
    while (at < MAX_PREFIXES && is_legacy_prefix(code[at]))
        at++;
    if (is_rex_prefix(code[at]))
        at++;
    /* The opcode first: only F6 and F7 have the ModRM byte read next, and a
     * one-byte instruction can be the last byte of the mapped code. */
    if (code[at] != OPCODE_GROUP3_BYTE && code[at] != OPCODE_GROUP3)
        return false;
    const unsigned reg = ((unsigned)code[at + 1u] >> MODRM_REG_SHIFT) & MODRM_FIELD_MASK;
    return reg == MODRM_REG_DIV || reg == MODRM_REG_IDIV;
}
