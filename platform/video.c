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
 * Presenting is not vsynced: frame_pacer owns timing, so a 60 Hz display
 * does not force 60 Hz. References: the ports in docs/research/ (read for
 * pitfalls only: unchecked SDL calls, missing cleanup); code is ours.
 * SDL docs: https://wiki.libsdl.org/SDL2/CategoryRender
 */
#include "video.h"

#include <stdio.h>
#include <stdlib.h>

#include <SDL.h>

#define MODULE "video"
#define TEXTURE_PITCH ((int)(VIDEO_WIDTH * sizeof(uint16_t)))

/* GBATEK "LCD Color Settings": 5 bits per channel. */
#define CHANNEL_BITS 5u
#define CHANNEL_MASK 0x1Fu
#define GREEN_SHIFT 5u
#define BLUE_SHIFT 10u

#define PPM_MAGIC "P6"
#define PPM_MAX_VALUE 255

static struct {
    bool initialised;
    bool headless;
    bool owns_video_subsystem;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} state;

static int sdl_error(const char *call)
{
    fprintf(stderr, MODULE ": %s failed: %s\n", call, SDL_GetError());
    return -1;
}

static int create_window_objects(const struct video_config *cfg)
{
    state.window = SDL_CreateWindow(cfg->title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                    (int)VIDEO_WIDTH * cfg->scale, (int)VIDEO_HEIGHT * cfg->scale,
                                    SDL_WINDOW_RESIZABLE);
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
    return 0;
}

int video_init(const struct video_config *cfg)
{
    if (cfg == NULL || cfg->title == NULL || cfg->scale < 1 || cfg->scale > VIDEO_MAX_SCALE) {
        fprintf(stderr, MODULE ": video_init: invalid config (need a title and scale 1..%d)\n", VIDEO_MAX_SCALE);
        return -1;
    }
    if (state.initialised) {
        fprintf(stderr, MODULE ": video_init: already initialised\n");
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
        fprintf(stderr, MODULE ": video_present: not initialised or no frame\n");
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
    SDL_RenderPresent(state.renderer); /* returns void */
    return 0;
}

bool video_poll_events(void)
{
    if (!state.initialised || state.headless)
        return true;
    bool keep_running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE))
            keep_running = false;
    }
    return keep_running;
}

void video_shutdown(void)
{
    if (state.texture != NULL)
        SDL_DestroyTexture(state.texture);
    if (state.renderer != NULL)
        SDL_DestroyRenderer(state.renderer);
    if (state.window != NULL)
        SDL_DestroyWindow(state.window);
    if (state.owns_video_subsystem)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    state.texture = NULL;
    state.renderer = NULL;
    state.window = NULL;
    state.owns_video_subsystem = false;
    state.initialised = false;
    state.headless = false;
}

/* 5 bits to 8 by replicating the top bits, so 0 -> 0 and 31 -> 255. */
static uint8_t expand_channel(uint16_t pixel, unsigned shift)
{
    uint32_t v = ((uint32_t)pixel >> shift) & CHANNEL_MASK;
    return (uint8_t)((v << (8u - CHANNEL_BITS)) | (v >> (2u * CHANNEL_BITS - 8u)));
}

int video_write_frame_ppm(const uint16_t *frame, const char *path)
{
    if (frame == NULL || path == NULL) {
        fprintf(stderr, MODULE ": video_write_frame_ppm: null argument\n");
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, MODULE ": fopen(%s) failed\n", path);
        return -1;
    }
    int result = 0;
    if (fprintf(f, PPM_MAGIC "\n%u %u\n%d\n", VIDEO_WIDTH, VIDEO_HEIGHT, PPM_MAX_VALUE) < 0)
        result = -1;
    uint8_t row[VIDEO_WIDTH * 3u];
    for (uint32_t y = 0; y < VIDEO_HEIGHT && result == 0; y++) {
        for (uint32_t x = 0; x < VIDEO_WIDTH; x++) {
            uint16_t pixel = frame[y * VIDEO_WIDTH + x];
            row[x * 3u + 0u] = expand_channel(pixel, 0u);
            row[x * 3u + 1u] = expand_channel(pixel, GREEN_SHIFT);
            row[x * 3u + 2u] = expand_channel(pixel, BLUE_SHIFT);
        }
        if (fwrite(row, 1, sizeof row, f) != sizeof row)
            result = -1;
    }
    if (fclose(f) != 0)
        result = -1;
    if (result != 0)
        fprintf(stderr, MODULE ": writing %s failed\n", path);
    return result;
}
