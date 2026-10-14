/*
 * test_host_fault.c: the faults the host hands the platform (host_fault.h).
 *
 * A division by zero, inline assembly with its operands in known registers,
 * completes through the installed handler, which reads every register
 * through the accessors and writes results the program then sees. Where a
 * fault goes when a handler does not complete it is tested in forked
 * children, in posix/test_host_fault_posix.c.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host_fault.h"
#include "host_fault_fixture.h"
#include "ptest.h"

/* DIV EDI: F7 /6 with ModRM 11 110 111 (Intel SDM vol. 2), the same bytes
 * on i386 and x86-64 (no REX). */
static const uint8_t div_edi[] = {0xF7, 0xF7};

/* ModRM register numbers (Intel SDM vol. 2, table 2-2). */
#define MODRM_EAX 0u
#define MODRM_ECX 1u
#define MODRM_EDX 2u
#define MODRM_EBX 3u
#define MODRM_ESI 6u
#define MODRM_EDI 7u
#define MODRM_R9 9u
/* A multiple of the register count at both widths (8 and 16). */
#define REGISTER_COUNT_MULTIPLE 16u

/* What the divide handler saw, read after the fault. */
static struct {
    bool called;
    bool divide_error;
    int number;
    uintptr_t pc, accumulator, data;
    uintptr_t eax, ecx, edx, ebx, esi, edi, esi_wrapped, r9;
} seen;

static bool complete_with_marks(struct host_fault *fault)
{
    seen.called = true;
    seen.divide_error = host_fault_is_divide_error(fault);
    seen.number = host_fault_number(fault);
    seen.pc = host_fault_register(fault, HOST_FAULT_PC);
    seen.accumulator = host_fault_register(fault, HOST_FAULT_ACCUMULATOR);
    seen.data = host_fault_register(fault, HOST_FAULT_DATA);
    seen.eax = host_fault_general_register(fault, MODRM_EAX);
    seen.ecx = host_fault_general_register(fault, MODRM_ECX);
    seen.edx = host_fault_general_register(fault, MODRM_EDX);
    seen.ebx = host_fault_general_register(fault, MODRM_EBX);
    seen.esi = host_fault_general_register(fault, MODRM_ESI);
    seen.edi = host_fault_general_register(fault, MODRM_EDI);
    seen.esi_wrapped = host_fault_general_register(fault, MODRM_ESI + REGISTER_COUNT_MULTIPLE);
    seen.r9 = host_fault_general_register(fault, MODRM_R9);
    if (memcmp((const void *)seen.pc, div_edi, sizeof div_edi) != 0)
        return false; /* not the division below: pass it on */
    host_fault_set_register(fault, HOST_FAULT_ACCUMULATOR, QUOTIENT_MARK);
    host_fault_set_register(fault, HOST_FAULT_DATA, REMAINDER_MARK);
    host_fault_set_register(fault, HOST_FAULT_PC, seen.pc + sizeof div_edi);
    return true;
}

PTEST(a_division_by_zero_completes_through_the_installed_handler)
{
    memset(&seen, 0, sizeof seen);
    uint32_t quotient = 0, remainder = 0;
    const int installed = host_fault_install_divide_error(complete_with_marks);
    if (installed == 0)
        divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT_EQ_INT(installed, 0);
    PT_ASSERT(seen.called);
    PT_ASSERT(seen.divide_error);
    PT_ASSERT_EQ_INT(seen.number, SIGFPE);
    PT_ASSERT_EQ_U32(quotient, QUOTIENT_MARK);
    PT_ASSERT_EQ_U32(remainder, REMAINDER_MARK);
}

PTEST(the_register_accessors_return_what_the_context_holds)
{
    memset(&seen, 0, sizeof seen);
    uint32_t quotient = 0, remainder = 0;
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(complete_with_marks), 0);
    divide_by_zero(&quotient, &remainder);
    host_fault_uninstall_divide_error();
    PT_ASSERT(seen.accumulator == DIVIDEND_LOW);
    PT_ASSERT(seen.data == EDX_MARK);
    PT_ASSERT(seen.eax == DIVIDEND_LOW);
    PT_ASSERT(seen.ecx == ECX_MARK);
    PT_ASSERT(seen.edx == EDX_MARK);
    PT_ASSERT(seen.ebx == EBX_MARK);
    PT_ASSERT(seen.esi == ESI_MARK);
    PT_ASSERT(seen.edi == 0);
    PT_ASSERT(seen.esi_wrapped == ESI_MARK);
#if defined(__x86_64__)
    PT_ASSERT(seen.r9 == R9_MARK);
#else
    PT_ASSERT(seen.r9 == seen.ecx); /* 9 is ECX, modulo i386's 8 registers */
#endif
}

PTEST(uninstall_is_safe_twice_and_without_install)
{
    host_fault_uninstall_divide_error();
    host_fault_uninstall_divide_error();
    host_fault_uninstall_crash();
    host_fault_uninstall_crash();
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(complete_with_marks), 0);
    host_fault_uninstall_divide_error();
    host_fault_uninstall_divide_error();
}

PTEST_MAIN()
