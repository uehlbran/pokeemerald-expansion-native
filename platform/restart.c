/*
 * restart.c: see restart.h.
 */
#include "restart.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The two arguments --resume-at LINE are always appended; room for them is
 * kept when the others are added. */
#define RESTART_APPENDED_ARGS 2u

static bool push(struct restart_args *out, size_t *count, char *argument)
{
    if (*count + RESTART_APPENDED_ARGS >= RESTART_MAX_ARGS)
        return false;
    out->argv[(*count)++] = argument;
    return true;
}

int restart_build_args(int argc, char *const *argv, uint32_t resume_line, int64_t new_epoch, struct restart_args *out)
{
    size_t count = 0;
    memset(out, 0, sizeof *out);
    memcpy(out->resume_option, RESTART_OPTION_RESUME_AT, sizeof out->resume_option);
    snprintf(out->resume_text, sizeof out->resume_text, "%" PRIu32, resume_line);
    snprintf(out->epoch_text, sizeof out->epoch_text, "%" PRId64, new_epoch);
    for (int i = 0; i < argc; i++) {
        const bool is_resume = i > 0 && strcmp(argv[i], RESTART_OPTION_RESUME_AT) == 0;
        const bool is_epoch = i > 0 && strcmp(argv[i], RESTART_OPTION_RTC_FIXED) == 0;
        if ((is_resume || is_epoch) && i + 1 >= argc) {
            fprintf(stderr, "restart: %s has no value\n", argv[i]);
            return -1;
        }
        if (is_resume) {
            i++; /* dropped with its value; the new one is appended below */
            continue;
        }
        bool pushed = push(out, &count, argv[i]);
        if (pushed && is_epoch)
            pushed = push(out, &count, out->epoch_text);
        if (!pushed) {
            fprintf(stderr, "restart: more than %u arguments\n", RESTART_MAX_ARGS);
            return -1;
        }
        if (is_epoch)
            i++;
    }
    out->argv[count++] = out->resume_option;
    out->argv[count++] = out->resume_text;
    out->argv[count] = NULL;
    return 0;
}

int restart_exec(const struct restart_args *args)
{
    fflush(NULL);
    execv(RESTART_EXE_PATH, args->argv);
    fprintf(stderr, "restart: execv(%s) failed: %s\n", RESTART_EXE_PATH, strerror(errno));
    return -1;
}
