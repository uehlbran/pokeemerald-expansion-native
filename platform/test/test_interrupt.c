// deps: gba_memmap.c
/*
 * Tests of interrupt, the interrupt controller and IntrMain.
 *
 * The register addresses and bits are written out here from GBATEK "GBA
 * Interrupt Control", and IntrMain's search order and gIntrTable slots from
 * src/crt0.s:53-96 and src/main.c gIntrTableTemplate, instead of being taken
 * from the module's header, so the header is under test too; they are checked
 * against expansion's own names at compile time.
 *
 * Every handler table holds counting handlers, one per gIntrTable slot, that
 * log the order they ran in and the IME and IF they saw. Each test starts
 * with setup(), which clears the registers and counters it uses and
 * initialises the controller with a fresh table, so a test that fails early
 * leaves nothing that a later test depends on.
 */
#include <gba/defines.h>
#include <gba/io_reg.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gba_memmap.h"
#include "interrupt.h"
#include "ptest.h"

/* GBATEK "GBA Interrupt Control". */
#define IE_ADDRESS 0x04000200u
#define IF_ADDRESS 0x04000202u
#define IME_ADDRESS 0x04000208u
#define ALL_SOURCES 0x3FFFu /* IE and IF bits 0-13; 14-15 not used */
#define IF_BIT_15 0x8000u   /* not used by the hardware; interrupt.h's marker */
/* GBATEK "GBA Sound Control Registers": SOUNDCNT_X at 4000084h, bit 7 is the
 * PSG/FIFO master enable. */
#define SOUNDCNT_X_ADDRESS 0x04000084u
#define SOUNDCNT_X_MASTER_ENABLE 0x0080u
/* GBATEK "BIOS Interrupt handling"; include/gba/defines.h INTR_CHECK. */
#define INTR_CHECK_ADDRESS 0x03007FF8u

/* expansion's gIntrTable has 14 entries (src/main.c INTR_COUNT). */
#define TABLE_LENGTH 14u
/* The slots of gIntrTable that IntrMain calls for each source: the source's
 * position in its search, src/crt0.s:53-93 (r12 += 4 per source passed). */
#define SLOT_VCOUNT 0u
#define SLOT_SERIAL 1u
#define SLOT_TIMER3 2u
#define SLOT_HBLANK 3u
#define SLOT_VBLANK 4u
#define SLOT_TIMER0 5u
#define SLOT_TIMER1 6u
#define SLOT_TIMER2 7u
#define SLOT_DMA0 8u
#define SLOT_DMA1 9u
#define SLOT_DMA2 10u
#define SLOT_DMA3 11u
#define SLOT_KEYPAD 12u
#define SLOT_SPURIOUS 13u /* reached only with nothing pending; Game Pak halts instead */

_Static_assert(GBA_IRQ_COUNT == 14, "GBATEK has fourteen interrupt sources");
_Static_assert((1u << GBA_IRQ_VBLANK) == INTR_FLAG_VBLANK, "VBlank bit");
_Static_assert((1u << GBA_IRQ_HBLANK) == INTR_FLAG_HBLANK, "HBlank bit");
_Static_assert((1u << GBA_IRQ_VCOUNT) == INTR_FLAG_VCOUNT, "VCount bit");
_Static_assert((1u << GBA_IRQ_TIMER0) == INTR_FLAG_TIMER0, "Timer 0 bit");
_Static_assert((1u << GBA_IRQ_TIMER1) == INTR_FLAG_TIMER1, "Timer 1 bit");
_Static_assert((1u << GBA_IRQ_TIMER2) == INTR_FLAG_TIMER2, "Timer 2 bit");
_Static_assert((1u << GBA_IRQ_TIMER3) == INTR_FLAG_TIMER3, "Timer 3 bit");
_Static_assert((1u << GBA_IRQ_SERIAL) == INTR_FLAG_SERIAL, "Serial bit");
_Static_assert((1u << GBA_IRQ_DMA0) == INTR_FLAG_DMA0, "DMA 0 bit");
_Static_assert((1u << GBA_IRQ_DMA1) == INTR_FLAG_DMA1, "DMA 1 bit");
_Static_assert((1u << GBA_IRQ_DMA2) == INTR_FLAG_DMA2, "DMA 2 bit");
_Static_assert((1u << GBA_IRQ_DMA3) == INTR_FLAG_DMA3, "DMA 3 bit");
_Static_assert((1u << GBA_IRQ_KEYPAD) == INTR_FLAG_KEYPAD, "Keypad bit");
_Static_assert((1u << GBA_IRQ_GAMEPAK) == INTR_FLAG_GAMEPAK, "Game Pak bit");
_Static_assert(GBA_IO_BASE + GBA_IE_OFFSET == IE_ADDRESS, "IE address");
_Static_assert(GBA_IO_BASE + GBA_IF_OFFSET == IF_ADDRESS, "IF address");
_Static_assert(GBA_IO_BASE + GBA_IME_OFFSET == IME_ADDRESS, "IME address");
_Static_assert(GBA_IE_OFFSET == REG_OFFSET_IE, "IE offset is not expansion's");
_Static_assert(GBA_IF_OFFSET == REG_OFFSET_IF, "IF offset is not expansion's");
_Static_assert(GBA_IME_OFFSET == REG_OFFSET_IME, "IME offset is not expansion's");
_Static_assert(GBA_IME_ENABLE == 1u, "IME enable is bit 0");
_Static_assert(INTERRUPT_IF_MARKER == IF_BIT_15, "the IF marker is bit 15, unused by the GBA");
_Static_assert(INTERRUPT_TABLE_LENGTH == TABLE_LENGTH, "gIntrTable has 14 slots");
_Static_assert(INTERRUPT_DISPATCH_CAP > GBA_IRQ_COUNT, "the cap must allow every source once");

