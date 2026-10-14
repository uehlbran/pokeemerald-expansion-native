/*
 * restart.h: the replay's `restart`, a cold restart of the whole process.
 *
 * Stands in for the player switching the console off and on. The in-game
 * soft reset (A+B+Start+Select, bios SoftReset) only reinitialises RAM, so
 * it cannot show that the game comes up from a save file alone, which is
 * what "save, restart, continue" must prove. A restart ends the process the
 * way a player would (main shuts every module down, which flushes the save)
 * and execs the same executable with the same arguments, plus
 * `--resume-at LINE` so the script carries on after the `restart` line, and
 * with `--rtc-fixed` advanced by the time that has passed so the clock does
 * not run backwards. The save path is among the arguments, so the new
 * process loads the file the old one stored.
 */
#ifndef PLATFORM_RESTART_H
#define PLATFORM_RESTART_H

#include <stdint.h>

/* Arguments the restarted process gets, at most (the original ones plus the
 * two this module adds). */
#define RESTART_MAX_ARGS 64u
#define RESTART_NUMBER_SIZE 24u

#define RESTART_OPTION_RESUME_AT "--resume-at"
#define RESTART_OPTION_RTC_FIXED "--rtc-fixed"

struct restart_args {
    char *argv[RESTART_MAX_ARGS + 1u]; /* NULL-terminated; points into the original argv and the numbers below */
    char resume_option[sizeof RESTART_OPTION_RESUME_AT];
    char resume_text[RESTART_NUMBER_SIZE];
    char epoch_text[RESTART_NUMBER_SIZE];
};

/* The arguments of the restarted process: argv without any earlier
 * `--resume-at N`, with `--resume-at resume_line` appended, and, if argv has
 * `--rtc-fixed E`, that E replaced by new_epoch. The original strings are
 * not copied: argv must outlive `out`. Returns 0, or -1 (reported) if there
 * are too many arguments or a value is missing. */
int restart_build_args(int argc, char *const *argv, uint32_t resume_line, int64_t new_epoch, struct restart_args *out);

/* Replaces this process with a new run of the executable with `args`
 * (host_process_restart). Returns only on failure: -1, reported. Call it
 * after every module is shut down; the stdio buffers are flushed first. */
int restart_exec(const struct restart_args *args);

#endif /* PLATFORM_RESTART_H */
