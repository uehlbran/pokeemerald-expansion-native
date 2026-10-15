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
 * VCOUNT and DISPSTAT read when the interrupt controller calls them. The
 * help overlay's Q runs with a window instead, on SDL's dummy driver, so
 * that the key events the test pushes reach the frame loop's poll.
 */
// deps: lcd_status.c gba_memmap.c interrupt.c timer.c dma.c transfer_bounds.c ppu.c ppu_bg.c ppu_obj.c ppu_window.c ppu_blend.c psg.c psg_channel.c psg_square.c psg_wave.c psg_noise.c audio.c video.c input.c overlay.c font.c flash.c save_file.c frame_pacer.c game_hooks.c debug_channel.c m4a_output.c sound_mix.c audio_dump.c sound_grid.c
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include <gba/types.h>
#include <gba/flash_internal.h>
#include <gba/m4a_internal.h>

#include "audio.h"
#include "audio_dump.h"
#include "dma.h"
#include "test_support.h"
#include "flash.h"
#include "frame.h"
#include "frame_pacer.h"
#include "game_hooks.h"
#include "gba_memmap.h"
#include "input.h"
#include "interrupt.h"
#include "lcd_status.h"
#include "overlay.h"
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
        events[event_count++] = (struct event){kind, *io(LCD_VCOUNT_OFFSET), *io(LCD_DISPSTAT_OFFSET)};
}

static void on_vcount(void)
{
    record(EVENT_VCOUNT);
}
static void on_hblank(void)
{
    record(EVENT_HBLANK);
}
static void on_vblank(void)
{
    record(EVENT_VBLANK);
}
static void on_timer0(void)
{
    record(EVENT_TIMER0);
}
static void on_nothing(void)
{
}

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
    on_vcount, on_nothing, on_nothing, on_hblank,  on_vblank,  on_timer0,  on_nothing,
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

/* Every module frame_run uses, with the video given, silent, scripted input;
 * IME on, the three LCD interrupts enabled in IE and DISPSTAT. */
static bool start_with_video(const struct video_config *video, uint32_t frame_limit, const char *screenshot_path,
                             const char *save_path)
{
    struct frame_config config = {.frame_limit = frame_limit, .screenshot_path = screenshot_path};
    memset((void *)(uintptr_t)GBA_IO_BASE, 0, GBA_IO_SIZE);
    event_count = 0;
    if (gba_memmap_init() != GBA_MEMMAP_OK || transfer_bounds_init() != 0
        || interrupt_init(table, INTERRUPT_TABLE_LENGTH) != INTERRUPT_OK || timer_init(interrupt_raise) != TIMER_OK
        || dma_init(interrupt_raise) != DMA_OK || ppu_init() != PPU_OK || frame_start_psg() != 0
        || audio_init(FRAME_AUDIO_SAMPLE_RATE_HZ, AUDIO_SILENT) != 0 || video_init(video) != 0
        || input_init(INPUT_SOURCE_SCRIPT, NULL) != 0 || save_file_init(save_path) != SAVE_FILE_OK
        || frame_pacer_init(FRAME_PACER_UNTHROTTLED) != 0 || frame_init(&config) != 0)
        return false;
    flash_init();
    *io(GBA_IME_OFFSET) = GBA_IME_ENABLE;
    *io(GBA_IE_OFFSET) = IE_VBLANK | IE_HBLANK | IE_VCOUNT;
    *io(LCD_DISPSTAT_OFFSET) = (uint16_t)(LCD_DISPSTAT_VBLANK_IRQ | LCD_DISPSTAT_HBLANK_IRQ | LCD_DISPSTAT_VCOUNTER_IRQ
                                          | (TEST_LYC << LCD_DISPSTAT_LYC_SHIFT));
    return true;
}

/* Headless, no save file unless one is given. */
static bool start(uint32_t frame_limit, const char *screenshot_path, const char *save_path)
{
    static const struct video_config headless = {.headless = true, .scale = 1, .title = "test_frame"};
    return start_with_video(&headless, frame_limit, screenshot_path, save_path);
}

