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
#include "dma.h"
#include "flash.h"
#include "frame_pacer.h"
#include "gba_irq.h"
#include "gba_memmap.h"
#include "gba_timing.h"
#include "input.h"
#include "interrupt.h"
#include "lcd_status.h"
#include "m4a_output.h"
#include "ppu.h"
#include "psg.h"
#include "save_file.h"
#include "sound_mix.h"
#include "timer.h"
#include "video.h"

/* The LCD's size is gba_timing.h's; the PPU and the panel restate it. */
_Static_assert(PPU_SCREEN_WIDTH == GBA_LCD_VISIBLE_DOTS && PPU_SCREEN_HEIGHT == GBA_LCD_VISIBLE_LINES,
               "the PPU draws the LCD's size");
_Static_assert(VIDEO_WIDTH == GBA_LCD_VISIBLE_DOTS && VIDEO_HEIGHT == GBA_LCD_VISIBLE_LINES,
               "the panel shows the LCD's size");

#define FRAME_EXIT_STATUS_OK 0
#define FRAME_EXIT_STATUS_FAILURE 1

static struct {
    struct frame_config config;
    bool initialised;
    uint32_t frames;
    bool exit_requested;
    int exit_status;
    /* The save: written by the game and not yet stored, and the frames to
     * wait before retrying a failed store. */
    bool save_pending;
    uint32_t save_retry_wait;
    /* Sound. */
    uint32_t audio_frames_queued;
    struct sound_mix_state mix;
    bool sound_rate_mismatch; /* the engine's frame size differed from the output's */
    bool reported_render_error;
} frame;

static uint16_t pixels[VIDEO_PIXEL_COUNT];
static int16_t psg_samples[FRAME_AUDIO_SAMPLES_PER_FRAME * 2];
static int16_t mixed_samples[FRAME_AUDIO_SAMPLES_PER_FRAME * 2];

static uint16_t io_read(uint32_t offset)
{
    return *(const volatile uint16_t *)(uintptr_t)(GBA_IO_BASE + offset);
}

int frame_init(const struct frame_config *config)
{
    if (config == NULL) {
        fprintf(stderr, "frame: frame_init: no configuration\n");
        return -1;
    }
    frame = (__typeof__(frame)){ .config = *config, .initialised = true };
    return 0;
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
        fprintf(stderr, "frame: ppu_render_line(%u) failed with %d (reported once)\n", line, result);
        frame.reported_render_error = true;
    }
}

static void run_visible_lines(void)
{
    for (uint16_t line = 0; line < LCD_VISIBLE_LINES; line++) {
        raise_and_dispatch(lcd_status_begin_line(line));
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

/* The FIFO samples to mix this frame: the m4a engine's ring frame, or none
 * when the engine is not running or mixes a different number of samples a
 * frame than the output plays (a mode other than Emerald's 13379 Hz), whose
 * samples would come out at the wrong pitch and length: then the FIFOs are
 * silent, reported once, and the PSG still plays. */
static bool fifo_frame(struct m4a_output_frame *fifo)
{
    if (!m4a_output_current_frame(fifo))
        return false;
    if (fifo->samples == FRAME_AUDIO_SAMPLES_PER_FRAME)
        return true;
    if (!frame.sound_rate_mismatch) {
        fprintf(stderr, "frame: the sound engine mixes %zu samples a frame (%u Hz), the output plays %u (%u Hz); "
                "its Direct Sound is left silent (reported once)\n", fifo->samples, m4a_output_sample_rate(),
                FRAME_AUDIO_SAMPLES_PER_FRAME, FRAME_AUDIO_SAMPLE_RATE_HZ);
        frame.sound_rate_mismatch = true;
    }
    return false;
}

static void mix_sound(void)
{
    struct m4a_output_frame fifo;
    bool has_fifo = fifo_frame(&fifo);
    if (psg_generate(psg_samples, FRAME_AUDIO_SAMPLES_PER_FRAME) != FRAME_AUDIO_SAMPLES_PER_FRAME) {
        fprintf(stderr, "frame: psg_generate did not produce %u samples\n", FRAME_AUDIO_SAMPLES_PER_FRAME);
        fail();
        return;
    }
    struct sound_mix_registers registers = {
        .soundcnt_h = io_read(GBA_SOUNDCNT_H_OFFSET),
        .soundcnt_x = io_read(GBA_SOUNDCNT_X_OFFSET),
    };
    sound_mix(&frame.mix, &registers, has_fifo ? fifo.fifo_a : NULL, has_fifo ? fifo.fifo_b : NULL, psg_samples,
              mixed_samples, FRAME_AUDIO_SAMPLES_PER_FRAME);
    if (audio_queue(mixed_samples, FRAME_AUDIO_SAMPLES_PER_FRAME) != 0) {
        fprintf(stderr, "frame: audio_queue failed\n");
        fail();
        return;
    }
    if (++frame.audio_frames_queued == FRAME_AUDIO_PREROLL_FRAMES && audio_set_paused(false) != 0) {
        fprintf(stderr, "frame: audio_set_paused(false) failed\n");
        fail();
    }
}

bool frame_sound_rate_mismatch(void)
{
    return frame.sound_rate_mismatch;
}

/* video_poll_events' callback: controller hot-plug. A controller that cannot
 * be opened is reported by input and play goes on with the others. */
static void on_sdl_event(const union SDL_Event *event, void *context)
{
    (void)context;
    (void)input_handle_event(event);
}

static void present_and_read_keys(void)
{
    if (video_present(pixels) != 0) {
        fprintf(stderr, "frame: video_present failed\n");
        fail();
        return;
    }
    if (!video_poll_events(on_sdl_event, NULL))
        frame.exit_requested = true;
    input_update_keyinput();
}

static int store_save(void)
{
    if (save_file_store() != SAVE_FILE_OK) {
        fprintf(stderr, "frame: the save file could not be stored; the game's save is only in memory, "
                "retrying in %u frames\n", FRAME_SAVE_RETRY_FRAMES);
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
        fprintf(stderr, "frame: the interrupt controller halted on a Game Pak interrupt, as the GBA hangs; "
                "stopping\n");
        fail();
    }
    if (frame.config.frame_limit != 0 && frame.frames >= frame.config.frame_limit)
        frame.exit_requested = true;
    if (frame.exit_requested && frame.config.screenshot_path != NULL
        && video_write_frame_ppm(pixels, frame.config.screenshot_path) != 0) {
        fprintf(stderr, "frame: could not write the screenshot to %s\n", frame.config.screenshot_path);
        frame.exit_status = FRAME_EXIT_STATUS_FAILURE;
    }
}

enum frame_result frame_run(void)
{
    if (!frame.initialised) {
        fprintf(stderr, "frame: frame_run before frame_init\n");
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
