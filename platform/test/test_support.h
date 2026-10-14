/*
 * test_support.h: what the unit tests share of the operating system, so that
 * a test of a module (save_file, rtc, frame, replay, ...) reads the same on
 * every target and holds no OS call of its own: scratch directories and
 * files under the temporary directory, the file queries a test asserts on,
 * the environment, and standard error sent elsewhere for a while.
 *
 * One implementation per OS family, in test/<family>/support.c (HOST_TEST_SUPPORT_DIR,
 * platform/host.mk): test/posix/support.c for Linux and macOS,
 * test/windows/support.c for Windows. What only some family can do (fork,
 * signals, pipes, file permissions; a child process on Windows) is not here
 * but in that family's tests and its own support header
 * (test/posix/posix_support.h, test/windows/windows_support.h).
 *
 * Every call returns what it did (true, or a size), never aborts, and leaves
 * the test to assert; a path is a NUL-terminated buffer of at least
 * TSUP_PATH_SIZE bytes.
 */
#ifndef PLATFORM_TEST_SUPPORT_H
#define PLATFORM_TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Room for any path a support call writes, its terminator included. */
#define TSUP_PATH_SIZE 512u

/* An object's address as an integer (memcpy, not a cast of the pointer). */
static inline uintptr_t tsup_address_of(const void *pointer)
{
    uintptr_t address = 0;
    memcpy(&address, &pointer, sizeof address);
    return address;
}

/* --- scratch files ---------------------------------------------------------- */

/* Creates a new, empty directory under $TMPDIR (else the system's temporary
 * directory) named <prefix>.<random>, and writes its path to path. */
bool tsup_make_temp_directory(char *path, size_t size, const char *prefix);

/* Creates a new, empty file the same way, closed. */
bool tsup_make_temp_file(char *path, size_t size, const char *prefix);

/* Creates a directory that only the user can use. False if it exists or its
 * parent does not. */
bool tsup_make_directory(const char *path);

/* Removes an empty directory. */
bool tsup_remove_directory(const char *path);

/* Removes a file; a file that is not there counts as removed. */
bool tsup_remove_file(const char *path);

/* Removes a directory and everything under it; a path that is not there counts as removed. */
bool tsup_remove_tree(const char *path);

/* True if something (a file or a directory) is at path. */
bool tsup_exists(const char *path);

/* The size of the file at path in bytes, or -1 if it cannot be read. */
intmax_t tsup_file_size(const char *path);

/* True if path is a directory with nothing in it. */
bool tsup_directory_is_empty(const char *path);

/* Runs body(context) with the working directory set to dir, then puts it
 * back. False if either change failed (body ran only if the first worked). */
bool tsup_in_directory(const char *dir, void (*body)(void *context), void *context);

/* --- the environment -------------------------------------------------------- */

/* Sets (replacing) or removes a variable of this process. */
bool tsup_set_environment(const char *name, const char *value);
bool tsup_unset_environment(const char *name);

/* --- time ------------------------------------------------------------------- */

/* Waits at least this long. */
void tsup_sleep_milliseconds(unsigned milliseconds);

/* A clock that only goes forward, in milliseconds, from an arbitrary start. */
uint64_t tsup_monotonic_milliseconds(void);

/* --- standard error --------------------------------------------------------- */

/* The path of the system's null device, which discards what is written to it
 * and reads empty. */
const char *tsup_null_device(void);

/* What tsup_redirect_stderr remembers to put standard error back. */
struct tsup_stderr {
    int saved; /* a copy of the original descriptor */
};

/* Sends standard error to the open stream to until tsup_restore_stderr (flushes
 * it first). False, with standard error unchanged, if it could not be done. */
bool tsup_redirect_stderr(struct tsup_stderr *saved, FILE *to);

/* Flushes standard error and puts it back as it was. False if either step failed. */
bool tsup_restore_stderr(struct tsup_stderr *saved);

/* Runs action(context) with standard error sent to a temporary file and copies
 * what it wrote into text (size bytes, NUL-terminated). True only if the
 * redirection, the restore and the read-back all worked and the text fit. */
static inline bool tsup_capture_stderr(void (*action)(void *context), void *context, char *text, size_t size)
{
    text[0] = '\0';
    FILE *const file = tmpfile();
    if (file == NULL)
        return false;
    struct tsup_stderr saved;
    if (!tsup_redirect_stderr(&saved, file)) {
        (void)fclose(file); /* nothing redirected; the caller sees the failure */
        return false;
    }
    action(context);
    bool captured = tsup_restore_stderr(&saved);
    if (captured && fseek(file, 0, SEEK_SET) == 0) {
        const size_t length = fread(text, 1, size - 1, file);
        text[length] = '\0';
        captured = !ferror(file) && length < size - 1;
    } else {
        captured = false;
    }
    return fclose(file) == 0 && captured;
}

#endif /* PLATFORM_TEST_SUPPORT_H */
