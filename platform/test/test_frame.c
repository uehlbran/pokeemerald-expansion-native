/*
 * test_frame.c: one frame of GBA time (frame.h) and the LCD's timing
 * registers (lcd_status.h).
 *
 * Expected behaviour from GBATEK "LCD I/O Interrupts and Status" (DISPSTAT
 * and VCOUNT, which lines set which flag, when each interrupt is requested),
 * "LCD Dimensions and Timings" (160 drawn lines, 280896 cycles a frame),
 * "GBA Timers" and "GBA DMA Transfers" (H-Blank DMA in drawn lines only),
 * and from expansion's src/gpu_regs.c for the line the game runs on (161).
 *
 * The frame runs with the real modules, headless and silent: handlers
 * installed in a table laid out like expansion's gIntrTable record what
 * VCOUNT and DISPSTAT read when the interrupt controller calls them.
 */
// deps: lcd_status.c gba_memmap.c interrupt.c timer.c dma.c transfer_bounds.c ppu.c ppu_bg.c ppu_obj.c ppu_window.c ppu_blend.c psg.c psg_channel.c psg_square.c psg_wave.c psg_noise.c audio.c video.c input.c flash.c save_file.c frame_pacer.c game_hooks.c debug_channel.c m4a_output.c sound_mix.c
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gba/types.h>
#include <gba/flash_internal.h>
#include <gba/m4a_internal.h>

#include "audio.h"
#include "dma.h"
#include "flash.h"
#include "frame.h"
#include "frame_pacer.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "input.h"
#include "interrupt.h"
#include "lcd_status.h"
#include "ppu.h"
#include "psg.h"
#include "ptest.h"
#include "save_file.h"
#include "timer.h"
#include "transfer_bounds.h"
#include "video.h"

/* GBATEK register offsets from GBA_IO_BASE. */
#define KEYINPUT_OFFSET UINT32_C(0x130)
#define TM0CNT_L_OFFSET UINT32_C(0x100)
#define TM0CNT_H_OFFSET UINT32_C(0x102)
#define TM2CNT_L_OFFSET UINT32_C(0x108)
#define TM2CNT_H_OFFSET UINT32_C(0x10A)

/* IE/IF bits (GBATEK "GBA Interrupt Control"). */
#define IE_VBLANK UINT16_C(0x0001)
#define IE_HBLANK UINT16_C(0x0002)
#define IE_VCOUNT UINT16_C(0x0004)
#define IE_TIMER0 UINT16_C(0x0008)
#define IE_TIMER2 UINT16_C(0x0020)

/* TMxCNT_H: bit 6 IRQ enable, bit 7 start (GBATEK "GBA Timers"). */
#define TIMER_START_WITH_IRQ UINT16_C(0x00C0)

/* DMAxCNT_H (GBATEK "GBA DMA Transfers"): bit 15 enable, bits 12-13 timing
 * (2 = H-Blank), bit 9 repeat, bits 5-6 destination control (2 = fixed). */
#define DMA_CNT_ENABLE UINT32_C(0x8000)
#define DMA_CNT_REPEAT UINT32_C(0x0200)
#define DMA_CNT_DEST_FIXED UINT32_C(0x0040)

/* KEYINPUT with no key held (GBATEK "GBA Keypad Input"). */
#define KEYINPUT_NONE UINT16_C(0x03FF)

/* The test's own LYC for most tests: a drawn line. */
#define TEST_LYC 100u

