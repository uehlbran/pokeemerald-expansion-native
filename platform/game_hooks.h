/*
 * game_hooks.h: the platform functions expansion's guarded game code calls
 * where the GBA used a few lines of assembly to move the CPU somewhere else.
 *
 * Stands in for: MultiBootWaitCycles (a Thumb delay loop, src/multiboot.c),
 * JumpToAgbMainLoop (an ARM routine that reloads the stack pointer saved in
 * gAgbMainLoop_sp and jumps to AgbMainLoop, test/test_runner.c), the BIOS
 * Stop call the test runner exits with (swi 0x3, which mgba-rom-test turns
 * into its exit status), the two accesses to mGBA's debug registers that
 * have side effects (the handshake and the send, test/test_runner.c; the
 * registers are debug_channel's), and the battle test runner's stack switch
 * (five Thumb routines, test/test_runner_battle.c). Each guarded call site
 * is a row of docs/UPSTREAM_MODIFICATIONS.md. Design: docs/RESEARCH.md, "The
 * test runner on the native build".
 *
 * Every jump here is a longjmp or a ucontext switch, never exit(): main owns
 * the process's exit (game_hooks_init's target). Single-threaded.
 *
 * This header is included by game code (src/main.c, under PLATFORM_NATIVE
 * and TESTING), so it uses only the C library's types. u32 is uint32_t.
 */
#ifndef PLATFORM_GAME_HOOKS_H
#define PLATFORM_GAME_HOOKS_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

/* Bytes of the stack PlatformCallWithStack runs the battle tests' functions
 * on (see there). */
#define GAME_HOOKS_CALL_STACK_SIZE (1024u * 1024u)

/* The value game_hooks longjmps to main's exit target with (non-zero, since
 * setjmp's first return is 0). */
#define GAME_HOOKS_JUMP_EXIT 1

/* The exit code main returns when a hook cannot do what the game asked
 * (sysexits.h EX_SOFTWARE, "internal software error"). */
#define GAME_HOOKS_EXIT_INTERNAL_ERROR 70u

/* --- For main and the frame loop --------------------------------------- */

/* Keeps exit_target, where PlatformTestRunnerExit unwinds to with
 * GAME_HOOKS_JUMP_EXIT; the frame that called setjmp on it must stay live
 * while the game runs. Clears the stored exit code. Returns 0, or -1 (and
 * reports) for NULL. */
int game_hooks_init(jmp_buf *exit_target);

/* Forgets the exit target and the AgbMainLoop target. Safe at any time and
 * twice. */
void game_hooks_shutdown(void);

/* Forgets the AgbMainLoop target: main calls it before every entry into
 * AgbMain (power-on and soft reset), whose old frame is gone. */
void game_hooks_forget_agb_main_loop(void);

/* The code PlatformTestRunnerExit was given, for main to return. */
uint32_t game_hooks_exit_code(void);

/* longjmp(*target, value), first leaving the battle runner's stack if
 * PlatformCallWithStack's entry is running: the switch back to the caller's
 * stack is made, with AddressSanitizer's fiber annotations, and the call is
 * over before the jump. Every jump out of game code that can happen while a
 * battle test body runs goes through here: the hooks below, main's frame
 * exit, and the BIOS's SoftReset. value must not be 0. Does not return. */
_Noreturn void game_hooks_unwind(jmp_buf *target, int value);

/* --- For expansion's guarded code ---------------------------------------- */

/* src/main.c, under PLATFORM_NATIVE and TESTING, runs
 *     setjmp(*PlatformAgbMainLoopTarget());
 * in AgbMain just before it calls AgbMainLoop, where it saves
 * gAgbMainLoop_sp: the native JumpToAgbMainLoop longjmps back there, so
 * AgbMainLoop starts again on AgbMain's frame as the ARM routine does. */
jmp_buf *PlatformAgbMainLoopTarget(void);

/* src/multiboot.c MultiBootWaitCycles: a busy delay of `cycles` CPU cycles.
 * Returns at once: game logic takes no emulated time natively (time advances
 * at frame boundaries, docs/ARCHITECTURE.md "The frame"), and the delay only
 * paces a link-cable transfer, for which there is no hardware. */
void PlatformWaitCycles(uint32_t cycles);

/* test/test_runner.c JumpToAgbMainLoop: longjmps to the AgbMainLoop target
 * (from the battle runner's stack too, see PlatformCallWithStack). Without a
 * target (a build without TESTING) it reports and exits the game with
 * GAME_HOOKS_EXIT_INTERNAL_ERROR. Does not return.
 *
 * Also called by the runner's Timer 2 handler (Intr_Timer2) when a test
 * times out, where the GBA rewrote the BIOS's saved IRQ return address
 * (03007F9Ch, IRQ_LR) so that the interrupt returned into JumpToAgbMainLoop.
 * Natively the handler runs inside interrupt_dispatch, inside frame_run,
 * inside the game's VBlankIntrWait, possibly on the battle runner's stack;
 * the jump leaves all of them. None keeps state across the call that the
 * jump would leave stale: interrupt_dispatch's only skipped step is the
 * restore of IE and IME, which the Timer 2 handler leaves as they were (IME
 * 1, as during any handler but VCount's; IE unchanged), and the next frame
 * starts afresh from line 0. */
void PlatformJumpToAgbMainLoop(void);

/* test/test_runner.c MgbaExit_: stores exit_code for main to return and
 * unwinds to main's exit target. Without one, reports and returns. */
void PlatformTestRunnerExit(uint32_t exit_code);

/* test/test_runner.c MgbaOpen_: the debug registers' handshake
 * (debug_channel_open): true when the channel is on, as mGBA's ENABLE reads
 * 1DEAh after C0DEh is written; false otherwise, and the runner exits 2. */
bool PlatformDebugOpen(void);

/* test/test_runner.c MgbaPutchar_ and MgbaVPrintf_: the store to mGBA's
 * FLAGS register that sends the message in STRING (debug_channel_send). */
void PlatformDebugSend(uint16_t flags);

/* test/test_runner_battle.c: calls entry(context) on a separate stack and
 * returns when it returns. On the GBA the stack is the 1 KiB DATA.stack whose
 * top the runner passes as stackTop, so that the test function's locals stay
 * where they are after it returns (the runner and HP_BAR write results into
 * them through pointers taken during the call) and are at the same addresses
 * in the next call. 1 KiB holds Thumb frames, not x86 ones (larger, and
 * larger again under AddressSanitizer), and overflowing it would overwrite
 * the runner state it lives in; so stackTop is not used: every call starts
 * at the top of one platform-owned stack of GAME_HOOKS_CALL_STACK_SIZE
 * bytes, which nothing else uses, so locals survive between calls and land
 * at the same addresses each time, which is what the runner relies on. The
 * switch is a ucontext swap, annotated for AddressSanitizer's fiber support.
 * A longjmp out of entry (PlatformJumpToAgbMainLoop, PlatformTestRunnerExit)
 * first switches back to the caller's stack. Not reentrant: a call from
 * inside entry runs entry on the current stack, with a report. */
void PlatformCallWithStack(void (*entry)(void *context), void *context, void *stackTop);

#endif /* PLATFORM_GAME_HOOKS_H */
