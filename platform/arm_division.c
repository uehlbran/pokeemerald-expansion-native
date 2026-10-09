/*
 * arm_division.c: integer division by zero (and INT_MIN by -1) completed
 * as the GBA's libgcc completes it (arm_division.h has the contract and
 * where the results come from).
 *
 * Followed: the Intel SDM, volume 2, for the encoding of DIV and IDIV
 * (opcode F7h /6 and /7; prefixes, REX, ModRM, SIB and displacements in 32-
 * and 64-bit mode) and their #DE cases; signal-safety(7) for the handler.
 */
#define _GNU_SOURCE /* REG_EAX, REG_RAX and friends */
#include "arm_division.h"

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

/* Intel SDM vol. 2: DIV r/m32 is F7 /6, IDIV r/m32 is F7 /7. */
#define OPCODE_GROUP3_32 0xF7u
#define MODRM_REG_DIV 6u
#define MODRM_REG_IDIV 7u

/* ModRM and SIB fields (Intel SDM vol. 2, 2.1.5). */
#define MODRM_FIELD_MASK 7u
#define MODRM_MOD_NO_DISP 0u
#define MODRM_MOD_REGISTER 3u
#define MODRM_RM_SIB 4u
#define MODRM_RM_DISP32 5u /* with mod 0 */
#define SIB_INDEX_NONE 4u
#define SIB_BASE_DISP32 5u /* with mod 0 */

/* The prefixes an instruction may carry: at most one per group. */
#define MAX_PREFIXES 4u

static struct {
    bool installed;
    struct sigaction previous;
} division;

#if defined(__i386__)

/* Segment override prefixes (Intel SDM vol. 2, 2.1.1 "Instruction
 * Prefixes", group 2) that address the flat user segment on Linux. FS
 * (64h) and GS (65h) hold thread-local bases and are refused, as are the
 * operand-size (66h) and address-size (67h) prefixes: with them the
 * instruction is not a 32-bit DIV or IDIV with 32-bit addressing. */
#define PREFIX_SEGMENT_ES 0x26u
#define PREFIX_SEGMENT_CS 0x2Eu
#define PREFIX_SEGMENT_SS 0x36u
#define PREFIX_SEGMENT_DS 0x3Eu

static bool is_flat_segment_prefix(uint8_t byte)
{
    return byte == PREFIX_SEGMENT_ES || byte == PREFIX_SEGMENT_CS || byte == PREFIX_SEGMENT_SS
           || byte == PREFIX_SEGMENT_DS;
}

/* ModRM register numbers to the saved registers (Intel SDM vol. 2,
 * table 2-2: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI). */
static const int register_slots[MODRM_FIELD_MASK + 1u] = {REG_EAX, REG_ECX, REG_EDX, REG_EBX,
                                                          REG_ESP, REG_EBP, REG_ESI, REG_EDI};

static uint32_t register_value(const greg_t *gregs, unsigned number)
{
    return (uint32_t)gregs[register_slots[number & MODRM_FIELD_MASK]];
}

static uint32_t read_u32(uintptr_t address)
{
    uint32_t value;
    memcpy(&value, (const void *)address, sizeof value);
    return value;
}

/* A decoded F7 /6 or /7, and its divisor. */
struct division_instruction {
    size_t length;
    bool is_signed;
    uint32_t divisor;
};

/* ModRM mod values with a displacement (Intel SDM vol. 2, table 2-2). */
#define MODRM_MOD_DISP8 1u
#define MODRM_MOD_DISP32 2u
#define MODRM_FIELD_SHIFT 3u
#define MODRM_MOD_SHIFT 6u
#define DISP32_SIZE 4u

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

/* The memory operand's address, from the bytes after the ModRM byte (code
 * points at them); *used is how many of those bytes (SIB, displacement)
 * the addressing form takes. mod is not MODRM_MOD_REGISTER. */
