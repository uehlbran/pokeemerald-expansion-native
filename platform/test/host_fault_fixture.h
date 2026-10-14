/*
 * host_fault_fixture.h: what test_host_fault.c and posix/test_host_fault_posix.c
 * share: a division by zero, inline assembly with its operands in known
 * registers, and the marks that tell which register a value came from.
 */
#ifndef PLATFORM_TEST_HOST_FAULT_FIXTURE_H
#define PLATFORM_TEST_HOST_FAULT_FIXTURE_H

#include <stdint.h>

/* A different value in every register the division names or marks, so that
 * a slot read from the wrong register shows. The divisor, EDI, is zero; EDX
 * is the dividend's high half (a division by zero faults whatever it is). */
#define DIVIDEND_LOW 0x12345678u
#define ECX_MARK 0x0C0C0C0Cu
#define EDX_MARK 0x0D0D0D0Du
#define EBX_MARK 0x0B0B0B0Bu
#define ESI_MARK 0x5E5E5E5Eu
#define R9_MARK 0x0909090909090909u
#define QUOTIENT_MARK 0xAAAA5555u
#define REMAINDER_MARK 0x5555AAAAu

/* EDX:EAX / EDI, EDI zero, with ECX, EBX, ESI (and on x86-64 R9) marked;
 * *quotient and *remainder are EAX and EDX after it. */
static inline void divide_by_zero(uint32_t *quotient, uint32_t *remainder)
{
    uint32_t eax = DIVIDEND_LOW;
    uint32_t edx = EDX_MARK;
    const uint32_t zero = 0;
#if defined(__x86_64__)
    register uint64_t r9 __asm__("r9") = R9_MARK;
    __asm__ volatile("divl %[divisor]"
                     : "+a"(eax), "+d"(edx)
                     : [divisor] "D"(zero), "c"(ECX_MARK), "b"(EBX_MARK), "S"(ESI_MARK), "r"(r9)
                     : "cc", "memory");
#else
    __asm__ volatile("divl %[divisor]"
                     : "+a"(eax), "+d"(edx)
                     : [divisor] "D"(zero), "c"(ECX_MARK), "b"(EBX_MARK), "S"(ESI_MARK)
                     : "cc", "memory");
#endif
    *quotient = eax;
    *remainder = edx;
}

#endif /* PLATFORM_TEST_HOST_FAULT_FIXTURE_H */
