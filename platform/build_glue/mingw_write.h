/*
 * mingw_write.h: force-included (-include, platform/Makefile) into
 * rtc_offset_store.c and crash_reboot.c in the Windows build only. They call
 * POSIX write(2) with a size_t count and open(2) with O_CLOEXEC, which MinGW's
 * headers do not match: write takes an unsigned int, and O_CLOEXEC does not
 * exist. This keeps -Wconversion on for both files.
 *
 * Temporary: task W5 moves rtc_offset_store's file writes behind host_file.h
 * and task W6 rewrites crash_reboot's report for Windows; whoever finishes
 * second deletes this file and the -include lines.
 */
#ifndef PLATFORM_MINGW_WRITE_H
#define PLATFORM_MINGW_WRITE_H

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

/* A count the C runtime cannot take is refused, not truncated. Both callers
 * write a few hundred bytes at most. */
static inline ssize_t mingw_write(int descriptor, const void *data, size_t length)
{
    if (length > UINT_MAX) {
        errno = EINVAL;
        return -1;
    }
    return write(descriptor, data, (unsigned int)length);
}

#define write(descriptor, data, length) mingw_write(descriptor, data, length)

/* O_CLOEXEC's two effects on Windows: the descriptor is not inherited by a
 * child process, and (these files are text) no newline translation. */
#define O_CLOEXEC (_O_NOINHERIT | _O_BINARY)

#endif /* PLATFORM_MINGW_WRITE_H */
