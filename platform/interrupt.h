/*
 * interrupt.h: the GBA interrupt controller and expansion's IRQ handler.
 *
 * Stands in for the CPU's IRQ line, the interrupt registers IE, IF and IME
 * (GBATEK "GBA Interrupt Control",
 * https://problemkaputt.de/gbatek.htm#gbainterruptcontrol) and expansion's
 * own IRQ handler, IntrMain in src/crt0.s, which the BIOS jumps to through
 * the pointer at 03007FFCh (INTR_VECTOR; crt0.s:23-25, src/main.c
 * InitIntrHandlers).
 *
 * THE HARDWARE. An event sets its source's bit in IF. While IME bit 0 is set
 * and a bit is set in both IE and IF (and the CPU's CPSR allows IRQs), the CPU
 * takes the IRQ and the BIOS calls the user handler. A request stays in IF
 * until the program acknowledges it by writing 1 to its bit.
 *
 * WHAT IntrMain DOES (src/crt0.s, line numbers in that file):
 *   45      reads IE and IF together (one 32-bit load at 4000200h);
 *   46      saves IME;
 *   49-50   writes 0 to IME;
 *   51      takes IE & IF;
 *   53-96   searches the sources in a fixed order that is not bit order:
 *           VCount, Serial, Timer 3, HBlank, VBlank, Timer 0, Timer 1,
 *           Timer 2, DMA 0, DMA 1, DMA 2, DMA 3, Keypad, Game Pak; r12 counts
 *           4 bytes per source passed, so the source found at position i is
 *           handled by gIntrTable[i] (src/main.c gIntrTableTemplate lists the
 *           same order). VCount is tested before line 56-57 writes 1 to IME,
 *           so IME is 0 while the VCount handler runs and 1 while any other
 *           handler runs;
 *   94-96   Game Pak (the cartridge was pulled) is never handled: IntrMain
 *           writes a zero byte to SOUNDCNT_X (sound master enable off) and
 *           spins forever. gIntrTable[13] is reached only when nothing at
 *           all was pending (the search falls through with r0 = 0);
 *   98      acknowledges the source: writes its bit to IF, BEFORE the handler
 *           runs, so a new request of the same source during the handler is
 *           latched and serviced afterwards;
 *   99-108  narrows IE, for the handler's duration, to the sources that may
 *           nest (a handler runs with CPU IRQs enabled, lines 109-112);
 *   113-118 calls the handler from gIntrTable;
 *   126-127 restores IE and IME to the values read at lines 45-46, which
 *           discards any change the handler made to either.
 * The handlers in src/main.c (VBlankIntr, HBlankIntr, VCountIntr, SerialIntr)
 * do not write IF: line 98 is the acknowledgement. They set their bit in
 * INTR_CHECK (03007FF8h), which the BIOS's IntrWait and VBlankIntrWait wait
 * on. Game code writes IF elsewhere only to discard stale requests before it
 * enables a source (agb_flash.c, link.c, ereader_helpers.c, librfu_*.c).
 *
 * THE NATIVE MODEL. The game's handlers run synchronously on the main thread,
 * when the platform calls interrupt_dispatch() at the points
 * docs/ARCHITECTURE.md "The frame" names; interrupt_raise() only latches a
 * request. interrupt_dispatch() reproduces IntrMain's search order, slot
 * mapping, acknowledgement at line 98, IME at lines 49-57, the restore of IE
 * and IME at lines 126-127, and the Game Pak halt. Two differences:
 *  - No nesting. A request raised while a handler runs is serviced when that
 *    handler returns, in the same call. So the nesting mask of lines 99-108
 *    has nothing to select and is not written; it would also need librfu's
 *    gSTWIStatus->timerSelect, which the platform cannot see. A handler that
 *    reads REG_IE therefore sees IE as it was, not narrowed; no handler in
 *    expansion reads it.
 *  - IF is write-1-to-clear on the GBA, but the game's REG_IF writes are
 *    plain stores into the mapped I/O page. This module keeps the real
 *    request bits and, after every change, stores them at IF together with
 *    INTERRUPT_IF_MARKER, bit 15, which the hardware never sets. Before
 *    using the requests it compares IF with what it stored: a different
 *    value is a game write, and the requests that write names are cleared.
 *    Every game write of REG_IF in expansion stores INTR_FLAG_* bits only
 *    (bits 0-13), so it always differs from the stored value and is seen,
 *    including the stale-request idiom REG_IF = INTR_FLAG_SERIAL with
 *    Serial the only request (librfu_sio32id.c:68 and :103; also
 *    agb_flash.c:93, link.c:1812, ereader_helpers.c:511, librfu_stwi.c:467,
 *    librfu_intr.c:370, libgcnmultiboot.s:324 and :614). What remains:
 *     * Of several game writes between two calls into this module, only the
 *       last is seen: two of the sites above with different bits in one
 *       frame, with no raise or dispatch between them, lose the first one's
 *       acknowledgement.
 *     * A raw read of IF sees bit 15 set (GBATEK: not used); a read masked
 *       with INTR_FLAG_* sees exactly the requests. Expansion never reads IF
 *       (no read of REG_IF in src/ or include/, checked 2026-10-08).
 *     * A read-modify-write would carry the marker back. REG_IF &= x is
 *       still seen and clears the requests in x, as on the GBA; REG_IF |= x
 *       is seen and clears every request, as on the GBA, unless x is 0 or
 *       already pending, when it equals the stored value and is missed.
 *       Expansion has no read-modify-write of IF.
 *
 * Registers are read and written as halfwords at their GBA addresses, which
 * gba_memmap.h maps natively; this module never includes expansion's headers.
 */
