/*
 * test_host_file.c: durable replacement, directories and the local time
 * (host_file.h), on real files in a fresh scratch directory per test
 * ($TMPDIR, or /tmp). A replacement leaves the new contents and no source;
 * a failed one leaves the old; a missing directory is created with the
 * permissions the umask allows, once; the local time follows TZ, past 2038
 * at both widths.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "host_file.h"
#include "ptest.h"

#define SCRATCH_PATH_SIZE 512u
#define TEXT_SIZE 64u

struct scratch {
    char dir[SCRATCH_PATH_SIZE / 2u];
    char path[SCRATCH_PATH_SIZE];    /* dir/file: the file replaced */
    char from[SCRATCH_PATH_SIZE];    /* dir/file.tmp: what replaces it */
    char made[SCRATCH_PATH_SIZE];    /* dir/made: a directory to create */
    char missing[SCRATCH_PATH_SIZE]; /* dir/missing/file: under no directory */
};

static bool scratch_create(struct scratch *scratch)
{
    const char *base = getenv("TMPDIR");
    (void)snprintf(scratch->dir, sizeof scratch->dir, "%s/ptest_host_file_XXXXXX", base != NULL ? base : "/tmp");
    if (mkdtemp(scratch->dir) == NULL)
        return false;
    (void)snprintf(scratch->path, sizeof scratch->path, "%s/file", scratch->dir);
    (void)snprintf(scratch->from, sizeof scratch->from, "%s/file.tmp", scratch->dir);
    (void)snprintf(scratch->made, sizeof scratch->made, "%s/made", scratch->dir);
    (void)snprintf(scratch->missing, sizeof scratch->missing, "%s/missing/file", scratch->dir);
    return true;
}

/* Removes what the tests may have left; true if the directory went. */
static bool scratch_remove(const struct scratch *scratch)
{
    (void)unlink(scratch->path);
    (void)unlink(scratch->from);
    (void)rmdir(scratch->made);
    return rmdir(scratch->dir) == 0;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
        return false;
    const bool written = fputs(text, file) != EOF;
    return fclose(file) == 0 && written;
}

/* The file's text, or "" if it cannot be read. */
static const char *read_text(const char *path)
{
    static char text[TEXT_SIZE];
    text[0] = '\0';
    FILE *file = fopen(path, "r");
    if (file != NULL) {
        text[fread(text, 1, sizeof text - 1u, file)] = '\0';
        (void)fclose(file);
    }
    return text;
}

static bool exists(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0;
}

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
    if (setenv("TZ", zone, 1) != 0)
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
