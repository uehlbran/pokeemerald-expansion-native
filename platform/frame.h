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
 *  5. Sound: the frame of the m4a engine's ring the FIFO DMAs play
 *     (m4a_output) and as many PSG samples (psg_generate), mixed
 *     (sound_mix) and queued (audio_queue).
 *  6. The panel shows the frame (video_present); SDL's events are polled
 *     (video_poll_events, each passed to input_handle_event; a quit ends the
 *     loop after this frame); KEYINPUT is written (input_update_keyinput).
 *  7. If the game wrote the flash chip, the save file is stored.
 *  8. The pacer holds the GBA frame rate (frame_pacer_wait).
 *  9. The frame count; with a frame limit, the last frame ends the loop;
 *     at the end of the loop the screenshot, if asked for, is written.
 * After every dispatch, game_hooks_take_irq_return honours a handler that
 * redirected its return (the test runner's timeout).
 *
 * Not simulated: lines 162..227 one by one, so their H-Blank interrupts and
 * a V-Counter setting of 162..227 never fire (expansion sets 150, and its
 * H-Blank effects only act on lines 0..159); time within a line.
 *
 * Single-threaded: the main thread only.
 */
#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

/* The m4a engine's rate as expansion sets it: m4aSoundInit selects
 * SOUND_MODE_FREQ_13379, 224 samples per frame, and SampleFreqSet (src/m4a.c)
 * computes pcmFreq = (597275 * 224 + 5000) / 10000 = 13379 Hz. main opens
 * the audio device and the PSG at this rate (DECISIONS.md "Frame rate is the
 * GBA's"). */
#define FRAME_AUDIO_SAMPLE_RATE_HZ 13379u
#define FRAME_AUDIO_SAMPLES_PER_FRAME 224u

/* The most samples of one frame: the whole of one FIFO's half of the ring
 * (PCM_DMA_BUF_SIZE in include/gba/m4a_internal.h). */
#define FRAME_AUDIO_MAX_SAMPLES 1584u

/* Frames queued before playback starts, so the device does not start on an
 * empty queue (audio.h: the device starts paused). */
#define FRAME_AUDIO_PREROLL_FRAMES 2u

/* After a failed store of the save file, frames before the next attempt
 * (about one second); the image stays newer than the file meanwhile. */
#define FRAME_SAVE_RETRY_FRAMES 60u

enum frame_result {
    FRAME_CONTINUE = 0,
    FRAME_EXIT = 1 /* stop the game: limit reached, quit, or a failure (frame_exit_status) */
};

struct frame_config {
    uint32_t frame_limit;        /* 0: no limit; otherwise end after this many frames */
    const char *screenshot_path; /* NULL: none; otherwise the last frame as a PPM, at the end */
};

/* Keeps the configuration (the path is not copied: it must outlive the
 * module) and zeroes the counters. The modules frame_run uses are main's to
 * initialise. Returns 0, or -1 (reported) for NULL. */
int frame_init(const struct frame_config *config);

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

/* The frame buffer: VIDEO_PIXEL_COUNT BGR555 pixels, row-major. */
const uint16_t *frame_pixels(void);

#endif /* FRAME_H */