#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gba_irq.h"

/* GBATEK "GBA Interrupt Control": IE at 4000200h, IF at 4000202h, IME at
 * 4000208h, each 16 bits here; offsets from GBA_IO_BASE (gba_memmap.h). */
#define GBA_IE_OFFSET UINT32_C(0x200)
#define GBA_IF_OFFSET UINT32_C(0x202)
#define GBA_IME_OFFSET UINT32_C(0x208)

/* GBATEK "GBA Interrupt Control", IME: "Bit 0 Disable all interrupts
 * (0=Disable All, 1=See IE register)"; bits 1-31 not used. */
#define GBA_IME_ENABLE UINT16_C(0x0001)

/* Kept set in the value this module stores at IF, so that every game store
 * of request bits differs from it and is seen (above). GBATEK "GBA Interrupt
 * Control", IF: "14-15 Not used"; the hardware never sets bit 15 and
 * expansion never writes it. */
#define INTERRUPT_IF_MARKER UINT16_C(0x8000)

/* The length of expansion's gIntrTable (src/main.c INTR_COUNT): IntrMain's
 * fourteen slots, r12 = 0 to 13 * 4 at src/crt0.s:113-115. */
#define INTERRUPT_TABLE_LENGTH 14u

/* The most requests one interrupt_dispatch() call services. A call services
 * each pending source once, at most GBA_IRQ_COUNT, plus the requests its
 * handlers cause (a DMA they start with its IRQ bit set, say). A handler
 * that keeps re-raising its own source would hang the GBA; here the call
 * stops after this many with a report on stderr and leaves the rest pending.
 * Far above any legitimate call, small enough to stop a runaway at once. */
#define INTERRUPT_DISPATCH_CAP 256u

enum interrupt_error {
    INTERRUPT_OK = 0,
    INTERRUPT_ERR_TABLE = -1 /* NULL table, or count 0 or above INTERRUPT_TABLE_LENGTH */
};

typedef void (*interrupt_handler)(void);

/* Powers the controller on: no requests (IF holds only INTERRUPT_IF_MARKER),
 * not halted. table is
 * the game's handler table in gIntrTable order (src/main.c), and count its
 * length, at most INTERRUPT_TABLE_LENGTH. The table is read at each dispatch,
 * not copied, because the game fills and changes gIntrTable as it runs
 * (InitIntrHandlers, RestoreSerialTimer3IntrHandlers, the flash and RFU
 * timers). IE and IME are the game's and are not touched. Calling it again
 * powers the controller on again. Returns INTERRUPT_OK, or
 * INTERRUPT_ERR_TABLE after reporting on stderr; the controller is then not
 * initialised. */
int interrupt_init(interrupt_handler const *table, size_t count);

/* Forgets the table and the requests. Safe at any time: before init, after
 * a failed init, more than once. The I/O registers are left as they are. */
void interrupt_shutdown(void);

/* Sets the source's bit in IF, as the hardware does when the event happens.
 * Does not dispatch. Rejected with a report on stderr, IF unchanged, if the
 * controller is not initialised, the source is not a gba_irq, or the table
 * given to interrupt_init() has no slot for it (slot = the source's position
 * in IntrMain's order, above). */
void interrupt_raise(enum gba_irq source);

/* Does what IntrMain does, repeatedly: while IME bit 0 is set and
 * (IE & IF) is non-zero, takes the source IntrMain would pick, acknowledges
 * it in IF, runs its handler with IME as crt0 sets it, and restores IE and
 * IME. Stops when nothing enabled is pending, after INTERRUPT_DISPATCH_CAP
 * handlers (reported), or when a Game Pak request halts the controller
 * (reported once). A NULL table entry is reported and skipped, not called.
 * Returns the number of handlers run; 0, with a report, if not initialised. */
unsigned interrupt_dispatch(void);

/* True once a Game Pak interrupt has been taken: like IntrMain's spin at
 * src/crt0.s:94-96, the controller runs no handler again until it is
 * initialised again. The caller decides what to do (the GBA hangs). */
bool interrupt_halted(void);

#endif /* INTERRUPT_H */
