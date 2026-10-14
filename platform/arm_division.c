/*
 * arm_division.c: integer division by zero (and INT_MIN by -1) completed
 * as the GBA's libgcc completes it (arm_division.h has the contract and
 * where the results come from).
 *
 * One decoder for both widths: i386 is x86-64 without REX prefixes, with
 * 32-bit registers and addresses, and with an absolute disp32 where x86-64
 * has a RIP-relative one; the width section below names those differences.
 *
 * Followed: the Intel SDM, volume 2, for the encoding of DIV and IDIV
 * (opcode F7h /6 and /7; prefixes, REX, ModRM, SIB and displacements in 32-
 * and 64-bit mode) and their #DE cases; signal-safety(7) for the handler.
 * The fault and its saved registers come from host_fault.h: the accumulator
 * holds the dividend's low half, then the quotient; the data register the
 * dividend's high half, then the remainder.
 */
#include "arm_division.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host_fault.h"
#include "host_file.h"

/* The width section: what 64-bit mode adds to the encoding. */
#if defined(__i386__)

#define HAS_REX_PREFIX false /* 40h-4Fh are INC and DEC in 32-bit mode */
#define DISP32_IS_RIP_RELATIVE false

#elif defined(__x86_64__)

#define HAS_REX_PREFIX true
#define DISP32_IS_RIP_RELATIVE true /* ModRM mod 0 rm 101 (Intel SDM vol. 2, 2.2.1.6) */

#else
#error "arm_division decodes the divide error of i386 and x86-64 only"
#endif

/* Intel SDM vol. 2: DIV r/m32 is F7 /6, IDIV r/m32 is F7 /7 (r/m64 with
 * REX.W). */
#define OPCODE_GROUP3 0xF7u
#define MODRM_REG_DIV 6u
#define MODRM_REG_IDIV 7u

/* ModRM and SIB fields (Intel SDM vol. 2, 2.1.5 and table 2-2). */
#define MODRM_FIELD_MASK 7u
#define MODRM_FIELD_SHIFT 3u
#define MODRM_MOD_SHIFT 6u
#define MODRM_MOD_NO_DISP 0u
#define MODRM_MOD_DISP8 1u
#define MODRM_MOD_DISP32 2u
#define MODRM_MOD_REGISTER 3u
#define MODRM_RM_SIB 4u
#define MODRM_RM_DISP32 5u /* with mod 0 */
#define SIB_INDEX_NONE 4u  /* without REX.X */
#define SIB_BASE_DISP32 5u /* with mod 0 */
#define DISP8_SIZE 1u
#define DISP32_SIZE 4u

/* Segment override prefixes (Intel SDM vol. 2, 2.1.1, group 2) that
 * address the flat user segment on Linux (in 64-bit mode they are null
 * prefixes, Intel SDM vol. 1, 3.3.7.1). FS (64h) and GS (65h) add
 * thread-local bases and are refused, as are the operand-size (66h) and
 * address-size (67h) prefixes (a 16-bit operand, other addressing), LOCK
 * and REP. At most one prefix per group, so at most four. */
#define PREFIX_SEGMENT_ES 0x26u
#define PREFIX_SEGMENT_CS 0x2Eu
#define PREFIX_SEGMENT_SS 0x36u
#define PREFIX_SEGMENT_DS 0x3Eu
#define MAX_PREFIXES 4u

/* The REX prefix, 0100WRXB (Intel SDM vol. 2, 2.2.1.2), 64-bit mode only,
 * which counts only as the last prefix, right before the opcode. W makes
 * the operand 64-bit; X and B add 8 to the register number in the SIB
 * index and in the ModRM rm or SIB base field. R would extend the ModRM
 * reg field, which for F7h is an opcode extension, so the processor
 * ignores it there (table 2-4). */
#define REX_HIGH_MASK 0xF0u
#define REX_HIGH_BITS 0x40u
#define REX_W 0x8u
#define REX_X 0x2u
#define REX_B 0x1u
#define REX_REGISTER_EXTENSION 8u

static bool is_flat_segment_prefix(uint8_t byte)
{
    return byte == PREFIX_SEGMENT_ES || byte == PREFIX_SEGMENT_CS || byte == PREFIX_SEGMENT_SS
           || byte == PREFIX_SEGMENT_DS;
}

static bool is_rex(uint8_t byte)
{
    return HAS_REX_PREFIX && (byte & REX_HIGH_MASK) == REX_HIGH_BITS;
}

/* The register number a three-bit ModRM or SIB field names, with the REX
 * bit (REX_X or REX_B) that extends that field. */
