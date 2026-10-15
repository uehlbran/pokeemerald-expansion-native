/*
 * wait_left.c: wait_left.h.
 */
#include "wait_left.h"

#define NANOSECONDS_PER_SECOND 1000000000L

bool wait_left(const struct timespec *deadline, const struct timespec *now, struct timespec *left)
{
    struct timespec rest = {.tv_sec = deadline->tv_sec - now->tv_sec, .tv_nsec = deadline->tv_nsec - now->tv_nsec};
    if (rest.tv_nsec < 0) {
        rest.tv_sec--;
        rest.tv_nsec += NANOSECONDS_PER_SECOND;
    }
    if (rest.tv_sec < 0 || (rest.tv_sec == 0 && rest.tv_nsec == 0))
        return false;
    if (rest.tv_sec >= WAIT_LEFT_MAX_SECONDS) {
        rest.tv_sec = WAIT_LEFT_MAX_SECONDS;
        rest.tv_nsec = 0;
    }
    *left = rest;
    return true;
}
