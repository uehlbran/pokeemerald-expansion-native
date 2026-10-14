/*
 * host_windows_stack.c: running a function on a stack of the caller's
 * choosing on Windows (host_stack.h), for the battle runner's stack at
 * 0x01801000 (game_hooks' PlatformCallWithStack).
 *
 * Followed: Microsoft's "x64 calling convention" and "x64 prolog and epilog"
 * (the registers a callee keeps, the 16-byte alignment, the 32 bytes of home
 * space, the unwind codes), GNU as's .seh_* directives for those codes, and
 * winnt.h's NT_TIB for the stack limits the system checks.
 *
 * Why not fibers: CreateFiber and CreateFiberEx allocate the fiber's stack
 * themselves, and the battle runner needs this one fixed stack (below 2^27,
 * the same addresses every call; game_hooks.h). So the switch is written
 * here, in assembly, as small as the ABI allows: host_windows_stack_enter
 * saves the registers a Win64 callee must keep (RBX, RBP, RDI, RSI, R12 to
 * R15, XMM6 to XMM15, MXCSR and the x87 control word) on the caller's stack,
 * moves RSP to the given top, calls the entry, and on its return restores
 * them; host_windows_stack_resume comes back to that same restore from
 * inside the entry, for host_stack_leave. No sanitizer annotates the switch:
 * MinGW-w64 has none.
 *
 * What the system must be told. Windows checks every frame it walks for an
 * exception, and every frame longjmp unwinds to, against the thread's stack
 * limits (NT_TIB's StackLimit and StackBase), and stops at one outside them.
 * While the entry runs, the limits are widened to cover both the caller's
 * stack and this one, so that a fault on this stack is dispatched through the
 * entry's frames, the switch's and the caller's, up to the unhandled-exception
 * filter (the crash reboot, host_windows_fault.c). For that walk the switch's
 * unwind information makes RBP its frame register: from inside the entry it
 * points at the caller's stack, where the saved registers and the return
 * address are. The limits are put back when the switch returns. A longjmp out
 * of the entry is game_hooks' to make after host_stack_leave, on the caller's
 * stack with its own limits.
 *
 * Single-threaded and not reentrant, as on Linux.
 */
#include <intrin.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <windows.h>

#include "host_stack.h"

#if !defined(__x86_64__)
#error "host_windows_stack.c switches stacks for the Win64 ABI only (the Windows build is 64-bit)"
#endif

/* Saves the caller's registers on the caller's stack, writes where (the frame)
 * to *resume, then calls entry(context) with RSP at top, which must be
 * 16-byte aligned; returns when entry returns or host_windows_stack_resume
 * is called with that frame. */
void host_windows_stack_enter(void *top, void (*entry)(void *context), void *context, void **resume);

/* From inside the entry: restores the registers saved at resume and returns
 * from host_windows_stack_enter as if the entry had returned. */
_Noreturn void host_windows_stack_resume(void *resume);

/* The frame host_windows_stack_enter builds below the return address: eight
 * pushed registers, then 168 bytes (XMM6 to XMM15 at 0 to 144, MXCSR at 160,
 * the x87 control word at 164; 168 keeps the XMM slots 16-byte aligned, as
 * movdqa needs). RBP is the frame register from the end of the prologue on. */