static volatile uint16_t *io(uint32_t offset)
{
    return (volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

enum event_kind { EVENT_VCOUNT, EVENT_HBLANK, EVENT_VBLANK, EVENT_TIMER0, EVENT_TIMER2 };

struct event {
    enum event_kind kind;
    uint16_t vcount;
    uint16_t dispstat;
};

#define MAX_EVENTS 1024u
static struct event events[MAX_EVENTS];
static size_t event_count;

static void record(enum event_kind kind)
{
    if (event_count < MAX_EVENTS)
        events[event_count++] = (struct event){ kind, *io(LCD_VCOUNT_OFFSET), *io(LCD_DISPSTAT_OFFSET) };
}

static void on_vcount(void) { record(EVENT_VCOUNT); }
static void on_hblank(void) { record(EVENT_HBLANK); }
static void on_vblank(void) { record(EVENT_VBLANK); }
static void on_timer0(void) { record(EVENT_TIMER0); }
static void on_nothing(void) {}

/* Set by a test: Timer 2's handler times the test out as the test runner's
 * Intr_Timer2 does natively, by jumping to AgbMainLoop. */
static bool timer2_times_out;

static void on_timer2(void)
{
    record(EVENT_TIMER2);
    if (timer2_times_out)
        PlatformJumpToAgbMainLoop();
}

/* src/main.c gIntrTableTemplate's order (interrupt.h). */
static interrupt_handler const table[INTERRUPT_TABLE_LENGTH] = {
    on_vcount, on_nothing, on_nothing, on_hblank, on_vblank, on_timer0, on_nothing,
    on_timer2, on_nothing, on_nothing, on_nothing, on_nothing, on_nothing, on_nothing,
};

static int count_events(enum event_kind kind)
{
    int n = 0;
    for (size_t i = 0; i < event_count; i++)
        n += events[i].kind == kind;
    return n;
}

static const struct event *first_event(enum event_kind kind)
{
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind == kind)
            return &events[i];
    }
    return NULL;
}

static size_t index_of(const struct event *event)
{
    return (size_t)(event - events);
}

/* Every module frame_run uses, headless, silent, scripted input, no save
 * file; IME on, the three LCD interrupts enabled in IE and DISPSTAT. */
static bool start(uint32_t frame_limit, const char *screenshot_path, const char *save_path)
{
    struct video_config video = { .headless = true, .scale = 1, .title = "test_frame" };
    struct frame_config config = { .frame_limit = frame_limit, .screenshot_path = screenshot_path };
    memset((void *)(uintptr_t)GBA_IO_BASE, 0, GBA_IO_SIZE);
    event_count = 0;
    if (gba_memmap_init() != GBA_MEMMAP_OK || transfer_bounds_init() != 0
        || interrupt_init(table, INTERRUPT_TABLE_LENGTH) != INTERRUPT_OK || timer_init(interrupt_raise) != TIMER_OK
        || dma_init(interrupt_raise) != DMA_OK || ppu_init() != PPU_OK || psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ) != PSG_OK
        || audio_init(FRAME_AUDIO_SAMPLE_RATE_HZ, true) != 0 || video_init(&video) != 0
        || input_init(INPUT_SOURCE_SCRIPT) != 0 || save_file_init(save_path) != SAVE_FILE_OK
        || frame_pacer_init(true) != 0 || frame_init(&config) != 0)
        return false;
    flash_init();
    *io(GBA_IME_OFFSET) = GBA_IME_ENABLE;
    *io(GBA_IE_OFFSET) = IE_VBLANK | IE_HBLANK | IE_VCOUNT;
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_VBLANK_IRQ | LCD_DISPSTAT_HBLANK_IRQ
                                          | LCD_DISPSTAT_VCOUNTER_IRQ | (TEST_LYC << LCD_DISPSTAT_LYC_SHIFT));
    return true;
}

static void stop(void)
{
    frame_shutdown();
    frame_pacer_shutdown();
    save_file_shutdown();
    flash_shutdown();
    input_shutdown();
    video_shutdown();
    audio_shutdown();
    psg_shutdown();
    ppu_shutdown();
    dma_shutdown();
    timer_shutdown();
    interrupt_shutdown();
    transfer_bounds_shutdown();
    gba_memmap_shutdown();
}

static void set_lyc(uint16_t line)
{
    uint16_t dispstat = *io(LCD_DISPSTAT_OFFSET);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)((dispstat & 0x00FFu) | ((uint32_t)line << LCD_DISPSTAT_LYC_SHIFT));
}

/* --- lcd_status ---------------------------------------------------------- */

