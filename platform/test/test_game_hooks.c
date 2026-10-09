/*
 * test_game_hooks.c: the hooks expansion's guarded game code calls
 * (game_hooks.h), and the stand-ins for its unassembled assembly files (crt0.h is tested in test_crt0.c):
 * decompress_asm.h, the GameCube multiboot stubs and the ROM header.
 *
 * The expected behaviour comes from expansion's own sources: test/test_runner.c
 * and test/test_runner_battle.c for the hooks, src/decompress_asm.s,
 * src/libgcnmultiboot.s and src/rom_header.s (with the Makefile's gbafix
 * arguments) for the rest; the LZ77 format from GBATEK "BIOS Decompression
 * Functions".
 */
// deps: decompress_asm.c gcn_multiboot.c rom_header.c gba_memmap.c debug_channel.c interrupt.c
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug_channel.h"
#include "decompress_asm.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "interrupt.h"
#include "ptest.h"


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

/* On the platform's call stack, which gba_regions.ld puts below 2^27 so that
 * the battle runner's 27-bit address fields hold its locals' addresses
 * (include/test/battle.h, QueuedSubHitEvent). */
#define BATTLE_RUNNER_ADDRESS_LIMIT (UINT32_C(1) << 27)

static bool on_call_stack(uintptr_t address)
{
    return address >= GAME_HOOKS_CALL_STACK_ADDRESS
           && address < GAME_HOOKS_CALL_STACK_ADDRESS + GAME_HOOKS_CALL_STACK_SIZE
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
    FILE *out = tmpfile();
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
    rewind(out);
    size_t length = fread(captured, 1, sizeof captured - 1, out);
    debug_channel_shutdown();
    fclose(out);
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

/* --- FastUnsafeCopy32 ---------------------------------------------------- */

#define COPY_GUARD 0xEE
#define COPY_AREA 160u

static void check_fast_copy(uint32_t size, size_t expected_bytes, size_t dest_offset)
{
    uint8_t source[COPY_AREA];
    uint8_t dest[COPY_AREA + 8];
    for (size_t i = 0; i < sizeof source; i++)
        source[i] = (uint8_t)(i + 1);
    memset(dest, COPY_GUARD, sizeof dest);
    FastUnsafeCopy32(dest + dest_offset, source, size);
    PT_ASSERT_MEM_EQ(dest + dest_offset, source, expected_bytes);
    for (size_t i = 0; i < dest_offset; i++)
        PT_ASSERT_EQ_INT(dest[i], COPY_GUARD);
    for (size_t i = dest_offset + expected_bytes; i < sizeof dest; i++)
        PT_ASSERT_EQ_INT(dest[i], COPY_GUARD);
}

PTEST(fast_unsafe_copy32_copies_whole_blocks)
{
    check_fast_copy(0, 32, 0); /* a block before the count is tested */
    check_fast_copy(1, 32, 0);
    check_fast_copy(32, 32, 0);
    check_fast_copy(33, 64, 0); /* rounded up */
    check_fast_copy(96, 96, 0);
    check_fast_copy(97, 128, 0);
}

PTEST(fast_unsafe_copy32_negative_size_copies_one_block)
{
    /* bgt after subs: a size with bit 31 set is negative, one block. */
    check_fast_copy(UINT32_C(0x80000000), 32, 0);
    check_fast_copy(UINT32_MAX, 32, 0);
}

PTEST(fast_unsafe_copy32_any_alignment)
{
    check_fast_copy(40, 64, 1);
    check_fast_copy(8, 32, 3);
}

/* --- LZ77UnCompWRAMOptimized --------------------------------------------- */

#define LZ77_TYPE 0x10u /* GBATEK: bits 4-7 compressed type 1 */

static uint32_t lz_header(uint32_t size)
{
    return (size << 8) | LZ77_TYPE;
}

/* Builds a stream: header then the payload bytes, word-aligned. */
static void lz_stream(uint32_t *stream, size_t words, uint32_t size, const uint8_t *payload, size_t payload_size)
{
    memset(stream, 0, words * sizeof *stream);
    stream[0] = lz_header(size);
    memcpy(&stream[1], payload, payload_size);
}

PTEST(lz77_literals)
{
    static const uint8_t payload[] = {0x00, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0x00, 'I'};
    uint32_t stream[8];
    uint8_t out[16];
    lz_stream(stream, 8, 9, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(out, "ABCDEFGHI", 9);
    for (size_t i = 9; i < sizeof out; i++)
        PT_ASSERT_EQ_INT(out[i], COPY_GUARD);
}

PTEST(lz77_overlapping_back_reference_repeats)
{
    /* 'x', then copy 18 bytes from 1 back: 19 'x'. Flags 0100 0000. */
    static const uint8_t payload[] = {0x40, 'x', 0xF0, 0x00};
    uint32_t stream[4];
    uint8_t out[24];
    lz_stream(stream, 4, 19, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    for (size_t i = 0; i < 19; i++)
        PT_ASSERT_EQ_INT(out[i], 'x');
    PT_ASSERT_EQ_INT(out[19], COPY_GUARD);
}

PTEST(lz77_back_reference_distance_and_count)
{
    /* "abc", then copy 3 from 3 back ("abc"), then 4 from 2 back ("bcbc"). */
    static const uint8_t payload[] = {0x18, 'a', 'b', 'c', 0x00, 0x02, 0x10, 0x01};
    uint32_t stream[4];
    uint8_t out[16];
    lz_stream(stream, 4, 10, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(out, "abcabcbcbc", 10);
    PT_ASSERT_EQ_INT(out[10], COPY_GUARD);
}

PTEST(lz77_largest_distance)
{
    /* 4096 literal bytes, then a 3-byte copy from 4096 back (the 12-bit
     * distance field at its maximum, 0xFFF, plus one). */
    enum { LITERALS = 4096, TOTAL = LITERALS + 3 };
    static uint8_t payload[LITERALS / 8 * 9 + 3];
    static uint32_t stream[(sizeof payload + 8) / 4 + 1];
    static uint8_t out[TOTAL + 4];
    size_t p = 0;
    for (size_t i = 0; i < LITERALS; i++) {
        if (i % 8 == 0)
            payload[p++] = 0x00;
        payload[p++] = (uint8_t)(i * 7 + 1);
    }
    payload[p++] = 0x80;
    payload[p++] = 0x0F;
    payload[p++] = 0xFF;
    lz_stream(stream, sizeof stream / 4, TOTAL, payload, p);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_MEM_EQ(&out[LITERALS], &out[0], 3);
    PT_ASSERT_EQ_INT(out[TOTAL], COPY_GUARD);
}

PTEST(lz77_stops_at_the_headers_size)
{
    /* A last block of 18 bytes when only 5 remain: exactly the size is
     * written (the assembly would write all 18). */
    static const uint8_t payload[] = {0x40, 'z', 0xF0, 0x00};
    uint32_t stream[4];
    uint8_t out[24];
    lz_stream(stream, 4, 6, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    for (size_t i = 0; i < 6; i++)
        PT_ASSERT_EQ_INT(out[i], 'z');
    PT_ASSERT_EQ_INT(out[6], COPY_GUARD);
}

PTEST(lz77_zero_size_writes_nothing)
{
    static const uint8_t payload[] = {0x00, 'q'};
    uint32_t stream[4];
    uint8_t out[4];
    lz_stream(stream, 4, 0, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], COPY_GUARD);
}

PTEST(lz77_reference_before_the_output_stops)
{
    /* 'a', then a copy from 2 back: before the first byte. */
    static const uint8_t payload[] = {0x40, 'a', 0x00, 0x01};
    uint32_t stream[4];
    uint8_t out[8];
    lz_stream(stream, 4, 4, payload, sizeof payload);
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], 'a');
    PT_ASSERT_EQ_INT(out[1], COPY_GUARD);
}

PTEST(lz77_type_bits_are_not_checked)
{
    /* As the assembly: only bits 8-31 of the header are read. */
    static const uint8_t payload[] = {0x00, 'k'};
    uint32_t stream[4];
    uint8_t out[4];
    lz_stream(stream, 4, 1, payload, sizeof payload);
    stream[0] = (1u << 8) | 0x2Fu;
    memset(out, COPY_GUARD, sizeof out);
    LZ77UnCompWRAMOptimized(stream, out);
    PT_ASSERT_EQ_INT(out[0], 'k');
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
    PT_ASSERT(heap != NULL);
    PT_ASSERT(!PlatformIsRomPointer(address_of(&writable_word)));
    PT_ASSERT(!PlatformIsRomPointer(address_of(&local)));
    PT_ASSERT(!PlatformIsRomPointer(address_of(heap)));
    free(heap);
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
