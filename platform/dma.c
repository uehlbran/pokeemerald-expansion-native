/*
 * dma.c: DMA channels 0 to 3.
 *
 * Stands in for the four DMA channels at 40000B0h-40000DFh. On hardware a
 * channel copies CNT_L units of 16 or 32 bits from SAD to DAD, stepping each
 * address up, down or not at all, at once or at each VBlank or HBlank, and
 * the CPU waits until it is done; GBATEK "GBA DMA Transfers",
 * https://problemkaputt.de/gbatek.htm#gbadmatransfers. Clean room from
 * GBATEK; the native policy (what is relaxed, refused or not emulated, and
 * why) is in dma.h. Design: docs/RESEARCH.md, "DMA". Single-threaded.
 *
 * Addresses. A channel's addresses are kept as uintptr_t, as the hardware's
 * internal address registers are integers: stepping a block's address below
 * its start or past its end is ordinary unsigned arithmetic, where the same
 * on a C pointer would be undefined. An address becomes a pointer only for
 * the memcpy of one unit, after transfer_bounds_ok() has accepted the whole
 * block that unit lies in. 64-bit: a native pointer round-trips through
 * uintptr_t unchanged on both widths; only the SAD and DAD registers, which
 * are 32 bits, truncate.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "dma.h"
#include "gba_memmap.h"
#include "transfer_bounds.h"

/* dma_set() stores DMAxCNT as one 32-bit word, CNT_L in the low half at the
 * lower address, as the GBA (little-endian) and expansion's macro do. */
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
               "DMAxCNT layout assumes a little-endian host");

/* GBATEK "GBA DMA Transfers": DMA0SAD at 40000B0h; each channel has SAD,
 * DAD, CNT_L and CNT_H, 12 bytes, so DMA1SAD is at 40000BCh and DMA3CNT_H
 * at 40000DEh. */
#define DMA_REGISTERS_OFFSET UINT32_C(0xB0)
#define DMA_REGISTER_STRIDE UINT32_C(12)
#define DMA_SAD_OFFSET UINT32_C(0)
#define DMA_DAD_OFFSET UINT32_C(4)
#define DMA_CNT_OFFSET UINT32_C(8)    /* CNT_L, and CNT_L with CNT_H as one word */
#define DMA_CNT_H_SHIFT 16u              /* CNT_H is the high halfword of that word (GBATEK "GBA DMA Transfers") */
#define DMA_CNT_H_OFFSET UINT32_C(10)

/* GBATEK "GBA DMA Transfers", DMAxCNT_L: "14 bit, 1..4000h" on DMA0-2,
 * "16 bit, 1..10000h" on DMA3; "a value of zero is treated as max length". */
#define DMA_CHANNEL_3 3u
#define DMA_CNT_L_MASK_DMA0_2 UINT32_C(0x3FFF)
#define DMA_CNT_L_MASK_DMA3 UINT32_C(0xFFFF)
#define DMA_MAX_UNITS_DMA0_2 UINT32_C(0x4000)
#define DMA_MAX_UNITS_DMA3 UINT32_C(0x10000)

/* GBATEK "GBA DMA Transfers", DMAxCNT_H. Bits 5-6: destination address
 * control; 7-8: source address control; 9: repeat; 10: transfer type
 * (0 = 16-bit, 1 = 32-bit); 11: Game Pak DRQ (DMA3); 12-13: start timing;
 * 14: IRQ at the end of the word count; 15: enable. */
#define DMA_CNT_H_DEST_CONTROL_SHIFT 5
#define DMA_CNT_H_SOURCE_CONTROL_SHIFT 7
#define DMA_CNT_H_ADDRESS_CONTROL_MASK 3u /* two bits each */
#define DMA_CNT_H_REPEAT UINT16_C(0x0200)
#define DMA_CNT_H_32BIT UINT16_C(0x0400)
#define DMA_CNT_H_GAMEPAK_DRQ UINT16_C(0x0800)
#define DMA_CNT_H_START_TIMING_MASK UINT16_C(0x3000)
#define DMA_CNT_H_IRQ_ENABLE UINT16_C(0x4000)
#define DMA_CNT_H_ENABLE UINT16_C(0x8000)

/* GBATEK: start timing 0 = "Immediately", 3 = "Special" (VBlank and HBlank
 * are in dma.h). */
#define DMA_TIMING_IMMEDIATE UINT16_C(0x0000)
#define DMA_TIMING_SPECIAL UINT16_C(0x3000)

