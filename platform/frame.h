/*
 * frame.h: one frame of GBA time, run at the game's VBlankIntrWait.
 *
 * Stands in for the hardware that runs alongside the CPU for a frame: the
 * LCD going down the screen (lcd_status, ppu), H-Blank and V-Blank DMA, the
 * timers, the interrupts they raise, the sound output, the panel, the keypad
 * and the cartridge's flash. The game runs on the main thread; its
 * VBlankIntrWait (the BIOS module) calls frame_run for each frame it waits
 * through. Design: docs/ARCHITECTURE.md "The frame", docs/RESEARCH.md "Entry
 * point and loop".
 *
 * frame_run, in order:
 *  1. Lines 0..159. For each: the line starts (VCOUNT = line, DISPSTAT's
 *     H-Blank flag clear, V-Counter flag and interrupt on a match with
 *     DISPSTAT's setting), dispatch; the PPU draws the line into the frame
 *     buffer; H-Blank starts (H-Blank flag set), H-Blank DMA runs, the
 *     H-Blank interrupt is raised if enabled, dispatch.
 *  2. The timers advance one frame of cycles (GBA_CYCLES_PER_FRAME) and
 *     raise their overflow interrupts; dispatch.
 *  3. V-Blank: line 160 starts (VCOUNT = 160, V-Blank flag set, V-Counter
 *     match), the PPU latches its affine reference points, V-Blank DMA
 *     runs, the V-Blank interrupt is raised if enabled, dispatch. This runs
 *     expansion's VBlankIntr: the sound engine mixes, the DMA3 queue and the
 *     buffered GPU registers are flushed, and INTR_CHECK's V-Blank bit is set,
 *     which ends VBlankIntrWait.
 *  4. Line 161 starts (LCD_LINE_GAME_LOGIC), with its V-Counter match and
 *     dispatch; VCOUNT stays 161 while the game runs until the next frame,
 *     so SetGpuReg writes through (lcd_status.h).
 *  5. Sound: the frame's samples on the output grid (sound_grid: the GBA's
 *     32768 Hz, 548 or 549 a frame): the frame of the m4a engine's ring the
 *     FIFO DMAs play (m4a_output), at whatever rate the engine mixes, read
 *     on the grid by zero-order hold, and as many PSG samples
 *     (psg_generate), mixed (sound_mix), queued (audio_queue) and, with
 *     --audio-dump, recorded (audio_dump). Under fast-forward (fastforward.h)
 *     at n times the speed, each n samples are first made one
 *     (sound_speedup), so that a frame period's worth is queued each period
 *     and the queue stays as deep as at the normal speed; unlimited, every
 *     frame is queued as it is and the queue's bound (audio.h) drops what
 *     does not fit; muted, the device is paused and nothing is queued or recorded (an
 *     --audio-dump of such a run skips those frames). The
 *     PSG and the mix run in every case, as the hardware's did. Leaving
 *     mute or unlimited, whose queue is stale or full, the queue starts
 *     again as at power-on: paused, emptied, and played after
 *     FRAME_AUDIO_PREROLL_FRAMES frames are queued.
 *  6. The panel shows the frame (video_present, with the help overlay over
 *     it while shown, and the fast-forward indicator while it is on); SDL's
 *     events are polled (video_poll_events, each passed to
 *     input_handle_event, overlay_handle_event and fastforward_handle_event;
 *     a quit, or Q in the overlay, ends the loop after this frame); the
 *     speed for the frames from now on is applied: the pacer's
 *     (frame_pacer_set_multiplier, fastforward_multiplier) and the sound's
 *     (step 5); the step hook, if one is set, runs (below); KEYINPUT is
 *     written (input_update_keyinput).
 *  7. If the game wrote the flash chip, the save file is stored.
 *  8. The pacer holds the GBA frame rate, or n times it under fast-forward
 *     (frame_pacer_wait): the same frames, closer together.
 *  9. The frame count; with a frame limit, the last frame ends the loop;
 *     at the end of the loop the screenshot, if asked for, is written.
 * A handler may leave the frame by a jump (the test runner's timeout,
 * PlatformJumpToAgbMainLoop): the rest of the frame is not run, and the next
 * frame_run starts a new frame from line 0.
 *
 * Not simulated: lines 162..227 one by one, so their H-Blank interrupts and
 * a V-Counter setting of 162..227 never fire (expansion sets 150, and its
 * H-Blank effects only act on lines 0..159); time within a line.
 *
 * Single-threaded: the main thread only.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stdbool.h>
#include <stdint.h>

#include "sound_grid.h"

/* The output's rate: the grid the GBA's PWM stage samples the PSG and the
 * FIFOs on (sound_grid.h; GBATEK SOUNDBIAS, "Resampling to 32.768kHz"), not
 * the m4a engine's mixing rate (13379 Hz in Emerald, 224 samples a frame),
 * whose frame is read on this grid. The device and the PSG run at it, one
 * accountant counting the samples of each frame (frame_run, step 5;
 * docs/DECISIONS.md, decision (1) of "Decisions on an independent review's
 * open questions"). */