/* IntrMain's search order, src/crt0.s:53-93: the source at position i runs
 * gIntrTable[i]. Game Pak, tested last (crt0.s:94-96), runs no handler. */
static const enum gba_irq intr_main_order[] = {
    GBA_IRQ_VCOUNT, GBA_IRQ_SERIAL, GBA_IRQ_TIMER3, GBA_IRQ_HBLANK, GBA_IRQ_VBLANK,
    GBA_IRQ_TIMER0, GBA_IRQ_TIMER1, GBA_IRQ_TIMER2, GBA_IRQ_DMA0,   GBA_IRQ_DMA1,
    GBA_IRQ_DMA2,   GBA_IRQ_DMA3,   GBA_IRQ_KEYPAD,
};
#define HANDLED_SOURCE_COUNT (sizeof intr_main_order / sizeof intr_main_order[0])
_Static_assert(HANDLED_SOURCE_COUNT == SLOT_SPURIOUS, "every source but Game Pak has a slot");

/* GBA memory, at its native address (gba_memmap.h: the integer-to-pointer
 * conversion is the identity). */
static uint16_t read16(uint32_t address)
{
    return *(const volatile uint16_t *)(uintptr_t)address;
}

static void write16(uint32_t address, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)address = value;
}

static uint16_t bit_of(enum gba_irq source)
{
    return (uint16_t)(1u << source);
}

/* The requests, as a game read of REG_IF masked with INTR_FLAG_* bits sees
 * them. Bit 15 of the raw value is the module's marker (interrupt.h). */
static uint16_t requested(void)
{
    return (uint16_t)(read16(IF_ADDRESS) & ALL_SOURCES);
}

/* ---- Counting handlers ---- */

#define LOG_CAPACITY (INTERRUPT_DISPATCH_CAP + 32u)

static struct {
    unsigned calls[TABLE_LENGTH];
    size_t log[LOG_CAPACITY]; /* slots, in the order their handlers ran */
    size_t log_length;
    uint16_t ime_seen[TABLE_LENGTH]; /* IME when the slot's handler last ran */
    uint16_t if_seen[TABLE_LENGTH];  /* IF when the slot's handler last ran */
} record;

static void handler_ran(size_t slot)
{
    record.calls[slot]++;
    if (record.log_length < LOG_CAPACITY)
        record.log[record.log_length++] = slot;
    record.ime_seen[slot] = read16(IME_ADDRESS);
    record.if_seen[slot] = requested();
}

static void count_slot_0(void) { handler_ran(0); }
static void count_slot_1(void) { handler_ran(1); }
static void count_slot_2(void) { handler_ran(2); }
static void count_slot_3(void) { handler_ran(3); }
static void count_slot_4(void) { handler_ran(4); }
static void count_slot_5(void) { handler_ran(5); }
static void count_slot_6(void) { handler_ran(6); }
static void count_slot_7(void) { handler_ran(7); }
static void count_slot_8(void) { handler_ran(8); }
static void count_slot_9(void) { handler_ran(9); }
static void count_slot_10(void) { handler_ran(10); }
static void count_slot_11(void) { handler_ran(11); }
static void count_slot_12(void) { handler_ran(12); }
static void count_slot_13(void) { handler_ran(13); }

