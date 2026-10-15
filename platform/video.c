/*
 * video.c: the LCD panel, via SDL2.
 *
 * Real hardware: the LCD shows 240x160 pixels of 15-bit colour, 59.7275
 * frames a second (GBATEK "LCD Dimensions and Timings", "LCD Color Settings").
 * Here: a streaming texture of the same format, scaled by SDL to the window
 * at an integer factor, with a logical size of 240x160 so resizing letterboxes.
 *
 * Pixel format: SDL_PIXELFORMAT_BGR555 is a packed 16-bit format whose masks
 * are red 0x001F, green 0x03E0, blue 0x7C00, exactly GBATEK's layout (bits
 * 0-4 red, 5-9 green, 10-14 blue), so the PPU's buffer is uploaded as is.
 * If a renderer cannot sample it natively, SDL converts inside
 * SDL_UpdateTexture; that costs speed, never correctness.
 *
 * The help overlay (overlay.h), when main has started it, is a second
 * texture of the window's own size at the chosen scale, 32-bit ARGB so that
 * its background can be translucent, drawn once at init (overlay_draw) and
 * copied over the frame at each present while it is shown. It covers the
 * same logical 240x160, so it scales and letterboxes with the frame. The
 * fast-forward indicator (fastforward.h), when fast-forward is started, is a
 * third, the size of its box in LCD pixels, drawn once at init the same way
 * and copied to its place in the logical 240x160 while it is shown, so it is
 * as sharp as the game's own pixels. The frame buffer itself is never touched.
 *
 * Presenting is not vsynced: frame_pacer owns timing, so a 60 Hz display
 * does not force 60 Hz. References: the ports in docs/research/ (read for
 * pitfalls only: unchecked SDL calls, missing cleanup); code is ours.
 * SDL docs: https://wiki.libsdl.org/SDL2/CategoryRender
 */
#include "video.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "fastforward.h"
#include "font.h"
#include "overlay.h"

#define MODULE "video"
#define TEXTURE_PITCH ((int)(VIDEO_WIDTH * sizeof(uint16_t)))

/* GBATEK "LCD Color Settings": 5 bits per channel. */
#define CHANNEL_BITS 5u
#define BITS_PER_BYTE 8u
#define RGB_BYTES_PER_PIXEL 3u
#define CHANNEL_MASK 0x1Fu
#define GREEN_SHIFT 5u
#define BLUE_SHIFT 10u

#define PPM_MAGIC "P6"
#define PPM_MAX_VALUE 255

/* The overlay's and the indicator's textures: 32-bit ARGB, one uint32_t a
 * pixel (overlay.h's colours). */
#define OVERLAY_TEXTURE_FORMAT SDL_PIXELFORMAT_ARGB8888
#define OVERLAY_BYTES_PER_PIXEL ((int)sizeof(uint32_t))

static struct {
    bool initialised;
    bool headless;
    bool owns_video_subsystem;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *overlay;   /* NULL unless the overlay is started */
    SDL_Texture *indicator; /* NULL unless fast-forward is started */
    SDL_Rect indicator_place;
    SDL_Point overlay_size;       /* the overlay texture's pixels */
    bool overlay_painted_for_pad; /* which hint it shows (overlay_hint_for_pad) */
} state;

static int sdl_error(const char *call)
{
    (void)fprintf(stderr, MODULE ": %s failed: %s\n", call, SDL_GetError());
    return -1;
}

/* A failed call on one of the textures drawn over the frame, named by what. */
static int sdl_error_on(const char *call, const char *what)
{
    (void)fprintf(stderr, MODULE ": %s(%s) failed: %s\n", call, what, SDL_GetError());
    return -1;
}

/* Makes texture's pixels, width x height, what paint draws, over a
 * translucent background; what names the texture in a report. */
static int paint_texture(SDL_Texture *texture, int width, int height, void (*paint)(const struct font_canvas *canvas),
                         const char *what)
{
    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0)
        return sdl_error_on("SDL_SetTextureBlendMode", what);
    void *pixels = NULL;
    int pitch = 0;
    if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0)
        return sdl_error_on("SDL_LockTexture", what);
    if (pitch < width * OVERLAY_BYTES_PER_PIXEL || pitch % OVERLAY_BYTES_PER_PIXEL != 0) {
        SDL_UnlockTexture(texture);
        (void)fprintf(stderr, MODULE ": SDL_LockTexture(%s) gave a pitch of %d bytes for %d pixels\n", what, pitch,
                      width);
        return -1;
    }
    const struct font_canvas canvas = {
        .pixels = pixels,
        .width = (size_t)width,
        .height = (size_t)height,
        .stride = (size_t)(pitch / OVERLAY_BYTES_PER_PIXEL),
    };
    paint(&canvas);
    SDL_UnlockTexture(texture); /* returns void */
    return 0;
}

