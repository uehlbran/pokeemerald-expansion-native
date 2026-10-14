/*
 * test_game_hooks.c: the hooks expansion's guarded game code calls
 * (game_hooks.h), and the stand-ins for its unassembled assembly files: the
 * GameCube multiboot stubs and the ROM header (crt0.h is tested in
 * test_crt0.c, decompress_asm.h in test_decompress_asm.c).
 *
 * The expected behaviour comes from expansion's own sources: test/test_runner.c
 * and test/test_runner_battle.c for the hooks, src/libgcnmultiboot.s and
 * src/rom_header.s (with the Makefile's gbafix arguments) for the rest.
 */
// deps: gcn_multiboot.c rom_header.c gba_memmap.c debug_channel.c interrupt.c
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug_channel.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "host_memory.h"
#include "interrupt.h"
#include "ptest.h"
#include "test_support.h"


extern const uint8_t RomHeaderGameCode[4];
extern const uint8_t RomHeaderSoftwareVersion;
void GameCubeMultiBoot_Init(void *mb);
void GameCubeMultiBoot_Main(void *mb);
void GameCubeMultiBoot_ExecuteProgram(void *mb);
void GameCubeMultiBoot_HandleSerialInterrupt(void *mb);
void GameCubeMultiBoot_Quit(void);

/* IE/IF bit and gIntrTable slot of Timer 2 (GBATEK "GBA Interrupt Control";
 * src/main.c gIntrTableTemplate, where test/test_runner.c installs
 * Intr_Timer2 at gIntrTable[7]). */
#define IE_TIMER2 UINT16_C(0x0020)
#define TIMER2_SLOT 7u