static const interrupt_handler counting_handlers[TABLE_LENGTH] = {
    count_slot_0, count_slot_1, count_slot_2,  count_slot_3,  count_slot_4,
    count_slot_5, count_slot_6, count_slot_7,  count_slot_8,  count_slot_9,
    count_slot_10, count_slot_11, count_slot_12, count_slot_13,
};

/* The table the controller is given; static, so it outlives every test. */
static interrupt_handler table[TABLE_LENGTH];

static unsigned total_calls(void)
{
    unsigned total = 0;
    for (size_t slot = 0; slot < TABLE_LENGTH; slot++)
        total += record.calls[slot];
    return total;
}

/* A VBlank handler as the game might write one that acknowledges itself:
 * writes its bit to IF and sets it in INTR_CHECK. */
static void vblank_acknowledging_itself(void)
{
    handler_ran(SLOT_VBLANK);
    write16(IF_ADDRESS, (uint16_t)INTR_FLAG_VBLANK);
    write16(INTR_CHECK_ADDRESS, (uint16_t)(read16(INTR_CHECK_ADDRESS) | INTR_FLAG_VBLANK));
}

/* A handler that discards another source's request, as the game's
 * stale-request writes do. */
static void vblank_acknowledging_timer0(void)
{
    handler_ran(SLOT_VBLANK);
    write16(IF_ADDRESS, (uint16_t)INTR_FLAG_TIMER0);
}

static void vblank_raising_timer0(void)
{
    handler_ran(SLOT_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER0);
}

static void vblank_raising_itself_once(void)
{
    handler_ran(SLOT_VBLANK);
    if (record.calls[SLOT_VBLANK] == 1)
        interrupt_raise(GBA_IRQ_VBLANK);
}

static void vblank_raising_itself_forever(void)
{
    handler_ran(SLOT_VBLANK);
    interrupt_raise(GBA_IRQ_VBLANK);
}

static void vblank_clearing_ie_and_ime(void)
{
    handler_ran(SLOT_VBLANK);
    write16(IE_ADDRESS, 0);
    write16(IME_ADDRESS, 0);
}

static void vcount_expecting_ime_off(void)
{
    handler_ran(SLOT_VCOUNT);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 0u);
}

static void vblank_expecting_ime_on(void)
{
    handler_ran(SLOT_VBLANK);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 1u);
}

/* ---- Setup ---- */

static void reset_registers(void)
{
    write16(IE_ADDRESS, 0);
    write16(IF_ADDRESS, 0);
    write16(IME_ADDRESS, 0);
    write16(SOUNDCNT_X_ADDRESS, 0);
    write16(INTR_CHECK_ADDRESS, 0);
}

/* A powered-on controller with the counting table, every register clear. */
static void setup(void)
{
    memset(&record, 0, sizeof record);
    memcpy(table, counting_handlers, sizeof table);
    interrupt_shutdown();
    reset_registers();
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(interrupt_init(table, TABLE_LENGTH), INTERRUPT_OK);
}

static void teardown(void)
{
    interrupt_shutdown();
    reset_registers();
}

/* IME on and every source enabled. */
static void enable_all(void)
{
    write16(IME_ADDRESS, GBA_IME_ENABLE);
    write16(IE_ADDRESS, ALL_SOURCES);
}

/* ---- What the module writes to stderr ---- */

#define REPORT_SIZE 1024

struct report {
    bool captured; /* stderr was redirected and read back whole */
    char text[REPORT_SIZE];
};

/* Runs action(context) with stderr sent to a temporary file, and returns
 * what was written to it. */
static struct report stderr_of(void (*action)(void *), void *context)
{
    struct report report = { .captured = false, .text = "" };
    FILE *const file = tmpfile();
    if (file == NULL)
        return report;
    const int saved = fflush(stderr) == 0 ? dup(STDERR_FILENO) : -1;
    if (saved < 0 || dup2(fileno(file), STDERR_FILENO) < 0) {
        if (saved >= 0)
            (void)close(saved); /* nothing redirected yet; captured stays false */
        (void)fclose(file);
        return report;
    }
    action(context);
    /* stderr is put back whether or not the flush worked. */
    bool restored = fflush(stderr) == 0;
    restored = dup2(saved, STDERR_FILENO) >= 0 && restored;
    restored = close(saved) == 0 && restored;
    if (restored && fseek(file, 0, SEEK_SET) == 0) {
        const size_t length = fread(report.text, 1, sizeof report.text - 1, file);
        report.text[length] = '\0';
        report.captured = !ferror(file) && length < sizeof report.text - 1;
    }
    if (fclose(file) != 0)
        report.captured = false;
    return report;
}

