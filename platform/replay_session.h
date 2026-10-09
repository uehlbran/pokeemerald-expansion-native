/*
 * replay_session.h: a replay (replay.h) wired to the running game.
 *
 * Connects the driver to its sources: the facts (inspect.h), the keypad
 * (input_script_set), the screen (video_write_frame_ppm), and the frame loop
 * (frame_set_step_hook). main owns the options and the process; this module
 * owns the script and the driver. Not unit tested on its own: it is glue
 * between modules that are, and tests/smoke/opening.replay exercises it.
 */
#ifndef PLATFORM_REPLAY_SESSION_H
#define PLATFORM_REPLAY_SESSION_H

#include <stdint.h>

#include "replay.h"

/* Reads (with its `include` lines expanded, replay_include.h) and parses the script at `script_path`, starts the inspection and the
 * driver (skipping the commands up to `resume_after_line`), makes sure
 * `out_dir` exists, and installs the frame hook. Call after frame_init and
 * input_init(INPUT_SOURCE_SCRIPT). Returns 0, or -1 after printing the
 * reason (an unreadable or oversized file, a bad include, a parse error with its line). */
int replay_session_start(const char *script_path, const char *out_dir, uint32_t resume_after_line);

/* Removes the hook and releases the inspection. Safe before a start and twice. */
void replay_session_stop(void);

/* Where the driver is: REPLAY_RUNNING while the script has commands left
 * (also if the game ended another way). */
enum replay_status replay_session_status(void);

/* The failure message, or "" (see replay_message). */
const char *replay_session_message(void);

/* The line of the `restart` that ended the run. */
uint32_t replay_session_restart_line(void);

#endif /* PLATFORM_REPLAY_SESSION_H */
