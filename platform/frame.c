/*
 * frame.c: one frame of GBA time (frame.h has the order and its reasons).
 *
 * Stands in for the hardware's activity during a frame, GBATEK "LCD
 * Dimensions and Timings" (228 lines of 1232 cycles, 160 drawn), driving the
 * modules that own each part. Followed: docs/ARCHITECTURE.md "The frame"; the
 * reference ports' VBlankIntrWait for the structure (render, then the VBlank
 * interrupt), not their code.
 */
#include "frame.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "audio.h"
#include "audio_dump.h"
#include "dma.h"
#include "fastforward.h"
#include "flash.h"
#include "frame_pacer.h"
#include "gba_irq.h"
#include "gba_memmap.h"
#include "gba_timing.h"
#include "input.h"
#include "interrupt.h"
#include "lcd_status.h"
#include "m4a_output.h"
#include "overlay.h"
#include "ppu.h"
#include "psg.h"
#include "save_file.h"
#include "sound_grid.h"
#include "sound_mix.h"
#include "sound_speedup.h"
#include "timer.h"
#include "video.h"

/* The LCD's size is gba_timing.h's; the PPU and the panel restate it. */
_Static_assert(PPU_SCREEN_WIDTH == GBA_LCD_VISIBLE_DOTS && PPU_SCREEN_HEIGHT == GBA_LCD_VISIBLE_LINES,
               "the PPU draws the LCD's size");
_Static_assert(VIDEO_WIDTH == GBA_LCD_VISIBLE_DOTS && VIDEO_HEIGHT == GBA_LCD_VISIBLE_LINES,
               "the panel shows the LCD's size");

_Static_assert(SOUND_SPEEDUP_MAX_FACTOR == FASTFORWARD_MAX_SPEED, "every finite fast-forward speed is sped-up sound");
_Static_assert(SOUND_SPEEDUP_CHANNELS == FRAME_AUDIO_CHANNELS, "the mix's channels are the speed-up's");

#define FRAME_EXIT_STATUS_OK 0
#define FRAME_EXIT_STATUS_FAILURE 1

static struct {
    struct frame_config config;
    frame_step_hook step_hook;
    void *step_hook_context;
    bool initialised;
    uint32_t frames;
    bool exit_requested;
    int exit_status;
    /* The save: written by the game and not yet stored, and the frames to
     * wait before retrying a failed store. */
    bool save_pending;
    uint32_t save_retry_wait;
    /* Sound. The grid is the one count of output samples: the PSG is asked
     * for this many and the FIFOs are read at the same points. */
    uint32_t audio_frames_queued;
    struct sound_grid grid;
    struct sound_mix_state mix;
    /* Fast-forward: the speed applied last (a frame_pacer multiplier), the
     * sound's speed-up at it, and whether the sound is off. */
    uint32_t speed;
    struct sound_speedup speedup;
    bool sound_muted;
    bool reported_render_error;
} frame;