PTEST(begin_line_sets_vcount_and_the_flags_of_that_line)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(5u << LCD_DISPSTAT_LYC_SHIFT);
    PT_ASSERT_EQ_U32(lcd_status_begin_line(5), 0); /* match, but its IRQ is not enabled */
    PT_ASSERT_EQ_INT(*io(LCD_VCOUNT_OFFSET), 5);
    PT_ASSERT_EQ_INT(lcd_status_line(), 5);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, LCD_DISPSTAT_VCOUNTER_FLAG);
    (void)lcd_status_begin_line(159);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, 0);
    (void)lcd_status_begin_line(160);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, LCD_DISPSTAT_VBLANK_FLAG);
    (void)lcd_status_begin_line(226);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, LCD_DISPSTAT_VBLANK_FLAG);
    (void)lcd_status_begin_line(227); /* GBATEK: the V-Blank flag is not set in line 227 */
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, 0);
    PT_ASSERT_EQ_U32((uint32_t)(*io(LCD_DISPSTAT_OFFSET) >> LCD_DISPSTAT_LYC_SHIFT), 5); /* the game's bits stay */
}

PTEST(begin_line_requests_only_enabled_interrupts)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_VBLANK_IRQ | (160u << LCD_DISPSTAT_LYC_SHIFT));
    PT_ASSERT_EQ_U32(lcd_status_begin_line(159), 0);
    PT_ASSERT_EQ_U32(lcd_status_begin_line(160), 1u << GBA_IRQ_VBLANK);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_VBLANK_IRQ | LCD_DISPSTAT_VCOUNTER_IRQ
                                          | (160u << LCD_DISPSTAT_LYC_SHIFT));
    PT_ASSERT_EQ_U32(lcd_status_begin_line(160), (1u << GBA_IRQ_VBLANK) | (1u << GBA_IRQ_VCOUNT));
    PT_ASSERT_EQ_U32(lcd_status_begin_line(161), 0); /* V-Blank is requested at its start only */
    *io(LCD_DISPSTAT_OFFSET) = 0;
    PT_ASSERT_EQ_U32(lcd_status_begin_line(160), 0);
}

PTEST(begin_line_ignores_lines_past_227)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_VCOUNTER_IRQ | (228u << LCD_DISPSTAT_LYC_SHIFT));
    (void)lcd_status_begin_line(7);
    PT_ASSERT_EQ_U32(lcd_status_begin_line(LCD_LINE_COUNT), 0);
    PT_ASSERT_EQ_U32(lcd_status_begin_line(UINT16_MAX), 0);
    PT_ASSERT_EQ_INT(*io(LCD_VCOUNT_OFFSET), 7);
}

PTEST(begin_hblank_sets_the_flag_and_requests_if_enabled)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    *io(LCD_DISPSTAT_OFFSET) = 0;
    (void)lcd_status_begin_line(3);
    PT_ASSERT_EQ_U32(lcd_status_begin_hblank(), 0);
    PT_ASSERT(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_HBLANK_FLAG);
    *io(LCD_DISPSTAT_OFFSET) = LCD_DISPSTAT_HBLANK_IRQ;
    (void)lcd_status_begin_line(4);
    PT_ASSERT(!(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_HBLANK_FLAG)); /* cleared at the line's start */
    PT_ASSERT_EQ_U32(lcd_status_begin_hblank(), 1u << GBA_IRQ_HBLANK);
}

PTEST(the_games_stores_to_the_status_bits_are_overwritten)
{
    PT_ASSERT_EQ_INT(gba_memmap_init(), GBA_MEMMAP_OK);
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_STATUS_MASK | (9u << LCD_DISPSTAT_LYC_SHIFT));
    (void)lcd_status_begin_line(8);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET), 9u << LCD_DISPSTAT_LYC_SHIFT);
}

/* --- frame_run: the lines ------------------------------------------------ */

/* DISPCNT (GBATEK "LCD I/O Display Control"), and its bit 7, forced blank,
 * which the PPU draws white. */
