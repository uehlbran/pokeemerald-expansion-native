/*
 * replay.c: the driver that plays a parsed replay script (replay.h).
 *
 * Not hardware: it stands in for a person at the keypad and for the person
 * watching the screen. A state machine stepped once per frame: each step runs
 * commands until one has to wait (frames, a fact, the end of a press),
 * pushes the resulting key state to the host, and returns. Everything the
 * game or the platform provides comes through struct replay_host, so the
 * tests play scripts against a fake.
 */
#include "replay.h"
#include "printf_format.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* `press` holds the keys for this many frames, then releases them for as
 * many, so that consecutive presses are separate presses. */
#define REPLAY_PRESS_FRAMES 1u
#define REPLAY_PRESS_RELEASE_FRAMES 1u

static void fail(struct replay *replay, uint32_t line, const char *format, ...) PLATFORM_PRINTF_FORMAT(3, 4);

static void fail(struct replay *replay, uint32_t line, const char *format, ...)
{
    va_list arguments;
    int used = snprintf(replay->message, sizeof replay->message, "line %u: ", (unsigned)line);
    va_start(arguments, format);
    if (used > 0 && (size_t)used < sizeof replay->message)
        (void)vsnprintf(replay->message + used, sizeof replay->message - (size_t)used, format, arguments);
    va_end(arguments);
    replay->failed_line = line;
    replay->status = REPLAY_FAILED;
}

int replay_start(struct replay *replay, const struct replay_script *script, const struct replay_host *host,
                 const char *out_dir, uint32_t resume_after_line)
{
    *replay = (struct replay){.script = script, .status = REPLAY_RUNNING};
    if (host == NULL || host->get_fact == NULL || host->fact_name == NULL || host->screenshot == NULL
        || host->set_keys == NULL || out_dir == NULL || strlen(out_dir) >= sizeof replay->out_dir) {
        fail(replay, 0, "replay_start: incomplete host or bad output directory");
        return -1;
    }
    replay->host = *host;
    (void)snprintf(replay->out_dir, sizeof replay->out_dir, "%s", out_dir); /* fits: checked above */
    while (replay->next < script->count && script->commands[replay->next].line <= resume_after_line)
        replay->next++;
    return 0;
}

const char *replay_message(const struct replay *replay)
{
    return replay->status == REPLAY_FAILED ? replay->message : "";
}

uint32_t replay_failed_line(const struct replay *replay)
{
    return replay->status == REPLAY_FAILED ? replay->failed_line : 0;
}

uint32_t replay_restart_line(const struct replay *replay)
{
    return replay->restart_line;
}

uint32_t replay_ticks(const struct replay *replay)
{
    return replay->tick;
}

static bool path_for(struct replay *replay, const struct replay_command *command, const char *extension, char *path,
                     size_t size)
{
    int length = snprintf(path, size, "%s/%s%s", replay->out_dir, command->name, extension);
    if (length < 0 || (size_t)length >= size) {
        fail(replay, command->line, "the path for '%s' is too long", command->name);
        return false;
    }
    return true;
}

static void run_expect(struct replay *replay, const struct replay_command *command)
{
    char actual[REPLAY_FACT_BUFFER_SIZE];
    if (replay->host.get_fact(replay->host.context, command->fact, actual, sizeof actual) != 0) {
        fail(replay, command->line, "expect: no such fact '%s'", command->fact);
    } else if (strcmp(actual, command->value) != 0) {
        fail(replay, command->line, "expect %s=%s: the game has %s=%s (frame %u)", command->fact, command->value,
             command->fact, actual, (unsigned)replay->tick);
    }
}

/* True when the wait-until condition holds; a missing fact fails the run. */
static bool condition_holds(struct replay *replay, const struct replay_command *command)
{
    char actual[REPLAY_FACT_BUFFER_SIZE];
    if (replay->host.get_fact(replay->host.context, command->fact, actual, sizeof actual) != 0) {
        fail(replay, command->line, "wait-until: no such fact '%s'", command->fact);
        return false;
    }
    return strcmp(actual, command->value) == 0;
}

static void begin_wait_frames(struct replay *replay, uint32_t target)
{
    replay->wait = REPLAY_WAIT_FRAME;
    replay->wait_until = target;
}

static void fail_timeout(struct replay *replay, const struct replay_command *command)
{
    char actual[REPLAY_FACT_BUFFER_SIZE] = "";
    (void)replay->host.get_fact(replay->host.context, command->fact, actual, sizeof actual);
    fail(replay, command->line, "%s %s=%s timed out after %u frames; the game has %s=%s",
         command->kind == REPLAY_CMD_MASH ? "mash until" : "wait-until", command->fact, command->value,
         (unsigned)command->number, command->fact, actual);
}

/* The keys of a mash for the current frame: down for the first
 * REPLAY_MASH_DOWN_FRAMES of each cycle. */
static void mash_keys(struct replay *replay)
{
    const uint32_t cycle = REPLAY_MASH_DOWN_FRAMES + REPLAY_MASH_UP_FRAMES;
    const bool down = (replay->tick - replay->wait_started) % cycle < REPLAY_MASH_DOWN_FRAMES;
    replay->transient = down ? replay->waiting_command->keys : 0;
    replay->release_at = UINT32_MAX; /* the mash itself releases */
}

static void run_wait_until(struct replay *replay, const struct replay_command *command)
{
    if (condition_holds(replay, command) || replay->status == REPLAY_FAILED)
        return;
    if (command->number == 0) {
        fail_timeout(replay, command);
        return;
    }
    replay->wait = REPLAY_WAIT_FACT;
    replay->wait_until = replay->tick + command->number;
    replay->wait_started = replay->tick;
    replay->waiting_command = command;
    if (command->kind == REPLAY_CMD_MASH)
        mash_keys(replay);
}