/* GBATEK: a unit is 16 or 32 bits. */
#define DMA_UNIT_16BIT 2u
#define DMA_UNIT_32BIT 4u

/* GBATEK, CNT_H bits 5-6 and 7-8. Value 3 is "Increment/Reload" for the
 * destination and "Prohibited" for the source. */
enum dma_address_control {
    DMA_ADDRESS_INCREMENT = 0,
    DMA_ADDRESS_DECREMENT = 1,
    DMA_ADDRESS_FIXED = 2,
    DMA_ADDRESS_INCREMENT_RELOAD = 3, /* destination only */
    DMA_ADDRESS_PROHIBITED = 3        /* source only */
};

struct dma_channel {
    /* SAD and DAD in full (64-bit: the registers hold the low 32 bits). */
    uintptr_t sad;
    uintptr_t dad;
    /* GBATEK's internal source and destination pointers: loaded from SAD
     * and DAD when the channel starts, stepped by every unit, kept between
     * repeats (the destination reloaded from DAD with Increment/Reload). */
    uintptr_t source;
    uintptr_t destination;
    uint16_t cnt_l;
    uint16_t cnt_h;
    bool armed; /* enabled with VBlank or HBlank timing: waiting for it */
};

static struct {
    void (*raise)(enum gba_irq);
    bool special_noted;
    struct dma_channel channels[DMA_CHANNEL_COUNT];
} state;

/* ---- registers ---- */

static uintptr_t register_address(unsigned channel, uint32_t offset)
{
    return (uintptr_t)(GBA_IO_BASE + DMA_REGISTERS_OFFSET + channel * DMA_REGISTER_STRIDE + offset);
}

static volatile uint32_t *register32(unsigned channel, uint32_t offset)
{
    return (volatile uint32_t *)register_address(channel, offset);
}

static volatile uint16_t *register_cnt_h(unsigned channel)
{
    return (volatile uint16_t *)register_address(channel, DMA_CNT_H_OFFSET);
}

/* The three 32-bit stores of expansion's DmaSetUnchecked. 64-bit: SAD and
 * DAD keep only the low 32 bits of an address above 4 GiB. */
static void write_registers(unsigned channel, uintptr_t sad, uintptr_t dad, uint32_t control)
{
    *register32(channel, DMA_SAD_OFFSET) = (uint32_t)sad;
    *register32(channel, DMA_DAD_OFFSET) = (uint32_t)dad;
    *register32(channel, DMA_CNT_OFFSET) = control;
}

/* Clears `bits` in CNT_H, in the register (keeping whatever else the game
 * stored there) and in the channel's copy. */
static void clear_cnt_h_bits(unsigned channel, uint16_t bits)
{
    volatile uint16_t *cnt_h = register_cnt_h(channel);

    *cnt_h = (uint16_t)(*cnt_h & ~bits);
    state.channels[channel].cnt_h = (uint16_t)(state.channels[channel].cnt_h & ~bits);
}

/* GBATEK "Transfer End": the enable bit is cleared when a transfer that does
 * not repeat completes. Also how a refused transfer ends. */
static void end_channel(unsigned channel)
{
    state.channels[channel].armed = false;
    clear_cnt_h_bits(channel, DMA_CNT_H_ENABLE);
}

/* ---- CNT fields ---- */

static uint16_t start_timing(uint16_t cnt_h)
{
    return (uint16_t)(cnt_h & DMA_CNT_H_START_TIMING_MASK);
}

static enum dma_address_control address_control(uint16_t cnt_h, unsigned shift)
{
    return (enum dma_address_control)(((unsigned)cnt_h >> shift) & DMA_CNT_H_ADDRESS_CONTROL_MASK);
}

static enum dma_address_control destination_control(uint16_t cnt_h)
{
    return address_control(cnt_h, DMA_CNT_H_DEST_CONTROL_SHIFT);
}

static enum dma_address_control source_control(uint16_t cnt_h)
{
    return address_control(cnt_h, DMA_CNT_H_SOURCE_CONTROL_SHIFT);
}

static size_t unit_size(uint16_t cnt_h)
{
    return (cnt_h & DMA_CNT_H_32BIT) != 0 ? DMA_UNIT_32BIT : DMA_UNIT_16BIT;
}

/* CNT_L through the channel's field width, 0 standing for the maximum. */
static uint32_t unit_count(unsigned channel, uint16_t cnt_l)
{
    bool dma3 = channel == DMA_CHANNEL_3;
    uint32_t count = cnt_l & (dma3 ? DMA_CNT_L_MASK_DMA3 : DMA_CNT_L_MASK_DMA0_2);

    if (count == 0)
        return dma3 ? DMA_MAX_UNITS_DMA3 : DMA_MAX_UNITS_DMA0_2;
    return count;
}

