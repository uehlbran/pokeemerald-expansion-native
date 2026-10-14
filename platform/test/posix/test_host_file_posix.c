/*
 * test_host_file_posix.c: the parts of host_file.h that are POSIX: the
 * descriptor calls (the open with O_CLOEXEC and its mode, the write of a
 * whole buffer, the sync), the directory fsync(2) is applied to, and the
 * directory the umask shapes. The rest is in ../test_host_file.c; the scratch
 * directory is ../host_file_fixture.h's.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host_file.h"
#include "host_file_fixture.h"
#include "posix_child.h"
#include "posix_support.h"
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

/* A write cut short and a write interrupted before it wrote anything: a child
 * writes through a pipe that a grandchild drains in slices. Before each slice
 * the grandchild, with the pipe full, signals the writer twice, pausing after
 * each (SIGUSR1, whose handler is installed without SA_RESTART): the first
 * signal ends a write(2) that has written part of its bytes, which returns
 * that count; the second ends the next write(2), blocked before it wrote
 * anything, which fails with EINTR. Every byte must arrive, in order. */
#define INTERRUPTED_SIZE (256u * 1024u)
#define SLICE_SIZE 4096u
#define PAUSE_MICROSECONDS 2000u
#define CHILD_WROTE_ALL 0
#define CHILD_WRITE_FAILED 1
#define CHILD_BYTES_WRONG 2
#define CHILD_NOT_INTERRUPTED 3
#define CHILD_SETUP_FAILED 4

static volatile sig_atomic_t interruptions;

static void count_interruption(int signal_number)
{
    (void)signal_number;
    interruptions++;
}

static uint8_t byte_at(size_t i)
{
    return (uint8_t)(i * 31u + i / 251u);
}

/* The grandchild: reads everything, a slice at a time, each slice after a
 * pause and a signal to the writer; ends 0 if every byte was right. */
static int drain_slowly(int descriptor, pid_t writer)
{
    static uint8_t slice[SLICE_SIZE];
    size_t total = 0;
    for (;;) {
        (void)usleep(PAUSE_MICROSECONDS);
        (void)kill(writer, SIGUSR1);
        (void)usleep(PAUSE_MICROSECONDS);
        (void)kill(writer, SIGUSR1);
        const ssize_t got = read(descriptor, slice, sizeof slice);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        for (ssize_t i = 0; i < got; i++) {
            if (slice[i] != byte_at(total + (size_t)i))
                return CHILD_BYTES_WRONG;
        }
        total += (size_t)got;
    }
    return total == INTERRUPTED_SIZE ? CHILD_WROTE_ALL : CHILD_BYTES_WRONG;
}

static void write_through_interruptions(void *context)
{
    (void)context;
    static uint8_t bytes[INTERRUPTED_SIZE];
    for (size_t i = 0; i < sizeof bytes; i++)
        bytes[i] = byte_at(i);
    struct sigaction interrupt;
    memset(&interrupt, 0, sizeof interrupt);
    interrupt.sa_handler = count_interruption; /* no SA_RESTART: a blocked write returns early */
    int fds[2];
    if (sigemptyset(&interrupt.sa_mask) != 0 || sigaction(SIGUSR1, &interrupt, NULL) != 0 || pipe(fds) != 0)
        _exit(CHILD_SETUP_FAILED);
    const pid_t writer = getpid();
    const pid_t reader = fork();
    if (reader < 0)
        _exit(CHILD_SETUP_FAILED);
    if (reader == 0) {
        (void)close(fds[1]);
        _exit(drain_slowly(fds[0], writer));
    }
    (void)close(fds[0]);
    const int written = host_file_write_all(fds[1], bytes, sizeof bytes);
    (void)close(fds[1]);
    int status = -1;
    pid_t waited;
    do /* the reader's last signals may interrupt the wait as well */
        waited = waitpid(reader, &status, 0);
    while (waited < 0 && errno == EINTR);
    const bool read_all = waited == reader && psup_exit_code(status) == CHILD_WROTE_ALL;
    if (written != 0)
        _exit(CHILD_WRITE_FAILED);
    if (!read_all)
        _exit(CHILD_BYTES_WRONG);
    _exit(interruptions > 0 ? CHILD_WROTE_ALL : CHILD_NOT_INTERRUPTED);
}

PTEST(a_write_cut_short_or_interrupted_by_a_signal_is_carried_on_to_the_end)
{
    PT_ASSERT_EQ_INT(psup_exit_code(psup_run_child(write_through_interruptions, NULL)), CHILD_WROTE_ALL);
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
