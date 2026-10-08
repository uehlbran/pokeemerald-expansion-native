/*
 * game_hooks.c: the platform functions expansion's guarded game code calls
 * in place of its control-flow assembly (game_hooks.h says which, and why
 * each behaves as it does).
 *
 * Stands in for ARM and Thumb routines, not hardware, except the IRQ return
 * slot: GBATEK "BIOS RAM Usage" puts the BIOS's IRQ stack at the top of
 * IWRAM, where the test runner rewrites the saved return address. Followed:
 * expansion's own sources (test/test_runner.c, test/test_runner_battle.c,
 * src/main.c, src/multiboot.c); no reference port does these.
 *
 * Stack switching uses POSIX ucontext (getcontext, makecontext,
 * swapcontext, setcontext), which glibc provides on both widths, with
 * AddressSanitizer's fiber annotations
 * (sanitizer/common_interface_defs.h) when built with it, so ASan knows which
 * stack is current and does not misreport the switch.
 */
#include "game_hooks.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/common_interface_defs.h>
#endif

/* GBATEK "BIOS RAM Usage": 3007FA0h is the IRQ stack top; the BIOS IRQ
 * handler pushes r0-r3, r12 and lr below it, so the saved lr is at 3007F9Ch.
 * test/test_runner.c names it IRQ_LR. */
#define GAME_HOOKS_IRQ_LR_ADDRESS UINT32_C(0x03007F9C)

/* x86-64's ABI wants the stack 16-byte aligned (i386's GCC also keeps 16). */
#define GAME_HOOKS_STACK_ALIGNMENT 16

static struct {
    jmp_buf *exit_target;
    uint32_t exit_code;
    jmp_buf agb_main_loop;
    bool agb_main_loop_captured;
} hooks;

/* The one call PlatformCallWithStack has in progress. */
static struct {
    bool active;
    void (*entry)(void *context);
    void *context;
    ucontext_t caller;
    ucontext_t callee;
    /* A longjmp asked for from inside entry, done once back on the caller's stack. */
    jmp_buf *unwind_target;
    int unwind_value;
    /* AddressSanitizer's view of the caller's stack, for switching back. */
    void *caller_fake_stack;
    const void *caller_stack_bottom;
    size_t caller_stack_size;
} call;

static _Alignas(GAME_HOOKS_STACK_ALIGNMENT) unsigned char call_stack[GAME_HOOKS_CALL_STACK_SIZE];

/* AddressSanitizer's fiber annotations, or nothing without it. */
static void fiber_start_switch(void **fake_stack_save, const void *bottom, size_t size)
{
#ifdef __SANITIZE_ADDRESS__
    __sanitizer_start_switch_fiber(fake_stack_save, bottom, size);
#else
    (void)fake_stack_save;
    (void)bottom;
    (void)size;
#endif
}

static void fiber_finish_switch(void *fake_stack_save, const void **old_bottom, size_t *old_size)
{
#ifdef __SANITIZE_ADDRESS__
    __sanitizer_finish_switch_fiber(fake_stack_save, old_bottom, old_size);
#else
    (void)fake_stack_save;
    (void)old_bottom;
    (void)old_size;
#endif
}

int game_hooks_init(jmp_buf *exit_target)
{
    if (exit_target == NULL) {
        fprintf(stderr, "game_hooks: game_hooks_init: no exit target\n");
        return -1;
    }
    hooks.exit_target = exit_target;
    hooks.exit_code = 0;
    hooks.agb_main_loop_captured = false;
    return 0;
}

void game_hooks_shutdown(void)
{
    hooks.exit_target = NULL;
    hooks.agb_main_loop_captured = false;
}

void game_hooks_forget_agb_main_loop(void)
{
    hooks.agb_main_loop_captured = false;
}

uint32_t game_hooks_exit_code(void)
{
    return hooks.exit_code;
}

/* longjmp to target, from the caller's stack if entry is running on ours. */
static void unwind_to(jmp_buf *target, int value)
{
    if (call.active) {
        call.unwind_target = target;
        call.unwind_value = value;
        fiber_start_switch(NULL, call.caller_stack_bottom, call.caller_stack_size);
        setcontext(&call.caller); /* returns only on failure */
        fprintf(stderr, "game_hooks: setcontext back to the caller's stack failed\n");
    }
    longjmp(*target, value);
}

jmp_buf *PlatformAgbMainLoopTarget(void)
{
    hooks.agb_main_loop_captured = true;
    return &hooks.agb_main_loop;
}

void PlatformWaitCycles(uint32_t cycles)
{
    (void)cycles;
}

void PlatformTestRunnerExit(uint32_t exit_code)
{
    if (hooks.exit_target == NULL) {
        fprintf(stderr, "game_hooks: PlatformTestRunnerExit(%u) before game_hooks_init\n", exit_code);
        return;
    }
    hooks.exit_code = exit_code;
    unwind_to(hooks.exit_target, GAME_HOOKS_JUMP_EXIT);
}

void PlatformJumpToAgbMainLoop(void)
{
    if (!hooks.agb_main_loop_captured) {
        fprintf(stderr, "game_hooks: JumpToAgbMainLoop with no AgbMainLoop target "
                "(src/main.c captures it only when TESTING)\n");
        PlatformTestRunnerExit(GAME_HOOKS_EXIT_INTERNAL_ERROR);
        return;
    }
    unwind_to(&hooks.agb_main_loop, 1);
}

void game_hooks_take_irq_return(void)
{
    volatile uint32_t *irq_lr = (volatile uint32_t *)(uintptr_t)GAME_HOOKS_IRQ_LR_ADDRESS;
    if (*irq_lr == 0)
        return;
    *irq_lr = 0;
    PlatformJumpToAgbMainLoop();
}

/* The first function on the call stack: makecontext passes no pointer
 * arguments portably, so it reads the call from `call`. Returning resumes
 * the caller through uc_link. */
static void run_entry(void)
{
    fiber_finish_switch(NULL, &call.caller_stack_bottom, &call.caller_stack_size);
    call.entry(call.context);
    fiber_start_switch(NULL, call.caller_stack_bottom, call.caller_stack_size);
}

void PlatformCallWithStack(void (*entry)(void *context), void *context, void *stackTop)
{
    (void)stackTop; /* why: game_hooks.h */
    if (entry == NULL) {
        fprintf(stderr, "game_hooks: PlatformCallWithStack with no function\n");
        return;
    }
    if (call.active) {
        fprintf(stderr, "game_hooks: nested PlatformCallWithStack; running it on the current stack\n");
        entry(context);
        return;
    }
    if (getcontext(&call.callee) != 0) {
        perror("game_hooks: getcontext");
        return;
    }
    call.callee.uc_stack.ss_sp = call_stack;
    call.callee.uc_stack.ss_size = sizeof call_stack;
    call.callee.uc_link = &call.caller;
    makecontext(&call.callee, run_entry, 0);
    call.entry = entry;
    call.context = context;
    call.unwind_target = NULL;
    call.active = true;

    fiber_start_switch(&call.caller_fake_stack, call_stack, sizeof call_stack);
    int swapped = swapcontext(&call.caller, &call.callee);
    fiber_finish_switch(call.caller_fake_stack, NULL, NULL);
    call.active = false;
    if (swapped != 0) {
        perror("game_hooks: swapcontext");
        return;
    }
    if (call.unwind_target != NULL) {
        jmp_buf *target = call.unwind_target;
        call.unwind_target = NULL;
        longjmp(*target, call.unwind_value);
    }
}
