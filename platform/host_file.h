/*
 * host_file.h: durable file replacement, directories, and the local time.
 *
 * Stands in for the cartridge's battery: the save (save_file) and the clock's
 * offset (rtc_offset_store) survive power-off on the GBA, and natively each
 * is written to a temporary file that replaces the old one only once it is
 * on the disk. Also the small C library calls that differ between hosts:
 * opening a file the same way on every OS, reading it (the clock's offset,
 * rtc_offset_store; the player's settings, settings) and writing all of a
 * buffer to it (rtc_offset_store; and stderr from a fault handler,
 * crash_reboot and arm_division), moving back in it and closing it (the WAV
 * recording, audio_dump), the lock that keeps a second process off a save
 * (save_lock), creating a directory and asking whether one is there (the
 * replay's output, replay_session; the data directory, data_dir),
 * the local time (the clock's first setting, rtc), and the two questions of
 * a path's syntax a replay's includes ask (replay_include).
 *
 * Wraps, on POSIX (host_posix.c): open with O_CLOEXEC, read, write, lseek,
 * close, fsync, rename, fsync of the directory, flock, mkdir, stat,
 * localtime_r. On Windows
 * (host_windows.c): _open (binary, not inherited), WriteFile,
 * SetFilePointerEx, _close, FlushFileBuffers, MoveFileExW with REPLACE_EXISTING and WRITE_THROUGH
 * (which is what makes the replacement durable there: there is no directory
 * sync), LockFileEx and UnlockFileEx, CreateDirectoryW, GetFileAttributesW,
 * ReadFile, _localtime64_s, and
 * Windows' path syntax (Microsoft, "File path formats on Windows systems");
 * paths in UTF-8, the code page the executable's manifest sets. A failed
 * call returns -1 with errno set (the handler's write: false, errno as it
 * was) and reports nothing: the caller names the file.
 */