static uint32_t operand_address(const uint8_t *code, const greg_t *gregs, struct modrm_fields modrm, size_t *used)
{
    size_t i = 0;
    uint32_t address = 0;
    if (modrm.low == MODRM_RM_SIB) {
        struct modrm_fields sib = split_modrm(code[i++]); /* scale, index, base */
        if (sib.middle != SIB_INDEX_NONE)
            address += register_value(gregs, sib.middle) << sib.high;
        if (sib.low == SIB_BASE_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
            address += read_u32((uintptr_t)&code[i]);
            i += DISP32_SIZE;
        } else {
            address += register_value(gregs, sib.low);
        }
    } else if (modrm.low == MODRM_RM_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
        address = read_u32((uintptr_t)&code[i]);
        i += DISP32_SIZE;
    } else {
        address = register_value(gregs, modrm.low);
    }
    if (modrm.high == MODRM_MOD_DISP8) {
        int8_t displacement; /* sign-extended */
        memcpy(&displacement, &code[i], sizeof displacement);
        const uint32_t offset = (uint32_t)(int32_t)displacement;
        address += offset;
        i += sizeof displacement;
    } else if (modrm.high == MODRM_MOD_DISP32) {
        address += read_u32((uintptr_t)&code[i]);
        i += DISP32_SIZE;
    }
    *used = i;
    return address;
}

/* Decodes the instruction at code. The refusals, each a plain false:
 *   - a prefix other than ES/CS/SS/DS (66h, 67h, FS, GS, LOCK, REP, ...) in
 *     front: the byte where the opcode should be is not F7h;
 *   - an opcode other than F7h (8-bit DIV/IDIV is F6h and cannot be the
 *     compiler's 32-bit division);
 *   - ModRM reg other than 6 (DIV) or 7 (IDIV): NOT, NEG, MUL, IMUL, TEST.
 * The operand is readable: the CPU fetched it before raising the divide
 * error. */
static bool decode(const uint8_t *code, const greg_t *gregs, struct division_instruction *out)
{
    size_t i = 0;
    while (i < MAX_PREFIXES && is_flat_segment_prefix(code[i]))
        i++;
    if (code[i++] != OPCODE_GROUP3_32)
        return false;
    struct modrm_fields modrm = split_modrm(code[i++]);
    if (modrm.middle != MODRM_REG_DIV && modrm.middle != MODRM_REG_IDIV)
        return false;
    out->is_signed = modrm.middle == MODRM_REG_IDIV;
    if (modrm.high == MODRM_MOD_REGISTER) {
        out->divisor = register_value(gregs, modrm.low);
    } else {
        size_t used = 0;
        out->divisor = read_u32(operand_address(&code[i], gregs, modrm, &used));
        i += used;
    }
    out->length = i;
    return true;
}

/* libgcc's result for a zero divisor (arm_division.h), or for INT32_MIN /
 * -1; false for any other divide error (a quotient too large for 32 bits
 * from a 64-bit dividend, which the GBA's 32-bit routines never see). */
static bool libgcc_result(const struct division_instruction *instruction, uint32_t eax, uint32_t edx,
                          uint32_t *quotient, uint32_t *remainder)
{
    if (!instruction->is_signed) {
        if (instruction->divisor != 0 || edx != 0)
            return false;
        *quotient = *remainder = eax != 0 ? UINT32_MAX : 0;
        return true;
    }
    int32_t dividend = (int32_t)eax;
    if (edx != (dividend < 0 ? UINT32_MAX : 0)) /* not a sign-extended 32-bit dividend */
        return false;
    if (instruction->divisor == 0) {
        int32_t result = 0;
        if (dividend > 0)
            result = INT32_MAX;
        else if (dividend < 0)
            result = INT32_MIN;
        *quotient = *remainder = (uint32_t)result;
        return true;
    }
    if (instruction->divisor == UINT32_MAX && dividend == INT32_MIN) {
        *quotient = (uint32_t)INT32_MIN;
        *remainder = 0;
        return true;
    }
    return false;
}

/* Completes the instruction in the saved context. True if it did. */
static bool complete(ucontext_t *context)
{
    greg_t *gregs = context->uc_mcontext.gregs;
    const uint8_t *code = (const uint8_t *)(uintptr_t)(uint32_t)gregs[REG_EIP];
    struct division_instruction instruction;
    uint32_t quotient, remainder;
    if (!decode(code, gregs, &instruction)
        || !libgcc_result(&instruction, (uint32_t)gregs[REG_EAX], (uint32_t)gregs[REG_EDX], &quotient, &remainder))
        return false;
    gregs[REG_EAX] = (greg_t)quotient;
    gregs[REG_EDX] = (greg_t)remainder;
    gregs[REG_EIP] = (greg_t)((uint32_t)gregs[REG_EIP] + (uint32_t)instruction.length);
    return true;
}

