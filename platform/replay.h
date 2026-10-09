/*
 * replay.h: the input-replay script and the driver that plays it.
 *
 * Stands in for a person at the keypad, for the headless smoke test (gate d,
 * docs/PLAN.md "Milestone 3 design"). The script is text, one command per
 * line; `#` starts a comment that runs to the end of the line. Keys are a
 * comma list of A, B, START, SELECT, UP, DOWN, LEFT, RIGHT, L, R (any case).
 * Facts are the named values platform/inspect.h reads from the game.
 *
 *   frame N                    advance to absolute frame N (replay_step call N)
 *   wait N                     advance N frames
 *   press KEYS                 hold KEYS for one frame, release them for one
 *   press KEYS times N         N such presses in a row (the numeric entry of the debug menu)
 *   hold KEYS                  hold KEYS until released
 *   release KEYS               stop holding KEYS
 *   hold KEYS for N            hold KEYS for N frames, then release them
 *   wait-until FACT=VALUE max N  advance until the fact equals the value; fail
 *                              if that takes more than N frames
 *   expect FACT=VALUE          fail the run if the fact differs
 *   screenshot NAME            write NAME.ppm in the output directory
 *   dump NAME                  write every fact, as key=value lines, to NAME.txt
 *   mash KEYS until FACT=VALUE max N
 *                              press KEYS over and over (REPLAY_MASH_DOWN_FRAMES
 *                              down, REPLAY_MASH_UP_FRAMES up, so each cycle is a
 *                              new press, and held long enough to speed text up)
 *                              until the fact equals the value (then the keys
 *                              stay up one frame); fail after N frames. For dialogue, whose number of pages the
 *                              script should not have to count.
 *   restart                    end this process and start the game again from
 *                              a cold start with the same save, continuing
 *                              the script after this line (replay_restart_line).
 *                              The frame count starts again in the new process, so
 *                              a `frame` command after a `restart` is a parse error.
 *
 *   include FILE               replaced, before parsing, by FILE's lines (replay_include.h)
 *
 * A value is the rest of the line (wait-until: up to " max"), trimmed. NAME
 * is letters, digits, '_' and '-'. The driver is a state machine stepped
 * once per frame, so it is tested against a fake host (test_replay.c).
 * Every failure carries the script line number.
 *
 * Frames. replay_step is called once at the end of every frame; its n-th call
 * is "frame n". A key state set by call n is what the game reads during frame
 * n + 1. `press` therefore occupies two frames (down, up) so that two
 * presses in a row are two presses. `wait-until` and `expect` see the state
 * the game left at the end of the frame.
 */
#ifndef PLATFORM_REPLAY_H
#define PLATFORM_REPLAY_H

#include <stddef.h>
#include <stdint.h>

/* `mash`: a cycle is this many frames with the keys down, then this many up. */
#define REPLAY_MASH_DOWN_FRAMES 4u
#define REPLAY_MASH_UP_FRAMES 4u
/* A finished mash leaves the keys up this long, so the next press is new. */
#define REPLAY_MASH_END_UP_FRAMES 1u

/* Limits, each reported by the parser when exceeded. */
#define REPLAY_MAX_COMMANDS 2048u
#define REPLAY_MAX_LINE_LENGTH 511u
#define REPLAY_MAX_FACT_LENGTH 31u
#define REPLAY_MAX_VALUE_LENGTH 255u
#define REPLAY_MAX_NAME_LENGTH 47u
#define REPLAY_MAX_FRAMES 100000000u
#define REPLAY_MESSAGE_SIZE 256u
#define REPLAY_PATH_SIZE 512u
#define REPLAY_FACT_BUFFER_SIZE (REPLAY_MAX_VALUE_LENGTH + 1u)

enum replay_command_kind {
    REPLAY_CMD_FRAME,
    REPLAY_CMD_WAIT,
    REPLAY_CMD_PRESS,
    REPLAY_CMD_HOLD,
    REPLAY_CMD_RELEASE,
    REPLAY_CMD_HOLD_FOR,
    REPLAY_CMD_WAIT_UNTIL,
    REPLAY_CMD_MASH,
    REPLAY_CMD_EXPECT,
    REPLAY_CMD_SCREENSHOT,
    REPLAY_CMD_DUMP,
    REPLAY_CMD_RESTART
};

