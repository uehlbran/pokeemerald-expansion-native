/*
 * bios_control.c: the BIOS reset and halt functions, the frame hook, and
 * MultiBoot.
 *
 * Stands in for SWI 00h-05h and 25h (GBATEK "BIOS Reset Functions", "BIOS
 * Halt Functions", "BIOS Multi Boot (Single Game Pak)", "GBA BIOS RAM Usage";
 * https://problemkaputt.de/gbatek.htm#biosfunctions) and for the parts of
 * libagbsyscall that wrap them (SoftReset = IME 0, RegisterRamReset, SWI 00h).
 *
 * The halt functions are where the native frame loop runs. On the GBA,
 * VBlankIntrWait sleeps the CPU while the hardware draws and raises
 * interrupts, and the game's handler ORs each serviced interrupt into the
 * BIOS flags at 3007FF8h (INTR_CHECK); the BIOS returns when the awaited
 * flag appears there. Natively, "sleeping" is calling the frame hook the
 * frame loop registered with bios_init(), which renders a frame, runs the
 * timers and dispatches the interrupts (docs/ARCHITECTURE.md, "The frame");
 * the flag check is the hardware's, unchanged.
 *
 * SoftReset restarts the game. Natively the restart point is a jmp_buf the
 * frame loop registers (bios_set_reset_target); the reset longjmps there
 * after clearing what the BIOS clears, and never exits the process (the
 * ports' exit(0) is what this replaces). The jump goes through
 * game_hooks_unwind, which first leaves the battle test runner's stack if a
 * test body is running on it (game_hooks.h). The re-entry flag at 3007FFAh (ROM
 * or EWRAM) has no native meaning: there is one entry point.
 *
 * Single-threaded: the frame loop and the game share the main thread, so
 * the hook and the target are plain module state.
 */
#include <stdio.h>
#include <string.h>

#include "bios.h"
#include "game_hooks.h"
#include "gba_irq.h"
#include "gba_memmap.h"

/* GBATEK "GBA BIOS RAM Usage": "3007FF8h 2 IRQ IF Check Flags (for SWI
 * IntrWait/VBlankIntrWait functions)". */
#define BIOS_INTR_CHECK_ADDRESS UINT32_C(0x03007FF8)
/* GBATEK SoftReset: zero-filled area [3007E00h..3007FFFh], the last 200h
 * bytes of IWRAM; RegisterRamReset's IWRAM clear is "excluding last 200h
 * bytes". */
#define BIOS_RAM_AREA_SIZE UINT32_C(0x200)
#define BIOS_RAM_AREA_BASE (GBA_IWRAM_BASE + GBA_IWRAM_SIZE - BIOS_RAM_AREA_SIZE)

/* GBATEK "GBA I/O Map": register offsets in the I/O region. */
#define IO_DISPCNT UINT32_C(0x000)
#define IO_DISPSTAT UINT32_C(0x004)
#define IO_RCNT UINT32_C(0x134)
#define IO_IME UINT32_C(0x208)

/* GBATEK RegisterRamReset: "always switches the screen into forced blank by
 * setting DISPCNT=0080h". */
#define DISPCNT_FORCED_BLANK UINT16_C(0x0080)
/* GBATEK "LCD I/O Interrupts and Status": DISPSTAT bits 0-2 (V-Blank,
 * H-Blank, V-Counter flags) are read-only. */
#define DISPSTAT_READ_ONLY UINT16_C(0x0007)
/* GBATEK "SIO General-Purpose Mode", RCNT: bit 15 "Must be 1", bit 14 "Must
 * be 0" for general-purpose mode; RegisterRamReset bit 5 "switches to
 * general purpose mode!". */
#define RCNT_GENERAL_PURPOSE UINT16_C(0x8000)
/* GBATEK "GBA Interrupt Control": IME bit 0. IntrWait "forcefully sets
 * IME=1"; libagbsyscall's SoftReset clears it first. */
#define IME_ENABLED UINT16_C(1)
#define IME_DISABLED UINT16_C(0)
/* GBATEK MultiBoot: "r0 0=okay, 1=failed". */
#define MULTIBOOT_FAILED 1

#define REGISTER_SIZE 2u

/* A run of I/O registers, offsets [start, end), halfword-aligned. */
struct io_range {
    uint32_t start;
    uint32_t end;
};

/* RegisterRamReset bit 7, "all other registers (except SIO, Sound)", from
 * GBATEK "GBA I/O Map". Not the read-only VCOUNT (006h) and KEYINPUT
 * (130h), which the hardware does not let a write change (natively the
 * frame loop and the input module own them); DISPSTAT (004h) keeps its
 * read-only bits; POSTFLG/HALTCNT (300h) are left alone, as a write to
 * HALTCNT halts the CPU. */
static const struct io_range other_registers[] = {
    {0x000, 0x004}, /* DISPCNT, green swap */
    {0x008, 0x060}, /* BG0CNT-BLDY and the unused 056h-05Fh */
    {0x0B0, 0x0E0}, /* DMA0-DMA3 */
    {0x100, 0x110}, /* TM0CNT-TM3CNT */
    {0x132, 0x134}, /* KEYCNT */
    {0x200, 0x20C}, /* IE, IF, WAITCNT, IME (IF: see RegisterRamReset) */
};

/* RegisterRamReset bit 6: GBATEK "Sound Registers", 060h-0A7h, except
 * SOUNDBIAS (088h), which GBATEK marks as BIOS-owned and gives no reset
 * value for (SoundBias sets it). */
static const struct io_range sound_registers[] = {
    {0x060, 0x088}, /* SOUND1CNT_L-SOUNDCNT_X */
    {0x08A, 0x0A8}, /* WAVE_RAM, FIFO_A, FIFO_B */
};

