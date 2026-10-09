/*
 * game_hooks.c: the platform functions expansion's guarded game code calls
 * in place of its control-flow assembly (game_hooks.h says which, and why
 * each behaves as it does).
 *
 * Stands in for ARM and Thumb routines, not hardware; the debug registers
 * the two debug hooks reach are debug_channel's. Followed: expansion's own
 * sources (test/test_runner.c, test/test_runner_battle.c, src/main.c,
 * src/multiboot.c); no reference port does these.
 *
 * Stack switching uses POSIX ucontext (getcontext, makecontext,
 * swapcontext, setcontext), which glibc provides on both widths, with
 * AddressSanitizer's fiber annotations
 * (sanitizer/common_interface_defs.h) when built with it, so ASan knows which
 * stack is current and does not misreport the switch.
 */
#include "game_hooks.h"

#include "debug_channel.h"

#include <link.h> /* ElfW */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <ucontext.h>

#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/common_interface_defs.h>
#endif

/* x86-64's ABI wants the stack 16-byte aligned (i386's GCC also keeps 16). */
#define GAME_HOOKS_STACK_ALIGNMENT 16

/* Stack between run_entry's frame and entry's. Before and after entry,
 * run_entry calls AddressSanitizer's fiber functions, whose frames go below
 * its own; entry's locals must stay untouched between calls, so entry runs
 * below this gap, which those calls stay within (they use well under a
 * KiB). */
#define GAME_HOOKS_CALL_STACK_GAP (64u * 1024u)

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

/* gba_regions.ld's .platform_call_stack (game_hooks.h says why there). */
extern unsigned char platform_call_stack_start[], platform_call_stack_end[];

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

void game_hooks_unwind(jmp_buf *target, int value)
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
    game_hooks_unwind(hooks.exit_target, GAME_HOOKS_JUMP_EXIT);
}

void PlatformJumpToAgbMainLoop(void)
{
    if (!hooks.agb_main_loop_captured) {
        fprintf(stderr, "game_hooks: JumpToAgbMainLoop with no AgbMainLoop target "
                        "(src/main.c captures it only when TESTING)\n");
        PlatformTestRunnerExit(GAME_HOOKS_EXIT_INTERNAL_ERROR);
        return;
    }
    game_hooks_unwind(&hooks.agb_main_loop, GAME_HOOKS_JUMP_AGB_MAIN_LOOP);
}

bool PlatformDebugOpen(void)
{
    return debug_channel_open();
}

void PlatformDebugSend(uint16_t flags)
{
    debug_channel_send(flags);
}

/* Calls entry below GAME_HOOKS_CALL_STACK_GAP bytes of its own frame. Not
 * instrumented, so that AddressSanitizer's use-after-return detection does
 * not move the gap off the stack. */
__attribute__((noinline, no_sanitize("address"))) static void call_entry_below_gap(void)
{
    unsigned char gap[GAME_HOOKS_CALL_STACK_GAP];
    /* An empty asm that takes the gap's address, so the compiler keeps it. */
    __asm__ volatile("" : : "r"(gap) : "memory");
    call.entry(call.context);
}

/* The first function on the call stack: makecontext passes no pointer
 * arguments portably, so it reads the call from `call`. Returning resumes
 * the caller through uc_link. Every call takes the same path from the same
 * stack top, so entry's frame is at the same address each time. */
static void run_entry(void)
{
    fiber_finish_switch(NULL, &call.caller_stack_bottom, &call.caller_stack_size);
    call_entry_below_gap();
    fiber_start_switch(NULL, call.caller_stack_bottom, call.caller_stack_size);
}

/* The section's place and size are the linker script's; this checks they
 * are what the header says. Flagged 64-bit: addresses compared as integers. */
static bool call_stack_is_linked(void)
{
    uintptr_t start = (uintptr_t)platform_call_stack_start;
    uintptr_t end = (uintptr_t)platform_call_stack_end;
    return start == GAME_HOOKS_CALL_STACK_ADDRESS && end - start == GAME_HOOKS_CALL_STACK_SIZE
           && start % GAME_HOOKS_STACK_ALIGNMENT == 0;
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
    if (!call_stack_is_linked()) {
        fprintf(stderr, "game_hooks: the call stack is not linked at %#x with %u bytes (gba_regions.ld)\n",
                GAME_HOOKS_CALL_STACK_ADDRESS, GAME_HOOKS_CALL_STACK_SIZE);
        return;
    }
    if (getcontext(&call.callee) != 0) {
        perror("game_hooks: getcontext");
        return;
    }
    call.callee.uc_stack.ss_sp = platform_call_stack_start;
    call.callee.uc_stack.ss_size = GAME_HOOKS_CALL_STACK_SIZE;
    call.callee.uc_link = &call.caller;
    makecontext(&call.callee, run_entry, 0);
    call.entry = entry;
    call.context = context;
    call.unwind_target = NULL;
    call.active = true;

    fiber_start_switch(&call.caller_fake_stack, platform_call_stack_start, GAME_HOOKS_CALL_STACK_SIZE);
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

/* --- PlatformIsRomPointer -------------------------------------------------- */

bool PlatformIsRomPointer(uintptr_t address)
{
    const ElfW(Phdr) *headers = (const ElfW(Phdr) *)(uintptr_t)getauxval(AT_PHDR);
    size_t count = (size_t)getauxval(AT_PHNUM);
    if (headers == NULL) /* getauxval gives 0 when the kernel passed no AT_PHDR: then nothing reads as ROM */
        return false;
    /* The executable is non-PIE (load bias 0); PT_PHDR gives the bias anyway. */
    uintptr_t bias = 0;
    for (size_t i = 0; i < count; i++) {
        if (headers[i].p_type == PT_PHDR)
            bias = (uintptr_t)headers - (uintptr_t)headers[i].p_vaddr;
    }
    for (size_t i = 0; i < count; i++) {
        const ElfW(Phdr) *header = &headers[i];
        if (header->p_type != PT_LOAD || (header->p_flags & PF_W) != 0)
            continue;
        uintptr_t start = bias + (uintptr_t)header->p_vaddr;
        if (address >= start && address - start < (uintptr_t)header->p_memsz)
            return true;
    }
    return false;
}
