// deps: bios_internal.c transfer_bounds.c gba_memmap.c game_hooks.c debug_channel.c
/*
 * Tests of the BIOS control functions, from GBATEK "BIOS Halt Functions"
 * (IntrWait's discard argument and the BIOS Interrupt Flags at 3007FF8h,
 * VBlankIntrWait = IntrWait(1, V-Blank), IntrWait sets IME=1), "BIOS Reset
 * Functions" (RegisterRamReset's flag bits, "excluding last 200h bytes" of
 * IWRAM, DISPCNT=0080h always, RCNT to general-purpose mode; SoftReset clears
 * 3007E00h-3007FFFh), "GBA I/O Map" (which registers each flag covers), and
 * bios.h's native contract: the frame hook, the reset target.
 */
#include <gba/types.h>
#include <gba/defines.h>
#include <gba/io_reg.h>

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "bios.h"
#include "gba_memmap.h"
#include "ptest.h"
#include "transfer_bounds.h"

#define PATTERN 0xA5
#define PATTERN_16 0xA5A5
#define BIOS_AREA_SIZE 0x200 /* 3007E00h-3007FFFh */

static unsigned frames;
static unsigned raise_on_frame;
static u16 raise_flags;

/* Stands in for the frame loop: on the chosen frame the game's interrupt
 * handler ORs its flags into INTR_CHECK, as src/main.c's VBlankIntr does. */
static void fake_frame(void)
{
    frames++;
    if (frames >= raise_on_frame)
        INTR_CHECK |= raise_flags;
}

static void setup_with_hook(unsigned frame, u16 flags)
{
    frames = 0;
    raise_on_frame = frame;
    raise_flags = flags;
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    PT_ASSERT_EQ_INT(bios_init(fake_frame), BIOS_OK);
    INTR_CHECK = 0;
}

static void setup_without_hook(void)
{
    frames = 0;
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    PT_ASSERT_EQ_INT(transfer_bounds_init(), 0);
    PT_ASSERT_EQ_INT(bios_init(NULL), BIOS_OK);
    INTR_CHECK = 0;
}

static void teardown(void)
{
    bios_shutdown();
    transfer_bounds_shutdown();
}

static uint8_t *mem_at(uint32_t address)
{
    return (uint8_t *)(uintptr_t)address;
}

static volatile u16 *io16(uint32_t offset)
{
    return (volatile u16 *)(uintptr_t)(GBA_IO_BASE + offset);
}

/* Every byte of [address, address + size) equals value. */
static int all_bytes(uint32_t address, uint32_t size, uint8_t value)
{
    const uint8_t *bytes = mem_at(address);
    for (uint32_t i = 0; i < size; i++) {
        if (bytes[i] != value)
            return 0;
    }
    return 1;
}

static void fill_all_regions(void)
{
    memset(mem_at(GBA_EWRAM_BASE), PATTERN, GBA_EWRAM_SIZE);
    memset(mem_at(GBA_IWRAM_BASE), PATTERN, GBA_IWRAM_SIZE);
    memset(mem_at(GBA_PALETTE_RAM_BASE), PATTERN, GBA_PALETTE_RAM_SIZE);
    memset(mem_at(GBA_VRAM_BASE), PATTERN, GBA_VRAM_SIZE);
    memset(mem_at(GBA_OAM_BASE), PATTERN, GBA_OAM_SIZE);
    for (uint32_t offset = 0; offset < GBA_IO_SIZE; offset += 2)
        *io16(offset) = PATTERN_16;
}

static void clear_all_regions(void)
{
    memset(mem_at(GBA_EWRAM_BASE), 0, GBA_EWRAM_SIZE);
    memset(mem_at(GBA_IWRAM_BASE), 0, GBA_IWRAM_SIZE);
    memset(mem_at(GBA_PALETTE_RAM_BASE), 0, GBA_PALETTE_RAM_SIZE);
    memset(mem_at(GBA_VRAM_BASE), 0, GBA_VRAM_SIZE);
    memset(mem_at(GBA_OAM_BASE), 0, GBA_OAM_SIZE);
    memset(mem_at(GBA_IO_BASE), 0, GBA_IO_SIZE);
}

/* ---- Frame hook and the halt functions ---------------------------------- */