static void dispatch_into(void *handlers_run)
{
    *(unsigned *)handlers_run = interrupt_dispatch();
}

static void raise_source(void *source)
{
    interrupt_raise(*(const enum gba_irq *)source);
}

struct init_call {
    interrupt_handler const *table;
    size_t count;
    int status;
};

static void init_with(void *call)
{
    struct init_call *const c = call;
    c->status = interrupt_init(c->table, c->count);
}

static void shutdown_twice(void *unused)
{
    (void)unused;
    interrupt_shutdown();
    interrupt_shutdown();
}

/* The report names the module and the call. */
static bool reports(const struct report *report, const char *call)
{
    char prefix[64];
    const int length = snprintf(prefix, sizeof prefix, "interrupt: %s:", call);
    return report->captured && length > 0 && (size_t)length < sizeof prefix &&
           strstr(report->text, prefix) != NULL;
}

static bool silent(const struct report *report)
{
    return report->captured && report->text[0] == '\0';
}

/* ---- init and shutdown ---- */

PTEST(init_with_a_null_table_fails_and_is_reported)
{
    setup();
    struct init_call call = { .table = NULL, .count = TABLE_LENGTH, .status = INTERRUPT_OK };
    const struct report report = stderr_of(init_with, &call);
    PT_ASSERT_EQ_INT(call.status, INTERRUPT_ERR_TABLE);
    PT_ASSERT(reports(&report, "interrupt_init"));
    /* Not initialised: a raise is rejected. */
    enum gba_irq source = GBA_IRQ_VBLANK;
    const struct report raise_report = stderr_of(raise_source, &source);
    PT_ASSERT(reports(&raise_report, "interrupt_raise"));
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

PTEST(init_with_no_entries_or_more_than_gIntrTable_fails)
{
    setup();
    struct init_call empty = { .table = table, .count = 0, .status = INTERRUPT_OK };
    const struct report empty_report = stderr_of(init_with, &empty);
    PT_ASSERT_EQ_INT(empty.status, INTERRUPT_ERR_TABLE);
    PT_ASSERT(reports(&empty_report, "interrupt_init"));
    struct init_call oversized = { .table = table, .count = TABLE_LENGTH + 1,
                                   .status = INTERRUPT_OK };
    const struct report oversized_report = stderr_of(init_with, &oversized);
    PT_ASSERT_EQ_INT(oversized.status, INTERRUPT_ERR_TABLE);
    PT_ASSERT(reports(&oversized_report, "interrupt_init"));
    teardown();
}

PTEST(init_clears_if_and_is_silent)
{
    setup();
    write16(IF_ADDRESS, (uint16_t)(INTR_FLAG_VBLANK | INTR_FLAG_TIMER3));
    struct init_call call = { .table = table, .count = TABLE_LENGTH,
                              .status = INTERRUPT_ERR_TABLE };
    const struct report report = stderr_of(init_with, &call);
    PT_ASSERT_EQ_INT(call.status, INTERRUPT_OK);
    PT_ASSERT(silent(&report));
    PT_ASSERT_EQ_U32(requested(), 0u);
    PT_ASSERT_EQ_U32(read16(IF_ADDRESS), IF_BIT_15); /* the marker alone */
    PT_ASSERT(!interrupt_halted());
    teardown();
}

PTEST(shutdown_without_init_is_safe_and_silent)
{
    interrupt_shutdown(); /* whatever an earlier test left */
    const struct report report = stderr_of(shutdown_twice, NULL);
    PT_ASSERT(silent(&report));
    PT_ASSERT(!interrupt_halted());
}

PTEST(raise_and_dispatch_after_shutdown_do_nothing_and_are_reported)
{
    setup();
    enable_all();
    interrupt_shutdown();
    enum gba_irq source = GBA_IRQ_VBLANK;
    const struct report raise_report = stderr_of(raise_source, &source);
    PT_ASSERT(reports(&raise_report, "interrupt_raise"));
    PT_ASSERT_EQ_U32(requested(), 0u);
    write16(IF_ADDRESS, (uint16_t)INTR_FLAG_VBLANK); /* as if requested before shutdown */
    unsigned handlers_run = 99;
    const struct report dispatch_report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(reports(&dispatch_report, "interrupt_dispatch"));
    PT_ASSERT_EQ_U32(handlers_run, 0u);
    PT_ASSERT_EQ_U32(total_calls(), 0u);
    teardown();
}

/* ---- raise ---- */

PTEST(raise_sets_exactly_the_source_bit_and_does_not_dispatch)
{
    for (int source = 0; source < GBA_IRQ_COUNT; source++) {
        setup();
        enable_all();
        enum gba_irq raised = (enum gba_irq)source;
        const struct report report = stderr_of(raise_source, &raised);
        PT_ASSERT(silent(&report));
        PT_ASSERT_EQ_U32(requested(), bit_of(raised));
        PT_ASSERT_EQ_U32(total_calls(), 0u);
        teardown();
    }
}

/* Raw IF is the requests plus the marker, bit 15; a game read masked with
 * INTR_FLAG_* bits, the way game code tests a flag, sees only the requests. */
PTEST(a_game_read_of_if_masked_with_the_flags_sees_only_the_requests)
{
    setup();
    interrupt_raise(GBA_IRQ_SERIAL);
    interrupt_raise(GBA_IRQ_VBLANK);
    const uint16_t raw = read16(IF_ADDRESS);
    PT_ASSERT_EQ_U32(raw, (uint32_t)(INTR_FLAG_SERIAL | INTR_FLAG_VBLANK) | IF_BIT_15);
    for (int source = 0; source < GBA_IRQ_COUNT; source++) {
        const uint16_t flag = bit_of((enum gba_irq)source);
        const bool requested_source = source == GBA_IRQ_SERIAL || source == GBA_IRQ_VBLANK;
        PT_ASSERT(((raw & flag) != 0) == requested_source);
    }
    teardown();
}

PTEST(raise_keeps_the_bits_already_requested)
{
    setup();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER3);
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)(INTR_FLAG_VBLANK | INTR_FLAG_TIMER3));
    teardown();
}