#define FRAME_TEST_DISPCNT_OFFSET UINT32_C(0x000)
#define FRAME_TEST_FORCED_BLANK UINT16_C(0x0080)

PTEST(skip_drawing_leaves_the_frame_buffer_and_keeps_the_lines_events)
{
    /* A drawn frame in forced blank is white; then, not drawing, a frame that
     * would be drawn black (backdrop colour 0) leaves the white alone. */
    PT_ASSERT(start(0, NULL, NULL));
    *io(FRAME_TEST_DISPCNT_OFFSET) = FRAME_TEST_FORCED_BLANK;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool drawn_white = frame_pixels()[0] == 0x7FFF && frame_pixels()[VIDEO_PIXEL_COUNT - 1] == 0x7FFF;
    struct frame_config undrawn = { .skip_drawing = true };
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_init(&undrawn), 0);
    *io(FRAME_TEST_DISPCNT_OFFSET) = 0;
    event_count = 0;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    int hblanks = count_events(EVENT_HBLANK);
    int vblanks = count_events(EVENT_VBLANK);
    bool still_white = true;
    for (size_t i = 0; i < VIDEO_PIXEL_COUNT; i++)
        still_white = still_white && frame_pixels()[i] == 0x7FFF;
    uint32_t frames = frame_count();
    stop();
    PT_ASSERT(drawn_white);
    PT_ASSERT(still_white);
    PT_ASSERT_EQ_INT(hblanks, LCD_VISIBLE_LINES);
    PT_ASSERT_EQ_INT(vblanks, 1);
    PT_ASSERT_EQ_U32(frames, 1);
}

/* BG palette entry 0, the backdrop (GBATEK "LCD Color Palettes"); a
 * colour that is neither forced-blank white nor black. */
#define FRAME_TEST_BACKDROP_RED UINT16_C(0x001F)

PTEST(a_frame_drawn_after_undrawn_frames_shows_the_current_image)
{
    /* Drawn: forced blank, white. Then three undrawn frames while the game
     * turns forced blank off and sets the backdrop red; then drawing again:
     * the frame is red, not the stale white or an undrawn black. */
    PT_ASSERT(start(0, NULL, NULL));
    *io(FRAME_TEST_DISPCNT_OFFSET) = FRAME_TEST_FORCED_BLANK;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool drawn_white = frame_pixels()[0] == 0x7FFF;
    struct frame_config undrawn = { .skip_drawing = true };
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_init(&undrawn), 0);
    *io(FRAME_TEST_DISPCNT_OFFSET) = 0;
    *(volatile uint16_t *)(uintptr_t)GBA_PALETTE_RAM_BASE = FRAME_TEST_BACKDROP_RED;
    for (int i = 0; i < 3; i++)
        PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool still_white = frame_pixels()[0] == 0x7FFF;
    struct frame_config drawn = { .skip_drawing = false };
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_init(&drawn), 0);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool all_red = true;
    for (size_t i = 0; i < VIDEO_PIXEL_COUNT; i++)
        all_red = all_red && frame_pixels()[i] == FRAME_TEST_BACKDROP_RED;
    stop();
    PT_ASSERT(drawn_white);
    PT_ASSERT(still_white);
    PT_ASSERT(all_red);
}

PTEST(hblank_follows_each_drawn_line)
{
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(count_events(EVENT_HBLANK), LCD_VISIBLE_LINES);
    uint16_t expected_line = 0;
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind != EVENT_HBLANK)
            continue;
        PT_ASSERT_EQ_INT(events[i].vcount, expected_line);
        PT_ASSERT(events[i].dispstat & LCD_DISPSTAT_HBLANK_FLAG);
        PT_ASSERT(!(events[i].dispstat & LCD_DISPSTAT_VBLANK_FLAG));
        expected_line++;
    }
    stop();
}