static uintptr_t address_of(const volatile void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

/* On the platform's call stack, where the link put it (host_memory.h), below
 * 2^27 so that the battle runner's 27-bit address fields hold its locals'
 * addresses (include/test/battle.h, QueuedSubHitEvent). */
#define BATTLE_RUNNER_ADDRESS_LIMIT UINT32_C(0x08000000) /* 2^27, written apart from game_hooks.h */
_Static_assert(GAME_HOOKS_CALL_STACK_LIMIT == BATTLE_RUNNER_ADDRESS_LIMIT,
               "the call stack must end below the battle runner's 27-bit address fields");

static bool on_call_stack(uintptr_t address)
{
    const struct host_span stack = host_memory_call_stack();
    return address >= stack.start && address < stack.start + GAME_HOOKS_CALL_STACK_SIZE
           && address < BATTLE_RUNNER_ADDRESS_LIMIT;
}

/* --- PlatformCallWithStack ----------------------------------------------- */

/* The test functions below stand for a battle test's function. Like the
 * game's test objects (Makefile.native, GAME_TEST_SANITIZER_FLAGS), they are
 * not instrumented by AddressSanitizer, whose use-after-return detection
 * would move their locals to heap frames that die with the call: the whole
 * point is that the locals stay where they are. */
#define NO_ASAN __attribute__((no_sanitize("address"), noinline))

struct probe {
    volatile uint32_t *local_address; /* where the entry's local was */
    uint32_t seen_on_entry;           /* the local's bytes when the second call starts */
    unsigned calls;
};

static NO_ASAN void record_local(void *context)
{
    struct probe *probe = context;
    /* What the previous call's local holds now: the stack memory is the same
     * each time and nothing else uses it, so this is what the runner left. */
    if (probe->calls > 0)
        probe->seen_on_entry = *probe->local_address;
    volatile uint32_t local = UINT32_C(0x11111111);
    probe->local_address = &local;
    probe->calls++;
}

PTEST(call_with_stack_runs_entry_on_the_platform_stack)
{
    struct probe probe = {0};
    volatile uint32_t here = 0;
    PlatformCallWithStack(record_local, &probe, NULL);
    PT_ASSERT_EQ_INT(probe.calls, 1);
    /* The platform's stack, not the caller's, and low enough for the
     * runner's 27-bit address fields. */
    PT_ASSERT(on_call_stack(address_of(probe.local_address)));
    PT_ASSERT(!on_call_stack(address_of(&here)));
}

PTEST(locals_stay_put_between_calls)
{
    struct probe probe = {0};
    PlatformCallWithStack(record_local, &probe, NULL);
    volatile uint32_t *first = probe.local_address;
    /* After the call, the runner writes results into the function's locals
     * through pointers taken during it (HP_BAR captures). */
    *first = UINT32_C(0xC0FFEE42);
    PlatformCallWithStack(record_local, &probe, NULL);
    PT_ASSERT_EQ_INT(probe.calls, 2);
    PT_ASSERT(probe.local_address == first);
    PT_ASSERT_EQ_U32(probe.seen_on_entry, UINT32_C(0xC0FFEE42));
}

PTEST(call_with_stack_ignores_the_games_stack_top)
{
    /* The 1 KiB DATA.stack top the runner passes is not used (game_hooks.h). */
    unsigned char games_stack[64];
    memset(games_stack, 0xA5, sizeof games_stack);
    struct probe probe = {0};
    PlatformCallWithStack(record_local, &probe, &games_stack[sizeof games_stack]);
    PT_ASSERT_EQ_INT(probe.calls, 1);
    for (size_t i = 0; i < sizeof games_stack; i++)
        PT_ASSERT_EQ_INT(games_stack[i], 0xA5);
}

static void count_call(void *context)
{
    (*(unsigned *)context)++;
}

PTEST(call_with_stack_without_a_function_does_nothing)
{
    unsigned calls = 0;
    PlatformCallWithStack(NULL, &calls, NULL);
    PlatformCallWithStack(count_call, &calls, NULL);
    PT_ASSERT_EQ_INT(calls, 1);
}

static void nested_entry(void *context)
{
    PlatformCallWithStack(count_call, context, NULL);
}

PTEST(nested_call_runs_on_the_current_stack)
{
    unsigned calls = 0;
    PlatformCallWithStack(nested_entry, &calls, NULL);
    PT_ASSERT_EQ_INT(calls, 1);
    PlatformCallWithStack(count_call, &calls, NULL); /* not left active */
    PT_ASSERT_EQ_INT(calls, 2);
}

/* --- PlatformTestRunnerExit ---------------------------------------------- */

static jmp_buf exit_target;

static void exit_with_seven(void *context)
{
    (void)context;
    PlatformTestRunnerExit(7);
}

PTEST(test_runner_exit_unwinds_to_the_exit_target)
{
    volatile int jumps = 0;
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    if (setjmp(exit_target) == GAME_HOOKS_JUMP_EXIT)
        jumps++;
    if (jumps == 0)
        PlatformTestRunnerExit(3);
    PT_ASSERT_EQ_INT(jumps, 1);
    PT_ASSERT_EQ_U32(game_hooks_exit_code(), 3);
    game_hooks_shutdown();
}

PTEST(test_runner_exit_hands_main_an_exit_code_above_127_unchanged)
{
    volatile int jumps = 0;
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    if (setjmp(exit_target) == GAME_HOOKS_JUMP_EXIT)
        jumps++;
    if (jumps == 0)
        PlatformTestRunnerExit(200);
    PT_ASSERT_EQ_INT(jumps, 1);
    PT_ASSERT_EQ_U32(game_hooks_exit_code(), 200);
    game_hooks_shutdown();
}

PTEST(test_runner_exit_unwinds_from_the_battle_stack)
{
    volatile int jumps = 0;
    unsigned calls = 0;
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    if (setjmp(exit_target) == GAME_HOOKS_JUMP_EXIT)
        jumps++;
    if (jumps == 0)
        PlatformCallWithStack(exit_with_seven, NULL, NULL);
    PT_ASSERT_EQ_INT(jumps, 1);
    PT_ASSERT_EQ_U32(game_hooks_exit_code(), 7);
    /* The call is over: the next one runs normally. */
    PlatformCallWithStack(count_call, &calls, NULL);
    PT_ASSERT_EQ_INT(calls, 1);
    game_hooks_shutdown();
}

/* main's frame hook unwinds to its exit target through game_hooks_unwind,
 * and so does the BIOS's SoftReset; a battle test body can be waiting a
 * frame on the platform stack when that happens. */
static jmp_buf frame_exit_target;
#define FRAME_EXIT_JUMP 2

static void exit_from_a_frame(void *context)
{
    (void)context;
    game_hooks_unwind(&frame_exit_target, FRAME_EXIT_JUMP);
}

PTEST(an_unwind_from_the_battle_stack_ends_the_call)
{
    volatile int jumps = 0;
    struct probe probe = {0};
    if (setjmp(frame_exit_target) == FRAME_EXIT_JUMP)
        jumps++;
    if (jumps == 0)
        PlatformCallWithStack(exit_from_a_frame, NULL, NULL);
    PT_ASSERT_EQ_INT(jumps, 1);
    /* The next call is a normal one: on the platform stack, not nested. */
    PlatformCallWithStack(record_local, &probe, NULL);
    PT_ASSERT_EQ_INT(probe.calls, 1);
    PT_ASSERT(on_call_stack(address_of(probe.local_address)));
}

PTEST(an_unwind_outside_a_call_is_a_longjmp)
{
    volatile int jumps = 0;
    if (setjmp(frame_exit_target) == FRAME_EXIT_JUMP)
        jumps++;
    if (jumps == 0)
        game_hooks_unwind(&frame_exit_target, FRAME_EXIT_JUMP);
    PT_ASSERT_EQ_INT(jumps, 1);
}

PTEST(test_runner_exit_without_a_target_returns)
{
    game_hooks_shutdown();
    PlatformTestRunnerExit(9); /* reported; nothing to unwind to */
    PT_ASSERT(game_hooks_init(NULL) != 0);
}

/* --- PlatformJumpToAgbMainLoop ------------------------------------------- */

static void jump_to_main_loop(void *context)
{
    (void)context;
    PlatformJumpToAgbMainLoop();
}

PTEST(jump_to_agb_main_loop_returns_to_the_captured_frame)
{
    volatile int arrivals = 0;
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    setjmp(*PlatformAgbMainLoopTarget()); /* as src/main.c does before AgbMainLoop */
    arrivals++;
    if (arrivals == 1)
        PlatformJumpToAgbMainLoop();
    if (arrivals == 2)
        PlatformCallWithStack(jump_to_main_loop, NULL, NULL);
    PT_ASSERT_EQ_INT(arrivals, 3);
    game_hooks_shutdown();
}

PTEST(jump_to_agb_main_loop_without_a_target_exits_with_an_error)
{
    volatile int exits = 0;
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    game_hooks_forget_agb_main_loop();
    if (setjmp(exit_target) == GAME_HOOKS_JUMP_EXIT)
        exits++;
    if (exits == 0)
        PlatformJumpToAgbMainLoop();
    PT_ASSERT_EQ_INT(exits, 1);
    PT_ASSERT_EQ_U32(game_hooks_exit_code(), GAME_HOOKS_EXIT_INTERNAL_ERROR);
    game_hooks_shutdown();
}

/* The runner's Timer 2 handler on a timeout (Intr_Timer2): natively it
 * jumps to AgbMainLoop from inside the interrupt dispatch. */
static void timeout_handler(void)
{
    PlatformJumpToAgbMainLoop();
}

static void no_handler(void)
{
}

static interrupt_handler const timer2_table[INTERRUPT_TABLE_LENGTH] = {
    no_handler,      no_handler, no_handler, no_handler, no_handler, no_handler, no_handler,
    timeout_handler, no_handler, no_handler, no_handler, no_handler, no_handler, no_handler,
};

static volatile uint16_t *io_register(uint32_t offset)
{
    return (volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

static void dispatch_on_the_battle_stack(void *context)
{
    (void)context;
    (void)interrupt_dispatch();
}

PTEST(a_jump_from_an_interrupt_handler_leaves_the_dispatch_and_the_battle_stack)
{
    volatile int arrivals = 0;
    struct probe probe = {0};
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    PT_ASSERT_EQ_INT(interrupt_init(timer2_table, INTERRUPT_TABLE_LENGTH), INTERRUPT_OK);
    *io_register(GBA_IE_OFFSET) = IE_TIMER2;
    *io_register(GBA_IME_OFFSET) = GBA_IME_ENABLE;
    setjmp(*PlatformAgbMainLoopTarget());
    arrivals++;
    if (arrivals == 1) {
        interrupt_raise(GBA_IRQ_TIMER2);
        (void)interrupt_dispatch(); /* on the game's stack */
    } else if (arrivals == 2) {
        interrupt_raise(GBA_IRQ_TIMER2);
        PlatformCallWithStack(dispatch_on_the_battle_stack, NULL, NULL);
    }
    /* The request was acknowledged before the handler ran, IE and IME are as
     * the handler left them (as IntrMain's restore would have made them),
     * nothing is pending, and the battle runner's call is over. */
    uint16_t ie = *io_register(GBA_IE_OFFSET);
    uint16_t ime = *io_register(GBA_IME_OFFSET);
    unsigned handlers = interrupt_dispatch();
    PlatformCallWithStack(record_local, &probe, NULL);
    interrupt_shutdown();
    game_hooks_shutdown();
    PT_ASSERT_EQ_INT(arrivals, 3);
    PT_ASSERT_EQ_U32(ie, IE_TIMER2);
    PT_ASSERT_EQ_U32(ime, GBA_IME_ENABLE);
    PT_ASSERT_EQ_INT(handlers, 0);
    PT_ASSERT_EQ_INT(probe.calls, 1);
    PT_ASSERT(on_call_stack(address_of(probe.local_address)));
}

/* --- PlatformDebugOpen, PlatformDebugSend -------------------------------- */

PTEST(debug_hooks_reach_the_debug_channel)
{
    FILE *out = tsup_open_temp_stream();
    PT_ASSERT(out != NULL);
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    bool closed = PlatformDebugOpen(); /* no channel: a GBA without mGBA */
    PT_ASSERT_EQ_INT(debug_channel_init(out), DEBUG_CHANNEL_OK);
    bool opened = PlatformDebugOpen();
    volatile char *string = (volatile char *)(uintptr_t)DEBUG_CHANNEL_STRING_ADDRESS;
    string[0] = ':';
    string[1] = 'P';
    string[2] = '\0';
    PlatformDebugSend((uint16_t)(DEBUG_CHANNEL_FLAGS_SEND | 3u)); /* MGBA_LOG_INFO */
    char captured[32] = {0};
    PT_ASSERT_EQ_INT(fseek(out, 0, SEEK_SET), 0);
    size_t length = fread(captured, 1, sizeof captured - 1, out);
    debug_channel_shutdown();
    (void)fclose(out);
    PT_ASSERT(!closed);
    PT_ASSERT(opened);
    PT_ASSERT_EQ_INT((intmax_t)length, 14);
    PT_ASSERT_EQ_STR(captured, "GBA Debug: :P\n");
}

PTEST(wait_cycles_returns)
{
    PlatformWaitCycles(0);
    PlatformWaitCycles(UINT32_MAX);
}

/* --- PlatformIsRomPointer (src/field_name_box.c SetSpeaker) ------------- */

static const uint32_t rom_table[4] = {1, 2, 3, 4};
static uint32_t writable_word = 5;

/* cppcheck-suppress constParameterPointer ; a function pointer has no const pointee to add */
static uintptr_t address_of_code(void (*function)(void))
{
    uintptr_t address = 0;
    memcpy(&address, &function, sizeof address);
    return address;
}

PTEST(rom_pointers_are_the_executables_code_and_read_only_data)
{
    PT_ASSERT(PlatformIsRomPointer(address_of(rom_table)));
    PT_ASSERT(PlatformIsRomPointer(address_of(&rom_table[3])));
    PT_ASSERT(PlatformIsRomPointer(address_of("BRENDAN")));
    PT_ASSERT(PlatformIsRomPointer(address_of_code(GameCubeMultiBoot_Quit)));
}

PTEST(rom_pointers_exclude_writable_data_stacks_heap_regions_and_indexes)
{
    uint32_t local = writable_word;
    void *heap = malloc(16);
    const uintptr_t heap_address = address_of(heap);
    free(heap);
    PT_ASSERT(heap_address != 0);
    PT_ASSERT(!PlatformIsRomPointer(address_of(&writable_word)));
    PT_ASSERT(!PlatformIsRomPointer(address_of(&local)));
    PT_ASSERT(!PlatformIsRomPointer(heap_address));
    PT_ASSERT(!PlatformIsRomPointer(0));
    PT_ASSERT(!PlatformIsRomPointer(16)); /* a speaker index (SP_NAME_COUNT) */
    PT_ASSERT(!PlatformIsRomPointer(GBA_EWRAM_BASE));
    PT_ASSERT(!PlatformIsRomPointer(UINT32_C(0x08000000))); /* the GBA's ROM: nothing natively */
    PT_ASSERT(!PlatformIsRomPointer(UINTPTR_MAX));
}

/* --- GameCube multiboot and the ROM header ------------------------------- */

PTEST(gamecube_multiboot_reports_nothing_received)
{
    uint8_t mb[0x2C];
    memset(mb, 0xCD, sizeof mb);
    GameCubeMultiBoot_Init(mb);
    PT_ASSERT_EQ_INT(mb[2], 0); /* MBPROGRESS_NONE */
    GameCubeMultiBoot_Main(mb);
    GameCubeMultiBoot_HandleSerialInterrupt(mb);
    GameCubeMultiBoot_ExecuteProgram(mb);
    GameCubeMultiBoot_Quit();
    PT_ASSERT_EQ_INT(mb[2], 0);
    for (size_t i = 0; i < sizeof mb; i++) {
        if (i != 2)
            PT_ASSERT_EQ_INT(mb[i], 0xCD);
    }
    GameCubeMultiBoot_Init(NULL);
}

PTEST(rom_header_matches_gbafix)
{
    PT_ASSERT_MEM_EQ(RomHeaderGameCode, "BPEE", 4);
    PT_ASSERT_EQ_INT(RomHeaderSoftwareVersion, 0);
}

PTEST_MAIN()
