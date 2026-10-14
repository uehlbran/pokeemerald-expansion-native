/*
 * test_host_stack_windows.c: what the Windows stack switch must tell the
 * system (host_stack.h, host_windows_stack.c), on the battle runner's stack
 * (host_memory_call_stack): while the entry runs, the thread's stack limits
 * hold both stacks, and afterwards they are the caller's again; a division by
 * zero there completes through the divide handler; a crash there is
 * dispatched up to the crash handler, through the switch's unwind
 * information (in a child, windows_support.h); and the registers a Win64
 * callee keeps, the SSE and x87 control words among them, are the caller's
 * after an entry that changed them and left early.
 */
#include <intrin.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <windows.h>

#include "host_fault.h"
#include "host_fault_fixture.h"
#include "host_memory.h"
#include "host_stack.h"
#include "ptest.h"
#include "windows_support.h"

static int run_on_the_call_stack(void (*entry)(void *context), void *context)
{
    const struct host_span stack = host_memory_call_stack();
    return host_stack_run((void *)stack.start, (size_t)(stack.end - stack.start), entry, context);
}

/* --- the stack limits ------------------------------------------------------------------ */

struct limits {
    uintptr_t base;  /* NT_TIB's StackBase: one past the top */
    uintptr_t limit; /* NT_TIB's StackLimit */
};

static struct limits current_limits(void)
{
    return (struct limits){.base = (uintptr_t)__readgsqword(offsetof(NT_TIB, StackBase)),
                           .limit = (uintptr_t)__readgsqword(offsetof(NT_TIB, StackLimit))};
}

static struct limits seen_inside;

static void record_limits(void *context)
{
    (void)context;
    seen_inside = current_limits();
}

PTEST(the_stack_limits_hold_both_stacks_while_the_entry_runs_and_are_the_callers_after)
{
    const struct limits before = current_limits();
    const uintptr_t here = (uintptr_t)__builtin_frame_address(0);
    PT_ASSERT_EQ_INT(run_on_the_call_stack(record_limits, NULL), 0);
    const struct limits after = current_limits();
    const struct host_span stack = host_memory_call_stack();
    PT_ASSERT(seen_inside.limit <= stack.start && seen_inside.base >= stack.end);
    PT_ASSERT(seen_inside.limit <= here && seen_inside.base > here);
    PT_ASSERT(after.base == before.base && after.limit == before.limit);
}

/* --- a division by zero on the stack -------------------------------------------------- */

/* DIV EDI (host_fault_fixture.h): two bytes. */
#define DIV_EDI_LENGTH 2u

static bool complete_with_marks(struct host_fault *fault)
{
    const uintptr_t pc = host_fault_register(fault, HOST_FAULT_PC);
    host_fault_set_register(fault, HOST_FAULT_ACCUMULATOR, QUOTIENT_MARK);
    host_fault_set_register(fault, HOST_FAULT_DATA, REMAINDER_MARK);
    host_fault_set_register(fault, HOST_FAULT_PC, pc + DIV_EDI_LENGTH);
    return true;
}

static struct {
    uint32_t quotient, remainder;
} divided;

static void divide_on_the_stack(void *context)
{
    (void)context;
    divide_by_zero(&divided.quotient, &divided.remainder);
}

PTEST(a_division_by_zero_on_the_given_stack_completes)
{
    PT_ASSERT_EQ_INT(host_fault_install_divide_error(complete_with_marks), 0);
    const int ran = run_on_the_call_stack(divide_on_the_stack, NULL);
    host_fault_uninstall_divide_error();
    PT_ASSERT_EQ_INT(ran, 0);
    PT_ASSERT_EQ_U32(divided.quotient, QUOTIENT_MARK);
    PT_ASSERT_EQ_U32(divided.remainder, REMAINDER_MARK);
}

/* --- a crash on the stack, in a child --------------------------------------------------- */

#define CHILD_CRASH_HANDLED 20u /* the crash handler got the fault on the battle runner's stack */
#define CHILD_WRONG_FAULT 21u

/* An address no one maps: in the first page. */
#define UNMAPPED_ADDRESS 0x10u

static void end_if_the_access_faulted(const struct host_fault *fault)
{
    wsup_end(host_fault_address(fault) == UNMAPPED_ADDRESS ? CHILD_CRASH_HANDLED : CHILD_WRONG_FAULT);
}

static void store_to_an_unmapped_address(void *context)
{
    (void)context;
    volatile uintptr_t where = UNMAPPED_ADDRESS; /* volatile: no constant address for the compiler to warn about */
    *(volatile uint8_t *)where = 1;
}

static void crash_on_the_call_stack(void)
{
    if (host_fault_install_crash(end_if_the_access_faulted) == 0)
        (void)run_on_the_call_stack(store_to_an_unmapped_address, NULL);
}

PTEST(a_crash_on_the_given_stack_reaches_the_crash_handler)
{
    uint32_t code = 0;
    PT_ASSERT(wsup_run_child("crash_on_stack", &code));
    PT_ASSERT_EQ_U32(code, CHILD_CRASH_HANDLED);
}

/* --- the registers a callee keeps ---------------------------------------------------- */