static unsigned extended(unsigned field, uint8_t rex, unsigned rex_bit)
{
    return (rex & rex_bit) != 0 ? field + REX_REGISTER_EXTENSION : field;
}

static uintptr_t register_value(const struct host_fault *fault, unsigned number)
{
    return host_fault_general_register(fault, number);
}

/* The fields of a ModRM byte, or of the SIB byte that has the same layout
 * (Intel SDM vol. 2, 2.1.5: high two bits, then three, then three). */
struct modrm_fields {
    unsigned high, middle, low;
};

static struct modrm_fields split_modrm(uint8_t byte)
{
    return (struct modrm_fields){.high = byte >> MODRM_MOD_SHIFT,
                                 .middle = (byte >> MODRM_FIELD_SHIFT) & MODRM_FIELD_MASK,
                                 .low = byte & MODRM_FIELD_MASK};
}

/* Displacements, sign-extended to the address width (Intel SDM vol. 2,
 * 2.2.1.3; in 32-bit mode a disp32 is already that wide). */
static uintptr_t read_disp8(const uint8_t *bytes)
{
    int8_t displacement;
    memcpy(&displacement, bytes, sizeof displacement);
    return (uintptr_t)(intptr_t)displacement;
}

static uintptr_t read_disp32(const uint8_t *bytes)
{
    int32_t displacement;
    memcpy(&displacement, bytes, sizeof displacement);
    return (uintptr_t)(intptr_t)displacement;
}

/* The memory operand's address, from the bytes after the ModRM byte (code
 * points at them); *used is how many of those bytes (SIB, displacement)
 * the addressing form takes. mod is not MODRM_MOD_REGISTER. The special
 * forms are told by the three-bit fields, before REX extends them (Intel
 * SDM vol. 2, table 2-5): rm 100 is a SIB byte, rm 101 with mod 0 a disp32
 * (RIP-relative in 64-bit mode), SIB base 101 with mod 0 a disp32 and no
 * base, all whatever REX.B says; SIB index 100 is no index only without
 * REX.X (with it, R12). Arithmetic wraps at the address width, as the
 * processor's does. */
static uintptr_t operand_address(const uint8_t *code, const struct host_fault *fault, struct modrm_fields modrm,
                                 uint8_t rex, size_t *used)
{
    size_t i = 0;
    uintptr_t address = 0;
    bool rip_relative = false;
    if (modrm.low == MODRM_RM_SIB) {
        struct modrm_fields sib = split_modrm(code[i++]); /* scale, index, base */
        unsigned index = extended(sib.middle, rex, REX_X);
        if (index != SIB_INDEX_NONE)
            address += register_value(fault, index) << sib.high;
        if (sib.low == SIB_BASE_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
            address += read_disp32(&code[i]);
            i += DISP32_SIZE;
        } else {
            address += register_value(fault, extended(sib.low, rex, REX_B));
        }
    } else if (modrm.low == MODRM_RM_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
        address = read_disp32(&code[i]);
        i += DISP32_SIZE;
        rip_relative = DISP32_IS_RIP_RELATIVE;
    } else {
        address = register_value(fault, extended(modrm.low, rex, REX_B));
    }
    if (modrm.high == MODRM_MOD_DISP8) {
        address += read_disp8(&code[i]);
        i += DISP8_SIZE;
    } else if (modrm.high == MODRM_MOD_DISP32) {
        address += read_disp32(&code[i]);
        i += DISP32_SIZE;
    }
    /* RIP-relative counts from the next instruction (Intel SDM vol. 2,
     * 2.2.1.6). F7 /6 and /7 have no immediate: it starts where the
     * displacement ends. */
    if (rip_relative)
        address += (uintptr_t)&code[i];
    *used = i;
    return address;
}

/* A decoded F7 /6 or /7, and its divisor. */
struct division_instruction {
    size_t length;
    bool is_signed;
    bool is_64_bit;   /* REX.W: RDX:RAX by a 64-bit divisor; else EDX:EAX by a 32-bit one */
    uint64_t divisor; /* a 32-bit divisor zero-extended */
};

/* The divisor in memory, 8 bytes with REX.W, else 4. */
static uint64_t read_operand(uintptr_t address, bool is_64_bit)
{
    const void *operand = (const void *)address;
    if (is_64_bit) {
        uint64_t value;
        memcpy(&value, operand, sizeof value);
        return value;
    }
    uint32_t value;
    memcpy(&value, operand, sizeof value);
    return value;
}

