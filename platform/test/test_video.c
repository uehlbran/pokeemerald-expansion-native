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
    struct video_config cfg = { .headless = false, .scale = 2, .title = "video test" };
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
    int present = video_present(frame_buffer);
    int present_again = video_present(frame_buffer);
    bool keep_running = video_poll_events();
    video_shutdown();
    PT_ASSERT_EQ_INT(present, 0);
    PT_ASSERT_EQ_INT(present_again, 0);
    PT_ASSERT(keep_running);
}

PTEST(headless_presents_without_a_window)
{
    /* No driver set on purpose: headless must not touch SDL's video. */
    struct video_config cfg = { .headless = true, .scale = 3, .title = "headless" };
    PT_ASSERT_EQ_INT(video_init(&cfg), 0);
    int present = video_present(frame_buffer);
    bool keep_running = video_poll_events();
    video_shutdown();
    PT_ASSERT_EQ_INT(present, 0);
    PT_ASSERT(keep_running);
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
    struct video_config zero = { .headless = false, .scale = 0, .title = "t" };
    struct video_config huge = { .headless = false, .scale = VIDEO_MAX_SCALE + 1, .title = "t" };
    struct video_config no_title = { .headless = false, .scale = 1, .title = NULL };
    PT_ASSERT(video_init(&zero) != 0);
    video_shutdown();
    PT_ASSERT(video_init(&huge) != 0);
    video_shutdown();
    PT_ASSERT(video_init(&no_title) != 0);
    video_shutdown();
    PT_ASSERT(video_init(NULL) != 0);
    video_shutdown();
    /* Headless validates the scale too, so a bad config is never half accepted. */
    struct video_config headless_zero = { .headless = true, .scale = 0, .title = "t" };
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
    frame_buffer[0] = 0x001F;                     /* pure red */
    frame_buffer[1] = (uint16_t)((1u << 10) | (16u << 5) | 8u); /* b=1 g=16 r=8 */
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
    const unsigned char expected[] = { 255, 0, 0, 66, 132, 8, 0, 0, 0 };
    PT_ASSERT_MEM_EQ(bytes + header, expected, sizeof expected);
    /* 8 -> (64|2)=66, 16 -> (128|4)=132 */
}

PTEST(ppm_to_unwritable_path_fails)
{
    PT_ASSERT(video_write_frame_ppm(frame_buffer, "/nonexistent-dir/x.ppm") != 0);
    PT_ASSERT(video_write_frame_ppm(NULL, "/tmp/x.ppm") != 0);
    PT_ASSERT(video_write_frame_ppm(frame_buffer, NULL) != 0);
}

PTEST_MAIN()
