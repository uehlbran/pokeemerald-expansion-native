/*
 * test_host_file_windows.c: the parts of host_file.h that are Windows' own
 * (host_windows.c): a file host_file_open makes is binary (a "\n" stays one
 * byte) and its handle is not inherited by a started process; the descriptor
 * calls refuse a bad descriptor and one not open for writing; the handler's
 * write leaves errno alone, whether it writes or fails; the directory
 * sync checks only the length; a directory is made once. The rest is in
 * ../test_host_file.c; the scratch directory is ../host_file_fixture.h's.
 */
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdbool.h>
#include <string.h>

#include <windows.h>

#include "host_file.h"
#include "host_file_fixture.h"
#include "ptest.h"

#define LINES "line\nline\n"

PTEST(a_created_file_is_binary_and_not_inherited)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int descriptor = host_file_open(scratch.path, O_WRONLY | O_CREAT | O_TRUNC);
    const int written = descriptor >= 0 ? host_file_write_all(descriptor, LINES, sizeof LINES - 1u) : -2;
    const int synced = descriptor >= 0 ? host_file_sync(descriptor) : -2;
    DWORD flags = HANDLE_FLAG_INHERIT;
    const bool queried = descriptor >= 0
                         && GetHandleInformation((HANDLE)_get_osfhandle(descriptor), &flags); /* the API's integer */
    const bool closed = descriptor >= 0 && _close(descriptor) == 0;
    const intmax_t size = tsup_file_size(scratch.path);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(descriptor >= 0);
    PT_ASSERT_EQ_INT(written, 0);
    PT_ASSERT_EQ_INT(synced, 0);
    PT_ASSERT(queried);
    PT_ASSERT((flags & HANDLE_FLAG_INHERIT) == 0);
    PT_ASSERT(closed);
    PT_ASSERT_EQ_INT(size, (intmax_t)(sizeof LINES - 1u)); /* no "\r" added */
    PT_ASSERT(removed);
}

PTEST(opening_a_missing_file_for_reading_fails)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int descriptor = host_file_open(scratch.path, O_RDONLY);
    const int error = errno;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(descriptor, -1);
    PT_ASSERT_EQ_INT(error, ENOENT);
    PT_ASSERT(removed);
}

PTEST(the_descriptor_calls_refuse_a_bad_or_read_only_descriptor)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int bad_write = host_file_write_all(-1, LINES, sizeof LINES - 1u);
    const int bad_write_error = errno;
    const int bad_sync = host_file_sync(-1);
    const int bad_sync_error = errno;
    const bool made = write_text(scratch.path, "kept");
    const int reader = host_file_open(scratch.path, O_RDONLY);
    const int refused = reader >= 0 ? host_file_write_all(reader, LINES, sizeof LINES - 1u) : -2;
    const bool closed = reader >= 0 && _close(reader) == 0;
    const bool kept = strcmp(read_text(scratch.path), "kept") == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(bad_write, -1);
    PT_ASSERT_EQ_INT(bad_write_error, EBADF);
    PT_ASSERT_EQ_INT(bad_sync, -1);
    PT_ASSERT_EQ_INT(bad_sync_error, EBADF);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(refused, -1);
    PT_ASSERT(closed);
    PT_ASSERT(kept);
    PT_ASSERT(removed);
}

/* A value errno does not take from a write, to see that the handler's write
 * leaves errno as it found it: it reads no error code and sets no errno. */
#define ERRNO_BEFORE EDOM

PTEST(a_handlers_write_writes_everything_and_leaves_errno_whether_it_writes_or_fails)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int writer = host_file_open(scratch.path, O_WRONLY | O_CREAT | O_TRUNC);
    errno = ERRNO_BEFORE;
    const bool written = writer >= 0 && host_file_write_from_handler(writer, LINES, sizeof LINES - 1u);
    const int error_after_writing = errno;
    const bool writer_closed = writer >= 0 && _close(writer) == 0;
    const intmax_t size = tsup_file_size(scratch.path);
    const int reader = host_file_open(scratch.path, O_RDONLY);
    errno = ERRNO_BEFORE;
    const bool refused = reader >= 0 && !host_file_write_from_handler(reader, LINES, sizeof LINES - 1u);
    const int error_after_failing = errno;
    const bool reader_closed = reader >= 0 && _close(reader) == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(error_after_writing, ERRNO_BEFORE);
    PT_ASSERT(writer_closed);
    PT_ASSERT_EQ_INT(size, (intmax_t)(sizeof LINES - 1u));
    PT_ASSERT(refused); /* WriteFile on a handle opened for reading */
    PT_ASSERT_EQ_INT(error_after_failing, ERRNO_BEFORE);
    PT_ASSERT(reader_closed);
    PT_ASSERT(removed);
}

PTEST(the_directory_sync_checks_the_length_only)
{
    static char path[HOST_FILE_PATH_SIZE + 1u];
    memset(path, 'a', HOST_FILE_PATH_SIZE);
    path[HOST_FILE_PATH_SIZE] = '\0';
    PT_ASSERT_EQ_INT(host_file_sync_directory_of("file"), 0);
    PT_ASSERT_EQ_INT(host_file_sync_directory_of(path), -1);
    PT_ASSERT_EQ_INT(errno, ENAMETOOLONG);
}

PTEST(a_missing_directory_is_created_once)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int made = host_file_make_directory(scratch.made);
    const DWORD attributes = GetFileAttributesA(scratch.made);
    const int again = host_file_make_directory(scratch.made);
    const int again_error = errno;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(made, 0);
    PT_ASSERT(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    PT_ASSERT_EQ_INT(again, -1);
    PT_ASSERT_EQ_INT(again_error, EEXIST);
    PT_ASSERT(removed);
}

PTEST_MAIN()