#define HEX_DIGIT_BITS 4u
#define HEX_DIGIT_MASK 0xFu
#define HEX_DIGITS_PER_U32 8u

/* "arm_division: undecodable divide error at pc 0x12345678, passed on\n",
 * formatted without stdio (signal-safety(7): write is async-signal-safe). */
static void report_passed_on(const void *context)
{
    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on (not a 32-bit DIV or IDIV by zero, nor INT32_MIN / -1)\n";
    static const char hex[] = "0123456789abcdef";
    char line[sizeof head - 1 + HEX_DIGITS_PER_U32 + sizeof tail - 1];
    uint32_t pc = (uint32_t)((const ucontext_t *)context)->uc_mcontext.gregs[REG_EIP];
    memcpy(line, head, sizeof head - 1);
    for (unsigned i = 0; i < HEX_DIGITS_PER_U32; i++) {
        unsigned shift = (HEX_DIGITS_PER_U32 - 1u - i) * HEX_DIGIT_BITS; /* most significant digit first */
        line[sizeof head - 1 + i] = hex[(pc >> shift) & HEX_DIGIT_MASK];
    }
    memcpy(line + sizeof head - 1 + HEX_DIGITS_PER_U32, tail, sizeof tail - 1);
    (void)!write(STDERR_FILENO, line, sizeof line); /* nothing to do if stderr is gone */
}

/* Reports the divide error it cannot complete (so that one fails loudly in
 * game mode too, where the default action ends the process with nothing
 * more than "Floating point exception"), then passes the signal to the
 * handler installed before (crash_reboot in test runner mode), or lets it
 * kill the process: with the default action restored, the instruction
 * faults again on return. */
static void pass_on(int signal_number, siginfo_t *info, void *context)
{
    report_passed_on(context);
    const struct sigaction *previous = &division.previous;
    if ((previous->sa_flags & SA_SIGINFO) != 0 && previous->sa_sigaction != NULL) {
        previous->sa_sigaction(signal_number, info, context);
        return;
    }
    if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN) {
        previous->sa_handler(signal_number);
        return;
    }
    struct sigaction fallback;
    memset(&fallback, 0, sizeof fallback);
    fallback.sa_handler = SIG_DFL;
    (void)sigemptyset(&fallback.sa_mask);
    (void)sigaction(signal_number, &fallback, NULL);
}

static void on_divide_error(int signal_number, siginfo_t *info, void *context)
{
    if (info->si_code == FPE_INTDIV && complete(context))
        return;
    pass_on(signal_number, info, context);
}

int arm_division_init(void)
{
    arm_division_shutdown();
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_divide_error;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGFPE, &action, &division.previous) != 0) {
        perror("arm_division: sigaction(SIGFPE)");
        return ARM_DIVISION_ERR_SIGNAL;
    }
    division.installed = true;
    return ARM_DIVISION_OK;
}

#elif !defined(__x86_64__)
#error "arm_division decodes the divide error of i386 and x86-64 only"
#endif

/* x86-64 has its own decoder and arm_division_init, at the end of the file:
 * written after arm_division_shutdown so that no line the i386 build
 * compiles moved (the 32-bit executables carry this file's line numbers in
 * their debug information, and milestone 7 keeps them byte-identical).
 * For the same reason the parts both widths share in form (the prefixes,
 * ModRM, passing a signal on) are written out again there. */


void arm_division_shutdown(void)
{
    if (!division.installed)
        return;
    (void)sigaction(SIGFPE, &division.previous, NULL); /* restoring what sigaction gave back cannot fail */
    division.installed = false;
}

#if defined(__x86_64__)

/* Segment override prefixes ES, CS, SS and DS (Intel SDM vol. 2, 2.1.1,
 * group 2), which 64-bit mode treats as null prefixes (Intel SDM vol. 1,
 * 3.3.7.1); accepted as on i386. FS (64h) and GS (65h) add thread-local
 * bases and are refused, as are the operand-size (66h) and address-size
 * (67h) prefixes (a 16-bit operand, 32-bit addressing), LOCK and REP. */
#define PREFIX_SEGMENT_ES 0x26u
#define PREFIX_SEGMENT_CS 0x2Eu
#define PREFIX_SEGMENT_SS 0x36u
#define PREFIX_SEGMENT_DS 0x3Eu