/* ---- one block ---- */

/* The next unit's address. Increment/Reload steps like Increment within a
 * block; the reload happens between blocks. */
static uintptr_t step(uintptr_t address, enum dma_address_control control, size_t unit)
{
    switch (control) {
    case DMA_ADDRESS_DECREMENT:
        return address - unit;
    case DMA_ADDRESS_FIXED:
        return address;
    default:
        return address + unit;
    }
}

/* The bytes one block's walk touches: [first, first + length). */
struct dma_range {
    uintptr_t first;
    size_t length;
    bool below_zero; /* a decrementing walk that would pass address 0 */
};

/* The range a walk of `count` units from `start` covers. Decrement walks
 * down from start, so its lowest byte is (count - 1) units below it. */
static struct dma_range walk_range(uintptr_t start, enum dma_address_control control, size_t unit,
                                   uint32_t count)
{
    struct dma_range range = { .first = start, .length = unit };

    if (control == DMA_ADDRESS_FIXED)
        return range;
    range.length = (size_t)count * unit;
    if (control == DMA_ADDRESS_DECREMENT) {
        size_t below = range.length - unit;
        range.below_zero = start < below;
        range.first = start - below;
    }
    return range;
}

/* True if transfer_bounds allows the whole range; otherwise prints which
 * range of `what` was refused. */
static bool range_allowed(unsigned channel, const char *what, struct dma_range range,
                          enum transfer_access access)
{
    if (!range.below_zero && transfer_bounds_ok(range.first, range.length, access))
        return true;
    fprintf(stderr, "dma: channel %u: %s: %zu bytes from 0x%" PRIxPTR " are not %s; "
            "transfer refused\n", channel, what, range.length, range.first,
            access == TRANSFER_READ ? "readable" : "writable");
    return false;
}

/* Checks the block the channel would copy next: DMA_OK, or DMA_ERR_BOUNDS
 * after printing what transfer_bounds refused. */
static int check_block(unsigned channel)
{
    const struct dma_channel *c = &state.channels[channel];
    size_t unit = unit_size(c->cnt_h);
    uint32_t count = unit_count(channel, c->cnt_l);
    struct dma_range source = walk_range(c->source, source_control(c->cnt_h), unit, count);
    struct dma_range destination =
        walk_range(c->destination, destination_control(c->cnt_h), unit, count);

    if (!range_allowed(channel, "source", source, TRANSFER_READ)
        || !range_allowed(channel, "destination", destination, TRANSFER_WRITE))
        return DMA_ERR_BOUNDS;
    return DMA_OK;
}

/* Copies one block, a unit at a time in GBATEK's order: each unit is read
 * into a latch and then written, so a unit that overlaps itself or one
 * written before it behaves as on hardware. Leaves the internal pointers
 * where the block stopped. */
static void copy_block(unsigned channel)
{
    struct dma_channel *c = &state.channels[channel];
    enum dma_address_control from_control = source_control(c->cnt_h);
    enum dma_address_control to_control = destination_control(c->cnt_h);
    size_t unit = unit_size(c->cnt_h);
    uint32_t count = unit_count(channel, c->cnt_l);
    unsigned char latch[DMA_UNIT_32BIT];
    uintptr_t from = c->source;
    uintptr_t to = c->destination;

    for (uint32_t i = 0; i < count; i++) {
        memcpy(latch, (const void *)from, unit);
        memcpy((void *)to, latch, unit);
        from = step(from, from_control, unit);
        to = step(to, to_control, unit);
    }
    c->source = from;
    c->destination = to;
}

/* What follows a completed block: GBATEK "DMA Repeat bit" and "Upon Repeat:
 * Reloads CNT_L, and optionally DAD". The count needs no reload here, since
 * every block reads CNT_L afresh. An "Immediately" channel never repeats. */
static void finish_block(unsigned channel)
{
    struct dma_channel *c = &state.channels[channel];
    bool repeats = (c->cnt_h & DMA_CNT_H_REPEAT) != 0
                   && start_timing(c->cnt_h) != DMA_TIMING_IMMEDIATE;

    if (repeats) {
        if (destination_control(c->cnt_h) == DMA_ADDRESS_INCREMENT_RELOAD)
            c->destination = c->dad;
    } else {
        end_channel(channel);
    }
    if ((c->cnt_h & DMA_CNT_H_IRQ_ENABLE) != 0)
        state.raise((enum gba_irq)(GBA_IRQ_DMA0 + channel));
}

