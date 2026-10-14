/*
 * test_host_file_posix.c: the parts of host_file.h that are POSIX: the
 * descriptor calls (the open with O_CLOEXEC and its mode, the write of a
 * whole buffer, the sync), the directory fsync(2) is applied to, and the
 * directory the umask shapes. The rest is in ../test_host_file.c; the scratch
 * directory is ../host_file_fixture.h's.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "host_file.h"
#include "host_file_fixture.h"
#include "ptest.h"

/* --- host_file_open and host_file_write_all ------------------------------------------ */

#define TEXT "offset 42\n"

PTEST(a_created_file_holds_what_was_written_readable_by_all_and_writable_by_its_owner)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const mode_t mask = umask(0); /* no mask, so that the mode is the call's own */
    const int descriptor = host_file_open(scratch.path, O_WRONLY | O_CREAT | O_TRUNC);
    (void)umask(mask);
    const int written = descriptor >= 0 ? host_file_write_all(descriptor, TEXT, sizeof TEXT - 1u) : -2;
    const int nothing = descriptor >= 0 ? host_file_write_all(descriptor, TEXT, 0) : -2;
    const int descriptor_flags = descriptor >= 0 ? fcntl(descriptor, F_GETFD) : -1;
    const bool closed = descriptor >= 0 && close(descriptor) == 0;
    struct stat info;
    const mode_t permissions = stat(scratch.path, &info) == 0 ? (info.st_mode & 0777u) : 0;
    const bool contents = strcmp(read_text(scratch.path), TEXT) == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(descriptor >= 0);
    PT_ASSERT_EQ_INT(written, 0);
    PT_ASSERT_EQ_INT(nothing, 0);
    PT_ASSERT(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0); /* not inherited across exec */
    PT_ASSERT(closed);
    PT_ASSERT_EQ_INT(permissions, 0644u);
    PT_ASSERT(contents);
    PT_ASSERT(removed);
}

PTEST(opening_truncates_with_o_trunc_and_reads_with_o_rdonly)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool old = write_text(scratch.path, "an older and longer text");
    const int writer = host_file_open(scratch.path, O_WRONLY | O_CREAT | O_TRUNC);
    const int written = writer >= 0 ? host_file_write_all(writer, "new", 3) : -2;
    const bool writer_closed = writer >= 0 && close(writer) == 0;
    const int reader = host_file_open(scratch.path, O_RDONLY);
    char bytes[16] = "";
    const ssize_t read_count = reader >= 0 ? read(reader, bytes, sizeof bytes - 1u) : -1;
    const int reader_flags = reader >= 0 ? fcntl(reader, F_GETFD) : -1;
    const bool reader_closed = reader >= 0 && close(reader) == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(old);
    PT_ASSERT_EQ_INT(written, 0);
    PT_ASSERT(writer_closed);
    PT_ASSERT_EQ_INT(read_count, 3);
    PT_ASSERT_EQ_STR(bytes, "new");
    PT_ASSERT(reader_flags >= 0 && (reader_flags & FD_CLOEXEC) != 0);
    PT_ASSERT(reader_closed);
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

PTEST(writing_to_a_bad_or_read_only_descriptor_fails)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int bad = host_file_write_all(-1, TEXT, sizeof TEXT - 1u);
    const int bad_error = errno;
    const bool made = write_text(scratch.path, "kept");
    const int reader = host_file_open(scratch.path, O_RDONLY);
    const int refused = reader >= 0 ? host_file_write_all(reader, TEXT, sizeof TEXT - 1u) : -2;
    const int refused_error = errno;
    const bool closed = reader >= 0 && close(reader) == 0;
    const bool kept = strcmp(read_text(scratch.path), "kept") == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(bad, -1);
    PT_ASSERT_EQ_INT(bad_error, EBADF);
    PT_ASSERT(made);
    PT_ASSERT_EQ_INT(refused, -1);
    PT_ASSERT_EQ_INT(refused_error, EBADF);
    PT_ASSERT(closed);
    PT_ASSERT(kept);
    PT_ASSERT(removed);
}

/* --- host_file_sync and host_file_sync_directory_of ------------------------------ */

PTEST(an_open_file_syncs_and_a_bad_descriptor_does_not)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int descriptor = open(scratch.path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    const bool written = descriptor >= 0 && write(descriptor, "data", 4) == 4;
    const int synced = descriptor >= 0 ? host_file_sync(descriptor) : -2;
    const bool closed = descriptor >= 0 && close(descriptor) == 0;
    const int bad = host_file_sync(-1);
    const int bad_error = errno;
    const bool contents = strcmp(read_text(scratch.path), "data") == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(synced, 0);
    PT_ASSERT(closed);
    PT_ASSERT_EQ_INT(bad, -1);
    PT_ASSERT_EQ_INT(bad_error, EBADF);
    PT_ASSERT(contents);
    PT_ASSERT(removed);
}

PTEST(the_directory_of_a_path_syncs_wherever_the_path_points)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int in_scratch = host_file_sync_directory_of(scratch.path); /* "dir/file": dir */
    const int bare = host_file_sync_directory_of("file");             /* the working directory */
    const int at_root = host_file_sync_directory_of("/file");         /* "/" */
    const int missing = host_file_sync_directory_of(scratch.missing);
    const int missing_error = errno;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(in_scratch, 0);
    PT_ASSERT_EQ_INT(bare, 0);
    PT_ASSERT_EQ_INT(at_root, 0);
    PT_ASSERT_EQ_INT(missing, -1);
    PT_ASSERT_EQ_INT(missing_error, ENOENT);
    PT_ASSERT(removed);
}

PTEST(a_path_too_long_for_the_directory_sync_is_refused)
{
    static char path[HOST_FILE_PATH_SIZE + 1u];
    memset(path, 'a', HOST_FILE_PATH_SIZE);
    path[HOST_FILE_PATH_SIZE] = '\0';
    PT_ASSERT_EQ_INT(host_file_sync_directory_of(path), -1);
    PT_ASSERT_EQ_INT(errno, ENAMETOOLONG);
}

/* --- host_file_make_directory ------------------------------------------------------ */

PTEST(a_missing_directory_is_created_with_what_the_umask_allows_once)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const mode_t mask = umask(0);
    (void)umask(mask);
    const int made = host_file_make_directory(scratch.made);
    struct stat info;
    const bool is_directory = stat(scratch.made, &info) == 0 && S_ISDIR(info.st_mode);
    const mode_t permissions = is_directory ? (info.st_mode & 0777u) : 0;
    const int again = host_file_make_directory(scratch.made);
    const int again_error = errno;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(made, 0);
    PT_ASSERT(is_directory);
    PT_ASSERT_EQ_INT(permissions, 0777u & ~mask);
    PT_ASSERT_EQ_INT(again, -1);
    PT_ASSERT_EQ_INT(again_error, EEXIST);
    PT_ASSERT(removed);
}

PTEST_MAIN()