PTEST(vcount_match_is_raised_at_the_start_of_its_line)
{
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(count_events(EVENT_VCOUNT), 1);
    const struct event *match = first_event(EVENT_VCOUNT);
    PT_ASSERT_EQ_INT(match->vcount, TEST_LYC);
    PT_ASSERT(match->dispstat & LCD_DISPSTAT_VCOUNTER_FLAG);
    PT_ASSERT(!(match->dispstat & LCD_DISPSTAT_HBLANK_FLAG));
    /* Before that line's H-Blank, after the previous one's. */
    PT_ASSERT(index_of(match) > 0);
    PT_ASSERT_EQ_INT(events[index_of(match) - 1].vcount, TEST_LYC - 1);
    PT_ASSERT_EQ_INT(events[index_of(match) + 1].kind, EVENT_HBLANK);
    PT_ASSERT_EQ_INT(events[index_of(match) + 1].vcount, TEST_LYC);
    /* The flag is set only during the matching line. */
    for (size_t i = 0; i < event_count; i++) {
        if (events[i].kind == EVENT_HBLANK && events[i].vcount != TEST_LYC)
            PT_ASSERT(!(events[i].dispstat & LCD_DISPSTAT_VCOUNTER_FLAG));
    }
    stop();
}

PTEST(vblank_is_raised_once_at_line_160_after_the_drawn_lines)
{
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(count_events(EVENT_VBLANK), 1);
    const struct event *vblank = first_event(EVENT_VBLANK);
    PT_ASSERT_EQ_INT(vblank->vcount, LCD_LINE_VBLANK_START);
    PT_ASSERT(vblank->dispstat & LCD_DISPSTAT_VBLANK_FLAG);
    PT_ASSERT(!(vblank->dispstat & LCD_DISPSTAT_HBLANK_FLAG));
    PT_ASSERT(index_of(vblank) == event_count - 1);
    stop();
}

PTEST(the_game_runs_on_line_161_in_vblank)
{
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(*io(LCD_VCOUNT_OFFSET), LCD_LINE_GAME_LOGIC);
    PT_ASSERT_EQ_U32(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_STATUS_MASK, LCD_DISPSTAT_VBLANK_FLAG);
    stop();
}

PTEST(a_vcount_setting_of_161_matches_after_vblank)
{
    PT_ASSERT(start(0, NULL, NULL));
    set_lyc(LCD_LINE_GAME_LOGIC);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(count_events(EVENT_VCOUNT), 1);
    const struct event *match = first_event(EVENT_VCOUNT);
    PT_ASSERT_EQ_INT(match->vcount, LCD_LINE_GAME_LOGIC);
    PT_ASSERT(index_of(match) > index_of(first_event(EVENT_VBLANK)));
    stop();
}

PTEST(vcount_settings_of_unsimulated_lines_never_match)
{
    PT_ASSERT(start(0, NULL, NULL));
    set_lyc(200); /* frame.h: lines 162..227 are not run one by one */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(count_events(EVENT_VCOUNT), 0);
    stop();
}

PTEST(disabled_lcd_interrupts_are_not_raised)
{
    PT_ASSERT(start(0, NULL, NULL));
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(TEST_LYC << LCD_DISPSTAT_LYC_SHIFT);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(event_count == 0);
    stop();
}

PTEST(ime_off_defers_the_handlers)
{
    PT_ASSERT(start(0, NULL, NULL));
    *io(GBA_IME_OFFSET) = 0;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(event_count == 0);
    stop();
}

/* --- frame_run: timers and DMA ------------------------------------------- */

PTEST(timers_advance_one_frame_of_cycles)
{
    PT_ASSERT(start(0, NULL, NULL));
    *io(GBA_IE_OFFSET) = IE_TIMER0;
    *io(TM0CNT_L_OFFSET) = 0;
    *io(TM0CNT_H_OFFSET) = TIMER_START_WITH_IRQ; /* F/1 */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    /* 280896 cycles from 0: four overflows and 18752 (4940h) left. */
    PT_ASSERT_EQ_U32(*io(TM0CNT_L_OFFSET), 280896u % 0x10000u);
    PT_ASSERT_EQ_INT(count_events(EVENT_TIMER0), 1);
    stop();
}

