/*
 * interrupt.c: the GBA interrupt controller and expansion's IRQ handler.
 *
 * Stands in for the CPU's IRQ line, the IE, IF and IME registers (GBATEK "GBA
 * Interrupt Control", https://problemkaputt.de/gbatek.htm#gbainterruptcontrol)
 * and IntrMain, expansion's IRQ handler in src/crt0.s:42-129. What the
 * hardware and IntrMain do, line by line, and where this model differs:
 * interrupt.h. Written from GBATEK and crt0.s; the reference ports have no
 * controller (they call three gIntrTable slots directly).
 *
 * IE and IME are plain read/write registers, read and written where they
 * live, in the mapped I/O page. IF is not: on the GBA a write to it clears
 * the 1 bits written, but the game's writes land in that page as plain
 * stores. So the requests live here (controller.requests) and are stored at
 * IF after every change, with INTERRUPT_IF_MARKER (bit 15) set beside them;
 * before they are used, IF is compared with what was last stored, and a
 * difference is a game write whose 1 bits are acknowledged. The game never
 * writes bit 15, so each of its writes differs from the stored value
 * (interrupt.h says what this still cannot see).
 */
#include <stdio.h>

#include "gba_memmap.h"
#include "interrupt.h"

/* GBATEK "GBA Sound Control Registers": SOUNDCNT_X (gba_memmap.h); bit 7 is
 * the PSG/FIFO master enable. crt0.s:95 stores a zero byte there (the low
 * byte of INTR_FLAG_GAMEPAK), which turns all sound off. */
#define SOUNDCNT_X_ALL_OFF UINT8_C(0x00)

/* What crt0.s:49-50 writes to IME. */
#define IME_DISABLED UINT16_C(0x0000)

_Static_assert(GBA_IE_OFFSET + sizeof(uint16_t) <= GBA_IO_SIZE, "IE is outside the I/O region");
_Static_assert(GBA_IF_OFFSET + sizeof(uint16_t) <= GBA_IO_SIZE, "IF is outside the I/O region");
_Static_assert(GBA_IME_OFFSET + sizeof(uint16_t) <= GBA_IO_SIZE, "IME is outside the I/O region");
_Static_assert(GBA_SOUNDCNT_X_OFFSET < GBA_IO_SIZE, "SOUNDCNT_X is outside the I/O region");
/* GBATEK: the sources are IF bits 0-13; the marker must be none of them. */
_Static_assert(INTERRUPT_IF_MARKER >= (1u << GBA_IRQ_COUNT), "the IF marker is a source bit");

/* IntrMain's search order, src/crt0.s:53-96. The source at position i is
 * handled by gIntrTable[i] (r12 = 4 * i at crt0.s:113-115), except Game Pak,
 * which halts (crt0.s:94-96). */
static const enum gba_irq intr_main_order[GBA_IRQ_COUNT] = {
    GBA_IRQ_VCOUNT, GBA_IRQ_SERIAL, GBA_IRQ_TIMER3, GBA_IRQ_HBLANK, GBA_IRQ_VBLANK, GBA_IRQ_TIMER0, GBA_IRQ_TIMER1,
    GBA_IRQ_TIMER2, GBA_IRQ_DMA0,   GBA_IRQ_DMA1,   GBA_IRQ_DMA2,   GBA_IRQ_DMA3,   GBA_IRQ_KEYPAD, GBA_IRQ_GAMEPAK,
};
_Static_assert(GBA_IRQ_COUNT == INTERRUPT_TABLE_LENGTH, "one gIntrTable slot per position");