PTEST(raise_of_a_source_beyond_the_table_is_rejected_and_reported)
{
    setup();
    /* A table with slots 0-4 (VCount to VBlank) only. */
    PT_ASSERT_EQ_INT(interrupt_init(table, SLOT_VBLANK + 1), INTERRUPT_OK);
    enum gba_irq timer0 = GBA_IRQ_TIMER0; /* slot 5 */
    const struct report report = stderr_of(raise_source, &timer0);
    PT_ASSERT(reports(&report, "interrupt_raise"));
    PT_ASSERT_EQ_U32(requested(), 0u);
    enum gba_irq gamepak = GBA_IRQ_GAMEPAK; /* slot 13 */
    const struct report gamepak_report = stderr_of(raise_source, &gamepak);
    PT_ASSERT(reports(&gamepak_report, "interrupt_raise"));
    PT_ASSERT_EQ_U32(requested(), 0u);
    /* The last slot the table has is accepted. */
    enum gba_irq vblank = GBA_IRQ_VBLANK; /* slot 4 */
    const struct report vblank_report = stderr_of(raise_source, &vblank);
    PT_ASSERT(silent(&vblank_report));
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_VBLANK);
    teardown();
}

PTEST(raise_of_a_value_that_is_not_a_source_is_rejected_and_reported)
{
    setup();
    enum gba_irq past_the_end = GBA_IRQ_COUNT;
    const struct report report = stderr_of(raise_source, &past_the_end);
    PT_ASSERT(reports(&report, "interrupt_raise"));
    enum gba_irq negative = (enum gba_irq)-1;
    const struct report negative_report = stderr_of(raise_source, &negative);
    PT_ASSERT(reports(&negative_report, "interrupt_raise"));
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

/* ---- dispatch: when nothing runs ---- */

PTEST(dispatch_with_ime_clear_runs_nothing_and_leaves_if)
{
    setup();
    write16(IE_ADDRESS, (uint16_t)ALL_SOURCES);
    write16(IME_ADDRESS, 0xFFFEu); /* GBATEK: only bit 0 enables */
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 0u);
    PT_ASSERT_EQ_U32(total_calls(), 0u);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_VBLANK);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 0xFFFEu);
    teardown();
}

