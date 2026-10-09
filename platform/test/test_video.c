/*
 * Tests of video. They run with SDL's dummy video driver (set with setenv
 * before the first SDL call, so no display is needed). The pixel format is
 * asserted from SDL's own mask table against GBATEK "LCD Color Settings"
 * (bits 0-4 red, 5-9 green, 10-14 blue). Rendering itself is not asserted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>

#include "ptest.h"
#include "video.h"

#define LCD_WIDTH 240u
#define LCD_HEIGHT 160u
#define TEST_PPM_HEADER "P6\n240 160\n255\n"

static uint16_t frame_buffer[LCD_WIDTH * LCD_HEIGHT];

static void use_dummy_driver(void)
{
    PT_ASSERT(setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
}

static struct video_config windowed_config(void)
{
    struct video_config cfg = {.headless = false, .scale = 2, .title = "video test"};
    return cfg;
}

PTEST(pixel_format_masks_match_gba_colour_layout)
{
    Uint32 rmask, gmask, bmask, amask;
    int bpp;
    PT_ASSERT(SDL_PixelFormatEnumToMasks(VIDEO_TEXTURE_FORMAT, &bpp, &rmask, &gmask, &bmask, &amask) == SDL_TRUE);
    PT_ASSERT_EQ_INT(bpp, 15);
    PT_ASSERT_EQ_U32(rmask, 0x001Fu); /* bits 0-4 red */
    PT_ASSERT_EQ_U32(gmask, 0x03E0u); /* bits 5-9 green */
    PT_ASSERT_EQ_U32(bmask, 0x7C00u); /* bits 10-14 blue */
    PT_ASSERT_EQ_U32(amask, 0u);
}

PTEST(windowed_init_present_shutdown_succeeds)
{
    use_dummy_driver();
    struct video_config cfg = windowed_config();
    memset(frame_buffer, 0, sizeof frame_buffer);
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    Uint32 inited = SDL_WasInit(SDL_INIT_VIDEO);
    int present = video_present(frame_buffer);
    int present_again = video_present(frame_buffer);
    bool keep_running = video_poll_events(NULL, NULL);
    video_shutdown();
    Uint32 after = SDL_WasInit(SDL_INIT_VIDEO);
    PT_ASSERT(inited != 0u);
    PT_ASSERT_EQ_U32(after, 0u);
    PT_ASSERT_EQ_INT(present, 0);
    PT_ASSERT_EQ_INT(present_again, 0);
    PT_ASSERT(keep_running);
}

struct event_log {
    int user_events;
    int quit_events;
};

static void log_event(const union SDL_Event *event, void *ctx)
{
    struct event_log *log = ctx;
    if (event->type == SDL_USEREVENT)
        log->user_events++;
    if (event->type == SDL_QUIT)
        log->quit_events++;
}

PTEST(events_reach_the_callback_with_ctx_and_quit_returns_false)
{
    use_dummy_driver();
    struct video_config cfg = windowed_config();
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    struct event_log log = {0, 0};
    SDL_Event user;
    SDL_zero(user);
    user.type = SDL_USEREVENT;
    SDL_Event quit;
    SDL_zero(quit);
    quit.type = SDL_QUIT;
    int pushed_user = SDL_PushEvent(&user);
    bool after_user = video_poll_events(log_event, &log);
    int pushed_quit = SDL_PushEvent(&quit);
    bool after_quit = video_poll_events(log_event, &log);
    int pushed_quit_null = SDL_PushEvent(&quit);
    bool null_callback_quit = video_poll_events(NULL, NULL);
    SDL_Event close;
    SDL_zero(close);
    close.type = SDL_WINDOWEVENT;
    close.window.event = SDL_WINDOWEVENT_CLOSE;
    int pushed_close = SDL_PushEvent(&close);
    bool after_close = video_poll_events(NULL, NULL);
    video_shutdown();
    PT_ASSERT(pushed_close == 1);
    PT_ASSERT(!after_close);
    PT_ASSERT(pushed_user == 1);
    PT_ASSERT(pushed_quit == 1);
    PT_ASSERT(pushed_quit_null == 1);
    PT_ASSERT(after_user);
    PT_ASSERT(!after_quit);
    PT_ASSERT(!null_callback_quit);
    PT_ASSERT_EQ_INT(log.user_events, 1);
    PT_ASSERT_EQ_INT(log.quit_events, 1);
}

PTEST(headless_presents_without_a_window)
{
    /* No driver set on purpose: headless must not touch SDL's video. */
    struct video_config cfg = {.headless = true, .scale = 3, .title = "headless"};
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    Uint32 video_inited = SDL_WasInit(SDL_INIT_VIDEO);
    int present = video_present(frame_buffer);
    bool keep_running = video_poll_events(NULL, NULL);
    video_shutdown();
    PT_ASSERT_EQ_U32(video_inited, 0u);
    PT_ASSERT_EQ_INT(present, 0);
    PT_ASSERT(keep_running);
}