/* GBATEK's names for the sources, for reports. */
static const char *const source_names[GBA_IRQ_COUNT] = {
    [GBA_IRQ_VBLANK] = "LCD V-Blank",
    [GBA_IRQ_HBLANK] = "LCD H-Blank",
    [GBA_IRQ_VCOUNT] = "LCD V-Counter Match",
    [GBA_IRQ_TIMER0] = "Timer 0 Overflow",
    [GBA_IRQ_TIMER1] = "Timer 1 Overflow",
    [GBA_IRQ_TIMER2] = "Timer 2 Overflow",
    [GBA_IRQ_TIMER3] = "Timer 3 Overflow",
    [GBA_IRQ_SERIAL] = "Serial Communication",
    [GBA_IRQ_DMA0] = "DMA 0",
    [GBA_IRQ_DMA1] = "DMA 1",
    [GBA_IRQ_DMA2] = "DMA 2",
    [GBA_IRQ_DMA3] = "DMA 3",
    [GBA_IRQ_KEYPAD] = "Keypad",
    [GBA_IRQ_GAMEPAK] = "Game Pak",
};

static struct {
    bool initialised;
    bool halted; /* a Game Pak interrupt was taken (crt0.s:94-96) */
    interrupt_handler const *table;
    size_t count;
    uint16_t requests; /* the hardware's IF: requested and not yet acknowledged */
    uint16_t stored;   /* what this module last stored at IF */
} controller;

static uint16_t read_register(uint32_t offset)
{
    return *gba_io_u16(offset);
}

static void write_register(uint32_t offset, uint16_t value)
{
    *gba_io_u16(offset) = value;
}

static uint16_t bit_of(enum gba_irq source)
{
    return (uint16_t)(1u << source);
}

static bool is_source(enum gba_irq source)
{
    return (unsigned)source < GBA_IRQ_COUNT;
}

/* The source's position in IntrMain's search, which is its gIntrTable slot. */
static size_t slot_of(enum gba_irq source)
{
    size_t slot = 0;
    while (intr_main_order[slot] != source)
        slot++;
    return slot;
}

/* controller.requests only ever holds source bits (bit_of), never the marker. */
static void store_requests(void)
{
    controller.stored = controller.requests | INTERRUPT_IF_MARKER;
    write_register(GBA_IF_OFFSET, controller.stored);
}

/* Applies a game write to IF made since the last store. GBATEK, IF:
 * "writing a "1" to one of the IRQ bits, the IRQ bit will then be cleared". */
static void take_game_acknowledgements(void)
{
    const uint16_t found = read_register(GBA_IF_OFFSET);
    if (found != controller.stored)
        controller.requests &= (uint16_t)~found;
    store_requests();
}

/* What IntrMain would find pending: IE & IF, or nothing while IME bit 0 is
 * clear (the CPU takes no IRQ then). */
static uint16_t enabled_requests(void)
{
    if ((read_register(GBA_IME_OFFSET) & GBA_IME_ENABLE) == 0)
        return 0;
    return read_register(GBA_IE_OFFSET) & controller.requests;
}

/* The slot of the source IntrMain picks from pending, which is not 0. */
static size_t first_in_intr_main_order(uint16_t pending)
{
    size_t slot = 0;
    while ((pending & bit_of(intr_main_order[slot])) == 0)
        slot++;
    return slot;
}

/* crt0.s:94-96: sound off, then spin forever. */
static void halt_for_game_pak(void)
{
    *gba_io_u8(GBA_SOUNDCNT_X_OFFSET) = SOUNDCNT_X_ALL_OFF;
    controller.halted = true;
    (void)fprintf(stderr, "interrupt: interrupt_dispatch: Game Pak interrupt (cartridge removed): "
                          "IntrMain turns the sound off and hangs here (src/crt0.s:94-96); no "
                          "handler runs again until interrupt_init\n"); /* best-effort */
}

/* Calls gIntrTable[slot] unless it is NULL. True if a handler ran. */
static bool call_handler(size_t slot)
{
    const interrupt_handler handler = slot < controller.count ? controller.table[slot] : NULL;
    if (handler == NULL) {
        (void)fprintf(stderr,
                      "interrupt: interrupt_dispatch: gIntrTable[%zu] (%s) is NULL; the "
                      "request is acknowledged and nothing is called\n",
                      slot, source_names[intr_main_order[slot]]); /* best-effort */
        return false;
    }
    handler();
    return true;
}

