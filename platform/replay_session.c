/*
 * replay_session.c: see replay_session.h.
 */
#include "replay_session.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "host_file.h"
#include "input.h"
#include "inspect.h"
#include "replay_include.h"
#include "video.h"

/* The driver reads facts into buffers of this size; inspect.h promises no fact is longer. */
_Static_assert(REPLAY_FACT_BUFFER_SIZE >= INSPECT_VALUE_SIZE, "a fact must fit the replay's buffer");

/* Where the expansion of a script with `include` lines is written, in the output directory. */
#define EXPANDED_SCRIPT_NAME "expanded.replay"

/* The screen at the moment a command failed, in the output directory. */
#define FAILURE_SCREENSHOT_NAME "failed.ppm"

static char session_out_dir[REPLAY_PATH_SIZE];
static struct replay_script script;
static struct replay driver;
/* Where each line of the (expanded) script came from, for messages. */
static struct replay_source_map source_map;
static char failure_text[REPLAY_SOURCE_WHERE_SIZE + REPLAY_MESSAGE_SIZE];

static int host_get_fact(void *context, const char *name, char *out, size_t size)
{
    (void)context;
    return inspect_get(name, out, size);
}

static const char *host_fact_name(void *context, size_t index)
{
    (void)context;
    return inspect_fact_name(index);
}

static int host_screenshot(void *context, const char *path)
{
    (void)context;
    return video_write_frame_ppm(frame_pixels(), path);
}

static void host_set_keys(void *context, uint16_t keys)
{
    (void)context;
    input_script_set(keys);
}

/* Best effort: a picture of what the script failed on is the quickest way to see why. */
static void write_failure_screenshot(void)
{
    char path[REPLAY_PATH_SIZE];
    int written = snprintf(path, sizeof path, "%s/%s", session_out_dir, FAILURE_SCREENSHOT_NAME);
    if (written > 0 && (size_t)written < sizeof path && video_write_frame_ppm(frame_pixels(), path) == 0)
        (void)fprintf(stderr, "replay: the screen at the failure is in %s\n", path);
}

/* The driver's "line N: what" with N, a line of the expansion, as the file and line it came from;
 * made when the command fails, while the map exists (modules are shut down before main prints it). */
static void make_failure_text(void)
{
    const char *message = replay_message(&driver);
    const char *reason = strstr(message, ": ");
    char where[REPLAY_SOURCE_WHERE_SIZE];
    if (reason == NULL)
        return;
    replay_source_map_describe(&source_map, replay_failed_line(&driver), where, sizeof where);
    (void)snprintf(failure_text, sizeof failure_text, "%s%s", where, reason);
}

/* The frame loop's hook: one driver step per frame. */
static enum frame_step_result step(void *context)
{
    (void)context;
    switch (replay_step(&driver)) {
    case REPLAY_RUNNING:
        return FRAME_STEP_CONTINUE;
    case REPLAY_DONE:
    case REPLAY_RESTART:
        return FRAME_STEP_STOP;
    case REPLAY_FAILED:
    default:
        make_failure_text();
        write_failure_screenshot();
        return FRAME_STEP_FAIL;
    }
}

static bool make_out_dir(const char *out_dir)
{
    if (host_file_make_directory(out_dir) != 0 && errno != EEXIST) {
        (void)fprintf(stderr, "replay: cannot create the output directory %s: %s\n", out_dir, strerror(errno));
        return false;
    }
    return true;
}

/* The script, includes expanded, in a NUL-terminated heap string the caller
 * frees, or NULL after printing why. When includes were expanded the result
 * is also written to out_dir/EXPANDED_SCRIPT_NAME (which must exist), because the
 * line numbers of messages and of `restart` are lines of the expansion. */
static char *read_text(const char *path, const char *out_dir, size_t *length_out)
{
    char *text;
    bool expanded;
    char message[REPLAY_INCLUDE_MESSAGE_SIZE];
    if (replay_include_expand(path, &text, length_out, &expanded, &source_map, message, sizeof message) != 0) {
        (void)fprintf(stderr, "replay: %s\n", message);
        return NULL;
    }
    if (expanded) {
        char copy_path[REPLAY_PATH_SIZE];
        FILE *copy = NULL;
        int written = snprintf(copy_path, sizeof copy_path, "%s/%s", out_dir, EXPANDED_SCRIPT_NAME);
        if (written > 0 && (size_t)written < sizeof copy_path)
            copy = fopen(copy_path, "wb");
        if (copy != NULL) {
            bool written_all = fwrite(text, 1, *length_out, copy) == *length_out;
            if (fclose(copy) != 0 || !written_all)
                (void)fprintf(stderr, "replay: could not write %s\n", copy_path);
        } else {
            (void)fprintf(stderr, "replay: could not write the expanded script to %s\n", out_dir);
        }
    }
    return text;
}

int replay_session_start(const char *script_path, const char *out_dir, uint32_t resume_after_line)
{
    struct replay_parse_error error;
    size_t length;
    if (snprintf(session_out_dir, sizeof session_out_dir, "%s", out_dir) >= (int)sizeof session_out_dir) {
        (void)fprintf(stderr, "replay: the output directory path is too long\n");
        return -1;
    }
    if (!make_out_dir(out_dir))
        return -1;
    char *text = read_text(script_path, out_dir, &length);
    if (text == NULL)
        return -1;
    int parsed = replay_parse_buffer(text, length, &script, &error);
    free(text);
    if (parsed != 0) {
        char where[REPLAY_SOURCE_WHERE_SIZE];
        replay_source_map_describe(&source_map, error.line, where, sizeof where);
        (void)fprintf(stderr, "replay: %s: %s\n", where, error.message);
        return -1; /* replay_session_stop releases the map */
    }
    (void)inspect_init(); /* reported; callbacks outside the known table then read "unknown:0x..." */
    const struct replay_host host = {
        .get_fact = host_get_fact,
        .fact_name = host_fact_name,
        .screenshot = host_screenshot,
        .set_keys = host_set_keys,
    };
    if (replay_start(&driver, &script, &host, out_dir, resume_after_line) != 0) {
        (void)fprintf(stderr, "replay: %s\n", replay_message(&driver));
        return -1;
    }
    frame_set_step_hook(step, NULL);
    return 0;
}

void replay_session_stop(void)
{
    replay_source_map_free(&source_map);
    frame_set_step_hook(NULL, NULL);
    inspect_shutdown();
}

enum replay_status replay_session_status(void)
{
    return driver.status;
}

const char *replay_session_message(void)
{
    return failure_text[0] != '\0' ? failure_text : replay_message(&driver);
}

uint32_t replay_session_restart_line(void)
{
    return replay_restart_line(&driver);
}