/* RegisterRamReset bit 5: GBATEK "Serial Communication (1)" and "(2)". */
static const struct io_range sio_registers[] = {
    {0x120, 0x12C}, /* SIODATA32/SIOMULTI0-3, SIOCNT, SIOMLT_SEND/SIODATA8 */
    {0x134, 0x136}, /* RCNT */
    {0x140, 0x142}, /* JOYCNT */
    {0x150, 0x15A}, /* JOY_RECV, JOY_TRANS, JOYSTAT */
};

static void (*frame_hook)(void);
static jmp_buf *reset_target;

static volatile uint16_t *intr_check(void)
{
    return (volatile uint16_t *)(uintptr_t)BIOS_INTR_CHECK_ADDRESS;
}

static void clear_memory(uint32_t address, uint32_t size)
{
    memset((void *)(uintptr_t)address, 0, size);
}

static void clear_registers(const struct io_range *ranges, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        for (uint32_t offset = ranges[i].start; offset < ranges[i].end; offset += REGISTER_SIZE) {
            *gba_io_u16(offset) = 0;
        }
    }
}

int bios_init(void (*run_frame)(void))
{
    frame_hook = run_frame;
    reset_target = NULL;
    return BIOS_OK;
}

void bios_shutdown(void)
{
    frame_hook = NULL;
    reset_target = NULL;
}

void bios_set_reset_target(jmp_buf *target)
{
    reset_target = target;
}

void RegisterRamReset(uint32_t resetFlags)
{
    if (resetFlags & BIOS_RESET_EWRAM) {
        clear_memory(GBA_EWRAM_BASE, GBA_EWRAM_SIZE);
    }
    if (resetFlags & BIOS_RESET_IWRAM) {
        clear_memory(GBA_IWRAM_BASE, GBA_IWRAM_SIZE - BIOS_RAM_AREA_SIZE);
    }
    if (resetFlags & BIOS_RESET_PALETTE) {
        clear_memory(GBA_PALETTE_RAM_BASE, GBA_PALETTE_RAM_SIZE);
    }
    if (resetFlags & BIOS_RESET_VRAM) {
        clear_memory(GBA_VRAM_BASE, GBA_VRAM_SIZE);
    }
    if (resetFlags & BIOS_RESET_OAM) {
        clear_memory(GBA_OAM_BASE, GBA_OAM_SIZE);
    }
    if (resetFlags & BIOS_RESET_SIO_REGS) {
        clear_registers(sio_registers, sizeof sio_registers / sizeof sio_registers[0]);
        *gba_io_u16(IO_RCNT) = RCNT_GENERAL_PURPOSE;
    }
    if (resetFlags & BIOS_RESET_SOUND_REGS) {
        clear_registers(sound_registers, sizeof sound_registers / sizeof sound_registers[0]);
    }
    if (resetFlags & BIOS_RESET_REGS) {
        /* This zeroes REG_IF too, but the interrupt module keeps the pending
         * requests in its own state and marks the value it last stored in
         * IF with INTERRUPT_IF_MARKER (bit 15; interrupt.h): to it a zero IF
         * is a game write acknowledging every request, not a reset of its
         * state. main's reset path must therefore re-initialise the
         * interrupt module after a reset; the BIOS does not call it (it
         * would make bios depend on the interrupt module's internals). */
        clear_registers(other_registers, sizeof other_registers / sizeof other_registers[0]);
        *gba_io_u16(IO_DISPSTAT) = (uint16_t)(*gba_io_u16(IO_DISPSTAT) & DISPSTAT_READ_ONLY);
    }
    *gba_io_u16(IO_DISPCNT) = DISPCNT_FORCED_BLANK;
}

int bios_soft_reset(uint32_t resetFlags)
{
    if (reset_target == NULL) {
        (void)fprintf(
            stderr,
            "bios: SoftReset: no reset target (bios_set_reset_target); nothing reset, returning to the caller\n");
        return BIOS_ERR_NO_RESET_TARGET;
    }
    *gba_io_u16(IO_IME) = IME_DISABLED;
    RegisterRamReset(resetFlags);
    clear_memory(BIOS_RAM_AREA_BASE, BIOS_RAM_AREA_SIZE);
    game_hooks_unwind(reset_target, BIOS_RESET_JUMP_VALUE);
}

void SoftReset(uint32_t resetFlags)
{
    (void)bios_soft_reset(resetFlags); /* returns only without a target, after printing */
}

void SoftResetRom(uint32_t resetFlags)
{
    (void)bios_soft_reset(resetFlags);
}

void SoftResetExram(uint32_t resetFlags)
{
    (void)bios_soft_reset(resetFlags & ~BIOS_RESET_EWRAM);
}

void IntrWait(uint32_t discardOldFlags, uint32_t waitFlags)
{
    uint16_t wanted = (uint16_t)(waitFlags & UINT16_MAX);
    *gba_io_u16(IO_IME) = IME_ENABLED;
    if (discardOldFlags != 0) {
        *intr_check() = (uint16_t)(*intr_check() & ~wanted);
    }
    while (frame_hook != NULL && (*intr_check() & wanted) == 0) {
        frame_hook();
    }
    *intr_check() = (uint16_t)(*intr_check() & ~wanted);
}

void VBlankIntrWait(void)
{
    IntrWait(1, UINT32_C(1) << GBA_IRQ_VBLANK);
}

void Halt(void)
{
    if (frame_hook != NULL) {
        frame_hook();
    }
}

void Stop(void)
{
}

int MultiBoot(struct MultiBootParam *mp)
{
    (void)mp;
    return MULTIBOOT_FAILED;
}