/* VBlankIntrWait waiting a frame on the battle runner's stack. */
static void run_frame_on_the_battle_stack(void *context)
{
    (void)context;
    (void)frame_run();
}

static jmp_buf exit_target;

PTEST(a_timeout_jump_from_the_timer_handler_leaves_the_frame)
{
    /* Timer 2 overflows within the frame (step 2, after the drawn lines); its
     * handler jumps to AgbMainLoop, once from the game's stack and once from
     * the battle runner's. The rest of each frame is not run; the next
     * frame_run is a whole frame. */
    volatile int arrivals = 0;
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT_EQ_INT(game_hooks_init(&exit_target), 0);
    *io(GBA_IE_OFFSET) = IE_VBLANK | IE_TIMER2;
    *io(TM2CNT_L_OFFSET) = 0;
    *io(TM2CNT_H_OFFSET) = TIMER_START_WITH_IRQ; /* F/1: overflows four times a frame */
    timer2_times_out = true;
    setjmp(*PlatformAgbMainLoopTarget());
    arrivals++;
    if (arrivals == 1)
        (void)frame_run();
    else if (arrivals == 2)
        PlatformCallWithStack(run_frame_on_the_battle_stack, NULL, NULL);
    int vblanks_in_abandoned_frames = count_events(EVENT_VBLANK);
    int timeouts = count_events(EVENT_TIMER2);
    uint32_t frames_before = frame_count();
    uint16_t ime = *io(GBA_IME_OFFSET);
    timer2_times_out = false;
    event_count = 0;
    enum frame_result result = frame_run();
    int vblanks = count_events(EVENT_VBLANK);
    uint16_t vcount = *io(LCD_VCOUNT_OFFSET);
    game_hooks_shutdown();
    stop();
    PT_ASSERT_EQ_INT(arrivals, 3);
    PT_ASSERT_EQ_INT(timeouts, 2);
    PT_ASSERT_EQ_INT(vblanks_in_abandoned_frames, 0);
    PT_ASSERT_EQ_U32(frames_before, 0);
    PT_ASSERT_EQ_U32(ime, GBA_IME_ENABLE);
    PT_ASSERT_EQ_INT(result, FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(vblanks, 1);
    PT_ASSERT_EQ_U32(vcount, LCD_LINE_GAME_LOGIC);
    PT_ASSERT_EQ_U32(frame_count(), 1);
}

static volatile uint16_t hblank_destination;
static uint16_t hblank_source[LCD_VISIBLE_LINES + 1];

PTEST(hblank_dma_runs_once_per_drawn_line)
{
    PT_ASSERT(start(0, NULL, NULL));
    for (uint16_t i = 0; i <= LCD_VISIBLE_LINES; i++)
        hblank_source[i] = (uint16_t)(0x2000u + i);
    /* One halfword per H-Blank, source incrementing, destination fixed. */
    uint32_t control = ((DMA_CNT_ENABLE | (uint32_t)DMA_TIMING_HBLANK | DMA_CNT_REPEAT | DMA_CNT_DEST_FIXED) << 16) | 1u;
    PT_ASSERT_EQ_INT(dma_set(0, hblank_source, (void *)&hblank_destination, control), DMA_OK);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(hblank_destination, 0x2000 + LCD_VISIBLE_LINES - 1);
    dma_stop(0);
    stop();
}

/* --- frame_run: the loop ------------------------------------------------- */

PTEST(frame_limit_ends_the_loop_and_counts_frames)
{
    PT_ASSERT(start(3, NULL, NULL));
    PT_ASSERT_EQ_INT(frame_count(), 0);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(frame_count(), 3);
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    size_t events_after_three = event_count;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT); /* nothing more runs */
    PT_ASSERT_EQ_INT(frame_count(), 3);
    PT_ASSERT(event_count == events_after_three);
    stop();
}

PTEST(no_limit_keeps_going)
{
    PT_ASSERT(start(0, NULL, NULL));
    for (int i = 0; i < 5; i++)
        PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_count(), 5);
    stop();
}