/* The overlay's texture, the window's size, drawn by overlay_draw. */
static int create_overlay(int scale)
{
    const int width = (int)VIDEO_WIDTH * scale;
    const int height = (int)VIDEO_HEIGHT * scale;
    state.overlay = SDL_CreateTexture(state.renderer, OVERLAY_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING, width,
                                      height);
    if (state.overlay == NULL)
        return sdl_error_on("SDL_CreateTexture", "overlay");
    state.overlay_size = (SDL_Point){.x = width, .y = height};
    state.overlay_painted_for_pad = overlay_hint_for_pad();
    return paint_texture(state.overlay, width, height, overlay_draw, "overlay");
}

/* The overlay's hint names the keyboard's keys or the controller's buttons,
 * by which was used last (overlay.h): painted again when that changes. */
static int repaint_overlay_hint(void)
{
    if (overlay_hint_for_pad() == state.overlay_painted_for_pad)
        return 0;
    state.overlay_painted_for_pad = overlay_hint_for_pad();
    return paint_texture(state.overlay, state.overlay_size.x, state.overlay_size.y, overlay_draw, "overlay");
}

/* The fast-forward indicator's texture, its box in LCD pixels, drawn by
 * fastforward_draw_indicator. */
static int create_indicator(void)
{
    const struct fastforward_rect box = fastforward_indicator_box();
    state.indicator_place = (SDL_Rect){.x = (int)box.x, .y = (int)box.y, .w = (int)box.width, .h = (int)box.height};
    state.indicator = SDL_CreateTexture(state.renderer, OVERLAY_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING,
                                        state.indicator_place.w, state.indicator_place.h);
    if (state.indicator == NULL)
        return sdl_error_on("SDL_CreateTexture", "indicator");
    return paint_texture(state.indicator, state.indicator_place.w, state.indicator_place.h, fastforward_draw_indicator,
                         "indicator");
}

static int create_window_objects(const struct video_config *cfg)
{
    state.window = SDL_CreateWindow(cfg->title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    (int)VIDEO_WIDTH * cfg->scale, (int)VIDEO_HEIGHT * cfg->scale,
                                    SDL_WINDOW_RESIZABLE | (cfg->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u));
    if (state.window == NULL)
        return sdl_error("SDL_CreateWindow");
    state.renderer = SDL_CreateRenderer(state.window, -1, 0);
    if (state.renderer == NULL)
        return sdl_error("SDL_CreateRenderer");
    if (SDL_RenderSetLogicalSize(state.renderer, (int)VIDEO_WIDTH, (int)VIDEO_HEIGHT) != 0)
        return sdl_error("SDL_RenderSetLogicalSize");
    if (SDL_RenderSetIntegerScale(state.renderer, SDL_TRUE) != 0)
        return sdl_error("SDL_RenderSetIntegerScale");
    state.texture = SDL_CreateTexture(state.renderer, VIDEO_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING,
                                      (int)VIDEO_WIDTH, (int)VIDEO_HEIGHT);
    if (state.texture == NULL)
        return sdl_error("SDL_CreateTexture");
    if (overlay_enabled() && create_overlay(cfg->scale) != 0)
        return -1;
    return fastforward_enabled() ? create_indicator() : 0;
}

int video_init(const struct video_config *cfg)
{
    if (cfg == NULL || cfg->title == NULL || cfg->scale < 1 || cfg->scale > VIDEO_MAX_SCALE) {
        (void)fprintf(stderr, MODULE ": video_init: invalid config (need a title and scale 1..%d)\n", VIDEO_MAX_SCALE);
        return -1;
    }
    if (state.initialised) {
        (void)fprintf(stderr, MODULE ": video_init: already initialised\n");
        return -1;
    }
    state.headless = cfg->headless;
    if (cfg->headless) {
        state.initialised = true;
        return 0;
    }
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return sdl_error("SDL_InitSubSystem(SDL_INIT_VIDEO)");
    state.owns_video_subsystem = true;
    state.initialised = true;
    if (create_window_objects(cfg) != 0) {
        video_shutdown();
        return -1;
    }
    return 0;
}