static uint16_t pixels[VIDEO_PIXEL_COUNT];
static int8_t fifo_a_samples[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
static int8_t fifo_b_samples[SOUND_GRID_MAX_SAMPLES_PER_FRAME];
static int16_t psg_samples[SOUND_GRID_MAX_SAMPLES_PER_FRAME * FRAME_AUDIO_CHANNELS];
static int16_t mixed_samples[SOUND_GRID_MAX_SAMPLES_PER_FRAME * FRAME_AUDIO_CHANNELS];

static uint16_t io_read(uint32_t offset)
{
    return *gba_io_u16(offset);
}

int frame_init(const struct frame_config *config)
{
    if (config == NULL) {
        (void)fprintf(stderr, "frame: frame_init: no configuration\n");
        return -1;
    }
    frame = (__typeof__(frame)){.config = *config, .initialised = true, .speed = FRAME_PACER_NORMAL_SPEED};
    return sound_speedup_start(&frame.speedup, FRAME_PACER_NORMAL_SPEED);
}

void frame_set_step_hook(frame_step_hook hook, void *context)
{
    frame.step_hook = hook;
    frame.step_hook_context = context;
}

void frame_shutdown(void)
{
    frame.initialised = false;
}

void frame_power_on(void)
{
    (void)lcd_status_begin_line(LCD_LINE_GAME_LOGIC); /* the enables are the game's to set; none yet */
    /* KEYINPUT always shows the buttons; a zeroed I/O page would read as
     * every button held, and AgbMainLoop's first ReadKeys would see
     * A+B+Start+Select and soft-reset. */
    input_update_keyinput();
}

uint32_t frame_count(void)
{
    return frame.frames;
}

int frame_exit_status(void)
{
    return frame.exit_status;
}

const uint16_t *frame_pixels(void)
{
    return pixels;
}

static void fail(void)
{
    frame.exit_requested = true;
    frame.exit_status = FRAME_EXIT_STATUS_FAILURE;
}

/* Raises the interrupts in `requested` (a mask of 1u << enum gba_irq) and
 * runs the handlers, as the CPU would take them at this point. */
static void raise_and_dispatch(unsigned requested)
{
    for (unsigned source = 0; source < GBA_IRQ_COUNT; source++) {
        if (requested & (1u << source))
            interrupt_raise((enum gba_irq)source);
    }
    (void)interrupt_dispatch(); /* the count is for tests; failures are reported inside */
}

static void draw_line(uint16_t line)
{
    int result = ppu_render_line(line, &pixels[(size_t)line * VIDEO_WIDTH]);
    if (result != PPU_OK && !frame.reported_render_error) {
        (void)fprintf(stderr, "frame: ppu_render_line(%u) failed with %d (reported once)\n", line, result);
        frame.reported_render_error = true;
    }
}

static void run_visible_lines(void)
{
    for (uint16_t line = 0; line < LCD_VISIBLE_LINES; line++) {
        raise_and_dispatch(lcd_status_begin_line(line));
        /* The only step skip_drawing skips (frame.h); everything else on the
         * line, and video_present after the frame, still runs. */
        if (!frame.config.skip_drawing)
            draw_line(line);
        unsigned hblank = lcd_status_begin_hblank();
        (void)dma_run_timing(DMA_TIMING_HBLANK); /* the block count is informational */
        raise_and_dispatch(hblank);
    }
}

static void run_vblank(void)
{
    unsigned requested = lcd_status_begin_line(LCD_LINE_VBLANK_START);
    ppu_latch_vblank();
    (void)dma_run_timing(DMA_TIMING_VBLANK);
    raise_and_dispatch(requested);
    raise_and_dispatch(lcd_status_begin_line(LCD_LINE_GAME_LOGIC));
}

int frame_start_psg(void)
{
    if (psg_init(FRAME_AUDIO_SAMPLE_RATE_HZ) != PSG_OK) {
        (void)fprintf(stderr, "frame: psg_init(%u) failed\n", FRAME_AUDIO_SAMPLE_RATE_HZ);
        return -1;
    }
    return 0;
}

/* The FIFOs' samples on this frame's grid points, or false when the engine
 * is not running. Any rate the engine mixes at plays: its frame spans the
 * video frame's cycles whatever its sample count. */
static bool hold_fifos(const struct sound_grid_frame *grid)
{
    struct m4a_output_frame fifo;
    if (!m4a_output_current_frame(&fifo))
        return false;
    sound_grid_hold_fifo(grid, fifo.fifo_a, fifo.samples, fifo_a_samples);
    sound_grid_hold_fifo(grid, fifo.fifo_b, fifo.samples, fifo_b_samples);
    return true;
}

static void mix_sound(void)
{
    const struct sound_grid_frame grid = sound_grid_next_frame(&frame.grid);
    const bool has_fifo = hold_fifos(&grid);
    if (psg_generate(psg_samples, grid.samples) != grid.samples) {
        (void)fprintf(stderr, "frame: psg_generate did not produce %zu samples\n", grid.samples);
        fail();
        return;
    }
    struct sound_mix_registers registers = {
        .soundcnt_h = io_read(GBA_SOUNDCNT_H_OFFSET),
        .soundcnt_x = io_read(GBA_SOUNDCNT_X_OFFSET),
    };
    sound_mix(&frame.mix, &registers, has_fifo ? fifo_a_samples : NULL, has_fifo ? fifo_b_samples : NULL, psg_samples,
              mixed_samples, grid.samples);
    if (frame.sound_muted)
        return;
    const size_t frames = sound_speedup_run(&frame.speedup, mixed_samples, grid.samples);
    if (audio_queue(mixed_samples, frames) != 0) {
        (void)fprintf(stderr, "frame: audio_queue failed\n");
        fail();
        return;
    }
    /* A failed write is reported and ends the recording, not the game; the
     * exit status says so (audio_dump.h). */
    (void)audio_dump_append(mixed_samples, frames);
    if (++frame.audio_frames_queued == FRAME_AUDIO_PREROLL_FRAMES && audio_set_paused(false) != 0) {
        (void)fprintf(stderr, "frame: audio_set_paused(false) failed\n");
        fail();
    }
}

/* The sound at a new speed (frame.h, step 5). */
static int change_sound(uint32_t speed, bool muted)
{
    const bool queue_stale = frame.sound_muted || frame.speed == FRAME_PACER_UNLIMITED;
    if ((muted || queue_stale) && audio_set_paused(true) != 0) {
        (void)fprintf(stderr, "frame: audio_set_paused(true) failed\n");
        return -1;
    }
    if (!muted && queue_stale) {
        audio_clear();
        frame.audio_frames_queued = 0;
    }
    const bool sped_up = !muted && speed != FRAME_PACER_UNLIMITED;
    return sound_speedup_start(&frame.speedup, sped_up ? speed : FRAME_PACER_NORMAL_SPEED);
}

/* The speed for the frames from now on, as fast-forward's keys left it: the
 * pacer's period and the sound's (frame.h, step 6). */
static void apply_speed(void)
{
    const uint32_t speed = fastforward_multiplier();
    const bool muted = fastforward_muted();
    if (frame_pacer_set_multiplier(speed) != 0) {
        fail();
        return;
    }
    if (speed == frame.speed && muted == frame.sound_muted)
        return;
    if (change_sound(speed, muted) != 0)
        fail();
    frame.speed = speed;
    frame.sound_muted = muted;
}

/* video_poll_events' callback: controller hot-plug, the help overlay's keys,
 * whose Q ends the game after this frame as closing the window does, and
 * fast-forward's. A controller that cannot be opened is reported by input
 * and play goes on with the others. */
static void on_sdl_event(const union SDL_Event *event, void *context)
{
    (void)context;
    (void)input_handle_event(event);
    if (overlay_handle_event(event) == OVERLAY_QUIT)
        frame.exit_requested = true;
    fastforward_handle_event(event);
}

static void run_step_hook(void)
{
    if (frame.step_hook == NULL)
        return;
    switch (frame.step_hook(frame.step_hook_context)) {
    case FRAME_STEP_CONTINUE:
        break;
    case FRAME_STEP_STOP:
        frame.exit_requested = true;
        break;
    case FRAME_STEP_FAIL:
    default:
        fail();
        break;
    }
}

static void present_and_read_keys(void)
{
    if (video_present(pixels) != 0) {
        (void)fprintf(stderr, "frame: video_present failed\n");
        fail();
        return;
    }
    if (!video_poll_events(on_sdl_event, NULL))
        frame.exit_requested = true;
    apply_speed();
    run_step_hook();
    input_update_keyinput();
}

static int store_save(void)
{
    if (save_file_store() != SAVE_FILE_OK) {
        (void)fprintf(stderr,
                      "frame: the save file could not be stored; the game's save is only in memory, "
                      "retrying in %u frames\n",
                      FRAME_SAVE_RETRY_FRAMES);
        frame.save_retry_wait = FRAME_SAVE_RETRY_FRAMES;
        return -1;
    }
    frame.save_pending = false;
    return 0;
}

static void persist_save(void)
{
    if (flash_take_dirty())
        frame.save_pending = true;
    if (!frame.save_pending)
        return;
    if (frame.save_retry_wait > 0) {
        frame.save_retry_wait--;
        return;
    }
    (void)store_save(); /* reported, and retried */
}

int frame_flush_save(void)
{
    if (flash_take_dirty())
        frame.save_pending = true;
    return frame.save_pending ? store_save() : 0;
}

static void finish_frame(void)
{
    frame.frames++;
    if (interrupt_halted()) {
        (void)fprintf(stderr, "frame: the interrupt controller halted on a Game Pak interrupt, as the GBA hangs; "
                              "stopping\n");
        fail();
    }
    if (frame.config.frame_limit != 0 && frame.frames >= frame.config.frame_limit)
        frame.exit_requested = true;
    if (frame.exit_requested && frame.config.screenshot_path != NULL
        && video_write_frame_ppm(pixels, frame.config.screenshot_path) != 0) {
        (void)fprintf(stderr, "frame: could not write the screenshot to %s\n", frame.config.screenshot_path);
        frame.exit_status = FRAME_EXIT_STATUS_FAILURE;
    }
}

enum frame_result frame_run(void)
{
    if (!frame.initialised) {
        (void)fprintf(stderr, "frame: frame_run before frame_init\n");
        return FRAME_EXIT;
    }
    if (frame.exit_requested)
        return FRAME_EXIT;
    run_visible_lines();
    timer_advance(GBA_CYCLES_PER_FRAME);
    raise_and_dispatch(0);
    run_vblank();
    mix_sound();
    present_and_read_keys();
    persist_save();
    frame_pacer_wait();
    finish_frame();
    return frame.exit_requested ? FRAME_EXIT : FRAME_CONTINUE;
}
