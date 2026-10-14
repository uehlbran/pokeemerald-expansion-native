/*
 * test_host_file.c: durable replacement, locks, directories and the local
 * time (host_file.h), on real files in a fresh scratch directory per test
 * (host_file_fixture.h). A replacement leaves the new contents and no source;
 * a failed one leaves the old; a lock keeps out every other open of its file
 * until it is released; a missing directory is created, once; the local time
 * follows TZ, past 2038 at both widths. What takes a file descriptor or a
 * POSIX permission (the syncs, the umask) is in posix/test_host_file_posix.c;
 * a lock held by another process, in the save lock's tests
 * (posix/test_save_lock_posix.c, windows/test_save_lock_windows.c).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "host_file.h"
#include "host_file_fixture.h"
#include "ptest.h"
#include "test_support.h"

/* --- host_file_replace ------------------------------------------------------------ */

PTEST(a_replacement_leaves_the_new_contents_and_no_source)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool written = write_text(scratch.path, "old") && write_text(scratch.from, "new");
    const int replaced = host_file_replace(scratch.from, scratch.path);
    const bool new_contents = strcmp(read_text(scratch.path), "new") == 0;
    const bool source_gone = !exists(scratch.from);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(replaced, 0);
    PT_ASSERT(new_contents);
    PT_ASSERT(source_gone);
    PT_ASSERT(removed);
}

PTEST(a_replacement_creates_a_missing_target)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool written = write_text(scratch.from, "new");
    const int replaced = host_file_replace(scratch.from, scratch.path);
    const bool new_contents = strcmp(read_text(scratch.path), "new") == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(replaced, 0);
    PT_ASSERT(new_contents);
    PT_ASSERT(removed);
}

PTEST(a_replacement_from_a_missing_file_fails_and_leaves_the_target)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const bool written = write_text(scratch.path, "old");
    const int replaced = host_file_replace(scratch.from, scratch.path);
    const int error = errno;
    const bool old_contents = strcmp(read_text(scratch.path), "old") == 0;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(written);
    PT_ASSERT_EQ_INT(replaced, -1);
    PT_ASSERT_EQ_INT(error, ENOENT);
    PT_ASSERT(old_contents);
    PT_ASSERT(removed);
}

/* --- host_file_take_lock and host_file_release_lock ---------------------------------- */

/* Two opens of one file in this process stand for two processes: the lock is
 * the open file's, not the process's. */
PTEST(a_lock_keeps_every_other_open_of_its_file_out_until_it_is_released)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int first = host_file_open(scratch.path, O_RDWR | O_CREAT);
    const int second = host_file_open(scratch.path, O_RDWR | O_CREAT);
    const int taken = host_file_take_lock(first);
    const int refused = host_file_take_lock(second);
    const int refused_error = errno;
    const int released = host_file_release_lock(first);
    const int taken_after = host_file_take_lock(second);
    const int released_after = host_file_release_lock(second);
    const bool kept = exists(scratch.path);
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT(first >= 0 && second >= 0);
    PT_ASSERT_EQ_INT(taken, 0);
    PT_ASSERT_EQ_INT(refused, -1);
    PT_ASSERT_EQ_INT(refused_error, EWOULDBLOCK);
    PT_ASSERT_EQ_INT(released, 0);
    PT_ASSERT_EQ_INT(taken_after, 0);
    PT_ASSERT_EQ_INT(released_after, 0);
    PT_ASSERT(kept); /* releasing closes the file, it does not remove it */
    PT_ASSERT(removed);
}

PTEST(a_lock_on_a_descriptor_that_is_not_open_fails_for_that_reason)
{
    PT_ASSERT_EQ_INT(host_file_take_lock(-1), -1);
    PT_ASSERT_EQ_INT(errno, EBADF); /* not EWOULDBLOCK: nobody holds anything */
}

PTEST(a_directory_under_a_missing_one_is_not_created)
{
    struct scratch scratch;
    if (!scratch_create(&scratch))
        PT_FAIL("cannot create a scratch directory");
    const int made = host_file_make_directory(scratch.missing);
    const int error = errno;
    const bool removed = scratch_remove(&scratch);
    PT_ASSERT_EQ_INT(made, -1);
    PT_ASSERT_EQ_INT(error, ENOENT);
    PT_ASSERT(removed);
}

/* --- host_file_local_time ------------------------------------------------------------ */

#define TM_YEAR_BASE 1900
#define SECONDS_AT_2100 INT64_C(4102444800) /* 2100-01-01 00:00:00 UTC */

static bool use_zone(const char *zone)
{
    if (!tsup_set_environment("TZ", zone))
        return false;
    tzset();
    return true;
}

PTEST(the_local_time_in_utc_is_the_calendar_time)
{
    struct tm epoch, later;
    PT_ASSERT(use_zone("UTC"));
    PT_ASSERT_EQ_INT(host_file_local_time(0, &epoch), 0);
    PT_ASSERT_EQ_INT(epoch.tm_year + TM_YEAR_BASE, 1970);
    PT_ASSERT_EQ_INT(epoch.tm_mon, 0);
    PT_ASSERT_EQ_INT(epoch.tm_mday, 1);
    PT_ASSERT_EQ_INT(epoch.tm_hour, 0);
    PT_ASSERT_EQ_INT(host_file_local_time(SECONDS_AT_2100, &later), 0); /* past 2038 at both widths */
    PT_ASSERT_EQ_INT(later.tm_year + TM_YEAR_BASE, 2100);
    PT_ASSERT_EQ_INT(later.tm_mon, 0);
    PT_ASSERT_EQ_INT(later.tm_mday, 1);
    PT_ASSERT_EQ_INT(later.tm_hour, 0);
    PT_ASSERT_EQ_INT(later.tm_sec, 0);
}

PTEST(the_local_time_follows_the_time_zone)
{
    struct tm east, west;
    const bool east_set = use_zone("JST-9");
    const int east_result = host_file_local_time(0, &east);
    const bool west_set = use_zone("EST5");
    const int west_result = host_file_local_time(0, &west);
    const bool reset = use_zone("UTC");
    PT_ASSERT(east_set && west_set && reset);
    PT_ASSERT_EQ_INT(east_result, 0);
    PT_ASSERT_EQ_INT(east.tm_hour, 9);
    PT_ASSERT_EQ_INT(east.tm_mday, 1);
    PT_ASSERT_EQ_INT(west_result, 0);
    PT_ASSERT_EQ_INT(west.tm_hour, 19);
    PT_ASSERT_EQ_INT(west.tm_mday, 31);
    PT_ASSERT_EQ_INT(west.tm_year + TM_YEAR_BASE, 1969);
}

PTEST(a_path_from_the_root_is_absolute_and_no_other)
{
    PT_ASSERT(host_file_path_is_absolute("/"));
    PT_ASSERT(host_file_path_is_absolute("/a/b.replay"));
    PT_ASSERT(!host_file_path_is_absolute("a/b.replay"));
    PT_ASSERT(!host_file_path_is_absolute("./b.replay"));
    PT_ASSERT(!host_file_path_is_absolute("b.replay"));
    PT_ASSERT(!host_file_path_is_absolute(""));
}

PTEST(the_name_starts_after_the_last_slash)
{
    PT_ASSERT(host_file_name_offset("a/b/c.replay") == 4u);
    PT_ASSERT(host_file_name_offset("/c.replay") == 1u);
    PT_ASSERT(host_file_name_offset("a/") == 2u);
    PT_ASSERT(host_file_name_offset("c.replay") == 0u);
    PT_ASSERT(host_file_name_offset("") == 0u);
}

PTEST_MAIN()