/* Decodes the instruction at code. The refusals, each a plain false:
 *   - a prefix other than ES/CS/SS/DS (66h, 67h, FS, GS, LOCK, REP, ...)
 *     before the REX prefix and the opcode, a second REX prefix, or a REX
 *     prefix followed by any prefix: the byte where the opcode should be
 *     is not F7h;
 *   - an opcode other than F7h (8-bit DIV/IDIV is F6h and cannot be the
 *     compiler's division of an int or wider);
 *   - ModRM reg other than 6 (DIV) or 7 (IDIV): NOT, NEG, MUL, IMUL, TEST.
 * The operand is readable: the CPU fetched it before raising the divide
 * error. */
static bool decode(const uint8_t *code, const struct host_fault *fault, struct division_instruction *out)
{
    size_t i = 0;
    while (i < MAX_PREFIXES && is_flat_segment_prefix(code[i]))
        i++;
    uint8_t rex = 0;
    if (is_rex(code[i]))
        rex = code[i++];
    if (code[i++] != OPCODE_GROUP3)
        return false;
    struct modrm_fields modrm = split_modrm(code[i++]);
    if (modrm.middle != MODRM_REG_DIV && modrm.middle != MODRM_REG_IDIV)
        return false;
    out->is_signed = modrm.middle == MODRM_REG_IDIV;
    out->is_64_bit = (rex & REX_W) != 0;
    if (modrm.high == MODRM_MOD_REGISTER) {
        const uint64_t value = register_value(fault, extended(modrm.low, rex, REX_B));
        out->divisor = out->is_64_bit ? value : (uint32_t)value;
    } else {
        size_t used = 0;
        out->divisor = read_operand(operand_address(&code[i], fault, modrm, rex, &used), out->is_64_bit);
        i += used;
    }
    out->length = i;
    return true;
}

/* libgcc's result for a 32-bit division by zero, or of INT32_MIN by -1
 * (arm_division.h: __udivsi3, __umodsi3, __divsi3, __modsi3), from EDX:EAX
 * and the divisor; false for any other divide error (a quotient too large
 * for 32 bits from a 64-bit dividend, which the GBA's 32-bit routines never
 * see). */
static bool libgcc_result_32(bool is_signed, uint32_t divisor, uint32_t eax, uint32_t edx, uint32_t *quotient,
                             uint32_t *remainder)
{
    if (!is_signed) {
        if (divisor != 0 || edx != 0)
            return false;
        *quotient = *remainder = eax != 0 ? UINT32_MAX : 0;
        return true;
    }
    int32_t dividend = (int32_t)eax;
    if (edx != (dividend < 0 ? UINT32_MAX : 0)) /* not a sign-extended 32-bit dividend */
        return false;
    if (divisor == 0) {
        int32_t result = 0;
        if (dividend > 0)
            result = INT32_MAX;
        else if (dividend < 0)
            result = INT32_MIN;
        *quotient = *remainder = (uint32_t)result;
        return true;
    }
    if (divisor == UINT32_MAX && dividend == INT32_MIN) {
        *quotient = (uint32_t)INT32_MIN;
        *remainder = 0;
        return true;
    }
    return false;
}

#define U64_BITS 64u

/* __udivdi3 and __umoddi3 by zero (arm_division.h): with k the bit length
 * of n, the quotient is 2^(k+1) - 1, all ones from k = 63, and the remainder
 * n mod 2^(64-k). The routines align the divisor under the dividend's top
 * bit and test-subtract k + 1 times, shifting the remainder left each time;
 * a zero divisor always subtracts, so each step adds a 1 to the quotient,
 * and the shifts lose the top k bits of n. Checked against libgcc under
 * qemu-arm for every bit length (platform/arm_division_libgcc). */
static void udivmoddi_by_zero(uint64_t n, uint64_t *quotient, uint64_t *remainder)
{
    const unsigned length = n == 0 ? 0 : U64_BITS - (unsigned)__builtin_clzll(n);
    *quotient = length >= U64_BITS - 1u ? UINT64_MAX : ((uint64_t)2 << length) - 1u;
    *remainder = length == 0 || length == U64_BITS ? 0 : n & (((uint64_t)1 << (U64_BITS - length)) - 1u);
}

/* libgcc's result for a 64-bit division (REX.W) by zero, or of INT64_MIN by
 * -1 (arm_division.h: __udivdi3, __umoddi3, __divdi3, __moddi3), from
 * RDX:RAX and the divisor; false for any other divide error (a 128-bit
 * dividend, which C's division never makes). */