/* Checks, copies and finishes one block. A refused block copies nothing and
 * leaves ending the channel to the caller. */
static int run_block(unsigned channel)
{
    int error = check_block(channel);

    if (error != DMA_OK)
        return error;
    copy_block(channel);
    finish_block(channel);
    return DMA_OK;
}

/* Special timing is legitimate game behaviour that the platform does not
 * emulate, so it is reported once rather than at every call. */
static void note_special_timing(unsigned channel)
{
    if (state.special_noted)
        return;
    state.special_noted = true;
    fprintf(stderr,
            "dma: channel %u: start timing \"special\" (sound FIFO, video capture) is not "
            "emulated; registers recorded, transfer never runs (reported once)\n",
            channel);
}

/* ---- public ---- */

int dma_init(void (*raise)(enum gba_irq))
{
    memset(&state, 0, sizeof state);
    if (raise == NULL)
        return DMA_ERR_NO_RAISE;
    state.raise = raise;
    return DMA_OK;
}

void dma_shutdown(void)
{
    memset(&state, 0, sizeof state);
}

/* Starts a channel dma_set() has just enabled: copies its block now or arms
 * it for its VBlank or HBlank. Returns DMA_OK or why it did not start. */
static int start_channel(unsigned channel)
{
    struct dma_channel *c = &state.channels[channel];

    if (state.raise == NULL) {
        fprintf(stderr, "dma: channel %u: dma_set before dma_init; transfer refused\n", channel);
        return DMA_ERR_NOT_INITIALISED;
    }
    if (start_timing(c->cnt_h) == DMA_TIMING_SPECIAL) {
        note_special_timing(channel);
        return DMA_ERR_TIMING;
    }
    if (source_control(c->cnt_h) == DMA_ADDRESS_PROHIBITED) {
        fprintf(stderr,
                "dma: channel %u: source address control 3 is prohibited; transfer refused\n",
                channel);
        return DMA_ERR_SOURCE_CONTROL;
    }
    if (start_timing(c->cnt_h) == DMA_TIMING_IMMEDIATE)
        return run_block(channel);
    int error = check_block(channel);
    if (error == DMA_OK)
        c->armed = true;
    return error;
}

int dma_set(unsigned channel, const void *src, void *dest, uint32_t control)
{
    if (channel >= DMA_CHANNEL_COUNT) {
        fprintf(stderr, "dma: dma_set: there is no channel %u (0 to 3); nothing written\n",
                channel);
        return DMA_ERR_CHANNEL;
    }
    uintptr_t sad = (uintptr_t)src;
    uintptr_t dad = (uintptr_t)dest;
    uint16_t cnt_h = (uint16_t)(control >> DMA_CNT_H_SHIFT);

    write_registers(channel, sad, dad, control);
    state.channels[channel] = (struct dma_channel){
        .sad = sad, .dad = dad, .source = sad, .destination = dad,
        .cnt_l = (uint16_t)control, .cnt_h = cnt_h,
    };
    if ((cnt_h & DMA_CNT_H_ENABLE) == 0)
        return DMA_OK;
    int error = start_channel(channel);
    /* Special timing stays recorded with its enable bit; any other refusal
     * leaves the channel idle. */
    if (error != DMA_OK && error != DMA_ERR_TIMING)
        end_channel(channel);
    return error;
}

void dma_stop(unsigned channel)
{
    if (channel >= DMA_CHANNEL_COUNT) {
        fprintf(stderr, "dma: dma_stop: there is no channel %u (0 to 3)\n", channel);
        return;
    }
    /* Expansion's DmaStop clears start timing, DRQ and repeat, then enable. */
    clear_cnt_h_bits(channel, DMA_CNT_H_START_TIMING_MASK | DMA_CNT_H_GAMEPAK_DRQ | DMA_CNT_H_REPEAT
                                  | DMA_CNT_H_ENABLE);
    state.channels[channel].armed = false;
}

unsigned dma_run_timing(uint16_t timing)
{
    unsigned blocks = 0;

    for (unsigned channel = 0; channel < DMA_CHANNEL_COUNT; channel++) {
        const struct dma_channel *c = &state.channels[channel];
        if (!c->armed || start_timing(c->cnt_h) != timing)
            continue;
        if (run_block(channel) == DMA_OK)
            blocks++;
        else
            end_channel(channel);
    }
    return blocks;
}
