/*
 * host_stack.h: running a function on a stack of the caller's choosing.
 *
 * Stands in for the battle test runner's Thumb routines that move the stack
 * pointer (test/test_runner_battle.c, through game_hooks'
 * PlatformCallWithStack, which says why the stack must be a fixed one).
 *
 * Wraps, on Linux (host_linux.c): POSIX ucontext (getcontext, makecontext,
 * swapcontext, setcontext; glibc at both widths), with AddressSanitizer's
 * fiber annotations (sanitizer/common_interface_defs.h) when built with it,
 * so that ASan knows which stack is current and does not misreport the
 * switch. On Windows (host_windows_stack.c): a switch in assembly for the
 * Win64 ABI (fibers cannot run on a stack of the caller's choosing), which
 * widens the thread's stack limits (NT_TIB) to hold both stacks while the
 * entry runs, so that the system's exception dispatch walks from the entry's
 * frames to the caller's (research/m8-inventory.md, 1.2 row 11).
 * Single-threaded, not reentrant.
 */
#ifndef PLATFORM_HOST_STACK_H
#define PLATFORM_HOST_STACK_H

#include <stddef.h>

/* Calls entry(context) on the stack [stack, stack + size) and returns when
 * entry returns or calls host_stack_leave. Every call takes the same path
 * from the top of the stack to entry, so entry's frame is at the same address
 * each time. stack is 16-byte aligned. Returns 0, or -1 after a report on
 * stderr if the switch failed. */
int host_stack_run(void *stack, size_t size, void (*entry)(void *context), void *context);

/* From inside host_stack_run's entry: switches back to the caller of
 * host_stack_run, which returns 0 as if entry had returned. Returns only if
 * the switch failed, after a report on stderr. */
void host_stack_leave(void);

#endif /* PLATFORM_HOST_STACK_H */