PTEST(vblankintrwait_without_hook_returns_at_once)
{
    setup_without_hook();
    INTR_CHECK = INTR_FLAG_VBLANK;
    VBlankIntrWait();
    PT_ASSERT_EQ_U32(INTR_CHECK & INTR_FLAG_VBLANK, 0); /* the old flag was discarded */
    teardown();
}

/* An old V-Blank flag is discarded (r0=1), one frame passes, the handler
 * sets the flag, and it is cleared again on return. */
PTEST(vblankintrwait_runs_one_frame_and_clears_flag)
{
    setup_with_hook(1, INTR_FLAG_VBLANK);
    INTR_CHECK = INTR_FLAG_VBLANK;
    VBlankIntrWait();
    PT_ASSERT_EQ_U32(frames, 1);
    PT_ASSERT_EQ_U32(INTR_CHECK, 0);
    teardown();
}

PTEST(vblankintrwait_waits_until_the_handler_sets_the_flag)
{
    setup_with_hook(3, INTR_FLAG_VBLANK | INTR_FLAG_TIMER2);
    VBlankIntrWait();
    PT_ASSERT_EQ_U32(frames, 3);
    PT_ASSERT_EQ_U32(INTR_CHECK, INTR_FLAG_TIMER2); /* only V-Blank is cleared */
    teardown();
}

PTEST(vblankintrwait_sets_ime)
{
    setup_with_hook(1, INTR_FLAG_VBLANK);
    REG_IME = 0;
    VBlankIntrWait();
    PT_ASSERT_EQ_U32(REG_IME, 1);
    REG_IME = 0;
    teardown();
}

/* r0=0: "Return immediately if an old flag was already set". */
PTEST(intrwait_keep_old_returns_at_once_on_an_old_flag)
{
    setup_with_hook(1, INTR_FLAG_TIMER3);
    INTR_CHECK = INTR_FLAG_TIMER3 | INTR_FLAG_HBLANK;
    IntrWait(0, INTR_FLAG_TIMER3);
    PT_ASSERT_EQ_U32(frames, 0);
    PT_ASSERT_EQ_U32(INTR_CHECK, INTR_FLAG_HBLANK);
    teardown();
}

/* r0=1: "Discard old flags, wait until a NEW flag becomes set". */
PTEST(intrwait_discard_waits_for_a_new_flag)
{
    setup_with_hook(2, INTR_FLAG_TIMER3);
    INTR_CHECK = INTR_FLAG_TIMER3;
    IntrWait(1, INTR_FLAG_TIMER3);
    PT_ASSERT_EQ_U32(frames, 2);
    PT_ASSERT_EQ_U32(INTR_CHECK, 0);
    teardown();
}

/* Any one of several flags ends the wait; all of them are cleared. */
PTEST(intrwait_any_of_several_flags)
{
    setup_with_hook(1, INTR_FLAG_TIMER2);
    IntrWait(0, INTR_FLAG_TIMER2 | INTR_FLAG_VBLANK);
    PT_ASSERT_EQ_U32(frames, 1);
    PT_ASSERT_EQ_U32(INTR_CHECK, 0);
    teardown();
}

PTEST(intrwait_without_hook_returns_at_once)
{
    setup_without_hook();
    REG_IME = 0;
    IntrWait(0, INTR_FLAG_TIMER2);
    PT_ASSERT_EQ_U32(REG_IME, 1);
    REG_IME = 0;
    teardown();
}

PTEST(halt_lets_one_frame_pass)
{
    setup_with_hook(1, INTR_FLAG_VBLANK);
    Halt();
    PT_ASSERT_EQ_U32(frames, 1);
    teardown();
    setup_without_hook();
    Halt();
    PT_ASSERT_EQ_U32(frames, 0);
    teardown();
}

PTEST(stop_returns_without_a_frame)
{
    setup_with_hook(1, INTR_FLAG_VBLANK);
    Stop();
    PT_ASSERT_EQ_U32(frames, 0);
    teardown();
}

PTEST(shutdown_forgets_the_hook)
{
    setup_with_hook(1, INTR_FLAG_VBLANK);
    bios_shutdown();
    VBlankIntrWait();
    PT_ASSERT_EQ_U32(frames, 0);
    bios_shutdown(); /* twice is safe */
    teardown();
}

/* ---- Soft reset ----------------------------------------------------------- */

static jmp_buf reset_target;