PTEST(frame_run_before_init_ends_the_loop)
{
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT(frame_init(NULL) != 0);
}

PTEST(power_on_hands_the_game_line_161_and_the_keys)
{
    PT_ASSERT(start(0, NULL, NULL));
    *io(KEYINPUT_OFFSET) = 0; /* a zeroed I/O page: every key held */
    frame_power_on();
    PT_ASSERT_EQ_INT(*io(LCD_VCOUNT_OFFSET), LCD_LINE_GAME_LOGIC);
    PT_ASSERT(*io(LCD_DISPSTAT_OFFSET) & LCD_DISPSTAT_VBLANK_FLAG);
    PT_ASSERT_EQ_U32(*io(KEYINPUT_OFFSET), KEYINPUT_NONE);
    PT_ASSERT(event_count == 0); /* no interrupt at power-on */
    stop();
}

PTEST(keyinput_is_written_every_frame)
{
    PT_ASSERT(start(0, NULL, NULL));
    input_script_set(INPUT_KEY_A | INPUT_KEY_START);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_U32(*io(KEYINPUT_OFFSET), KEYINPUT_NONE & ~(INPUT_KEY_A | INPUT_KEY_START));
    stop();
}

/* --- the step hook ------------------------------------------------------- */

struct hook_log {
    int calls;
    uint16_t keyinput_seen;
    enum frame_step_result answer_at_third;
};

static enum frame_step_result step_hook(void *context)
{
    struct hook_log *log = context;
    log->calls++;
    log->keyinput_seen = *io(KEYINPUT_OFFSET);
    input_script_set(INPUT_KEY_B);
    return log->calls == 3 ? log->answer_at_third : FRAME_STEP_CONTINUE;
}

PTEST(the_step_hook_runs_once_a_frame_before_keyinput_is_written)
{
    struct hook_log log = { .answer_at_third = FRAME_STEP_CONTINUE };
    PT_ASSERT(start(0, NULL, NULL));
    frame_set_step_hook(step_hook, &log);
    *io(KEYINPUT_OFFSET) = KEYINPUT_NONE;
    input_script_set(INPUT_KEY_A);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 1);
    PT_ASSERT_EQ_U32(log.keyinput_seen, KEYINPUT_NONE); /* KEYINPUT not written yet this frame */
    PT_ASSERT_EQ_U32(*io(KEYINPUT_OFFSET), KEYINPUT_NONE & ~INPUT_KEY_B); /* what the hook set */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 2);
    stop();
}

PTEST(a_step_hook_stop_ends_the_game_with_status_0_after_the_frame)
{
    struct hook_log log = { .answer_at_third = FRAME_STEP_STOP };
    PT_ASSERT(start(0, NULL, NULL));
    frame_set_step_hook(step_hook, &log);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(frame_count(), 3);
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    stop();
}

PTEST(a_step_hook_fail_ends_the_game_with_status_1)
{
    struct hook_log log = { .answer_at_third = FRAME_STEP_FAIL };
    PT_ASSERT(start(0, NULL, NULL));
    frame_set_step_hook(step_hook, &log);
    for (int i = 0; i < 2; i++)
        PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(frame_exit_status(), 1);
    stop();
}

PTEST(removing_the_step_hook_and_frame_init_both_clear_it)
{
    struct hook_log log = { .answer_at_third = FRAME_STEP_CONTINUE };
    PT_ASSERT(start(0, NULL, NULL));
    frame_set_step_hook(step_hook, &log);
    frame_set_step_hook(NULL, NULL);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 0);
    stop();
    PT_ASSERT(start(0, NULL, NULL)); /* frame_init clears a hook left from before */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 0);
    stop();
}

/* A fresh directory for files, under $TMPDIR or /tmp. */
static bool temp_dir(char *path, size_t size)
{
    const char *base = getenv("TMPDIR");
    int n = snprintf(path, size, "%s/test_frame.XXXXXX", base != NULL ? base : "/tmp");
    return n > 0 && (size_t)n < size && mkdtemp(path) != NULL;
}