static bool is_flat_segment_prefix(uint8_t byte)
{
    return byte == PREFIX_SEGMENT_ES || byte == PREFIX_SEGMENT_CS || byte == PREFIX_SEGMENT_SS
           || byte == PREFIX_SEGMENT_DS;
}

/* The REX prefix, 0100WRXB (Intel SDM vol. 2, 2.2.1.2), which counts only
 * as the last prefix, right before the opcode. W makes the operand 64-bit;
 * X and B add 8 to the register number in the SIB index and in the ModRM rm
 * or SIB base field. R would extend the ModRM reg field, which for F7h is
 * an opcode extension, so the processor ignores it there (table 2-4). */
#define REX_HIGH_MASK 0xF0u
#define REX_HIGH_BITS 0x40u
#define REX_W 0x8u
#define REX_X 0x2u
#define REX_B 0x1u
#define REX_REGISTER_EXTENSION 8u

static bool is_rex(uint8_t byte)
{
    return (byte & REX_HIGH_MASK) == REX_HIGH_BITS;
}

/* The register number a three-bit ModRM or SIB field names, with the REX
 * bit (REX_X or REX_B) that extends that field. */
static unsigned extended(unsigned field, uint8_t rex, unsigned rex_bit)
{
    return (rex & rex_bit) != 0 ? field + REX_REGISTER_EXTENSION : field;
}

/* Register numbers to the saved registers (Intel SDM vol. 2, table 2-2 and
 * 2.2.1.2: RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, then R8 to R15). */
#define REGISTER_COUNT 16u
static const int register_slots[REGISTER_COUNT] = {REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
                                                   REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
                                                   REG_R12, REG_R13, REG_R14, REG_R15};

static uint64_t register_value(const greg_t *gregs, unsigned number)
{
    return (uint64_t)gregs[register_slots[number % REGISTER_COUNT]];
}

/* A decoded F7 /6 or /7, and its divisor. */
struct division_instruction {
    size_t length;
    bool is_signed;
    bool is_64_bit;   /* REX.W: RDX:RAX by a 64-bit divisor; else EDX:EAX by a 32-bit one */
    uint64_t divisor; /* a 32-bit divisor zero-extended */
};

/* ModRM mod values with a displacement, and the field layout (Intel SDM
 * vol. 2, table 2-2 and 2.1.5). */
#define MODRM_MOD_DISP8 1u
#define MODRM_MOD_DISP32 2u
#define MODRM_FIELD_SHIFT 3u
#define MODRM_MOD_SHIFT 6u
#define DISP32_SIZE 4u

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

/* A 32-bit displacement, sign-extended to 64 bits as 64-bit addressing
 * does (Intel SDM vol. 2, 2.2.1.3). */
static uint64_t read_disp32(const uint8_t *bytes)
{
    int32_t displacement;
    memcpy(&displacement, bytes, sizeof displacement);
    return (uint64_t)(int64_t)displacement;
}

/* The memory operand's address, from the bytes after the ModRM byte (code
 * points at them); *used is how many of those bytes (SIB, displacement)
 * the addressing form takes. mod is not MODRM_MOD_REGISTER. The special
 * forms are told by the three-bit fields, before REX extends them (Intel
 * SDM vol. 2, table 2-5): rm 100 is a SIB byte and rm 101 with mod 0 is
 * RIP-relative, and SIB base 101 with mod 0 is a disp32 and no base, all
 * whatever REX.B says; SIB index 100 is no index only without REX.X (with
 * it, R12). Arithmetic wraps at 64 bits, as the processor's does. */
