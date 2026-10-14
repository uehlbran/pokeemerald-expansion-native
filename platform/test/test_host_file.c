/*
 * test_host_file.c: durable replacement, directories and the local time
 * (host_file.h), on real files in a fresh scratch directory per test
 * (host_file_fixture.h). A replacement leaves the new contents and no source;
 * a failed one leaves the old; a missing directory is created, once; the
 * local time follows TZ, past 2038 at both widths. What takes a file
 * descriptor or a POSIX permission (the syncs, the umask) is in
 * posix/test_host_file_posix.c.
 */
#include <errno.h>
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

PTEST_MAIN()
