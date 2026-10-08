/*
 * dma.h: DMA channels 0 to 3 (see dma.c for the implementation notes).
 *
 * Stands in for the four DMA channels at 40000B0h-40000DFh, GBATEK "GBA DMA
 * Transfers" (https://problemkaputt.de/gbatek.htm#gbadmatransfers). On
 * hardware a channel copies a block of 16- or 32-bit units while the CPU
 * waits: at once when its enable bit is set ("Immediately"), or at each
 * VBlank or HBlank, optionally repeating; it can raise an interrupt at the
 * end of each block. Design: docs/RESEARCH.md, "DMA".
 *
 * How the game reaches it. A register write is a plain store in the native
 * process, so it cannot start a transfer. Under PLATFORM_NATIVE, expansion's
 * DmaSetUnchecked (which every DmaSet, DmaCopy*, DmaFill* and DmaClear*
 * macro reaches) calls dma_set() and DmaStop calls dma_stop()
 * (dma_hooks.h). The frame loop calls dma_run_timing(DMA_TIMING_HBLANK)
 * between scanlines and dma_run_timing(DMA_TIMING_VBLANK) on entering VBlank
 * (docs/ARCHITECTURE.md, "The frame").
 *
 * Registers. dma_set() writes DMAxSAD, DMAxDAD and DMAxCNT (CNT_L and CNT_H
 * as one 32-bit store) exactly as the macro did, so the game reads back what
 * it wrote. As GBATEK says, the hardware never changes SAD, DAD or CNT_L;
 * the module clears the CNT_H enable bit when a non-repeating transfer ends.
 * 64-bit: SAD and DAD are 32 bits wide, so a native address above 4 GiB (the
 * 64-bit stack, for example) is kept only in its low 32 bits there; the
 * module keeps the full address and uses only that. The module's state, not
 * the register page, decides what runs: a store to the DMA registers that
 * does not come through dma_set() (src/m4a.c writes DMA1 and DMA2 directly
 * for the sound FIFOs) is recorded and never acted on.
 *
 * Transfers follow GBATEK: a unit count of 0 means the maximum (4000h on
 * DMA0-2, whose CNT_L field is 14 bits; 10000h on DMA3); destination
 * increment, decrement, fixed, or increment/reload; source increment,
 * decrement or fixed, a decrementing address walking down from the start
 * address; units copied one at a time in order, so overlapping blocks behave
 * as on hardware. A transfer starts by loading the internal source,
 * destination and count from SAD, DAD and CNT_L; on each repeat the count is
 * reloaded from CNT_L and, with increment/reload, the destination from DAD,
 * while the source carries on from where it stopped. A non-repeating channel
 * clears its enable bit after one block; an "Immediately" channel always
 * runs exactly once (GBATEK does not define repeat for it). Channels run in
 * priority order, DMA0 first. The IRQ bit raises GBA_IRQ_DMA0+n once per
 * completed block.
 *
 * Deliberate departures from the hardware:
 *  - Alignment. GBATEK: the GBA ignores the low address bits, forcing 16-bit
 *    units to 2-byte and 32-bit units to 4-byte boundaries. Expansion is
 *    compiled for the GBA with -mabi=apcs-gnu, which rounds every struct to
 *    4-byte alignment and size; x86 does not, so natively the game
 *    legitimately passes 2-aligned addresses to 32-bit transfers. Masking
 *    the low bits would write over neighbouring bytes, so the module copies
 *    each unit at the exact address given, any alignment, with memcpy.
 *  - Address masks. The hardware keeps the low 27 bits (DMA0 source, DMA0-2
 *    destination) or 28 bits of an address. Native addresses are not GBA
 *    bus addresses (the executable sits at 40000000h, the 64-bit stack far
 *    above), so no mask is applied; transfer_bounds_ok() decides instead
 *    what a transfer may read and write, and refuses the rest.
 *  - Start edge. GBATEK loads the internal registers when the enable bit
 *    goes from 0 to 1. Every dma_set() with the enable bit set starts the
 *    channel afresh: expansion's every DmaSet follows a DmaStop or a
 *    finished immediate transfer, so on hardware each is such an edge.
 *  - "Special" start timing (sound FIFO on DMA1/2, video capture on DMA3,
 *    prohibited on DMA0) is not emulated: the audio mixer feeds the speaker
 *    directly (docs/RESEARCH.md, "Audio"). dma_set() records it in the
 *    registers, with the enable bit, prints a note to stderr the first time,
 *    and returns DMA_ERR_TIMING; the channel never runs.
 *  - Game Pak DRQ (CNT_H bit 11, DMA3) is recorded and has no effect: there
 *    is no cartridge to request transfers.
 *
 * Refusals. A transfer is refused, with a message on stderr naming the
 * channel and the reason and a negative return, when the module is not
 * initialised, the source control is the prohibited value 3, or a byte the
 * block would read is not readable, or one it would write not writable, per
 * transfer_bounds_ok(). A refused transfer touches no memory and raises no
 * interrupt; its registers are written as given and then its enable bit is
 * cleared, as at the end of a transfer, since the channel is not running.
 * A deferred channel is checked when armed and again at every run (its
 * source and destination move between runs); a run that fails is refused
 * the same way and stops the channel. The game does not check DMA results,
 * so the macros discard the return value; the message is what is left.
 *
 * Single-threaded: everything here runs on the main thread.
 */
