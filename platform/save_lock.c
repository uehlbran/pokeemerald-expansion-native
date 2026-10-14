/*
 * save_lock.c: one running game per save file. Interface and design:
 * save_lock.h; the lock itself is host_file.h's (flock on POSIX, LockFileEx
 * on Windows). Module state: whether a lock is held, its descriptor, and the
 * lock file's path for the reports, a fixed buffer of HOST_FILE_PATH_SIZE.
 * Nothing is allocated.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "host_file.h"
#include "save_lock.h"

/* The longest save path whose lock file's path fits host_file.h's paths. */
#define LONGEST_SAVE_PATH (HOST_FILE_PATH_SIZE - sizeof SAVE_LOCK_SUFFIX)

static struct {
    bool held;
    int descriptor;
    char path[HOST_FILE_PATH_SIZE]; /* <save>.lock */
} lock;

/* "save_lock: <call>(<lock file>): <error>" on stderr. */
static void report(const char *call, int error)
{
    (void)fprintf(stderr, "save_lock: %s(%s): %s\n", call, lock.path,
                  strerror(error)); /* best-effort: this is the error report */
}

/* Fills lock.path for save_path; false if it does not fit. */
static bool set_path(const char *save_path)
{
    const size_t length = strlen(save_path);
    if (length > LONGEST_SAVE_PATH)
        return false;
    memcpy(lock.path, save_path, length);
    memcpy(lock.path + length, SAVE_LOCK_SUFFIX, sizeof SAVE_LOCK_SUFFIX);
    return true;
}

int save_lock_take(const char *save_path)
{
    save_lock_release();
    if (!set_path(save_path)) {
        (void)fprintf(stderr, "save_lock: the save path %s is too long for its lock file: at most %u bytes\n",
                      save_path, (unsigned)LONGEST_SAVE_PATH); /* best-effort */
        return SAVE_LOCK_ERR;
    }
    const int descriptor = host_file_open(lock.path, O_RDWR | O_CREAT);
    if (descriptor < 0) {
        report("open", errno);
        return SAVE_LOCK_ERR;
    }
    if (host_file_take_lock(descriptor) != 0) {
        const int error = errno;
        (void)close(descriptor); /* already failing, and holding no lock to release */
        if (error != EWOULDBLOCK) {
            report("flock", error);
            return SAVE_LOCK_ERR;
        }
        (void)fprintf(stderr,
                      "save_lock: another instance of the game holds the save %s (the lock on %s); not starting, "
                      "so that the two cannot write over each other's saves\n",
                      save_path, lock.path); /* best-effort */
        return SAVE_LOCK_HELD;
    }
    lock.descriptor = descriptor;
    lock.held = true;
    return SAVE_LOCK_OK;
}

void save_lock_release(void)
{
    if (!lock.held)
        return;
    lock.held = false;
    if (host_file_release_lock(lock.descriptor) != 0)
        report("close", errno);
}
