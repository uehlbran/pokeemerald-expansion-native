/*
 * options.c: the command line of the native executable.
 *
 * Not hardware: the host-side choices a player or a test script makes when
 * starting the process (a window or none, a save file's path, a replay),
 * which on a cartridge were the console's switches and the cartridge itself.
 * Split out of main.c, which starts the platform modules and runs the game;
 * this file only reads the arguments and checks them against each other, so
 * it starts nothing and is unit tested (test/test_options.c).
 *
 * Every option's value is validated here: numbers are decimal digits only and
 * within their range, a bad argument is named on stderr with the usage text.
 * The strings in the options (paths, the persistent state) point into argv,
 * which main keeps for the whole run.
 */
#include "options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crash_reboot.h"

void options_print_usage(FILE *out)
{
    fprintf(out,
            "usage: pokeemerald-native [options]\n"
            "  --headless         no window (input reads no keys); the frames still run\n"
            "  --silent           no audio device\n"
            "  --save PATH        the save file (default %s)\n"
            "  --rtc-offset PATH  the real-time clock's offset file (default %s)\n"
            "  --scale N          window scale, 1 to %d (default %d)\n"
            "  --unthrottled      do not hold the GBA frame rate\n"
            "  --frames N         exit after N frames (exit status 0)\n"
            "  --screenshot PATH  write the last frame to PATH as a PPM when the game ends\n"
            "  --test-runner      run expansion's test runner as mgba-rom-test does for its hydra:\n"
            "                     debug output on stdout, the runner's exit status, reboot on a\n"
            "                     crash; no save or clock file is read or written\n"
            "  --persistent-state HEX\n"
            "                     (added by the reboot) the runner's state to start from\n"
            "  --replay SCRIPT    play a replay script (tests/smoke/README.md): the keypad reads the script, not the\n"
            "                     keyboard; exit 0 when the script ends, 1 on the first failed command\n"
            "  --out DIR          where the replay's screenshots and dumps go (default %s)\n"
            "  --rtc-fixed EPOCH  the clock starts at EPOCH (Unix seconds) and advances with the frame count,\n"
            "                     so a replay sees the same time every run\n"
            "  --resume-at LINE   (a restart adds it) continue the replay after script line LINE\n"
            "  --help             this text\n",
            OPTIONS_DEFAULT_SAVE_PATH, OPTIONS_DEFAULT_RTC_OFFSET_PATH, VIDEO_MAX_SCALE, OPTIONS_DEFAULT_SCALE,
            OPTIONS_DEFAULT_OUT_DIR);
}

static bool parse_unsigned(const char *text, unsigned long max, unsigned long *out)
{
    char *end;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || value > max)
        return false;
    *out = value;
    return true;
}

/* A Unix time in seconds: decimal digits, at most INT64_MAX. */
static bool parse_epoch(const char *text, int64_t *out)
{
    char *end;
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return false;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || *end != '\0')
        return false;
    *out = value;
    return true;
}

/* Returns OPTIONS_OK, or OPTIONS_ERR_USAGE after printing what is wrong. */
enum options_error options_parse(int argc, char **argv, struct options *out)
{
    *out = (struct options){
        .scale = OPTIONS_DEFAULT_SCALE,
        .frame_limit = OPTIONS_DEFAULT_FRAME_LIMIT,
        .save_path = OPTIONS_DEFAULT_SAVE_PATH,
        .rtc_offset_path = OPTIONS_DEFAULT_RTC_OFFSET_PATH,
        .argc = argc,
        .argv = argv,
        .out_dir = OPTIONS_DEFAULT_OUT_DIR,
    };
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *value = i + 1 < argc ? argv[i + 1] : NULL;
        unsigned long number;
        if (strcmp(arg, "--headless") == 0) {
            out->headless = true;
        } else if (strcmp(arg, "--silent") == 0) {
            out->silent = true;
        } else if (strcmp(arg, "--unthrottled") == 0) {
            out->unthrottled = true;
        } else if (strcmp(arg, "--help") == 0) {
            out->help = true;
        } else if (strcmp(arg, "--test-runner") == 0) {
            out->test_runner = true;
        } else if (strcmp(arg, CRASH_REBOOT_OPTION) == 0 && value != NULL) {
            out->persistent_state = value;
            i++;
        } else if (strcmp(arg, "--save") == 0 && value != NULL) {
            out->save_path = value;
            i++;
        } else if (strcmp(arg, "--rtc-offset") == 0 && value != NULL) {
            out->rtc_offset_path = value;
            i++;
        } else if (strcmp(arg, "--screenshot") == 0 && value != NULL) {
            out->screenshot_path = value;
            i++;
        } else if (strcmp(arg, "--replay") == 0 && value != NULL) {
            out->replay_path = value;
            i++;
        } else if (strcmp(arg, "--out") == 0 && value != NULL) {
            out->out_dir = value;
            i++;
        } else if (strcmp(arg, "--rtc-fixed") == 0 && parse_epoch(value, &out->rtc_fixed_epoch)) {
            out->rtc_fixed = true;
            i++;
        } else if (strcmp(arg, "--resume-at") == 0 && parse_unsigned(value, UINT32_MAX, &number)) {
            out->resume_after_line = (uint32_t)number;
            i++;
        } else if (strcmp(arg, "--scale") == 0 && parse_unsigned(value, VIDEO_MAX_SCALE, &number) && number >= 1) {
            out->scale = (int)number;
            i++;
        } else if (strcmp(arg, "--frames") == 0 && parse_unsigned(value, UINT32_MAX, &number) && number >= 1) {
            out->frame_limit = (uint32_t)number;
            i++;
        } else {
            fprintf(stderr, "options: bad argument '%s'%s\n", arg, value == NULL ? "" : " (or its value)");
            options_print_usage(stderr);
            return OPTIONS_ERR_USAGE;
        }
    }
    if (out->persistent_state != NULL && !out->test_runner) {
        fprintf(stderr, "options: %s is for --test-runner only\n", CRASH_REBOOT_OPTION);
        return OPTIONS_ERR_USAGE;
    }
    if (out->test_runner) {
        out->save_path = NULL;
        out->rtc_offset_path = NULL;
    }
    return OPTIONS_OK;
}
