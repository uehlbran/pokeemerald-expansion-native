/*
 * options.h: the command line of the native executable.
 *
 * Not hardware: the host-side choices made when the process starts (window
 * or none, the data directory and the save and clock file paths, replay
 * script, frame limit). main.c parses them once, before any module starts,
 * and hands the result to each module's start step; where a path the command
 * line does not give goes is data_dir.h's. The option list is
 * options_print_usage's text, and docs/RUNNING.md, docs/BUILDING.md and
 * tests/smoke/README.md use the same names.
 */
#ifndef PLATFORM_OPTIONS_H
#define PLATFORM_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* What an option takes when the command line does not give it (the save's
 * and the clock's paths: data_dir.h). The scale is also the settings
 * file's default (settings.h). */
#define OPTIONS_DEFAULT_SCALE 3        /* 720x480 */
#define OPTIONS_DEFAULT_FRAME_LIMIT 0u /* none */
#define OPTIONS_DEFAULT_OUT_DIR "."

/* The option that records the sound output; a restart leaves it out
 * (restart.h). */
#define OPTIONS_AUDIO_DUMP "--audio-dump"

/* The option that names the data directory (data_dir.h); a restart keeps it
 * (restart.h), so the restarted run finds the files the first one wrote. */
#define OPTIONS_DATA_DIR "--data-dir"

/* The largest --scale, the window's whole multiple of the GBA's 240x160:
 * 16 gives 3840x2560, beyond any display. video.h takes it as its own
 * bound. */
#define OPTIONS_MAX_SCALE 16

struct options {
    bool headless;
    bool silent;
    bool unthrottled;
    bool help;
    bool keys;    /* --keys: print the keys, the files and the version, and exit */
    bool version; /* --version: print the version and exit */
    bool test_runner;
    const char *persistent_state; /* the hex the crash reboot passes, or NULL */
    int argc;                     /* main's, for the crash reboot */
    char **argv;
    int scale;
    bool scale_given; /* --scale was given: it wins over the settings file's */
    uint32_t frame_limit;
    const char *data_dir;        /* --data-dir, or NULL: data_dir.h finds it; NULL in test-runner mode */
    const char *save_path;       /* --save, or NULL: in the data directory; NULL in test-runner mode */
    const char *rtc_offset_path; /* --rtc-offset, or NULL: in the data directory; NULL in test-runner mode */
    const char *screenshot_path;
    const char *replay_path; /* NULL: play normally */
    const char *out_dir;
    const char *audio_dump_path; /* NULL: the sound output is not recorded */
    uint32_t resume_after_line;  /* --resume-at, set by a restart */
    bool rtc_fixed;
    int64_t rtc_fixed_epoch;
};

enum options_error {
    OPTIONS_OK = 0,
    OPTIONS_ERR_USAGE = -1 /* an unknown option, a bad or missing value, or a bad combination */
};

/* Parses argv[1..argc-1] into *out, starting from the defaults. The strings
 * in *out point into argv. Returns OPTIONS_OK, or OPTIONS_ERR_USAGE after
 * printing what is wrong and the usage text to stderr; *out is then
 * unspecified. A persistent state without --test-runner is an error, and so
 * is --keys with it (that mode has no files to show); --test-runner clears
 * the data directory and the save and clock paths. */
enum options_error options_parse(int argc, char **argv, struct options *out);

/* The --data-dir the arguments give (the last, as options_parse takes it),
 * or NULL, found without a report: console.h opens its log before the
 * command line is parsed for real, and needs the directory to put it in. A
 * bad argument ends the scan; what came before it counts. */
const char *options_data_dir_given(int argc, char **argv);

/* The usage text, to `out`. */
void options_print_usage(FILE *out);

#endif /* PLATFORM_OPTIONS_H */