#define FRAME_AUDIO_SAMPLE_RATE_HZ SOUND_GRID_RATE_HZ

/* Output channels: the mix is interleaved left, right (GBATEK "GBA Sound
 * Controller": the PSG and the FIFOs mix to a stereo output). */
#define FRAME_AUDIO_CHANNELS 2u

/* Frames queued before playback starts, so the device does not start on an
 * empty queue (audio.h: the device starts paused). */
#define FRAME_AUDIO_PREROLL_FRAMES 2u

/* After a failed store of the save file, frames before the next attempt
 * (about one second); the image stays newer than the file meanwhile. */
#define FRAME_SAVE_RETRY_FRAMES 60u

/* What the step hook tells the frame loop. */
enum frame_step_result {
    FRAME_STEP_CONTINUE = 0,
    FRAME_STEP_STOP = 1, /* end the game after this frame, status 0 */
    FRAME_STEP_FAIL = 2  /* end the game after this frame, status 1 (the hook has reported why) */
};

/* A function called once per frame, after the frame has been shown and before
 * KEYINPUT is written, so what it sets on the keypad (input_script_set) is
 * what the game reads next. The replay driver is the one user. The frame loop
 * only schedules the call and acts on the result; what the hook does is its
 * own. */
typedef enum frame_step_result (*frame_step_hook)(void *context);

enum frame_result {
    FRAME_CONTINUE = 0,
    FRAME_EXIT = 1 /* stop the game: limit reached, quit, or a failure (frame_exit_status) */
};

struct frame_config {
    uint32_t frame_limit;        /* 0: no limit; otherwise end after this many frames */
    const char *screenshot_path; /* NULL: none; otherwise the last frame as a PPM, at the end */
    /* Skip ppu_render_line in step 1 (--test-runner without a replay or a
     * screenshot: nobody sees the frames). Everything the game can observe
     * still happens (VCOUNT and DISPSTAT, H-Blank DMA, the interrupts,
     * V-Blank DMA), and skipping loses no state: the only state drawing
     * advances, the affine reference points, is reloaded every V-Blank. */
    bool skip_drawing;
};

/* Keeps the configuration (the path is not copied: it must outlive the
 * module) and zeroes the counters. The modules frame_run uses are main's to
 * initialise. Returns 0, or -1 (reported) for NULL. */
int frame_init(const struct frame_config *config);

/* Installs (or, with NULL, removes) the step hook. Call after frame_init,
 * which clears it. */
void frame_set_step_hook(frame_step_hook hook, void *context);

/* Forgets everything. Safe at any time and twice. */
void frame_shutdown(void);

/* The hardware state the BIOS leaves when it starts the cartridge: the LCD
 * in V-Blank with VCOUNT 161 (lcd_status_begin_line), so the registers
 * AgbMain writes through SetGpuReg before its first VBlankIntrWait take
 * effect, and KEYINPUT showing the buttons (input_update_keyinput). main
 * calls it before every entry into AgbMain. */
void frame_power_on(void);

/* Runs one frame (above). FRAME_EXIT ends the game. */
enum frame_result frame_run(void);

/* Frames run since frame_init. */
uint32_t frame_count(void);

/* 0 if the loop ended normally or has not ended; 1 after a failure (a
 * module call failed, the screenshot could not be written, the interrupt
 * controller halted). */
int frame_exit_status(void);

/* Stores the save file if the game wrote the flash chip since the last
 * successful store. main calls it once more before shutting down. Returns 0,
 * or -1 if the store failed (reported). */
int frame_flush_save(void);

/* Starts the PSG on the output grid (psg_init at FRAME_AUDIO_SAMPLE_RATE_HZ):
 * at that rate each of its samples is exactly one 512-cycle span, so the
 * count frame_run asks it for keeps it on the grid the FIFOs are read on.
 * main's start step for the PSG, at power-on and after a soft reset. Returns
 * 0, or -1 (reported). */
int frame_start_psg(void);

/* The frame buffer: VIDEO_PIXEL_COUNT BGR555 pixels, row-major. */
const uint16_t *frame_pixels(void);

#endif /* FRAME_H */
