/*
 * host_macos_stack.c: running a function on a stack of the caller's choosing
 * on macOS (host_stack.h), for the battle runner's stack at 0x01800000
 * (game_hooks' PlatformCallWithStack).
 *
 * Followed: the System V AMD64 ABI, section 3.2 (the registers a callee
 * keeps: RBX, RBP, R12 to R15, MXCSR's control bits and the x87 control
 * word; RSP 16-byte aligned at a call), and AddressSanitizer's fiber
 * annotations (sanitizer/common_interface_defs.h, through host_internal.h).
 *
 * Why not ucontext, which Linux uses: macOS has deprecated getcontext,
 * makecontext and swapcontext since 10.6, and <ucontext.h> refuses to compile
 * without _XOPEN_SOURCE; under the platform's -Werror each call is an error
 * (probe M2, research/probes/README.md), and docs/C-RULES.md does not let a
 * warning be silenced. So the switch is written here, as Windows' is
 * (host_windows_stack.c), smaller for this ABI: host_macos_stack_enter saves
 * the caller's callee-saved registers on the caller's stack, moves RSP to the
 * given top, calls the entry, and on its return restores them;
 * host_macos_stack_resume comes back to that same restore from inside the
 * entry, for host_stack_leave. RBP stays the frame pointer of the switch's
 * frame on the caller's stack, which the call frame information names, so
 * that a debugger or an unwinder walks from the entry's frames to the
 * caller's.
 *
 * Single-threaded and not reentrant, as on Linux.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "host_internal.h"
#include "host_stack.h"

#if !defined(__x86_64__)
#error "host_macos_stack.c switches stacks for the System V AMD64 ABI only (the macOS build is x86_64)"
#endif

/* Saves the caller's registers on the caller's stack, writes where (the
 * frame) to *resume, then calls entry(context) with RSP at top, which must be
 * 16-byte aligned; returns when entry returns or host_macos_stack_resume is
 * called with that frame. */
void host_macos_stack_enter(void *top, void (*entry)(void *context), void *context, void **resume);

/* From inside the entry: restores the registers saved at resume and returns
 * from host_macos_stack_enter as if the entry had returned. */
_Noreturn void host_macos_stack_resume(void *resume);

/* The frame host_macos_stack_enter builds below the return address: RBP,
 * which then points at it, RBX and R12 to R15, then 8 bytes holding MXCSR (at
 * 0) and the x87 control word (at 4), which leave RSP 16-byte aligned. The
 * frame is RSP after them, 48 bytes below RBP. Mach-O's C names take a
 * leading underscore. */
__asm__(".text\n"
        ".globl _host_macos_stack_enter\n"
        ".p2align 4\n"
        "_host_macos_stack_enter:\n"
        "    .cfi_startproc\n"
        "    pushq %rbp\n"
        "    .cfi_def_cfa_offset 16\n"
        "    .cfi_offset %rbp, -16\n"
        "    movq %rsp, %rbp\n"
        "    .cfi_def_cfa_register %rbp\n"
        "    pushq %rbx\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    .cfi_offset %rbx, -24\n"
        "    .cfi_offset %r12, -32\n"
        "    .cfi_offset %r13, -40\n"
        "    .cfi_offset %r14, -48\n"
        "    .cfi_offset %r15, -56\n"
        "    subq $8, %rsp\n"
        "    stmxcsr (%rsp)\n"
        "    fnstcw 4(%rsp)\n"
        "    movq %rsp, (%rcx)\n" /* *resume = the frame */
        "    movq %rdi, %rsp\n"   /* the new stack's top */
        "    movq %rdx, %rdi\n"   /* entry(context) */
        "    callq *%rsi\n"
        "Lhost_macos_stack_restore:\n"
        "    leaq -48(%rbp), %rsp\n" /* back on the caller's stack, at the frame */
        "    ldmxcsr (%rsp)\n"
        "    fldcw 4(%rsp)\n"
        "    addq $8, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    .cfi_def_cfa %rsp, 8\n"
        "    retq\n"
        "    .cfi_endproc\n"
        "\n"
        ".globl _host_macos_stack_resume\n"
        ".p2align 4\n"
        "_host_macos_stack_resume:\n"
        "    leaq 48(%rdi), %rbp\n" /* the frame's RBP, which the restore counts from */
        "    jmp Lhost_macos_stack_restore\n");

/* The System V ABI wants RSP 16-byte aligned at a call (host_stack.h promises
 * a 16-byte aligned stack; its size must keep the top so). */
#define STACK_ALIGNMENT 16u

/* The one switch host_stack_run has in progress. */
static struct {
    void (*entry)(void *context);
    void *context;
    /* Where host_macos_stack_enter saved the caller's registers, while the
     * entry runs; NULL otherwise. */
    void *caller_frame;
    /* AddressSanitizer's view of the caller's stack, for switching back. */
    void *caller_fake_stack;
    const void *caller_stack_bottom;
    size_t caller_stack_size;
} switched;

/* The first function on the new stack: tells AddressSanitizer which stack is
 * current, runs the entry, and says it is leaving before the switch back. */
static void run_entry(void *unused)
{
    (void)unused;
    host_stack_finish_switch(NULL, &switched.caller_stack_bottom, &switched.caller_stack_size);
    switched.entry(switched.context);
    host_stack_start_switch(NULL, switched.caller_stack_bottom, switched.caller_stack_size);
}

int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context)
{
    const uintptr_t low = (uintptr_t)stack;
    if (size == 0 || low % STACK_ALIGNMENT != 0 || size % STACK_ALIGNMENT != 0 || size > UINTPTR_MAX - low) {
        (void)fprintf(stderr, "host_stack: a stack at %p of %zu bytes is not 16-byte aligned at both ends\n", stack,
                      size);
        return -1;
    }
    switched.entry = entry;
    switched.context = context;
    host_stack_start_switch(&switched.caller_fake_stack, stack, size);
    host_macos_stack_enter((unsigned char *)stack + size, run_entry, NULL, &switched.caller_frame);
    host_stack_finish_switch(switched.caller_fake_stack, NULL, NULL);
    switched.caller_frame = NULL;
    return 0;
}

void host_stack_leave(void)
{
    if (switched.caller_frame == NULL) {
        (void)fprintf(stderr, "host_stack: host_stack_leave outside host_stack_run\n");
        return;
    }
    host_stack_start_switch(NULL, switched.caller_stack_bottom, switched.caller_stack_size);
    host_macos_stack_resume(switched.caller_frame);
}