static bool libgcc_result_64(bool is_signed, uint64_t divisor, uint64_t rax, uint64_t rdx, uint64_t *quotient,
                             uint64_t *remainder)
{
    if (!is_signed) {
        if (divisor != 0 || rdx != 0)
            return false;
        udivmoddi_by_zero(rax, quotient, remainder);
        return true;
    }
    const bool negative = (int64_t)rax < 0;
    if (rdx != (negative ? UINT64_MAX : 0)) /* not a sign-extended 64-bit dividend */
        return false;
    if (divisor == 0) {
        /* __divdi3 and __moddi3 divide the magnitudes; both results take the
         * dividend's sign (a zero divisor counts as positive). */
        udivmoddi_by_zero(negative ? 0u - rax : rax, quotient, remainder);
        if (negative) {
            *quotient = 0u - *quotient;
            *remainder = 0u - *remainder;
        }
        return true;
    }
    if (divisor == UINT64_MAX && rax == (uint64_t)INT64_MIN) {
        *quotient = (uint64_t)INT64_MIN;
        *remainder = 0;
        return true;
    }
    return false;
}

/* libgcc's results for the decoded division, from the accumulator and the
 * data register (the dividend's low and high halves). A 32-bit result
 * replaces the whole register: writing EAX and EDX zero-extends into RAX
 * and RDX (Intel SDM vol. 1, 3.4.1.1). */
static bool libgcc_result(const struct division_instruction *instruction, uint64_t accumulator, uint64_t data,
                          uint64_t *quotient, uint64_t *remainder)
{
    if (instruction->is_64_bit)
        return libgcc_result_64(instruction->is_signed, instruction->divisor, accumulator, data, quotient, remainder);
    uint32_t quotient_32, remainder_32;
    if (!libgcc_result_32(instruction->is_signed, (uint32_t)instruction->divisor, (uint32_t)accumulator, (uint32_t)data,
                          &quotient_32, &remainder_32))
        return false;
    *quotient = quotient_32;
    *remainder = remainder_32;
    return true;
}

/* Completes the instruction in the saved context. True if it did. A 64-bit
 * result only comes from REX.W, so it always fits the register. */
static bool complete(struct host_fault *fault)
{
    const uintptr_t pc = host_fault_register(fault, HOST_FAULT_PC);
    struct division_instruction instruction;
    uint64_t quotient, remainder;
    if (!decode((const uint8_t *)pc, fault, &instruction)
        || !libgcc_result(&instruction, host_fault_register(fault, HOST_FAULT_ACCUMULATOR),
                          host_fault_register(fault, HOST_FAULT_DATA), &quotient, &remainder))
        return false;
    host_fault_set_register(fault, HOST_FAULT_ACCUMULATOR, (uintptr_t)quotient);
    host_fault_set_register(fault, HOST_FAULT_DATA, (uintptr_t)remainder);
    host_fault_set_register(fault, HOST_FAULT_PC, pc + instruction.length);
    return true;
}

#define HEX_DIGIT_BITS 4u
#define HEX_DIGIT_MASK 0xFu
#define PC_HEX_DIGITS (sizeof(uintptr_t) * 2u)

/* "arm_division: undecodable divide error at pc 0x12345678, passed on
 * (...)\n", 16 digits on x86-64, formatted without stdio and written with
 * host_file_write_all, which a fault handler may call. */
static void report_passed_on(const struct host_fault *fault)
{
    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on (not a DIV or IDIV by zero, nor INT_MIN / -1)\n";
    static const char hex[] = "0123456789abcdef";
    char line[sizeof head - 1 + PC_HEX_DIGITS + sizeof tail - 1];
    const uintptr_t pc = host_fault_register(fault, HOST_FAULT_PC);
    memcpy(line, head, sizeof head - 1);
    for (size_t i = 0; i < PC_HEX_DIGITS; i++) {
        size_t shift = (PC_HEX_DIGITS - 1u - i) * HEX_DIGIT_BITS; /* most significant digit first */
        line[sizeof head - 1 + i] = hex[(pc >> shift) & HEX_DIGIT_MASK];
    }
    memcpy(line + sizeof head - 1 + PC_HEX_DIGITS, tail, sizeof tail - 1);
    (void)host_file_write_all(STDERR_FILENO, line, sizeof line); /* nothing to do if stderr is gone */
}

/* Completes a divide error, or reports the one it cannot complete (so that
 * one fails loudly in game mode too, where the default action ends the
 * process with nothing more than "Floating point exception") and passes it
 * on: host_fault gives it to the handler installed before (crash_reboot in
 * test runner mode), or lets it kill the process. */
static bool on_divide_error(struct host_fault *fault)
{
    if (host_fault_is_divide_error(fault) && complete(fault))
        return true;
    report_passed_on(fault);
    return false;
}

int arm_division_init(void)
{
    if (host_fault_install_divide_error(on_divide_error) != 0)
        return ARM_DIVISION_ERR_SIGNAL;
    return ARM_DIVISION_OK;
}

void arm_division_shutdown(void)
{
    host_fault_uninstall_divide_error();
}