static void stop(void)
{
    overlay_shutdown();
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

static volatile uint16_t hblank_destination;
static uint16_t hblank_source[LCD_VISIBLE_LINES + 1];

/* One halfword per H-Blank from hblank_source (incrementing) to
 * hblank_destination (fixed), repeating. */
static bool start_hblank_dma(void)
{
    for (uint16_t i = 0; i <= LCD_VISIBLE_LINES; i++)
        hblank_source[i] = (uint16_t)(0x2000u + i);
    uint32_t control = ((DMA_CNT_ENABLE | (uint32_t)DMA_TIMING_HBLANK | DMA_CNT_REPEAT | DMA_CNT_DEST_FIXED) << 16)
                       | 1u;
    return dma_set(0, hblank_source, (void *)&hblank_destination, control) == DMA_OK;
}

/* The lines' events of one frame, as a drawn frame has them: each H-Blank
 * on its line with the H-Blank flag; the VCount match at the start of
 * TEST_LYC, before that line's H-Blank; one V-Blank at line 160. */
static bool lines_events_in_order(void)
{
    uint16_t expected_line = 0;
    bool vcount_seen = false, vblank_seen = false;
    for (size_t i = 0; i < event_count; i++) {
        const struct event *e = &events[i];
        if (e->kind == EVENT_HBLANK) {
            if (e->vcount != expected_line || !(e->dispstat & LCD_DISPSTAT_HBLANK_FLAG)
                || (e->dispstat & LCD_DISPSTAT_VBLANK_FLAG))
                return false;
            expected_line++;
        } else if (e->kind == EVENT_VCOUNT) {
            if (vcount_seen || e->vcount != TEST_LYC || expected_line != TEST_LYC
                || !(e->dispstat & LCD_DISPSTAT_VCOUNTER_FLAG) || (e->dispstat & LCD_DISPSTAT_HBLANK_FLAG))
                return false;
            vcount_seen = true;
        } else if (e->kind == EVENT_VBLANK) {
            if (vblank_seen || expected_line != LCD_VISIBLE_LINES || e->vcount != LCD_VISIBLE_LINES
                || !(e->dispstat & LCD_DISPSTAT_VBLANK_FLAG))
                return false;
            vblank_seen = true;
        }
    }
    return vcount_seen && vblank_seen && expected_line == LCD_VISIBLE_LINES;
}

PTEST(skip_drawing_leaves_the_frame_buffer_and_keeps_the_lines_events)
{
    /* A drawn frame in forced blank is white; then, not drawing, a frame that
     * would be drawn black (backdrop colour 0) leaves the white alone. */
    PT_ASSERT(start(0, NULL, NULL));
    *io(FRAME_TEST_DISPCNT_OFFSET) = FRAME_TEST_FORCED_BLANK;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool drawn_white = frame_pixels()[0] == 0x7FFF && frame_pixels()[VIDEO_PIXEL_COUNT - 1] == 0x7FFF;
    struct frame_config undrawn = {.skip_drawing = true};
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_init(&undrawn), 0);
    *io(FRAME_TEST_DISPCNT_OFFSET) = 0;
    bool dma_started = start_hblank_dma();
    event_count = 0;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    int hblanks = count_events(EVENT_HBLANK);
    int vblanks = count_events(EVENT_VBLANK);
    bool in_order = lines_events_in_order();
    uint16_t last_dma = hblank_destination;
    dma_stop(0);
    bool still_white = true;
    for (size_t i = 0; i < VIDEO_PIXEL_COUNT; i++)
        still_white = still_white && frame_pixels()[i] == 0x7FFF;
    uint32_t frames = frame_count();
    stop();
    PT_ASSERT(drawn_white);
    PT_ASSERT(still_white);
    PT_ASSERT_EQ_INT(hblanks, LCD_VISIBLE_LINES);
    PT_ASSERT_EQ_INT(vblanks, 1);
    PT_ASSERT(in_order);
    PT_ASSERT(dma_started);
    PT_ASSERT_EQ_INT(last_dma, 0x2000 + LCD_VISIBLE_LINES - 1); /* one transfer per line */
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
    struct frame_config undrawn = {.skip_drawing = true};
    frame_shutdown();
    PT_ASSERT_EQ_INT(frame_init(&undrawn), 0);
    *io(FRAME_TEST_DISPCNT_OFFSET) = 0;
    *(volatile uint16_t *)(uintptr_t)GBA_PALETTE_RAM_BASE = FRAME_TEST_BACKDROP_RED;
    for (int i = 0; i < 3; i++)
        PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    bool still_white = frame_pixels()[0] == 0x7FFF;
    struct frame_config drawn = {.skip_drawing = false};
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

PTEST(hblank_dma_runs_once_per_drawn_line)
{
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT(start_hblank_dma());
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

/* --- the help overlay's Q -------------------------------------------------- */

static void one_line(overlay_line_sink sink, void *context)
{
    sink("help", context);
}

/* A window on SDL's dummy driver and the overlay started, as main starts them for a player. */
static bool start_windowed(void)
{
    static const struct video_config windowed = {.headless = false, .scale = 1, .title = "test_frame"};
    return tsup_set_environment("SDL_VIDEODRIVER", "dummy") && overlay_init(one_line) == 0
           && start_with_video(&windowed, 0, NULL, NULL);
}

static bool push_key(SDL_Scancode scancode, SDL_Keycode keycode)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = keycode;
    return SDL_PushEvent(&event) == 1;
}

PTEST(q_in_the_shown_overlay_ends_the_game_after_the_frame_with_status_0)
{
    PT_ASSERT(start_windowed());
    const bool pushed = push_key(SDL_SCANCODE_F1, SDLK_F1) && push_key(SDL_SCANCODE_Q, SDLK_q);
    const enum frame_result result = frame_run();
    const uint32_t frames = frame_count();
    const int status = frame_exit_status();
    const enum frame_result after = frame_run();
    stop();
    PT_ASSERT(pushed);
    PT_ASSERT_EQ_INT(result, FRAME_EXIT);
    PT_ASSERT_EQ_U32(frames, 1u);
    PT_ASSERT_EQ_INT(status, 0);
    PT_ASSERT_EQ_INT(after, FRAME_EXIT);
}

/* Hidden, Q is an ordinary key (perhaps a button's): the game goes on, also once the overlay has been
 * shown and hidden again. */
PTEST(q_with_the_overlay_hidden_does_not_end_the_game)
{
    PT_ASSERT(start_windowed());
    const bool pushed = push_key(SDL_SCANCODE_Q, SDLK_q);
    const enum frame_result first = frame_run();
    const bool opened = push_key(SDL_SCANCODE_F1, SDLK_F1);
    const bool closed = push_key(SDL_SCANCODE_F1, SDLK_F1);
    const bool pushed_again = opened && closed && push_key(SDL_SCANCODE_Q, SDLK_q);
    const enum frame_result second = frame_run();
    const bool shown = overlay_shown();
    stop();
    PT_ASSERT(pushed && pushed_again);
    PT_ASSERT_EQ_INT(first, FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(second, FRAME_CONTINUE);
    PT_ASSERT(!shown);
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
    struct hook_log log = {.answer_at_third = FRAME_STEP_CONTINUE};
    PT_ASSERT(start(0, NULL, NULL));
    frame_set_step_hook(step_hook, &log);
    *io(KEYINPUT_OFFSET) = KEYINPUT_NONE;
    input_script_set(INPUT_KEY_A);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 1);
    PT_ASSERT_EQ_U32(log.keyinput_seen, KEYINPUT_NONE);                   /* KEYINPUT not written yet this frame */
    PT_ASSERT_EQ_U32(*io(KEYINPUT_OFFSET), KEYINPUT_NONE & ~INPUT_KEY_B); /* what the hook set */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(log.calls, 2);
    stop();
}

PTEST(a_step_hook_stop_ends_the_game_with_status_0_after_the_frame)
{
    struct hook_log log = {.answer_at_third = FRAME_STEP_STOP};
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
    struct hook_log log = {.answer_at_third = FRAME_STEP_FAIL};
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
    struct hook_log log = {.answer_at_third = FRAME_STEP_CONTINUE};
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

/* A fresh directory for files, under the temporary directory. */
static bool temp_dir(char *path, size_t size)
{
    return tsup_make_temp_directory(path, size, "test_frame");
}

PTEST(the_screenshot_is_the_last_frame)
{
    char dir[256], path[300];
    PT_ASSERT(temp_dir(dir, sizeof dir));
    (void)snprintf(path, sizeof path, "%s/last.ppm", dir);
    PT_ASSERT(start(2, path, NULL));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(!tsup_exists(path)); /* not before the end */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_EXIT);
    PT_ASSERT(tsup_exists(path));
    PT_ASSERT(tsup_file_size(path) == (intmax_t)(strlen("P6\n240 160\n255\n") + VIDEO_PIXEL_COUNT * 3u));
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    stop();
    (void)tsup_remove_file(path);
    (void)tsup_remove_directory(dir);
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
    (void)snprintf(path, sizeof path, "%s/game.sav", dir);
    PT_ASSERT(start(0, NULL, path));
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(!tsup_exists(path)); /* nothing written, nothing stored */
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(0), FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(tsup_exists(path));
    PT_ASSERT_EQ_INT(frame_flush_save(), 0); /* nothing new to store */
    stop();
    (void)tsup_remove_file(path);
    (void)tsup_remove_directory(dir);
}

PTEST(a_failed_save_store_is_retried_after_the_retry_wait_and_flush_reports_it)
{
    char dir[256], path[300], missing[320];
    PT_ASSERT(temp_dir(dir, sizeof dir));
    (void)snprintf(missing, sizeof missing, "%s/later", dir);
    (void)snprintf(path, sizeof path, "%s/later/game.sav", dir);
    PT_ASSERT(start(0, NULL, path));
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(0), FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE); /* the store fails: no such directory */
    PT_ASSERT(!tsup_exists(path));
    PT_ASSERT_EQ_INT(frame_flush_save(), -1); /* still pending, still failing, reported to main */
    PT_ASSERT(tsup_make_directory(missing));
    for (uint32_t i = 0; i < FRAME_SAVE_RETRY_FRAMES; i++)
        PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(!tsup_exists(path)); /* still waiting out the retry frames */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(tsup_exists(path)); /* the retry stored it */
    stop();
    (void)tsup_remove_file(path);
    (void)tsup_remove_directory(missing);
    (void)tsup_remove_directory(dir);
}