/* One pass of IntrMain for the source at slot. True if a handler ran. */
static bool service(size_t slot)
{
    const enum gba_irq source = intr_main_order[slot];
    const uint16_t saved_ie = read_register(GBA_IE_OFFSET);   /* crt0.s:45 */
    const uint16_t saved_ime = read_register(GBA_IME_OFFSET); /* crt0.s:46 */
    write_register(GBA_IME_OFFSET, IME_DISABLED);             /* crt0.s:49-50 */
    if (source != GBA_IRQ_VCOUNT)
        write_register(GBA_IME_OFFSET, GBA_IME_ENABLE); /* crt0.s:56-57, past VCount's test */
    if (source == GBA_IRQ_GAMEPAK) {
        halt_for_game_pak();
        return false;
    }
    controller.requests &= (uint16_t)~bit_of(source); /* crt0.s:98 */
    store_requests();
    const bool ran = call_handler(slot);       /* crt0.s:113-118 */
    write_register(GBA_IE_OFFSET, saved_ie);   /* crt0.s:126 */
    write_register(GBA_IME_OFFSET, saved_ime); /* crt0.s:127 */
    return ran;
}

int interrupt_init(interrupt_handler const *table, size_t count)
{
    interrupt_shutdown();
    if (table == NULL || count == 0 || count > INTERRUPT_TABLE_LENGTH) {
        (void)fprintf(stderr,
                      "interrupt: interrupt_init: need gIntrTable and its length (1 to %u); "
                      "got a %s table of length %zu\n",
                      INTERRUPT_TABLE_LENGTH, table == NULL ? "NULL" : "non-NULL", count); /* best-effort */
        return INTERRUPT_ERR_TABLE;
    }
    controller.table = table;
    controller.count = count;
    store_requests(); /* no requests: GBATEK, IF is 0 at power-on */
    controller.initialised = true;
    return INTERRUPT_OK;
}

void interrupt_shutdown(void)
{
    controller.initialised = false;
    controller.halted = false;
    controller.table = NULL;
    controller.count = 0;
    controller.requests = 0;
    controller.stored = 0;
}

void interrupt_raise(enum gba_irq source)
{
    if (!controller.initialised) {
        (void)fputs("interrupt: interrupt_raise: called before interrupt_init\n", stderr); /* best-effort */
        return;
    }
    if (!is_source(source)) {
        (void)fprintf(stderr, "interrupt: interrupt_raise: %u is not a source (0 to %d)\n", (unsigned)source,
                      GBA_IRQ_COUNT - 1); /* best-effort */
        return;
    }
    const size_t slot = slot_of(source);
    if (slot >= controller.count) {
        (void)fprintf(stderr,
                      "interrupt: interrupt_raise: %s needs gIntrTable[%zu], but the table "
                      "given to interrupt_init has %zu entries; not raised\n",
                      source_names[source], slot, controller.count); /* best-effort */
        return;
    }
    take_game_acknowledgements();
    controller.requests |= bit_of(source);
    store_requests();
}

unsigned interrupt_dispatch(void)
{
    if (!controller.initialised) {
        (void)fputs("interrupt: interrupt_dispatch: called before interrupt_init\n", stderr); /* best-effort */
        return 0;
    }
    unsigned handlers_run = 0;
    for (unsigned serviced = 0; !controller.halted; serviced++) {
        take_game_acknowledgements();
        const uint16_t pending = enabled_requests();
        if (pending == 0)
            break;
        if (serviced == INTERRUPT_DISPATCH_CAP) {
            (void)fprintf(stderr,
                          "interrupt: interrupt_dispatch: stopped after %u requests with IE & "
                          "IF = 0x%04X still pending; does a handler keep raising its own "
                          "source?\n",
                          INTERRUPT_DISPATCH_CAP, (unsigned)pending); /* best-effort */
            break;
        }
        if (service(first_in_intr_main_order(pending)))
            handlers_run++;
    }
    return handlers_run;
}

bool interrupt_halted(void)
{
    return controller.halted;
}
