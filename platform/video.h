/*
 * video.h: the LCD panel, via SDL.
 *
 * Stands in for the GBA's 240x160 LCD (GBATEK "LCD Dimensions and Timings").
 * The PPU draws a frame into a BGR555 buffer; this module shows it. It and
 * audio are the only modules that call SDL's video API. Design:
 * docs/RESEARCH.md "Video".
 *
 * Headless mode creates no window and no SDL state; present just discards.
 * That is what the smoke test and the test runner use.
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL_pixels.h>

/* GBATEK "LCD Dimensions and Timings": 240 x 160 pixel visible area. */
#define VIDEO_WIDTH 240u
#define VIDEO_HEIGHT 160u
#define VIDEO_PIXEL_COUNT (VIDEO_WIDTH * VIDEO_HEIGHT)

/* Window scale (integer multiple of 240x160). 3 gives 720x480. */
#define VIDEO_DEFAULT_SCALE 3
/* Upper bound: 16 gives 3840x2560, beyond any display; larger is a caller bug. */
#define VIDEO_MAX_SCALE 16

struct video_config {
    bool headless;     /* no window; present discards */
    int scale;         /* 1..VIDEO_MAX_SCALE, also checked in headless mode */
    const char *title; /* window title, not NULL */
};

/* Returns 0 on success, nonzero after printing a message to stderr. On
 * failure everything already created is released. Calling it while
 * initialised is an error. */
int video_init(const struct video_config *cfg);

/* frame: VIDEO_PIXEL_COUNT uint16_t in GBATEK BGR555 (bits 0-4 red, 5-9
 * green, 10-14 blue, bit 15 ignored), row-major. Returns 0 on success. */
int video_present(const uint16_t *frame);

/* Pumps SDL events (the keyboard state input reads is updated by this) and
 * returns false once the user asked to quit. Events are consumed here, so
 * input must read SDL's keyboard/joystick state arrays, not the queue. */
bool video_poll_events(void);

/* Releases texture, renderer, window in reverse order. Safe after a failed
 * or partial init, without init, and twice. */
void video_shutdown(void);

/* Writes frame as a binary PPM (P6, 8-bit RGB). Returns 0 on success. */
int video_write_frame_ppm(const uint16_t *frame, const char *path);

/* The texture format: SDL's 15-bit BGR555 is, in SDL's naming, red in the
 * low bits, so it has the GBA's bit layout and no conversion is needed.
 * Exposed so a test can check the masks. */
#define VIDEO_TEXTURE_FORMAT SDL_PIXELFORMAT_BGR555

#endif /* VIDEO_H */