PTEST(the_screenshot_is_the_last_frame)
{
    char dir[256], path[300];
    PT_ASSERT(temp_dir(dir, sizeof dir));
    snprintf(path, sizeof path, "%s/last.ppm", dir);
    PT_ASSERT(start(2, path, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    struct stat info;
    PT_ASSERT(stat(path, &info) != 0); /* not before the end */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(stat(path, &info), 0);
    PT_ASSERT(info.st_size == (off_t)(strlen("P6\n240 160\n255\n") + VIDEO_PIXEL_COUNT * 3u));
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    stop();
    unlink(path);
    rmdir(dir);
}

PTEST(an_unwritable_screenshot_fails_the_exit_status)
{
    PT_ASSERT(start(1, "/nonexistent-directory-for-test_frame/last.ppm", NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(frame_exit_status(), 1);
    stop();
}

PTEST(a_flash_write_is_stored_at_the_frame_boundary)
{
    char dir[256], path[300];
    PT_ASSERT(temp_dir(dir, sizeof dir));
    snprintf(path, sizeof path, "%s/game.sav", dir);
    PT_ASSERT(start(0, NULL, path));
    struct stat info;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(stat(path, &info) != 0); /* nothing written, nothing stored */
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(0), FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(stat(path, &info), 0);
    PT_ASSERT_EQ_INT(frame_flush_save(), 0); /* nothing new to store */
    stop();
    unlink(path);
    rmdir(dir);
}

PTEST(a_game_pak_interrupt_stops_the_loop_with_a_failure)
{
    PT_ASSERT(start(0, NULL, NULL));
    *io(GBA_IE_OFFSET) = (uint16_t)(1u << GBA_IRQ_GAMEPAK);
    interrupt_raise(GBA_IRQ_GAMEPAK); /* the cartridge pulled: IntrMain spins forever */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT_EQ_INT(frame_exit_status(), 1);
    stop();
}

/* --- frame_run: sound --------------------------------------------------- */

static struct SoundInfo engine;

/* A running m4a engine, as SampleFreqSet leaves it for `samples` a frame. */
static void install_engine(s32 samples)
{
    memset(&engine, 0, sizeof engine);
    engine.ident = ID_NUMBER;
    engine.pcmSamplesPerVBlank = samples;
    engine.pcmDmaPeriod = (u8)(PCM_DMA_BUF_SIZE / samples);
    engine.pcmDmaCounter = 1;
    engine.pcmFreq = (597275 * samples + 5000) / 10000; /* src/m4a.c SampleFreqSet */
    SOUND_INFO_PTR = &engine;
}

PTEST(each_frame_queues_one_frame_of_sound)
{
    PT_ASSERT(start(0, NULL, NULL));
    install_engine(FRAME_AUDIO_SAMPLES_PER_FRAME);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(audio_total_frames() == 2u * FRAME_AUDIO_SAMPLES_PER_FRAME);
    PT_ASSERT(!frame_sound_rate_mismatch());
    SOUND_INFO_PTR = NULL;
    stop();
}

PTEST(an_engine_at_another_rate_is_left_out)
{
    /* SOUND_MODE_FREQ_05734: 96 samples a frame. The output still plays its
     * 224 a frame (the PSG), without the engine's samples. */
    PT_ASSERT(start(0, NULL, NULL));
    install_engine(96);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(frame_sound_rate_mismatch());
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE); /* reported once, still left out */
    PT_ASSERT(audio_total_frames() == 2u * FRAME_AUDIO_SAMPLES_PER_FRAME);
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    SOUND_INFO_PTR = NULL;
    stop();
}

PTEST(no_engine_is_not_a_rate_mismatch)
{
    PT_ASSERT(start(0, NULL, NULL));
    SOUND_INFO_PTR = NULL;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(!frame_sound_rate_mismatch());
    PT_ASSERT(audio_total_frames() == FRAME_AUDIO_SAMPLES_PER_FRAME);
    stop();
}

PTEST_MAIN()