struct replay_command {
    enum replay_command_kind kind;
    uint32_t line;                              /* 1-based line in the script */
    uint32_t number;                            /* frame: N; wait: N; hold for: N; press: times N (at least 1); wait-until, mash: max N */
    uint16_t keys;                              /* INPUT_KEY_* bits, for press/hold/release/hold for */
    char fact[REPLAY_MAX_FACT_LENGTH + 1u];     /* expect, wait-until */
    char value[REPLAY_MAX_VALUE_LENGTH + 1u];   /* expect, wait-until */
    char name[REPLAY_MAX_NAME_LENGTH + 1u];     /* screenshot, dump */
};

struct replay_script {
    struct replay_command commands[REPLAY_MAX_COMMANDS];
    size_t count;
};

struct replay_parse_error {
    uint32_t line;
    char message[REPLAY_MESSAGE_SIZE];
};

/* Parses `text` (NUL-terminated) into `script`. Returns 0, or -1 with the
 * first error in `error` (line number and reason). */
int replay_parse(const char *text, struct replay_script *script, struct replay_parse_error *error);

/* As replay_parse for a file's bytes: `bytes` has `length` bytes and bytes[length]
 * is NUL. A NUL byte inside is an error (reported at its line), as it would
 * silently end the script. */
int replay_parse_buffer(const char *bytes, size_t length, struct replay_script *script,
                        struct replay_parse_error *error);

/* What the driver needs from the game and the platform. */
struct replay_host {
    void *context;
    /* Writes the fact's value to out (size bytes); 0, or -1 if there is no
     * such fact. */
    int (*get_fact)(void *context, const char *name, char *out, size_t size);
    /* The name of fact `index`, NULL past the last, for `dump`. */
    const char *(*fact_name)(void *context, size_t index);
    /* Writes the last frame as a PPM at path; 0, or -1. */
    int (*screenshot)(void *context, const char *path);
    /* Sets the keys the game reads next, INPUT_KEY_* bits. */
    void (*set_keys)(void *context, uint16_t keys);
};

enum replay_status {
    REPLAY_RUNNING,
    REPLAY_DONE,    /* the script ended */
    REPLAY_FAILED,  /* a command failed: replay_message */
    REPLAY_RESTART  /* a `restart` command ran: replay_restart_line */
};

enum replay_wait {
    REPLAY_WAIT_NONE,
    REPLAY_WAIT_FRAME, /* until replay_step call wait_until */
    REPLAY_WAIT_FACT   /* until the fact equals the value, or call wait_until passed (wait-until, mash) */
};

struct replay {
    const struct replay_script *script;
    struct replay_host host;
    char out_dir[REPLAY_PATH_SIZE];
    size_t next;
    uint32_t tick;
    enum replay_wait wait;
    uint32_t wait_until;
    uint32_t wait_started; /* the tick a mash began */
    uint32_t release_at;
    uint32_t presses_left; /* presses of a `press ... times N` still to come after the one in progress */
    uint16_t held;
    uint16_t transient;
    uint16_t pushed;
    int pushed_valid;
    enum replay_status status;
    uint32_t failed_line;
    uint32_t restart_line;
    char message[REPLAY_MESSAGE_SIZE];
    const struct replay_command *waiting_command;
};

/* Starts playing `script` (which must outlive the driver) from the first
 * command whose line is greater than `resume_after_line` (0: from the
 * start). out_dir is where screenshots and dumps go (copied). Returns 0, or
 * -1 if the arguments are unusable (reported in the message). */
int replay_start(struct replay *replay, const struct replay_script *script, const struct replay_host *host,
                 const char *out_dir, uint32_t resume_after_line);

/* Call once at the end of each frame. Runs commands until one waits, sets
 * the keys through the host, and returns the status; once it is not
 * REPLAY_RUNNING it stays so. */
enum replay_status replay_step(struct replay *replay);

/* The failure, as "line N: ..." (empty unless REPLAY_FAILED). */
const char *replay_message(const struct replay *replay);

/* The line of the `restart` that ended the run (REPLAY_RESTART). */
uint32_t replay_restart_line(const struct replay *replay);

/* Frames stepped so far. */
uint32_t replay_ticks(const struct replay *replay);

#endif /* PLATFORM_REPLAY_H */