PTEST(dispatch_with_the_source_disabled_in_ie_runs_nothing_and_leaves_if)
{
    setup();
    enable_all();
    write16(IE_ADDRESS, (uint16_t)(ALL_SOURCES & ~(unsigned)INTR_FLAG_VBLANK));
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 0u);
    PT_ASSERT_EQ_U32(total_calls(), 0u);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_VBLANK);
    teardown();
}

PTEST(dispatch_with_nothing_pending_runs_nothing)
{
    setup();
    enable_all();
    unsigned handlers_run = 99;
    const struct report report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(silent(&report));
    PT_ASSERT_EQ_U32(handlers_run, 0u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_SPURIOUS], 0u);
    PT_ASSERT_EQ_U32(total_calls(), 0u);
    teardown();
}

/* ---- dispatch: the handler, its slot and its acknowledgement ---- */

PTEST(dispatch_runs_the_handler_and_its_own_acknowledge_leaves_if_clear)
{
    setup();
    table[SLOT_VBLANK] = vblank_acknowledging_itself;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    unsigned handlers_run = 0;
    const struct report report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(silent(&report));
    PT_ASSERT_EQ_U32(handlers_run, 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 1u);
    PT_ASSERT_EQ_U32(total_calls(), 1u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    PT_ASSERT_EQ_U32(read16(INTR_CHECK_ADDRESS), (uint32_t)INTR_FLAG_VBLANK);
    teardown();
}

/* crt0.s:98 writes the bit to IF before the handler runs, and main.c's
 * handlers never write IF: the request is cleared without the handler. */
PTEST(dispatch_acknowledges_the_request_before_the_handler_runs)
{
    setup();
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER0);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT_EQ_U32(record.if_seen[SLOT_VBLANK], (uint32_t)INTR_FLAG_TIMER0);
    PT_ASSERT_EQ_U32(record.if_seen[SLOT_TIMER0], 0u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

PTEST(each_source_runs_the_gIntrTable_slot_intr_main_gives_it)
{
    for (size_t slot = 0; slot < HANDLED_SOURCE_COUNT; slot++) {
        setup();
        enable_all();
        interrupt_raise(intr_main_order[slot]);
        PT_ASSERT_EQ_U32(interrupt_dispatch(), 1u);
        PT_ASSERT_EQ_U32(total_calls(), 1u);
        PT_ASSERT_EQ_U32(record.calls[slot], 1u);
        PT_ASSERT_EQ_U32(requested(), 0u);
        teardown();
    }
}

/* ---- dispatch: order ---- */

/* VBlank is bit 0 and HBlank bit 1, but IntrMain tests HBlank first
 * (crt0.s:64 before crt0.s:67). */
PTEST(two_sources_run_in_intr_main_order_not_bit_order)
{
    setup();
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_HBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT(record.log_length == 2);
    PT_ASSERT(record.log[0] == SLOT_HBLANK);
    PT_ASSERT(record.log[1] == SLOT_VBLANK);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

PTEST(every_source_pending_runs_in_intr_main_order)
{
    setup();
    enable_all();
    for (int source = GBA_IRQ_COUNT - 1; source >= 0; source--)
        if (source != GBA_IRQ_GAMEPAK)
            interrupt_raise((enum gba_irq)source);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), (uint32_t)HANDLED_SOURCE_COUNT);
    PT_ASSERT(record.log_length == HANDLED_SOURCE_COUNT);
    for (size_t position = 0; position < HANDLED_SOURCE_COUNT; position++)
        PT_ASSERT(record.log[position] == position);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

/* ---- dispatch: requests raised by handlers ---- */

PTEST(a_source_raised_inside_a_handler_runs_in_the_same_call)
{
    setup();
    table[SLOT_VBLANK] = vblank_raising_timer0;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT(record.log_length == 2);
    PT_ASSERT(record.log[0] == SLOT_VBLANK);
    PT_ASSERT(record.log[1] == SLOT_TIMER0);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

/* Because the request was acknowledged before the handler ran, the handler's
 * own new request is kept and serviced. */
PTEST(a_handler_re_raising_its_own_source_runs_again)
{
    setup();
    table[SLOT_VBLANK] = vblank_raising_itself_once;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 2u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

PTEST(the_cap_stops_a_handler_that_re_raises_forever_and_is_reported)
{
    setup();
    table[SLOT_VBLANK] = vblank_raising_itself_forever;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    unsigned handlers_run = 0;
    const struct report report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT_EQ_U32(handlers_run, INTERRUPT_DISPATCH_CAP);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], INTERRUPT_DISPATCH_CAP);
    PT_ASSERT(reports(&report, "interrupt_dispatch"));
    char cap[16];
    const int cap_length = snprintf(cap, sizeof cap, "%u", INTERRUPT_DISPATCH_CAP);
    PT_ASSERT(cap_length > 0 && (size_t)cap_length < sizeof cap);
    PT_ASSERT(strstr(report.text, cap) != NULL); /* the report gives the cap */
    /* The runaway request is left pending, and IE and IME as they were. */
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_VBLANK);
    PT_ASSERT_EQ_U32(read16(IE_ADDRESS), ALL_SOURCES);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 1u);
    teardown();
}