#ifndef DMA_H
#define DMA_H

#include <stdint.h>

#include "dma_hooks.h"
#include "gba_irq.h"

/* GBATEK "GBA DMA Transfers": four channels, DMA0 to DMA3. */
#define DMA_CHANNEL_COUNT 4u

/* GBATEK "GBA DMA Transfers", DMAxCNT_H bits 12-13 "DMA Start Timing":
 * 1 = VBlank, 2 = HBlank, as the field's value within CNT_H (equal to
 * expansion's DMA_START_VBLANK and DMA_START_HBLANK). */
#define DMA_TIMING_VBLANK UINT16_C(0x1000)
#define DMA_TIMING_HBLANK UINT16_C(0x2000)

enum dma_error {
    DMA_OK = 0,
    DMA_ERR_NO_RAISE = -1,        /* dma_init() was given a NULL raise function */
    DMA_ERR_NOT_INITIALISED = -2, /* dma_set() before dma_init() or after dma_shutdown() */
    DMA_ERR_CHANNEL = -3,         /* channel number 4 or above */
    DMA_ERR_TIMING = -4,          /* "special" start timing: recorded, never run */
    DMA_ERR_SOURCE_CONTROL = -5,  /* source address control 3, "Prohibited" */
    DMA_ERR_BOUNDS = -6           /* the block reads or writes memory transfer_bounds refuses */
};

/* Starts with every channel idle and keeps `raise`, which is called with
 * GBA_IRQ_DMA0..3 when a channel with its IRQ bit set completes a block
 * (main passes interrupt_raise). Does not touch the registers. Returns
 * DMA_OK, or DMA_ERR_NO_RAISE for NULL, leaving the module uninitialised. */
int dma_init(void (*raise)(enum gba_irq));

/* Forgets the raise function and every channel's state. Safe at any time,
 * including before init and twice. Does not touch the registers. */
void dma_shutdown(void);

/* dma_set (declared in dma_hooks.h). Writes the registers as above. Then,
 * if control's enable bit is clear, the channel is idle and the result is
 * DMA_OK. If it is set: with timing "Immediately" the block is copied now;
 * with VBlank or HBlank timing the channel is checked and armed, and
 * nothing is copied until dma_run_timing(); with "special" timing see
 * above. Returns DMA_OK or a negative enum dma_error; DMA_ERR_CHANNEL
 * writes nothing at all. */

/* dma_stop (declared in dma_hooks.h). Clears CNT_H's start timing, Game Pak
 * DRQ, repeat and enable bits, leaving the register as expansion's DmaStop
 * macro did, and disarms the channel. Channel 4 or above: prints, nothing
 * else. Works with or without init. */

/* Runs, in priority order, every armed channel whose start timing equals
 * `timing` (DMA_TIMING_HBLANK between scanlines, DMA_TIMING_VBLANK at the
 * start of VBlank), one block each, as the hardware does at that event.
 * Returns how many blocks were copied; 0 for any other timing value and
 * before init. Cheap when nothing is armed: it is called 160 times a frame. */
unsigned dma_run_timing(uint16_t timing);

#endif /* DMA_H */
