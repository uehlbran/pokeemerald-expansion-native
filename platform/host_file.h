/*
 * host_file.h: durable file replacement, directories, and the local time.
 *
 * Stands in for the cartridge's battery: the save (save_file) and the clock's
 * offset (rtc_offset_store) survive power-off on the GBA, and natively each
 * is written to a temporary file that replaces the old one only once it is
 * on the disk. Also the small C library calls that differ between hosts:
 * opening a file the same way on every OS and writing all of a buffer to it
 * (rtc_offset_store; and stderr from a fault handler, crash_reboot and
 * arm_division), creating a directory (the replay's output, replay_session)
 * and the local time (the clock's first setting, rtc).
 *
 * Wraps, on Linux and the other POSIX systems (host_posix.c): open with
 * O_CLOEXEC, write, fsync, rename (atomic over an existing file), open of a
 * directory with fsync, mkdir with a mode, localtime_r. On Windows
 * (host_windows.c): the C runtime's _open in binary mode and not inherited,
 * WriteFile and FlushFileBuffers on a descriptor's handle, MoveFileExW with
 * MOVEFILE_REPLACE_EXISTING and MOVEFILE_WRITE_THROUGH, no directory sync
 * (the replacement's write-through is what makes it durable there),
 * CreateDirectoryW, _localtime64_s; paths in UTF-8, the code page the
 * executable's manifest gives the C runtime's own narrow calls. Every call returns -1 with errno set
 * on failure and reports nothing: the caller knows which file it was and
 * says so.
 */
#ifndef PLATFORM_HOST_FILE_H
#define PLATFORM_HOST_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The longest path host_file_sync_directory_of takes, its terminating NUL
 * included (Linux's PATH_MAX). */
#define HOST_FILE_PATH_SIZE 4096u

/* Opens path with open(2)'s flags (O_RDONLY; O_WRONLY | O_CREAT | O_TRUNC)
 * and what every file the platform keeps is opened with: not inherited by a
 * program this one starts (O_CLOEXEC; Windows _O_NOINHERIT) and, on Windows,
 * in binary mode, so that the bytes on the disk are the same on every OS. A
 * file it creates is readable by everyone and writable by its owner (0644,
 * less the umask; Windows: not read-only). Returns the descriptor, or -1
 * with errno set. */
int host_file_open(const char *path, int flags);

/* Writes the size bytes at bytes to the open descriptor, all of them,
 * retrying a short write (and on POSIX one interrupted by a signal). No
 * allocation, no lock, no stdio (write(2); Windows: WriteFile on the
 * descriptor's handle, past the C runtime's own lock), so that a fault
 * handler may call it. Returns 0, or -1 with errno set and part of the bytes
 * perhaps written. */
int host_file_write_all(int descriptor, const void *bytes, size_t size);

/* Writes what the open file descriptor holds through to the disk. Returns 0,
 * or -1 with errno set. */
int host_file_sync(int descriptor);

/* Replaces the file at path with the one at from, atomically (on Windows, on
 * an NTFS volume): path names the old file or the new one, never neither.
 * Returns 0, or -1 with errno set and both files as they were. */
int host_file_replace(const char *from, const char *path);

/* Makes a replacement durable: writes the directory that holds path through
 * to the disk ("/x" is in "/", "a/b/x" in "a/b", "x" in "."). On Windows only
 * the length is checked: no call writes a directory's entries through, and
 * host_file_replace's MOVEFILE_WRITE_THROUGH returns only once the rename is
 * on the disk. Returns 0, or -1 with errno set (ENAMETOOLONG for a path of
 * HOST_FILE_PATH_SIZE bytes or more). */
int host_file_sync_directory_of(const char *path);

/* Creates the directory path with every permission the process's umask
 * allows (Windows: the permissions it inherits from its parent directory).
 * Returns 0, or -1 with errno set (EEXIST if it exists). */
int host_file_make_directory(const char *path);

/* The host's local wall-clock time, in its time zone and daylight saving
 * rules, at seconds since the Unix epoch (64-bit, so past 2038 at both
 * widths; Windows' C runtime takes 1970 to 3000 only). Returns 0, or -1 with
 * errno set. */
int host_file_local_time(int64_t seconds, struct tm *out);

#endif /* PLATFORM_HOST_FILE_H */
