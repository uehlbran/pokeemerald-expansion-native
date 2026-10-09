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
 * The stack switch is host_stack's (with AddressSanitizer's fiber
 * annotations), the stack host_memory's, and the executable's read-only
 * segments host_image's.
 */
#include "game_hooks.h"

#include "debug_channel.h"
#include "host_image.h"
#include "host_memory.h"
#include "host_stack.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* x86-64's ABI wants the stack 16-byte aligned (i386's GCC also keeps 16). */
#define GAME_HOOKS_STACK_ALIGNMENT 16

/* Stack between the frame host_stack starts the call stack with and entry's.
 * Before and after entry, host_stack calls AddressSanitizer's fiber
 * functions, whose frames go below its own; entry's locals must stay
 * untouched between calls, so entry runs below this gap, which those calls
 * stay within (they use well under a KiB). */
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
    /* A longjmp asked for from inside entry, done once back on the caller's stack. */
    jmp_buf *unwind_target;
    int unwind_value;
} call;

int game_hooks_init(jmp_buf *exit_target)
{
    if (exit_target == NULL) {
        (void)fprintf(stderr, "game_hooks: game_hooks_init: no exit target\n");
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
        host_stack_leave(); /* returns only on failure, reported */
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
        (void)fprintf(stderr, "game_hooks: PlatformTestRunnerExit(%u) before game_hooks_init\n", exit_code);
        return;
    }
    hooks.exit_code = exit_code;
    game_hooks_unwind(hooks.exit_target, GAME_HOOKS_JUMP_EXIT);
}

void PlatformJumpToAgbMainLoop(void)
{
    if (!hooks.agb_main_loop_captured) {
        (void)fprintf(stderr, "game_hooks: JumpToAgbMainLoop with no AgbMainLoop target "
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

/* The function host_stack starts on the call stack: calls entry below
 * GAME_HOOKS_CALL_STACK_GAP bytes of its own frame. Every call takes the same
 * path from the same stack top, so entry's frame is at the same address each
 * time. Not instrumented, so that AddressSanitizer's use-after-return
 * detection does not move the gap off the stack. */
__attribute__((noinline, no_sanitize("address"))) static void call_entry_below_gap(void *unused)
{
    (void)unused; /* the call is in `call` */
    unsigned char gap[GAME_HOOKS_CALL_STACK_GAP];
    /* An empty asm that takes the gap's address, so the compiler keeps it. */
    __asm__ volatile("" : : "r"(gap) : "memory");
    call.entry(call.context);
}

/* The stack's place and size are the link's (host_memory); this checks they
 * are what the header says. */
static bool call_stack_is_linked(void)
{
    const struct host_span stack = host_memory_call_stack();
    return stack.start == GAME_HOOKS_CALL_STACK_ADDRESS && stack.end - stack.start == GAME_HOOKS_CALL_STACK_SIZE
           && stack.start % GAME_HOOKS_STACK_ALIGNMENT == 0;
}

void PlatformCallWithStack(void (*entry)(void *context), void *context, void *stackTop)
{
    (void)stackTop; /* why: game_hooks.h */
    if (entry == NULL) {
        (void)fprintf(stderr, "game_hooks: PlatformCallWithStack with no function\n");
        return;
    }
    if (call.active) {
        (void)fprintf(stderr, "game_hooks: nested PlatformCallWithStack; running it on the current stack\n");
        entry(context);
        return;
    }
    if (!call_stack_is_linked()) {
        (void)fprintf(stderr, "game_hooks: the call stack is not linked at %#x with %zu bytes (gba_regions.ld)\n",
                      GAME_HOOKS_CALL_STACK_ADDRESS, GAME_HOOKS_CALL_STACK_SIZE);
        return;
    }
    call.entry = entry;
    call.context = context;
    call.unwind_target = NULL;
    call.active = true;
    const int ran = host_stack_run((void *)host_memory_call_stack().start, GAME_HOOKS_CALL_STACK_SIZE,
                                   call_entry_below_gap, NULL);
    call.active = false;
    if (ran != 0) /* reported */
        return;
    if (call.unwind_target != NULL) {
        jmp_buf *target = call.unwind_target;
        call.unwind_target = NULL;
        longjmp(*target, call.unwind_value);
    }
}

/* --- PlatformIsRomPointer -------------------------------------------------- */

bool PlatformIsRomPointer(uintptr_t address)
{
    return host_image_is_read_only(address);
}