__asm__(".text\n"
        ".globl host_windows_stack_enter\n"
        ".def host_windows_stack_enter; .scl 2; .type 32; .endef\n"
        ".seh_proc host_windows_stack_enter\n"
        "host_windows_stack_enter:\n"
        "    pushq %rbp\n"
        "    .seh_pushreg %rbp\n"
        "    pushq %rbx\n"
        "    .seh_pushreg %rbx\n"
        "    pushq %rdi\n"
        "    .seh_pushreg %rdi\n"
        "    pushq %rsi\n"
        "    .seh_pushreg %rsi\n"
        "    pushq %r12\n"
        "    .seh_pushreg %r12\n"
        "    pushq %r13\n"
        "    .seh_pushreg %r13\n"
        "    pushq %r14\n"
        "    .seh_pushreg %r14\n"
        "    pushq %r15\n"
        "    .seh_pushreg %r15\n"
        "    subq $168, %rsp\n"
        "    .seh_stackalloc 168\n"
        "    movq %rsp, %rbp\n"
        "    .seh_setframe %rbp, 0\n"
        "    movdqa %xmm6, 0(%rsp)\n"
        "    .seh_savexmm %xmm6, 0\n"
        "    movdqa %xmm7, 16(%rsp)\n"
        "    .seh_savexmm %xmm7, 16\n"
        "    movdqa %xmm8, 32(%rsp)\n"
        "    .seh_savexmm %xmm8, 32\n"
        "    movdqa %xmm9, 48(%rsp)\n"
        "    .seh_savexmm %xmm9, 48\n"
        "    movdqa %xmm10, 64(%rsp)\n"
        "    .seh_savexmm %xmm10, 64\n"
        "    movdqa %xmm11, 80(%rsp)\n"
        "    .seh_savexmm %xmm11, 80\n"
        "    movdqa %xmm12, 96(%rsp)\n"
        "    .seh_savexmm %xmm12, 96\n"
        "    movdqa %xmm13, 112(%rsp)\n"
        "    .seh_savexmm %xmm13, 112\n"
        "    movdqa %xmm14, 128(%rsp)\n"
        "    .seh_savexmm %xmm14, 128\n"
        "    movdqa %xmm15, 144(%rsp)\n"
        "    .seh_savexmm %xmm15, 144\n"
        "    .seh_endprologue\n"
        "    stmxcsr 160(%rbp)\n"
        "    fnstcw 164(%rbp)\n"
        "    movq %rbp, (%r9)\n" /* *resume = the frame */
        "    movq %rcx, %rsp\n"  /* the new stack's top */
        "    subq $32, %rsp\n"   /* the entry's home space for its four register arguments */
        "    movq %r8, %rcx\n"   /* entry(context) */
        "    callq *%rdx\n"
        "host_windows_stack_restore:\n"
        "    movq %rbp, %rsp\n" /* back on the caller's stack */
        "    ldmxcsr 160(%rsp)\n"
        "    fldcw 164(%rsp)\n"
        "    movdqa 0(%rsp), %xmm6\n"
        "    movdqa 16(%rsp), %xmm7\n"
        "    movdqa 32(%rsp), %xmm8\n"
        "    movdqa 48(%rsp), %xmm9\n"
        "    movdqa 64(%rsp), %xmm10\n"
        "    movdqa 80(%rsp), %xmm11\n"
        "    movdqa 96(%rsp), %xmm12\n"
        "    movdqa 112(%rsp), %xmm13\n"
        "    movdqa 128(%rsp), %xmm14\n"
        "    movdqa 144(%rsp), %xmm15\n"
        "    addq $168, %rsp\n" /* the epilogue proper: add, pops, ret */
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rsi\n"
        "    popq %rdi\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    retq\n"
        ".seh_endproc\n"
        "\n"
        ".globl host_windows_stack_resume\n"
        ".def host_windows_stack_resume; .scl 2; .type 32; .endef\n"
        ".seh_proc host_windows_stack_resume\n"
        "host_windows_stack_resume:\n"
        "    .seh_endprologue\n"
        "    movq %rcx, %rbp\n"
        "    jmp host_windows_stack_restore\n"
        ".seh_endproc\n");

/* x64's ABI wants RSP 16-byte aligned at a call (host_stack.h promises a
 * 16-byte aligned stack; its size must keep the top so). */
#define STACK_ALIGNMENT 16u

/* The thread's stack limits, NT_TIB's StackBase (the top) and StackLimit (the
 * lowest committed byte), which GS addresses on x64. */
struct stack_limits {
    uintptr_t base;
    uintptr_t limit;
};

static struct stack_limits current_limits(void)
{
    return (struct stack_limits){.base = (uintptr_t)__readgsqword(offsetof(NT_TIB, StackBase)),
                                 .limit = (uintptr_t)__readgsqword(offsetof(NT_TIB, StackLimit))};
}

static void set_limits(struct stack_limits limits)
{
    __writegsqword(offsetof(NT_TIB, StackBase), limits.base);
    __writegsqword(offsetof(NT_TIB, StackLimit), limits.limit);
}

/* Limits that hold both the thread's own stack and [low, high). */
static struct stack_limits widened(struct stack_limits own, uintptr_t low, uintptr_t high)
{
    return (struct stack_limits){.base = own.base > high ? own.base : high, .limit = own.limit < low ? own.limit : low};
}

/* The frame host_windows_stack_enter saved the caller's registers in, while
 * the entry runs; NULL otherwise. */
static void *caller_frame;

/* The stack's ends are compared with the limits as integers, as NT_TIB keeps
 * them. */
int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context)
{
    const uintptr_t low = (uintptr_t)stack;
    if (size == 0 || low % STACK_ALIGNMENT != 0 || size % STACK_ALIGNMENT != 0 || size > UINTPTR_MAX - low) {
        (void)fprintf(stderr, "host_stack: a stack at %p of %zu bytes is not 16-byte aligned at both ends\n", stack,
                      size);
        return -1;
    }
    unsigned char *const top = (unsigned char *)stack + size;
    const struct stack_limits own = current_limits();
    set_limits(widened(own, low, (uintptr_t)top));
    host_windows_stack_enter(top, entry, context, &caller_frame);
    caller_frame = NULL;
    set_limits(own);
    return 0;
}

void host_stack_leave(void)
{
    if (caller_frame == NULL) {
        (void)fprintf(stderr, "host_stack: host_stack_leave outside host_stack_run\n");
        return;
    }
    host_windows_stack_resume(caller_frame);
}