PTEST(flush_stores_a_pending_write_without_running_a_frame)
{
    char dir[256], path[300];
    PT_ASSERT(temp_dir(dir, sizeof dir));
    (void)snprintf(path, sizeof path, "%s/game.sav", dir);
    PT_ASSERT(start(0, NULL, path));
    PT_ASSERT_EQ_INT(frame_flush_save(), 0); /* nothing written, nothing to store */
    PT_ASSERT(!tsup_exists(path));
    PT_ASSERT_EQ_U32(NativeFlash.eraseFlashSector(0), FLASH_STATUS_OK);
    PT_ASSERT_EQ_INT(frame_flush_save(), 0);
    PT_ASSERT(tsup_exists(path));
    stop();
    (void)tsup_remove_file(path);
    (void)tsup_remove_directory(dir);
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

/* The output grid (sound_grid.h): 280896 cycles a frame, 512 a sample, so
 * the first frame holds 548 samples and carries 320 cycles, and the second,
 * from 320, holds 549. */
#define FIRST_FRAME_SAMPLES 548u
#define SECOND_FRAME_SAMPLES 549u

/* GBATEK "GBA Sound Channel 2 - Tone" and "GBA Sound Control Registers". */
#define SOUND2CNT_L_OFFSET UINT32_C(0x068)
#define SOUND2CNT_H_OFFSET UINT32_C(0x06C)
#define SOUNDCNT_X_MASTER_ENABLE UINT16_C(0x0080)
#define SOUNDCNT_X_SOUND_2_ON UINT16_C(0x0002)
/* SOUNDCNT_H as src/m4a.c SoundInit leaves it: FIFO A right, B left, both
 * at 100% (bits 2, 3, 8, 13). */
#define SOUNDCNT_H_A_RIGHT_B_LEFT_FULL UINT16_C(0x210C)

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

/* A scratch WAV path for a recording of the frames a test runs. */
struct recording {
    char directory[TSUP_PATH_SIZE];
    char path[TSUP_PATH_SIZE];
};

static bool start_recording(struct recording *r)
{
    return tsup_make_temp_directory(r->directory, sizeof r->directory, "test_frame")
           && snprintf(r->path, sizeof r->path, "%s/sound.wav", r->directory) < (int)sizeof r->path
           && audio_dump_start(r->path, FRAME_AUDIO_SAMPLE_RATE_HZ) == 0;
}

/* Finishes the recording and reads its first stereo frame (left, right);
 * returns the file's size, or -1. */
static intmax_t finish_recording(const struct recording *r, int16_t first[2])
{
    intmax_t size = -1;
    FILE *file = NULL;
    if (audio_dump_finish() == 0)
        file = fopen(r->path, "rb");
    if (file != NULL) {
        uint8_t bytes[AUDIO_DUMP_HEADER_BYTES + AUDIO_DUMP_BYTES_PER_FRAME];
        if (fread(bytes, 1, sizeof bytes, file) == sizeof bytes) {
            first[0] = (int16_t)(uint16_t)(bytes[AUDIO_DUMP_HEADER_BYTES] | bytes[AUDIO_DUMP_HEADER_BYTES + 1] << 8);
            first[1] = (int16_t)(uint16_t)(bytes[AUDIO_DUMP_HEADER_BYTES + 2]
                                           | bytes[AUDIO_DUMP_HEADER_BYTES + 3] << 8);
        }
        (void)fclose(file);
        size = tsup_file_size(r->path);
    }
    (void)tsup_remove_tree(r->directory);
    return size;
}

PTEST(each_frame_queues_its_samples_on_the_32768_hz_grid)
{
    PT_ASSERT(start(0, NULL, NULL));
    install_engine(224); /* Emerald's SOUND_MODE_FREQ_13379 */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(audio_total_frames() == FIRST_FRAME_SAMPLES);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(audio_total_frames() == FIRST_FRAME_SAMPLES + SECOND_FRAME_SAMPLES);
    SOUND_INFO_PTR = NULL;
    stop();
}

PTEST(the_psg_runs_one_frame_of_cycles_a_frame)
{
    /* Channel 2 with its length enabled and 64 - 59 = 5 ticks: the frame
     * sequencer, started when the PSG powers on in the first frame, clocks
     * lengths at 32768, 98304, 163840, 229376 and 294912 cycles (Pan Docs
     * "Audio Details"), so the channel stops at 294912: after the first
     * frame's 548 samples (280576 cycles) it plays, after the second's 1097
     * (561664) it does not. A PSG off the grid (another rate) runs another
     * number of cycles for the same samples. */
    PT_ASSERT(start(0, NULL, NULL));
    SOUND_INFO_PTR = NULL;
    *io(GBA_SOUNDCNT_X_OFFSET) = SOUNDCNT_X_MASTER_ENABLE;
    *io(SOUND2CNT_L_OFFSET) = UINT16_C(0xF0BB); /* NR22 volume 15, no envelope; NR21 duty 50%, length 59 */
    *io(SOUND2CNT_H_OFFSET) = UINT16_C(0xC700); /* NR24 trigger, length enable, frequency 700h */
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    const bool on_after_first = (*io(GBA_SOUNDCNT_X_OFFSET) & SOUNDCNT_X_SOUND_2_ON) != 0;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    const bool on_after_second = (*io(GBA_SOUNDCNT_X_OFFSET) & SOUNDCNT_X_SOUND_2_ON) != 0;
    stop();
    PT_ASSERT(on_after_first);
    PT_ASSERT(!on_after_second);
}

/* Runs one frame with the engine at `samples` a frame, its whole ring
 * holding a in FIFO A's half and b in FIFO B's, routed as SoundInit routes
 * them, and returns the first stereo frame recorded. */
static bool first_output_with_engine(s32 samples, s8 a, s8 b, int16_t first[2], intmax_t *size)
{
    struct recording r;
    if (!start(0, NULL, NULL) || !start_recording(&r))
        return false;
    install_engine(samples);
    memset(engine.pcmBuffer, (u8)a, PCM_DMA_BUF_SIZE);
    memset(&engine.pcmBuffer[PCM_DMA_BUF_SIZE], (u8)b, PCM_DMA_BUF_SIZE);
    *io(GBA_SOUNDCNT_X_OFFSET) = SOUNDCNT_X_MASTER_ENABLE;
    *io(GBA_SOUNDCNT_H_OFFSET) = SOUNDCNT_H_A_RIGHT_B_LEFT_FULL;
    const bool ran = frame_run() == FRAME_CONTINUE;
    *size = finish_recording(&r, first);
    SOUND_INFO_PTR = NULL;
    stop();
    return ran;
}

PTEST(the_engines_frame_plays_at_its_own_rate_and_at_any_other)
{
    /* Emerald's 224 samples a frame, and SOUND_MODE_FREQ_05734's 96, which
     * the output used to leave silent: both are read on the grid, the
     * FIFO's value at 100% is 64 per unit (sound_mix.h), and the DC filter
     * passes the first sample of a step unchanged. Both fill the frame's 548
     * samples. */
    static const s32 rates[] = {224, 96};
    for (size_t i = 0; i < sizeof rates / sizeof rates[0]; i++) {
        int16_t first[2] = {0, 0};
        intmax_t size = 0;
        PT_ASSERT(first_output_with_engine(rates[i], 10, -3, first, &size));
        PT_ASSERT_EQ_INT(first[0], -3 * 64); /* B, left */
        PT_ASSERT_EQ_INT(first[1], 10 * 64); /* A, right */
        PT_ASSERT(size == (intmax_t)(AUDIO_DUMP_HEADER_BYTES + FIRST_FRAME_SAMPLES * AUDIO_DUMP_BYTES_PER_FRAME));
    }
}

PTEST(the_audio_dump_records_every_frame_queued)
{
    struct recording r;
    PT_ASSERT(start(0, NULL, NULL));
    PT_ASSERT(start_recording(&r));
    install_engine(224);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    const uint64_t queued = audio_total_frames();
    int16_t first[2];
    const intmax_t size = finish_recording(&r, first);
    SOUND_INFO_PTR = NULL;
    stop();
    PT_ASSERT(queued == FIRST_FRAME_SAMPLES + SECOND_FRAME_SAMPLES);
    PT_ASSERT(size == (intmax_t)(AUDIO_DUMP_HEADER_BYTES + queued * AUDIO_DUMP_BYTES_PER_FRAME));
}

PTEST(no_engine_plays_the_psg_alone)
{
    PT_ASSERT(start(0, NULL, NULL));
    SOUND_INFO_PTR = NULL;
    PT_ASSERT_EQ_INT(frame_run(), FRAME_CONTINUE);
    PT_ASSERT(audio_total_frames() == FIRST_FRAME_SAMPLES);
    PT_ASSERT_EQ_INT(frame_exit_status(), 0);
    stop();
}

PTEST_MAIN()
