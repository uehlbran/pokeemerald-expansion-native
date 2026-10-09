/*
 * replay_session.c: see replay_session.h.
 */
#include "replay_session.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "frame.h"
#include "input.h"
#include "inspect.h"
#include "video.h"

/* The driver reads facts into buffers of this size; inspect.h promises no fact is longer. */
_Static_assert(REPLAY_FACT_BUFFER_SIZE >= INSPECT_VALUE_SIZE, "a fact must fit the replay's buffer");

static struct replay_script script;
static struct replay driver;

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
        return FRAME_STEP_FAIL;
    }
}

/* The whole file in a NUL-terminated heap string the caller frees, or NULL
 * after printing why. */
static char *read_text(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "replay: cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }
    char *text = malloc(REPLAY_SESSION_MAX_FILE_SIZE + 1u);
    size_t length = text != NULL ? fread(text, 1, REPLAY_SESSION_MAX_FILE_SIZE + 1u, file) : 0;
    bool failed = text == NULL || ferror(file) != 0 || length > REPLAY_SESSION_MAX_FILE_SIZE;
    fclose(file);
    if (failed) {
        fprintf(stderr, "replay: cannot read %s (or it is larger than %u bytes)\n", path,
                REPLAY_SESSION_MAX_FILE_SIZE);
        free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
}

static bool make_out_dir(const char *out_dir)
{
    if (mkdir(out_dir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "replay: cannot create the output directory %s: %s\n", out_dir, strerror(errno));
        return false;
    }
    return true;
}

int replay_session_start(const char *script_path, const char *out_dir, uint32_t resume_after_line)
{
    struct replay_parse_error error;
    char *text = read_text(script_path);
    if (text == NULL)
        return -1;
    int parsed = replay_parse(text, &script, &error);
    free(text);
    if (parsed != 0) {
        fprintf(stderr, "replay: %s:%u: %s\n", script_path, (unsigned)error.line, error.message);
        return -1;
    }
    if (!make_out_dir(out_dir))
        return -1;
    (void)inspect_init(); /* reported; callbacks outside the known table then read "unknown:0x..." */
    const struct replay_host host = {
        .get_fact = host_get_fact,
        .fact_name = host_fact_name,
        .screenshot = host_screenshot,
        .set_keys = host_set_keys,
    };
    if (replay_start(&driver, &script, &host, out_dir, resume_after_line) != 0) {
        fprintf(stderr, "replay: %s\n", replay_message(&driver));
        return -1;
    }
    frame_set_step_hook(step, NULL);
    return 0;
}

void replay_session_stop(void)
{
    frame_set_step_hook(NULL, NULL);
    inspect_shutdown();
}

enum replay_status replay_session_status(void)
{
    return driver.status;
}

const char *replay_session_message(void)
{
    return replay_message(&driver);
}

uint32_t replay_session_restart_line(void)
{
    return replay_restart_line(&driver);
}