/* MXCSR with its rounding control (bits 13 and 14, Intel SDM vol. 1, 10.2.3)
 * set to round toward zero, and the x87 control word with its rounding
 * control (bits 10 and 11, 8.1.5) set the same way: the defaults are round
 * to nearest. */
#define MXCSR_ROUND_TOWARD_ZERO 0x6000u
#define X87_ROUND_TOWARD_ZERO 0x0C00u

static uint32_t read_mxcsr(void)
{
    uint32_t value = 0;
    __asm__ volatile("stmxcsr %0" : "=m"(value));
    return value;
}

static uint16_t read_x87_control(void)
{
    uint16_t value = 0;
    __asm__ volatile("fnstcw %0" : "=m"(value));
    return value;
}

/* Overwrites every register a Win64 callee keeps, and both control words,
 * then leaves without restoring any: host_stack_leave must. */
static void clobber_and_leave(void *context)
{
    (void)context;
    const uint32_t mxcsr = read_mxcsr() | MXCSR_ROUND_TOWARD_ZERO;
    const uint16_t x87 = (uint16_t)(read_x87_control() | X87_ROUND_TOWARD_ZERO);
    __asm__ volatile("ldmxcsr %0\n\t"
                     "fldcw %1\n\t"
                     "movq $-1, %%rbx\n\t"
                     "movq $-1, %%rsi\n\t"
                     "movq $-1, %%rdi\n\t"
                     "movq $-1, %%r12\n\t"
                     "movq $-1, %%r13\n\t"
                     "movq $-1, %%r14\n\t"
                     "movq $-1, %%r15\n\t"
                     "pcmpeqd %%xmm6, %%xmm6\n\t"
                     "pcmpeqd %%xmm7, %%xmm7\n\t"
                     "pcmpeqd %%xmm8, %%xmm8\n\t"
                     "pcmpeqd %%xmm9, %%xmm9\n\t"
                     "pcmpeqd %%xmm10, %%xmm10\n\t"
                     "pcmpeqd %%xmm11, %%xmm11\n\t"
                     "pcmpeqd %%xmm12, %%xmm12\n\t"
                     "pcmpeqd %%xmm13, %%xmm13\n\t"
                     "pcmpeqd %%xmm14, %%xmm14\n\t"
                     "pcmpeqd %%xmm15, %%xmm15"
                     :
                     : "m"(mxcsr), "m"(x87)
                     : "rbx", "rsi", "rdi", "r12", "r13", "r14", "r15", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10",
                       "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "memory");
    host_stack_leave();
}

PTEST(the_control_words_are_the_callers_after_a_leave)
{
    const uint32_t mxcsr = read_mxcsr();
    const uint16_t x87 = read_x87_control();
    PT_ASSERT_EQ_INT(run_on_the_call_stack(clobber_and_leave, NULL), 0);
    PT_ASSERT_EQ_U32(read_mxcsr(), mxcsr);
    PT_ASSERT_EQ_U32(read_x87_control(), x87);
}

#define GENERAL_MARK UINT64_C(0x0123456789ABCDEF)

/* Values pinned in the registers a callee keeps across the run (local
 * register variables, bound at the asm statements that read and write them),
 * and in XMM6 and XMM7, compared after it. */
PTEST(the_callers_registers_survive_a_leave)
{
    register uint64_t rbx __asm__("rbx") = GENERAL_MARK + 1u;
    register uint64_t rsi __asm__("rsi") = GENERAL_MARK + 2u;
    register uint64_t rdi __asm__("rdi") = GENERAL_MARK + 3u;
    register uint64_t r12 __asm__("r12") = GENERAL_MARK + 4u;
    register uint64_t r13 __asm__("r13") = GENERAL_MARK + 5u;
    register uint64_t r14 __asm__("r14") = GENERAL_MARK + 6u;
    register uint64_t r15 __asm__("r15") = GENERAL_MARK + 7u;
    register double xmm6 __asm__("xmm6") = 6.5;
    register double xmm7 __asm__("xmm7") = 7.25;
    __asm__ volatile(""
                     : "+r"(rbx), "+r"(rsi), "+r"(rdi), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15), "+x"(xmm6),
                       "+x"(xmm7));
    const int ran = run_on_the_call_stack(clobber_and_leave, NULL);
    __asm__ volatile(""
                     : "+r"(rbx), "+r"(rsi), "+r"(rdi), "+r"(r12), "+r"(r13), "+r"(r14), "+r"(r15), "+x"(xmm6),
                       "+x"(xmm7));
    PT_ASSERT_EQ_INT(ran, 0);
    PT_ASSERT(rbx == GENERAL_MARK + 1u && rsi == GENERAL_MARK + 2u && rdi == GENERAL_MARK + 3u);
    PT_ASSERT(r12 == GENERAL_MARK + 4u && r13 == GENERAL_MARK + 5u && r14 == GENERAL_MARK + 6u
              && r15 == GENERAL_MARK + 7u);
    PT_ASSERT(xmm6 == 6.5 && xmm7 == 7.25);
}

int main(void)
{
    const char *const name = wsup_child_name();
    if (name == NULL)
        return ptest_main_for(&ptest_tests, stdout);
    if (strcmp(name, "crash_on_stack") == 0)
        crash_on_the_call_stack();
    return (int)WSUP_CHILD_RETURNED;
}