#ifndef PLATFORM_HOST_FILE_H
#define PLATFORM_HOST_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The longest path host_file_sync_directory_of takes, its terminating NUL
 * included (Linux's PATH_MAX). */
#define HOST_FILE_PATH_SIZE 4096u

/* Opens path with open(2)'s flags (O_RDONLY; O_WRONLY | O_CREAT | O_TRUNC;
 * O_RDWR | O_CREAT) and what every file the platform keeps is opened with:
 * not inherited by a program this one starts (O_CLOEXEC; Windows
 * _O_NOINHERIT) and, on Windows, in binary mode, so that the bytes on the
 * disk are the same on every OS. A file it creates is readable by everyone
 * and writable by its owner (0644, less the umask; Windows: not read-only).
 * Returns the descriptor, or -1 with errno set. */
int host_file_open(const char *path, int flags);

/* Reads up to size bytes from the open descriptor into buffer, from its
 * position, which moves past them. Fewer than size is not an error (a file's
 * end is near, or a pipe has no more yet): call again until it returns 0.
 * POSIX: read(2), retried when a signal interrupts it, at most PTRDIFF_MAX
 * bytes a call. Windows: ReadFile on the descriptor's handle, as
 * host_file_write_all writes through it (no text-mode translation, the
 * platform's files are binary), at most MAXDWORD bytes a call; the far end of
 * a pipe closing is the end. Returns the count read, 0 at the end of the file
 * and for a size of 0, or -1 with errno set (EBADF: the descriptor is not
 * open). */
ptrdiff_t host_file_read(int descriptor, void *buffer, size_t size);

/* Writes the size bytes at bytes to the open descriptor, all of them,
 * retrying a short write (and on POSIX one interrupted by a signal). No
 * allocation, no lock, no stdio (write(2); Windows: WriteFile on the
 * descriptor's handle, past the C runtime's own lock). Returns 0, or -1 with
 * errno set and part of the bytes perhaps written. A handler writes with
 * host_file_write_from_handler instead. */
int host_file_write_all(int descriptor, const void *bytes, size_t size);

/* An open file as a fault handler writes to it: the descriptor on POSIX, the
 * system's handle on Windows, resolved by host_file_for_handler before any
 * handler runs. */
typedef intptr_t host_handler_file;

/* What host_file_for_handler gives for a descriptor that is not open, and the
 * value to hold before a module has installed its handler. */
#define HOST_HANDLER_FILE_NONE ((host_handler_file) - 1)

/* The descriptor as host_file_write_from_handler takes it. Call it at install
 * time, never from a handler: on Windows it asks the C runtime for the
 * descriptor's handle, which sets errno if the descriptor is not open. Returns
 * HOST_HANDLER_FILE_NONE for a descriptor that is not open, and the write
 * then fails. */
host_handler_file host_file_for_handler(int descriptor);

/* host_file_write_all for a fault handler: the same writes, but errno is
 * left as it was, whatever happens, so that a handler neither changes what
 * the interrupted code sees in errno nor, on Windows, makes the C runtime
 * set it up on a thread that has not used it (msvcrt allocates a thread's
 * errno block on first use). POSIX: write(2) with errno saved and restored,
 * async-signal-safe. Windows: WriteFile on the handle and nothing else, so
 * that nothing of the C runtime runs in the handler. Returns true if every
 * byte was written. */
bool host_file_write_from_handler(host_handler_file file, const void *bytes, size_t size);

/* Moves the open descriptor's position to offset bytes from the start of the
 * file, where the next host_file_write_all writes (lseek with SEEK_SET;
 * Windows: SetFilePointerEx from FILE_BEGIN on the handle host_file_write_all
 * writes through, so the two agree). Returns 0, or -1 with errno set: EBADF
 * for a descriptor that is not open, EINVAL for an offset above INT64_MAX
 * (an off_t's range). */
int host_file_seek(int descriptor, uint64_t offset);

/* Closes the descriptor (close(2); Windows: _close, once the descriptor is
 * known to be open, since the C runtime may treat closing one that is not
 * as an invalid parameter). Returns 0, or -1 with errno set: EBADF for a
 * descriptor that is not open, or what the close reported (EIO for a write
 * the system had deferred); the descriptor is closed either way. */
int host_file_close(int descriptor);

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

/* Takes an exclusive lock on the file open at descriptor, without waiting:
 * one holder at a time among every open of the file, in this process (through
 * another descriptor) or another. The lock is the open file's, kept until
 * host_file_release_lock closes it, and the system drops it when the process
 * ends however it ends, a crash or a kill included; the descriptor is not
 * inherited by a program this one starts (host_file_open), so no other
 * program ever holds it. Returns 0, or -1 with errno set: EWOULDBLOCK if
 * another open of the file holds the lock, anything else for any other
 * failure (EBADF: the descriptor is not open).
 *
 * POSIX: flock(LOCK_EX | LOCK_NB) (BSD's, on Linux and macOS alike), whose
 * lock belongs to the open file description, so that closing some other
 * descriptor of the same file does not drop it, as it would drop fcntl's
 * record locks; advisory: it keeps out only those who take it too. Windows:
 * LockFileEx(LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY) over every
 * byte the file could have (ERROR_LOCK_VIOLATION is EWOULDBLOCK); mandatory
 * there, so a read or a write of the file through any other handle fails,
 * which nobody does to a lock file. */
int host_file_take_lock(int descriptor);

/* Drops the lock host_file_take_lock took on descriptor and closes it.
 * POSIX: close(2), which drops the lock with the last descriptor of the
 * open file. Windows: UnlockFileEx first, then the close; Microsoft
 * ("LockFileEx") documents the system's own unlocking of a closed file as
 * taking a time that "depends upon available system resources", and a
 * process that restarts must leave the lock free for the next run at once.
 * Returns 0, or -1 with errno set; the descriptor is closed either way. */
int host_file_release_lock(int descriptor);

/* Creates the directory path with every permission the process's umask
 * allows (Windows: the permissions it inherits from its parent directory).
 * Returns 0, or -1 with errno set (EEXIST if something exists at path, a
 * directory or not: host_file_is_directory tells which). */
int host_file_make_directory(const char *path);

/* True if path names a directory (a symbolic link to one too); false for a
 * file or anything else, for nothing there, and for a path that cannot be
 * examined. POSIX: stat(2). Windows: GetFileAttributesW. errno is not part
 * of the answer. */
bool host_file_is_directory(const char *path);

/* The host's local wall-clock time, in its time zone and daylight saving
 * rules, at seconds since the Unix epoch (64-bit, so past 2038 at both
 * widths; Windows' C runtime takes 1970 to 3000 only). Returns 0, or -1 with
 * errno set. */
int host_file_local_time(int64_t seconds, struct tm *out);

/* Where path's last component starts: just after its last directory
 * separator ('/'; Windows: '/' or '\\', or the colon of a drive, "C:name"),
 * or 0 if it has none ("a/b/c" gives 4, "c" 0). */
size_t host_file_name_offset(const char *path);

/* True if path names a file without the working directory's help: it starts
 * with '/' (Windows: or '\\', which also starts "\\\\server\\share", or with a
 * drive and a separator, "C:\\" or "C:/"; "C:name" goes from the drive's own
 * working directory, so it is not). */
bool host_file_path_is_absolute(const char *path);

#endif /* PLATFORM_HOST_FILE_H */
