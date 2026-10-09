/*
 * arm_division.c: integer division by zero (and INT32_MIN by -1) completed
 * as the GBA's libgcc completes it (arm_division.h has the contract and
 * where the results come from).
 *
 * Followed: the Intel SDM, volume 2, for the encoding of DIV and IDIV
 * (opcode F7h /6 and /7, ModRM, SIB and displacements in 32-bit addressing)
 * and their #DE cases; signal-safety(7) for what the handler may do.
 */
#define _GNU_SOURCE /* REG_EAX and friends */
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
#define MODRM_MOD_REGISTER 3u
#define MODRM_RM_SIB 4u
#define MODRM_RM_DISP32 5u     /* with mod 0 */
#define SIB_INDEX_NONE 4u
#define SIB_BASE_DISP32 5u     /* with mod 0 */

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
static const int register_slots[8] = { REG_EAX, REG_ECX, REG_EDX, REG_EBX, REG_ESP, REG_EBP, REG_ESI, REG_EDI };

static uint32_t register_value(const greg_t *gregs, unsigned number)
{
    return (uint32_t)gregs[register_slots[number & 7u]];
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

/* Decodes the instruction at code. False for anything but a 32-bit DIV or
 * IDIV on a flat segment. The operand is readable: the CPU fetched it
 * before raising the divide error. */
static bool decode(const uint8_t *code, const greg_t *gregs, struct division_instruction *out)
{
    size_t i = 0;
    while (i < MAX_PREFIXES && is_flat_segment_prefix(code[i]))
        i++;
    if (code[i++] != OPCODE_GROUP3_32)
        return false;
    uint8_t modrm = code[i++];
    unsigned mod = modrm >> 6, reg = (modrm >> 3) & 7u, rm = modrm & 7u;
    if (reg != MODRM_REG_DIV && reg != MODRM_REG_IDIV)
        return false;
    out->is_signed = reg == MODRM_REG_IDIV;
    if (mod == MODRM_MOD_REGISTER) {
        out->divisor = register_value(gregs, rm);
        out->length = i;
        return true;
    }
    uint32_t address = 0;
    if (rm == MODRM_RM_SIB) {
        uint8_t sib = code[i++];
        unsigned scale = sib >> 6, index = (sib >> 3) & 7u, base = sib & 7u;
        if (index != SIB_INDEX_NONE)
            address += register_value(gregs, index) << scale;
        if (base == SIB_BASE_DISP32 && mod == 0) {
            address += read_u32((uintptr_t)&code[i]);
            i += 4;
        } else {
            address += register_value(gregs, base);
        }
    } else if (rm == MODRM_RM_DISP32 && mod == 0) {
        address = read_u32((uintptr_t)&code[i]);
        i += 4;
    } else {
        address = register_value(gregs, rm);
    }
    if (mod == 1) {
        int8_t displacement; /* sign-extended, Intel SDM vol. 2, 2.1.5 */
        memcpy(&displacement, &code[i], sizeof displacement);
        uint32_t offset = (uint32_t)(int32_t)displacement;
        address += offset;
        i += 1;
    } else if (mod == 2) {
        address += read_u32((uintptr_t)&code[i]);
        i += 4;
    }
    out->divisor = read_u32((uintptr_t)address);
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
        int32_t result = dividend > 0 ? INT32_MAX : dividend < 0 ? INT32_MIN : 0;
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

/* "arm_division: undecodable divide error at pc 0x12345678, passed on\n",
 * formatted without stdio (signal-safety(7): write is async-signal-safe). */
static void report_passed_on(const void *context)
{
    static const char head[] = "arm_division: undecodable divide error at pc 0x";
    static const char tail[] = ", passed on (not a 32-bit DIV or IDIV by zero, nor INT32_MIN / -1)\n";
    static const char hex[] = "0123456789abcdef";
    char line[sizeof head - 1 + 8 + sizeof tail - 1];
    uint32_t pc = (uint32_t)((const ucontext_t *)context)->uc_mcontext.gregs[REG_EIP];
    memcpy(line, head, sizeof head - 1);
    for (size_t i = 0; i < 8; i++)
        line[sizeof head - 1 + i] = hex[(pc >> (28u - 4u * (unsigned)i)) & 0xFu];
    memcpy(line + sizeof head - 1 + 8, tail, sizeof tail - 1);
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

#else /* not i386 */

int arm_division_init(void)
{
    arm_division_shutdown();
    fprintf(stderr, "arm_division: only the 32-bit build decodes x86 division; the 64-bit build stops here until "
            "the decoder is redone for x86-64 (arm_division.h)\n");
    return ARM_DIVISION_ERR_64_BIT;
}

#endif

void arm_division_shutdown(void)
{
    if (!division.installed)
        return;
    (void)sigaction(SIGFPE, &division.previous, NULL); /* restoring what sigaction gave back cannot fail */
    division.installed = false;
}