static uint64_t operand_address(const uint8_t *code, const greg_t *gregs, struct modrm_fields modrm, uint8_t rex,
                                size_t *used)
{
    size_t i = 0;
    uint64_t address = 0;
    bool rip_relative = false;
    if (modrm.low == MODRM_RM_SIB) {
        struct modrm_fields sib = split_modrm(code[i++]); /* scale, index, base */
        unsigned index = extended(sib.middle, rex, REX_X);
        if (index != SIB_INDEX_NONE)
            address += register_value(gregs, index) << sib.high;
        if (sib.low == SIB_BASE_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
            address += read_disp32(&code[i]);
            i += DISP32_SIZE;
        } else {
            address += register_value(gregs, extended(sib.low, rex, REX_B));
        }
    } else if (modrm.low == MODRM_RM_DISP32 && modrm.high == MODRM_MOD_NO_DISP) {
        address = read_disp32(&code[i]);
        i += DISP32_SIZE;
        rip_relative = true;
    } else {
        address = register_value(gregs, extended(modrm.low, rex, REX_B));
    }
    if (modrm.high == MODRM_MOD_DISP8) {
        int8_t displacement; /* sign-extended */
        memcpy(&displacement, &code[i], sizeof displacement);
        address += (uint64_t)(int64_t)displacement;
        i += sizeof displacement;
    } else if (modrm.high == MODRM_MOD_DISP32) {
        address += read_disp32(&code[i]);
        i += DISP32_SIZE;
    }
    /* RIP-relative counts from the next instruction (Intel SDM vol. 2,
     * 2.2.1.6). F7 /6 and /7 have no immediate: it starts where the
     * displacement ends. */
    if (rip_relative)
        address += (uint64_t)(uintptr_t)&code[i];
    *used = i;
    return address;
}