PTEST(soft_reset_without_target_reports_and_changes_nothing)
{
    setup_without_hook();
    fill_all_regions();
    PT_ASSERT_EQ_INT(bios_soft_reset(BIOS_RESET_EWRAM), BIOS_ERR_NO_RESET_TARGET);
    SoftReset(BIOS_RESET_EWRAM); /* prints, returns */
    int ewram_kept = all_bytes(GBA_EWRAM_BASE, GBA_EWRAM_SIZE, PATTERN);
    int bios_area_kept = all_bytes(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - BIOS_AREA_SIZE, BIOS_AREA_SIZE, PATTERN);
    clear_all_regions();
    PT_ASSERT(ewram_kept);
    PT_ASSERT(bios_area_kept);
    teardown();
}

/* RegisterRamReset(flags), then SWI 00h clears 3007E00h-3007FFFh (stacks,
 * INTR_CHECK, INTR_VECTOR) and restarts: the longjmp. */
PTEST(soft_reset_clears_and_longjmps)
{
    setup_without_hook();
    fill_all_regions();
    REG_IME = 1;
    bios_set_reset_target(&reset_target);
    volatile int returned = 0;
    volatile int jumped = 0;
    if (setjmp(reset_target) == 0) {
        SoftReset(BIOS_RESET_EWRAM | BIOS_RESET_OAM);
        returned = 1;
    } else {
        jumped = 1;
    }
    int ewram_cleared = all_bytes(GBA_EWRAM_BASE, GBA_EWRAM_SIZE, 0);
    int oam_cleared = all_bytes(GBA_OAM_BASE, GBA_OAM_SIZE, 0);
    int bios_area_cleared = all_bytes(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - BIOS_AREA_SIZE, BIOS_AREA_SIZE, 0);
    int iwram_kept = all_bytes(GBA_IWRAM_BASE, GBA_IWRAM_SIZE - BIOS_AREA_SIZE, PATTERN);
    int vram_kept = all_bytes(GBA_VRAM_BASE, GBA_VRAM_SIZE, PATTERN);
    u16 ime = REG_IME;
    clear_all_regions();
    PT_ASSERT(jumped);
    PT_ASSERT(!returned);
    PT_ASSERT(ewram_cleared);
    PT_ASSERT(oam_cleared);
    PT_ASSERT(bios_area_cleared);
    PT_ASSERT(iwram_kept);
    PT_ASSERT(vram_kept);
    PT_ASSERT_EQ_U32(ime, 0);
    teardown();
}

/* SoftResetExram keeps EWRAM whatever the flags say; SoftResetRom does not. */
PTEST(soft_reset_rom_and_exram)
{
    setup_without_hook();
    fill_all_regions();
    bios_set_reset_target(&reset_target);
    volatile int phase = 0;
    if (setjmp(reset_target) == 0) {
        phase = 1;
        SoftResetExram(BIOS_RESET_EWRAM | BIOS_RESET_PALETTE);
    }
    int exram_kept = all_bytes(GBA_EWRAM_BASE, GBA_EWRAM_SIZE, PATTERN);
    int palette_cleared = all_bytes(GBA_PALETTE_RAM_BASE, GBA_PALETTE_RAM_SIZE, 0);
    if (setjmp(reset_target) == 0) {
        phase = 2;
        SoftResetRom(BIOS_RESET_EWRAM);
    }
    int rom_cleared_ewram = all_bytes(GBA_EWRAM_BASE, GBA_EWRAM_SIZE, 0);
    clear_all_regions();
    PT_ASSERT_EQ_INT(phase, 2);
    PT_ASSERT(exram_kept);
    PT_ASSERT(palette_cleared);
    PT_ASSERT(rom_cleared_ewram);
    teardown();
}

PTEST(reset_target_forgotten_by_null_and_shutdown)
{
    setup_without_hook();
    bios_set_reset_target(&reset_target);
    bios_set_reset_target(NULL);
    PT_ASSERT_EQ_INT(bios_soft_reset(0), BIOS_ERR_NO_RESET_TARGET);
    bios_set_reset_target(&reset_target);
    bios_shutdown();
    PT_ASSERT_EQ_INT(bios_soft_reset(0), BIOS_ERR_NO_RESET_TARGET);
    bios_set_reset_target(&reset_target);
    PT_ASSERT_EQ_INT(bios_init(NULL), BIOS_OK);
    PT_ASSERT_EQ_INT(bios_soft_reset(0), BIOS_ERR_NO_RESET_TARGET);
    teardown();
}