/* A wait-until in progress: true if it is still waiting. */
static bool still_waiting_for_fact(struct replay *replay)
{
    const struct replay_command *command = replay->waiting_command;
    if (condition_holds(replay, command)) {
        replay->wait = REPLAY_WAIT_NONE;
        replay->transient = 0;
        if (command->kind == REPLAY_CMD_MASH) {
            /* the keys stay up one frame, so a press that follows is a new press */
            begin_wait_frames(replay, replay->tick + REPLAY_MASH_END_UP_FRAMES);
            return true;
        }
        return false;
    }
    if (replay->status == REPLAY_FAILED)
        return false;
    if (replay->tick >= replay->wait_until) {
        fail_timeout(replay, command);
        return false;
    }
    if (command->kind == REPLAY_CMD_MASH)
        mash_keys(replay);
    return true;
}

static void run_write(struct replay *replay, const struct replay_command *command)
{
    char path[REPLAY_PATH_SIZE];
    if (command->kind == REPLAY_CMD_SCREENSHOT) {
        if (path_for(replay, command, ".ppm", path, sizeof path)
            && replay->host.screenshot(replay->host.context, path) != 0)
            fail(replay, command->line, "screenshot: could not write %s", path);
        return;
    }
    if (!path_for(replay, command, ".txt", path, sizeof path))
        return;
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fail(replay, command->line, "dump: could not open %s", path);
        return;
    }
    char value[REPLAY_FACT_BUFFER_SIZE];
    for (size_t i = 0;; i++) {
        const char *name = replay->host.fact_name(replay->host.context, i);
        if (name == NULL)
            break;
        if (replay->host.get_fact(replay->host.context, name, value, sizeof value) != 0) {
            fail(replay, command->line, "dump: fact '%s' is listed but cannot be read", name);
            break;
        }
        (void)fprintf(file, "%s=%s\n", name, value);
    }
    const bool write_failed = ferror(file) != 0;
    if ((fclose(file) != 0 || write_failed) && replay->status != REPLAY_FAILED)
        fail(replay, command->line, "dump: could not write %s", path);
}

static void run_command(struct replay *replay, const struct replay_command *command)
{
    switch (command->kind) {
    case REPLAY_CMD_FRAME:
        if (command->number < replay->tick)
            fail(replay, command->line, "frame %u is in the past (now at frame %u)", (unsigned)command->number,
                 (unsigned)replay->tick);
        else
            begin_wait_frames(replay, command->number);
        break;
    case REPLAY_CMD_WAIT:
        begin_wait_frames(replay, replay->tick + command->number);
        break;
    case REPLAY_CMD_PRESS:
        replay->transient = command->keys;
        replay->release_at = replay->tick + REPLAY_PRESS_FRAMES;
        begin_wait_frames(replay, replay->release_at + REPLAY_PRESS_RELEASE_FRAMES);
        /* `times N`: run this command again after the release until N presses were made */
        if (replay->presses_left == 0)
            replay->presses_left = command->number;
        replay->presses_left--;
        if (replay->presses_left > 0)
            replay->next--;
        break;
    case REPLAY_CMD_HOLD:
        replay->held = (uint16_t)(replay->held | command->keys);
        break;
    case REPLAY_CMD_RELEASE:
        replay->held = (uint16_t)(replay->held & ~command->keys);
        break;
    case REPLAY_CMD_HOLD_FOR:
        replay->transient = command->keys;
        replay->release_at = replay->tick + command->number;
        begin_wait_frames(replay, replay->release_at);
        break;
    case REPLAY_CMD_WAIT_UNTIL:
    case REPLAY_CMD_MASH:
        run_wait_until(replay, command);
        break;
    case REPLAY_CMD_EXPECT:
        run_expect(replay, command);
        break;
    case REPLAY_CMD_SCREENSHOT:
    case REPLAY_CMD_DUMP:
        run_write(replay, command);
        break;
    case REPLAY_CMD_RESTART:
        replay->restart_line = command->line;
        replay->status = REPLAY_RESTART;
        break;
    }
}

static void push_keys(struct replay *replay)
{
    uint16_t keys = (uint16_t)(replay->held | replay->transient);
    if (replay->status != REPLAY_RUNNING)
        keys = 0;
    if (!replay->pushed_valid || keys != replay->pushed) {
        replay->host.set_keys(replay->host.context, keys);
        replay->pushed = keys;
        replay->pushed_valid = true;
    }
}

enum replay_status replay_step(struct replay *replay)
{
    if (replay->status != REPLAY_RUNNING)
        return replay->status;
    replay->tick++;
    if (replay->transient != 0 && replay->tick >= replay->release_at)
        replay->transient = 0;
    if (replay->wait == REPLAY_WAIT_FRAME && replay->tick >= replay->wait_until)
        replay->wait = REPLAY_WAIT_NONE;
    if (replay->wait == REPLAY_WAIT_FACT && !still_waiting_for_fact(replay) && replay->status == REPLAY_RUNNING)
        replay->wait = REPLAY_WAIT_NONE;

    while (replay->status == REPLAY_RUNNING && replay->wait == REPLAY_WAIT_NONE) {
        if (replay->next >= replay->script->count) {
            replay->status = REPLAY_DONE;
            break;
        }
        run_command(replay, &replay->script->commands[replay->next++]);
        /* A wait that is already satisfied (wait 0, frame now) must not
         * block until the next step. */
        if (replay->wait == REPLAY_WAIT_FRAME && replay->tick >= replay->wait_until)
            replay->wait = REPLAY_WAIT_NONE;
        if (replay->transient != 0 && replay->tick >= replay->release_at)
            replay->transient = 0;
    }
    push_keys(replay);
    return replay->status;
}
