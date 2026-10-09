/*
 * host_file.h: durable file replacement, directories, and the local time.
 *
 * Stands in for the cartridge's battery: the save (save_file) and the clock's
 * offset (rtc_offset_store) survive power-off on the GBA, and natively each
 * is written to a temporary file that replaces the old one only once it is
 * on the disk. Also the two small C library calls that differ between hosts:
 * creating a directory (the replay's output, replay_session) and the local
 * time (the clock's first setting, rtc).
 *
 * Wraps, on Linux and the other POSIX systems (host_posix.c): fsync, rename
 * (atomic over an existing file), open of a directory with fsync, mkdir with
 * a mode, localtime_r. Another OS adds its own source behind the same calls
 * (research/m8-inventory.md, 1.2 rows 12 to 14). Every call returns -1 with
 * errno set on failure and reports nothing: the caller knows which file it
 * was and says so.
 */
#ifndef PLATFORM_HOST_FILE_H
#define PLATFORM_HOST_FILE_H

#include <stdint.h>
#include <time.h>

/* The longest path host_file_sync_directory_of takes, its terminating NUL
 * included (Linux's PATH_MAX). */
#define HOST_FILE_PATH_SIZE 4096u

/* Writes what the open file descriptor holds through to the disk. Returns 0,
 * or -1 with errno set. */
int host_file_sync(int descriptor);

/* Replaces the file at path with the one at from, atomically: path names the
 * old file or the new one, never neither. Returns 0, or -1 with errno set
 * and both files as they were. */
int host_file_replace(const char *from, const char *path);

/* Makes a replacement durable: writes the directory that holds path through
 * to the disk ("/x" is in "/", "a/b/x" in "a/b", "x" in "."). Returns 0, or
 * -1 with errno set (ENAMETOOLONG for a path of HOST_FILE_PATH_SIZE bytes or
 * more). */
int host_file_sync_directory_of(const char *path);

/* Creates the directory path with every permission the process's umask
 * allows. Returns 0, or -1 with errno set (EEXIST if it exists). */
int host_file_make_directory(const char *path);

/* The host's local wall-clock time, in its time zone and daylight saving
 * rules, at seconds since the Unix epoch (64-bit, so past 2038 at both
 * widths). Returns 0, or -1 with errno set. */
int host_file_local_time(int64_t seconds, struct tm *out);

#endif /* PLATFORM_HOST_FILE_H */