/* ---- RegisterRamReset: each flag clears only its own region -------------- */

struct region_state {
    int ewram, iwram, iwram_tail, palette, vram, oam;
};

static struct region_state cleared_after(uint32_t flags)
{
    fill_all_regions();
    RegisterRamReset(flags);
    struct region_state s = {
        all_bytes(GBA_EWRAM_BASE, GBA_EWRAM_SIZE, 0),
        all_bytes(GBA_IWRAM_BASE, GBA_IWRAM_SIZE - BIOS_AREA_SIZE, 0),
        all_bytes(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - BIOS_AREA_SIZE, BIOS_AREA_SIZE, 0),
        all_bytes(GBA_PALETTE_RAM_BASE, GBA_PALETTE_RAM_SIZE, 0),
        all_bytes(GBA_VRAM_BASE, GBA_VRAM_SIZE, 0),
        all_bytes(GBA_OAM_BASE, GBA_OAM_SIZE, 0),
    };
    return s;
}

static int untouched(uint32_t address, uint32_t size)
{
    return all_bytes(address, size, PATTERN);
}

PTEST(register_ram_reset_each_memory_flag)
{
    setup_without_hook();
    struct region_state s = cleared_after(BIOS_RESET_EWRAM);
    int others = untouched(GBA_IWRAM_BASE, GBA_IWRAM_SIZE) && untouched(GBA_VRAM_BASE, GBA_VRAM_SIZE);
    PT_ASSERT(s.ewram && !s.iwram && !s.palette && !s.vram && !s.oam && others);

    s = cleared_after(BIOS_RESET_IWRAM);
    int tail_kept = untouched(GBA_IWRAM_BASE + GBA_IWRAM_SIZE - BIOS_AREA_SIZE, BIOS_AREA_SIZE);
    PT_ASSERT(s.iwram && !s.iwram_tail && tail_kept && !s.ewram && !s.palette && !s.vram && !s.oam);

    s = cleared_after(BIOS_RESET_PALETTE);
    PT_ASSERT(s.palette && !s.ewram && !s.iwram && !s.vram && !s.oam);

    s = cleared_after(BIOS_RESET_VRAM);
    PT_ASSERT(s.vram && !s.ewram && !s.iwram && !s.palette && !s.oam);

    s = cleared_after(BIOS_RESET_OAM);
    PT_ASSERT(s.oam && !s.ewram && !s.iwram && !s.palette && !s.vram);

    s = cleared_after(0xFF);
    PT_ASSERT(s.ewram && s.iwram && !s.iwram_tail && s.palette && s.vram && s.oam);
    clear_all_regions();
    teardown();
}

/* DISPCNT becomes 0080h (forced blank) whatever the flags. */
PTEST(register_ram_reset_always_forces_blank)
{
    setup_without_hook();
    struct region_state s = cleared_after(0);
    int io_rest = REG_BG0CNT == PATTERN_16 && REG_SOUNDCNT_X == PATTERN_16 && REG_SIOCNT == PATTERN_16;
    u16 dispcnt = REG_DISPCNT;
    clear_all_regions();
    PT_ASSERT(!s.ewram && !s.iwram && !s.palette && !s.vram && !s.oam);
    PT_ASSERT(io_rest);
    PT_ASSERT_EQ_U32(dispcnt, 0x0080);
    teardown();
}

/* Bit 7: LCD, DMA, timer, KEYCNT, IE/IF/WAITCNT/IME registers; not the
 * read-only VCOUNT, KEYINPUT, DISPSTAT status bits, nor SIO, sound,
 * POSTFLG/HALTCNT. */