int video_present(const uint16_t *frame)
{
    if (!state.initialised || frame == NULL) {
        (void)fprintf(stderr, MODULE ": video_present: not initialised or no frame\n");
        return -1;
    }
    if (state.headless)
        return 0;
    if (SDL_UpdateTexture(state.texture, NULL, frame, TEXTURE_PITCH) != 0)
        return sdl_error("SDL_UpdateTexture");
    if (SDL_RenderClear(state.renderer) != 0)
        return sdl_error("SDL_RenderClear");
    if (SDL_RenderCopy(state.renderer, state.texture, NULL, NULL) != 0)
        return sdl_error("SDL_RenderCopy");
    if (state.indicator != NULL && fastforward_indicator_shown()
        && SDL_RenderCopy(state.renderer, state.indicator, NULL, &state.indicator_place) != 0)
        return sdl_error_on("SDL_RenderCopy", "indicator");
    if (state.overlay != NULL && overlay_shown() && repaint_overlay_hint() != 0)
        return -1;
    if (state.overlay != NULL && overlay_shown() && SDL_RenderCopy(state.renderer, state.overlay, NULL, NULL) != 0)
        return sdl_error_on("SDL_RenderCopy", "overlay");
    SDL_RenderPresent(state.renderer); /* returns void */
    return 0;
}

int video_set_fullscreen(bool fullscreen)
{
    if (!state.initialised) {
        (void)fprintf(stderr, MODULE ": video_set_fullscreen: not initialised\n");
        return -1;
    }
    if (state.headless)
        return 0;
    if (SDL_SetWindowFullscreen(state.window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0u) != 0)
        return sdl_error("SDL_SetWindowFullscreen");
    return 0;
}

bool video_is_fullscreen(void)
{
    if (!state.initialised || state.headless)
        return false;
    return (SDL_GetWindowFlags(state.window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

bool video_poll_events(void (*on_event)(const union SDL_Event *event, void *ctx), void *ctx)
{
    if (!state.initialised || state.headless)
        return true;
    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (on_event != NULL)
            on_event(&event, ctx);
        if (event.type == SDL_QUIT || (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
            keep_running = false;
    }
    return keep_running;
}

void video_shutdown(void)
{
    if (state.indicator != NULL)
        SDL_DestroyTexture(state.indicator);
    if (state.overlay != NULL)
        SDL_DestroyTexture(state.overlay);
    if (state.texture != NULL)
        SDL_DestroyTexture(state.texture);
    if (state.renderer != NULL)
        SDL_DestroyRenderer(state.renderer);
    if (state.window != NULL)
        SDL_DestroyWindow(state.window);
    if (state.owns_video_subsystem)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    state.indicator = NULL;
    state.overlay = NULL;
    state.texture = NULL;
    state.renderer = NULL;
    state.window = NULL;
    state.owns_video_subsystem = false;
    state.initialised = false;
    state.headless = false;
    state.overlay_painted_for_pad = false;
}

/* 5 bits to 8 by replicating the top bits, so 0 -> 0 and 31 -> 255. */
static uint8_t expand_channel(uint16_t pixel, unsigned shift)
{
    uint32_t v = ((uint32_t)pixel >> shift) & CHANNEL_MASK;
    return (uint8_t)((v << (BITS_PER_BYTE - CHANNEL_BITS)) | (v >> (2u * CHANNEL_BITS - BITS_PER_BYTE)));
}

int video_write_frame_ppm(const uint16_t *frame, const char *path)
{
    if (frame == NULL || path == NULL) {
        (void)fprintf(stderr, MODULE ": video_write_frame_ppm: null argument\n");
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        (void)fprintf(stderr, MODULE ": fopen(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }
    int result = 0;
    if (fprintf(f, PPM_MAGIC "\n%u %u\n%d\n", VIDEO_WIDTH, VIDEO_HEIGHT, PPM_MAX_VALUE) < 0)
        result = -1;
    uint8_t row[VIDEO_WIDTH * RGB_BYTES_PER_PIXEL];
    for (uint32_t y = 0; y < VIDEO_HEIGHT && result == 0; y++) {
        for (uint32_t x = 0; x < VIDEO_WIDTH; x++) {
            uint16_t pixel = frame[y * VIDEO_WIDTH + x];
            row[x * RGB_BYTES_PER_PIXEL + 0u] = expand_channel(pixel, 0u);
            row[x * RGB_BYTES_PER_PIXEL + 1u] = expand_channel(pixel, GREEN_SHIFT);
            row[x * RGB_BYTES_PER_PIXEL + 2u] = expand_channel(pixel, BLUE_SHIFT);
        }
        if (fwrite(row, 1, sizeof row, f) != sizeof row)
            result = -1;
    }
    if (fclose(f) != 0)
        result = -1;
    if (result != 0)
        (void)fprintf(stderr, MODULE ": writing %s failed\n", path);
    return result;
}