/* The divisor in memory, 8 bytes with REX.W, else 4 zero-extended. */
static uint64_t read_operand(uint64_t address, bool is_64_bit)
{
    const void *operand = (const void *)(uintptr_t)address;
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
static bool decode(const uint8_t *code, const greg_t *gregs, struct division_instruction *out)
{
    size_t i = 0;
    while (i < MAX_PREFIXES && is_flat_segment_prefix(code[i]))
        i++;
    uint8_t rex = 0;
    if (is_rex(code[i]))
        rex = code[i++];
    if (code[i++] != OPCODE_GROUP3_32)
        return false;
    struct modrm_fields modrm = split_modrm(code[i++]);
    if (modrm.middle != MODRM_REG_DIV && modrm.middle != MODRM_REG_IDIV)
        return false;
    out->is_signed = modrm.middle == MODRM_REG_IDIV;
    out->is_64_bit = (rex & REX_W) != 0;
    if (modrm.high == MODRM_MOD_REGISTER) {
        const uint64_t value = register_value(gregs, extended(modrm.low, rex, REX_B));
        out->divisor = out->is_64_bit ? value : (uint32_t)value;
    } else {
        size_t used = 0;
        out->divisor = read_operand(operand_address(&code[i], gregs, modrm, rex, &used), out->is_64_bit);
        i += used;
    }
    out->length = i;
    return true;
}

/* libgcc's result for a 32-bit division by zero, or of INT32_MIN by -1
 * (arm_division.h: __udivsi3, __umodsi3, __divsi3, __modsi3); false for any
 * other divide error (a quotient too large for 32 bits from a 64-bit
 * dividend, which the GBA's 32-bit routines never see). */
static bool libgcc_result_32(const struct division_instruction *instruction, uint32_t eax, uint32_t edx,
                             uint32_t *quotient, uint32_t *remainder)
{
    const uint32_t divisor = (uint32_t)instruction->divisor;
    if (!instruction->is_signed) {
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

/* libgcc's result for a 64-bit division by zero, or of INT64_MIN by -1
 * (arm_division.h: __udivdi3, __umoddi3, __divdi3, __moddi3); false for any
 * other divide error (a 128-bit dividend, which C's division never makes). */
static bool libgcc_result_64(const struct division_instruction *instruction, uint64_t rax, uint64_t rdx,
                             uint64_t *quotient, uint64_t *remainder)
{
    if (!instruction->is_signed) {
        if (instruction->divisor != 0 || rdx != 0)
            return false;
        udivmoddi_by_zero(rax, quotient, remainder);
        return true;
    }
    const bool negative = (int64_t)rax < 0;
    if (rdx != (negative ? UINT64_MAX : 0)) /* not a sign-extended 64-bit dividend */
        return false;
    if (instruction->divisor == 0) {
        /* __divdi3 and __moddi3 divide the magnitudes; both results take the
         * dividend's sign (a zero divisor counts as positive). */
        udivmoddi_by_zero(negative ? 0u - rax : rax, quotient, remainder);
        if (negative) {
            *quotient = 0u - *quotient;
            *remainder = 0u - *remainder;
        }
        return true;
    }
    if (instruction->divisor == UINT64_MAX && rax == (uint64_t)INT64_MIN) {
        *quotient = (uint64_t)INT64_MIN;
        *remainder = 0;
        return true;
    }
    return false;
}

/* Completes the instruction in the saved context. True if it did. */
static bool complete(ucontext_t *context)
{
    greg_t *gregs = context->uc_mcontext.gregs;
    const uint8_t *code = (const uint8_t *)(uintptr_t)gregs[REG_RIP];
    struct division_instruction instruction;
    if (!decode(code, gregs, &instruction))
        return false;
    const uint64_t rax = (uint64_t)gregs[REG_RAX];
    const uint64_t rdx = (uint64_t)gregs[REG_RDX];
    uint64_t quotient, remainder;
    if (instruction.is_64_bit) {
        if (!libgcc_result_64(&instruction, rax, rdx, &quotient, &remainder))
            return false;
    } else {
        uint32_t quotient_32, remainder_32;
        if (!libgcc_result_32(&instruction, (uint32_t)rax, (uint32_t)rdx, &quotient_32, &remainder_32))
            return false;
        /* Writing EAX and EDX zero-extends into RAX and RDX (Intel SDM vol. 1, 3.4.1.1). */
        quotient = quotient_32;
        remainder = remainder_32;
    }
    gregs[REG_RAX] = (greg_t)quotient;
    gregs[REG_RDX] = (greg_t)remainder;
    const uint64_t next_instruction = (uint64_t)gregs[REG_RIP] + instruction.length;
    gregs[REG_RIP] = (greg_t)next_instruction;
    return true;
}

#define HEX_DIGIT_BITS 4u
#define HEX_DIGIT_MASK 0xFu
#define HEX_DIGITS_PER_U64 16u

/* "arm_division: undecodable divide error at pc 0x0000000012345678, passed
 * on\n", formatted without stdio (signal-safety(7): write is
 * async-signal-safe). */
static void report_passed_on(const void *context)
{
    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on (not a DIV or IDIV by zero, nor INT_MIN / -1)\n";
    static const char hex[] = "0123456789abcdef";
    char line[sizeof head - 1 + HEX_DIGITS_PER_U64 + sizeof tail - 1];
    uint64_t pc = (uint64_t)((const ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
    memcpy(line, head, sizeof head - 1);
    for (unsigned i = 0; i < HEX_DIGITS_PER_U64; i++) {
        unsigned shift = (HEX_DIGITS_PER_U64 - 1u - i) * HEX_DIGIT_BITS; /* most significant digit first */
        line[sizeof head - 1 + i] = hex[(pc >> shift) & HEX_DIGIT_MASK];
    }
    memcpy(line + sizeof head - 1 + HEX_DIGITS_PER_U64, tail, sizeof tail - 1);
    (void)!write(STDERR_FILENO, line, sizeof line); /* nothing to do if stderr is gone */
}

/* Reports the divide error it cannot complete (so that one fails loudly in
 * game mode too, where the default action ends the process with nothing
 * more than "Floating point exception"), then passes the signal to the
 * handler installed before (crash_reboot in test runner mode), or lets it
 * kill the process: with the default action restored, the instruction
 * faults again on return. */
static void pass_on(int signal_number, siginfo_t *info, void *context)
{
    report_passed_on(context);
    const struct sigaction *previous = &division.previous;
    if ((previous->sa_flags & SA_SIGINFO) != 0 && previous->sa_sigaction != NULL) {
        previous->sa_sigaction(signal_number, info, context);
        return;
    }
    if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN) {
        previous->sa_handler(signal_number);
        return;
    }
    struct sigaction fallback;
    memset(&fallback, 0, sizeof fallback);
    fallback.sa_handler = SIG_DFL;
    (void)sigemptyset(&fallback.sa_mask);
    (void)sigaction(signal_number, &fallback, NULL);
}

static void on_divide_error(int signal_number, siginfo_t *info, void *context)
{
    if (info->si_code == FPE_INTDIV && complete(context))
        return;
    pass_on(signal_number, info, context);
}

int arm_division_init(void)
{
    arm_division_shutdown();
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_divide_error;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigemptyset(&action.sa_mask) != 0 || sigaction(SIGFPE, &action, &division.previous) != 0) {
        perror("arm_division: sigaction(SIGFPE)");
        return ARM_DIVISION_ERR_SIGNAL;
    }
    division.installed = true;
    return ARM_DIVISION_OK;
}

#endif /* __x86_64__ */