PTEST(failed_driver_init_fails_and_leaves_no_subsystem)
{
    PT_ASSERT(setenv("SDL_VIDEODRIVER", "no_such_driver", 1) == 0);
    struct video_config cfg = windowed_config();
    int result = video_init(&cfg);
    Uint32 inited = SDL_WasInit(SDL_INIT_VIDEO);
    video_shutdown();
    PT_ASSERT(result != 0);
    PT_ASSERT_EQ_U32(inited, 0u);
    PT_ASSERT_EQ_U32(SDL_WasInit(SDL_INIT_VIDEO), 0u);
}

PTEST(shutdown_twice_and_without_init_are_safe)
{
    video_shutdown();
    use_dummy_driver();
    struct video_config cfg = windowed_config();
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    video_shutdown();
    video_shutdown();
}

PTEST(init_rejects_invalid_config_and_shutdown_after_is_safe)
{
    use_dummy_driver();
    struct video_config zero = {.headless = false, .scale = 0, .title = "t"};
    struct video_config huge = {.headless = false, .scale = VIDEO_MAX_SCALE + 1, .title = "t"};
    struct video_config no_title = {.headless = false, .scale = 1, .title = NULL};
    PT_ASSERT(video_init(&zero) != 0);
    video_shutdown();
    PT_ASSERT(video_init(&huge) != 0);
    video_shutdown();
    PT_ASSERT(video_init(&no_title) != 0);
    video_shutdown();
    PT_ASSERT(video_init(NULL) != 0);
    video_shutdown();
    /* Headless validates the scale too, so a bad config is never half accepted. */
    struct video_config headless_zero = {.headless = true, .scale = 0, .title = "t"};
    PT_ASSERT(video_init(&headless_zero) != 0);
    video_shutdown();
}

PTEST(init_twice_is_an_error_and_keeps_the_first)
{
    use_dummy_driver();
    struct video_config cfg = windowed_config();
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    int second = video_init(&cfg);
    int present = video_present(frame_buffer);
    video_shutdown();
    PT_ASSERT(second != 0);
    PT_ASSERT_EQ_INT(present, 0);
}

PTEST(present_without_init_or_with_null_frame_fails)
{
    PT_ASSERT(video_present(frame_buffer) != 0);
    use_dummy_driver();
    struct video_config cfg = windowed_config();
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    int null_frame = video_present(NULL);
    video_shutdown();
    PT_ASSERT(null_frame != 0);
}

static int read_file(const char *path, unsigned char *out, size_t cap, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    *size = fread(out, 1, cap, f);
    int closed = fclose(f);
    return closed == 0 ? 0 : -1;
}

PTEST(ppm_of_two_pixel_frame_has_exact_bytes)
{
    /* GBATEK colour: bits 0-4 red, 5-9 green, 10-14 blue. Each 5-bit value v
     * becomes (v << 3) | (v >> 2), so 0 -> 0 and 31 -> 255. */
    memset(frame_buffer, 0, sizeof frame_buffer);
    frame_buffer[0] = 0x001F;                                   /* pure red */
    frame_buffer[1] = (uint16_t)((1u << 10) | (16u << 5) | 8u); /* b=1 g=16 r=8 */
    frame_buffer[LCD_WIDTH] = 0x0021;                           /* row 1, column 0: r=1 g=1 b=0 */
    frame_buffer[LCD_WIDTH * LCD_HEIGHT - 1u] = 0xFFFF;         /* last pixel, bit 15 set */
    char path[] = "/tmp/video_ppm_XXXXXX";
    int fd = mkstemp(path);
    PT_ASSERT(fd >= 0);
    close(fd);
    int written = video_write_frame_ppm(frame_buffer, path);

    static unsigned char bytes[sizeof TEST_PPM_HEADER + LCD_WIDTH * LCD_HEIGHT * 3u + 16u];
    size_t size = 0;
    int read = read_file(path, bytes, sizeof bytes, &size);
    remove(path);

    PT_ASSERT_EQ_INT(written, 0);
    PT_ASSERT_EQ_INT(read, 0);
    const size_t header = sizeof TEST_PPM_HEADER - 1u;
    PT_ASSERT(size == header + LCD_WIDTH * LCD_HEIGHT * 3u);
    PT_ASSERT_MEM_EQ(bytes, TEST_PPM_HEADER, header);
    const unsigned char expected[] = {255, 0, 0, 66, 132, 8, 0, 0, 0};
    PT_ASSERT_MEM_EQ(bytes + header, expected, sizeof expected);
    /* 8 -> (64|2)=66, 16 -> (128|4)=132 */
    const unsigned char row1[] = {8, 8, 0}; /* 1 -> (8|0)=8 */
    PT_ASSERT_MEM_EQ(bytes + header + LCD_WIDTH * 3u, row1, sizeof row1);
    const unsigned char last[] = {255, 255, 255};
    PT_ASSERT_MEM_EQ(bytes + size - 3u, last, sizeof last);
    const unsigned char before_last[] = {0, 0, 0};
    PT_ASSERT_MEM_EQ(bytes + size - 6u, before_last, sizeof before_last);
}

PTEST(ppm_to_unwritable_path_fails)
{
    PT_ASSERT(video_write_frame_ppm(frame_buffer, "/nonexistent-dir/x.ppm") != 0);
    PT_ASSERT(video_write_frame_ppm(NULL, "/tmp/x.ppm") != 0);
    PT_ASSERT(video_write_frame_ppm(frame_buffer, NULL) != 0);
}

PTEST_MAIN()