PTEST(register_ram_reset_other_registers)
{
    setup_without_hook();
    fill_all_regions();
    RegisterRamReset(BIOS_RESET_REGS);
    u16 dispcnt = REG_DISPCNT, dispstat = REG_DISPSTAT, vcount = REG_VCOUNT;
    u16 bg0cnt = REG_BG0CNT, bldy = REG_BLDY, tm3cnt_h = REG_TM3CNT_H, keycnt = REG_KEYCNT;
    u32 dma0sad = REG_DMA0SAD, dma3cnt = REG_DMA3CNT;
    u16 ie = REG_IE, intr_if = REG_IF, waitcnt = REG_WAITCNT, ime = REG_IME;
    u16 keyinput = REG_KEYINPUT, siocnt = REG_SIOCNT, soundcnt_x = REG_SOUNDCNT_X;
    u16 postflg_haltcnt = *io16(0x300);
    clear_all_regions();
    PT_ASSERT_EQ_U32(dispcnt, 0x0080);
    PT_ASSERT_EQ_U32(dispstat, PATTERN_16 & 0x0007); /* bits 0-2 are read-only */
    PT_ASSERT_EQ_U32(vcount, PATTERN_16);
    PT_ASSERT_EQ_U32(bg0cnt, 0);
    PT_ASSERT_EQ_U32(bldy, 0);
    PT_ASSERT_EQ_U32(dma0sad, 0);
    PT_ASSERT_EQ_U32(dma3cnt, 0);
    PT_ASSERT_EQ_U32(tm3cnt_h, 0);
    PT_ASSERT_EQ_U32(keycnt, 0);
    PT_ASSERT_EQ_U32(ie, 0);
    PT_ASSERT_EQ_U32(intr_if, 0);
    PT_ASSERT_EQ_U32(waitcnt, 0);
    PT_ASSERT_EQ_U32(ime, 0);
    PT_ASSERT_EQ_U32(keyinput, PATTERN_16);
    PT_ASSERT_EQ_U32(siocnt, PATTERN_16);
    PT_ASSERT_EQ_U32(soundcnt_x, PATTERN_16);
    PT_ASSERT_EQ_U32(postflg_haltcnt, PATTERN_16);
    teardown();
}

/* Bit 6: 4000060h-40000A7h except SOUNDBIAS. */
PTEST(register_ram_reset_sound_registers)
{
    setup_without_hook();
    fill_all_regions();
    RegisterRamReset(BIOS_RESET_SOUND_REGS);
    u16 sound1cnt_l = REG_SOUND1CNT_L, soundcnt_x = REG_SOUNDCNT_X, soundbias = REG_SOUNDBIAS;
    u32 wave_ram0 = REG_WAVE_RAM0, fifo_b = REG_FIFO_B;
    u16 after_fifo = *io16(0xA8), bg0cnt = REG_BG0CNT, siocnt = REG_SIOCNT;
    clear_all_regions();
    PT_ASSERT_EQ_U32(sound1cnt_l, 0);
    PT_ASSERT_EQ_U32(soundcnt_x, 0);
    PT_ASSERT_EQ_U32(wave_ram0, 0);
    PT_ASSERT_EQ_U32(fifo_b, 0);
    PT_ASSERT_EQ_U32(soundbias, PATTERN_16);
    PT_ASSERT_EQ_U32(after_fifo, PATTERN_16);
    PT_ASSERT_EQ_U32(bg0cnt, PATTERN_16);
    PT_ASSERT_EQ_U32(siocnt, PATTERN_16);
    teardown();
}

/* Bit 5: SIO registers zeroed, RCNT 8000h ("switches to general purpose
 * mode!"). */
PTEST(register_ram_reset_sio_registers)
{
    setup_without_hook();
    fill_all_regions();
    RegisterRamReset(BIOS_RESET_SIO_REGS);
    u32 siodata32 = REG_SIODATA32;
    u16 siocnt = REG_SIOCNT, send = REG_SIOMLT_SEND, rcnt = REG_RCNT;
    u16 joycnt = *io16(0x140), joy_trans = *io16(0x154), joystat = *io16(0x158);
    u16 keyinput = REG_KEYINPUT, keycnt = REG_KEYCNT, soundbias = REG_SOUNDBIAS;
    clear_all_regions();
    PT_ASSERT_EQ_U32(siodata32, 0);
    PT_ASSERT_EQ_U32(siocnt, 0);
    PT_ASSERT_EQ_U32(send, 0);
    PT_ASSERT_EQ_U32(rcnt, 0x8000);
    PT_ASSERT_EQ_U32(joycnt, 0);
    PT_ASSERT_EQ_U32(joy_trans, 0);
    PT_ASSERT_EQ_U32(joystat, 0);
    PT_ASSERT_EQ_U32(keyinput, PATTERN_16);
    PT_ASSERT_EQ_U32(keycnt, PATTERN_16);
    PT_ASSERT_EQ_U32(soundbias, PATTERN_16);
    teardown();
}

PTEST(multiboot_fails_without_a_link_port)
{
    setup_without_hook();
    PT_ASSERT_EQ_INT(MultiBoot(NULL), 1);
    teardown();
}

PTEST_MAIN()