/* ---- dispatch: IME and IE around the handler ---- */

/* crt0.s:49-50 writes 0 to IME; crt0.s:56-57 writes 1 unless the source is
 * VCount (tested first, crt0.s:53-54); crt0.s:127 restores the saved IME. */
PTEST(ime_during_a_handler_is_what_crt0_writes_and_is_restored_after)
{
    setup();
    table[SLOT_VCOUNT] = vcount_expecting_ime_off;
    table[SLOT_VBLANK] = vblank_expecting_ime_on;
    write16(IE_ADDRESS, (uint16_t)ALL_SOURCES);
    write16(IME_ADDRESS, 0x8001u); /* bit 0 set; the whole halfword is saved */
    for (int source = 0; source < GBA_IRQ_COUNT; source++)
        if (source != GBA_IRQ_GAMEPAK)
            interrupt_raise((enum gba_irq)source);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), (uint32_t)HANDLED_SOURCE_COUNT);
    PT_ASSERT_EQ_U32(record.ime_seen[SLOT_VCOUNT], 0u);
    for (size_t slot = SLOT_VCOUNT + 1; slot < HANDLED_SOURCE_COUNT; slot++)
        PT_ASSERT_EQ_U32(record.ime_seen[slot], 1u);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 0x8001u);
    teardown();
}

/* crt0.s:126-127 store the IE and IME read on entry: whatever the handler
 * wrote to them is undone, and the next source still runs. */
PTEST(a_handlers_writes_to_ie_and_ime_are_undone_when_it_returns)
{
    setup();
    table[SLOT_VBLANK] = vblank_clearing_ie_and_ime;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER0);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT(record.log_length == 2);
    PT_ASSERT(record.log[0] == SLOT_VBLANK);
    PT_ASSERT(record.log[1] == SLOT_TIMER0);
    PT_ASSERT_EQ_U32(read16(IE_ADDRESS), ALL_SOURCES);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 1u);
    teardown();
}

/* ---- IF acknowledged by the game ---- */

/* GBATEK: writing 1 to an IF bit clears it, as link.c's DisableSerial does
 * with REG_IF = INTR_FLAG_TIMER3 | INTR_FLAG_SERIAL. */
PTEST(a_game_write_of_ones_to_if_clears_those_requests)
{
    setup();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER3);
    write16(IF_ADDRESS, (uint16_t)(INTR_FLAG_TIMER3 | INTR_FLAG_SERIAL));
    interrupt_raise(GBA_IRQ_HBLANK);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)(INTR_FLAG_VBLANK | INTR_FLAG_HBLANK));
    enable_all();
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 2u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_TIMER3], 0u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_HBLANK], 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 1u);
    teardown();
}

PTEST(a_game_write_to_if_before_dispatch_is_applied_by_dispatch)
{
    setup();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER3);
    write16(IF_ADDRESS, (uint16_t)INTR_FLAG_TIMER3);
    enable_all();
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_TIMER3], 0u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

/* The stale-request idiom, REG_IF = INTR_FLAG_SERIAL before enabling Serial
 * (librfu_sio32id.c:68 and :103), when that request is the only one: the
 * write stores the value the requests alone would have, and is still seen.
 * The same for every source. */
PTEST(a_game_write_of_the_only_pending_request_discards_it)
{
    for (int source = 0; source < GBA_IRQ_COUNT; source++) {
        setup();
        const enum gba_irq stale = (enum gba_irq)source;
        write16(IME_ADDRESS, GBA_IME_ENABLE);
        interrupt_raise(stale); /* IE clear: stays pending */
        write16(IF_ADDRESS, bit_of(stale));
        write16(IE_ADDRESS, ALL_SOURCES);
        unsigned handlers_run = 99;
        const struct report report = stderr_of(dispatch_into, &handlers_run);
        PT_ASSERT(silent(&report));
        PT_ASSERT_EQ_U32(handlers_run, 0u);
        PT_ASSERT_EQ_U32(total_calls(), 0u);
        PT_ASSERT(!interrupt_halted());
        PT_ASSERT_EQ_U32(requested(), 0u);
        teardown();
    }
}

PTEST(a_game_write_to_if_inside_a_handler_clears_another_request)
{
    setup();
    table[SLOT_VBLANK] = vblank_acknowledging_timer0;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER0); /* after VBlank in IntrMain's order */
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_TIMER0], 0u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

/* ---- errors during dispatch ---- */

/* gIntrTable is all NULL until InitIntrHandlers fills it. On the GBA a NULL
 * entry jumps to address 0; here it is reported and skipped. crt0.s:98 has
 * already acknowledged the request. */
PTEST(a_null_table_entry_is_reported_and_skipped)
{
    setup();
    table[SLOT_VBLANK] = NULL;
    enable_all();
    interrupt_raise(GBA_IRQ_VBLANK);
    interrupt_raise(GBA_IRQ_TIMER0);
    unsigned handlers_run = 99;
    const struct report report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(reports(&report, "interrupt_dispatch"));
    PT_ASSERT_EQ_U32(handlers_run, 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_TIMER0], 1u);
    PT_ASSERT_EQ_U32(total_calls(), 1u);
    PT_ASSERT_EQ_U32(requested(), 0u);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 1u);
    teardown();
}

/* crt0.s:94-96: a Game Pak request writes a zero byte to SOUNDCNT_X and spins
 * forever; no handler runs, not even gIntrTable[13], and IF keeps the bit. */
PTEST(a_game_pak_request_turns_the_sound_off_and_halts)
{
    setup();
    enable_all();
    write16(SOUNDCNT_X_ADDRESS, SOUNDCNT_X_MASTER_ENABLE);
    interrupt_raise(GBA_IRQ_GAMEPAK);
    interrupt_raise(GBA_IRQ_VBLANK); /* higher priority: runs first */
    unsigned handlers_run = 99;
    const struct report report = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(reports(&report, "interrupt_dispatch"));
    PT_ASSERT_EQ_U32(handlers_run, 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 1u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_SPURIOUS], 0u);
    PT_ASSERT_EQ_U32(read16(SOUNDCNT_X_ADDRESS), 0u);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_GAMEPAK);
    PT_ASSERT_EQ_U32(read16(IME_ADDRESS), 1u);
    PT_ASSERT(interrupt_halted());
    /* Halted: nothing runs again, and the halt is not reported again. */
    interrupt_raise(GBA_IRQ_VBLANK);
    const struct report again = stderr_of(dispatch_into, &handlers_run);
    PT_ASSERT(silent(&again));
    PT_ASSERT_EQ_U32(handlers_run, 0u);
    PT_ASSERT_EQ_U32(record.calls[SLOT_VBLANK], 1u);
    /* Powering on again ends the halt. */
    PT_ASSERT_EQ_INT(interrupt_init(table, TABLE_LENGTH), INTERRUPT_OK);
    PT_ASSERT(!interrupt_halted());
    PT_ASSERT_EQ_U32(requested(), 0u);
    teardown();
}

PTEST(a_game_pak_request_disabled_in_ie_does_not_halt)
{
    setup();
    enable_all();
    write16(IE_ADDRESS, (uint16_t)(ALL_SOURCES & ~(unsigned)INTR_FLAG_GAMEPAK));
    write16(SOUNDCNT_X_ADDRESS, SOUNDCNT_X_MASTER_ENABLE);
    interrupt_raise(GBA_IRQ_GAMEPAK);
    interrupt_raise(GBA_IRQ_VBLANK);
    PT_ASSERT_EQ_U32(interrupt_dispatch(), 1u);
    PT_ASSERT(!interrupt_halted());
    PT_ASSERT_EQ_U32(read16(SOUNDCNT_X_ADDRESS), SOUNDCNT_X_MASTER_ENABLE);
    PT_ASSERT_EQ_U32(requested(), (uint32_t)INTR_FLAG_GAMEPAK);
    teardown();
}

PTEST_MAIN()
