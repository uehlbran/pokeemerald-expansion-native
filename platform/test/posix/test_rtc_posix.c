/*
 * Tests of the RTC's persistence that need a POSIX system: a directory the
 * process may enter and write but not open for reading, where the offset
 * file's replacement succeeds and the directory sync after it fails. A
 * process that skips permission checks (root) gets the same directory through
 * setfsuid(2), which changes who the file system sees without a privilege
 * the test needs to give back. The rest of the RTC's tests are in
 * ../test_rtc.c.
 */
// deps: rtc.c rtc_offset_store.c rtc_calendar.c
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/stat.h>
#include <unistd.h>

#include "host_file.h"
#include "ptest.h"
#include "rtc.h"
#include "rtc_offset_store.h"
#include "test_support.h"

#define PATH_SIZE 512
#define EPOCH_2024_03_15_13_45_30 1710510330LL
#define NOBODY_UID 65534 /* a user the scratch directory does not belong to */
/* Write and search, no read: rename works, opening the directory does not. */
#define ENTER_AND_WRITE_ONLY 0333
#define OWNER_ONLY 0700
#define OFFSET_TEXT_SIZE 32

static int64_t fake_source(void)
{
    return EPOCH_2024_03_15_13_45_30;
}

/* The scratch directory, with the permissions the tests below need. */
struct locked_directory {
    char dir[PATH_SIZE / 2];
    char path[PATH_SIZE]; /* the offset file in it */
    int previous_fsuid;
};

static void remove_directory(struct locked_directory *scratch)
{
    (void)setfsuid((uid_t)scratch->previous_fsuid);
    (void)chmod(scratch->dir, OWNER_ONLY);
    (void)tsup_remove_file(scratch->path);
    (void)snprintf(scratch->path, sizeof scratch->path, "%s/offset.tmp", scratch->dir);
    (void)tsup_remove_file(scratch->path);
    (void)tsup_remove_directory(scratch->dir);
}

static bool make_directory(struct locked_directory *scratch)
{
    if (!tsup_make_temp_directory(scratch->dir, sizeof scratch->dir, "ptest_rtc_posix"))
        return false;
    (void)snprintf(scratch->path, sizeof scratch->path, "%s/offset", scratch->dir);
    return true;
}

/* From here the directory cannot be opened for reading, whoever runs this. */
static bool lock_directory(struct locked_directory *scratch)
{
    if (chmod(scratch->dir, ENTER_AND_WRITE_ONLY) != 0)
        return false;
    scratch->previous_fsuid = setfsuid(NOBODY_UID);
    const int reader = open(scratch->dir, O_RDONLY | O_DIRECTORY);
    if (reader >= 0)
        (void)close(reader);
    return reader < 0;
}

static void read_offset_file(const char *path, char *text)
{
    FILE *const file = fopen(path, "r");
    const size_t length = file != NULL ? fread(text, 1, OFFSET_TEXT_SIZE - 1, file) : 0;
    text[length] = '\0';
    if (file != NULL)
        (void)fclose(file);
}

PTEST(the_store_reports_a_replaced_file_whose_directory_sync_failed)
{
    struct locked_directory scratch = {0};
    PT_ASSERT(make_directory(&scratch));
    PT_ASSERT_EQ_INT(rtc_offset_store_save(scratch.path, 3600), 0);
    const bool locked = lock_directory(&scratch);
    const int result = rtc_offset_store_save(scratch.path, 7200);
    remove_directory(&scratch); /* restores the fsuid and mode first */
    PT_ASSERT(locked);
    PT_ASSERT_EQ_INT(result, RTC_OFFSET_STORE_REPLACED_UNSYNCED);
}

PTEST(a_set_whose_directory_sync_failed_keeps_the_offset_the_file_has)
{
    struct locked_directory scratch = {0};
    struct rtc_datetime set = {.hour_bcd = 0x20, .minute_bcd = 0x15, .second_bcd = 0x00};
    struct rtc_datetime read = {0};
    char text[OFFSET_TEXT_SIZE];

    PT_ASSERT(make_directory(&scratch));
    PT_ASSERT_EQ_INT(rtc_init_utc(fake_source, scratch.path), 0);
    const bool locked = lock_directory(&scratch);
    const int set_result = rtc_set_time(&set);
    const int get_result = rtc_get_time(&read);
    read_offset_file(scratch.path, text);
    rtc_shutdown();
    remove_directory(&scratch);
    PT_ASSERT(locked);
    PT_ASSERT_EQ_INT(set_result, 0);
    PT_ASSERT_EQ_INT(get_result, 0);
    /* The clock shows the time that was set (20h, PM flag set), and the file holds the same offset. */
    PT_ASSERT_EQ_U32(read.hour_bcd, 0xA0);
    PT_ASSERT_EQ_U32(read.minute_bcd, 0x15);
    PT_ASSERT(strcmp(text, "23370\n") == 0); /* 20:15:00 - 13:45:30 UTC = 6h29m30s */
}

PTEST(a_first_run_whose_directory_sync_failed_starts_on_the_offset_the_file_has)
{
    struct locked_directory scratch = {0};
    struct rtc_datetime read = {0};
    char text[OFFSET_TEXT_SIZE];

    PT_ASSERT(make_directory(&scratch));
    const bool locked = lock_directory(&scratch);
    const int init_result = rtc_init_utc(fake_source, scratch.path);
    const int get_result = rtc_get_datetime(&read);
    read_offset_file(scratch.path, text);
    rtc_shutdown();
    remove_directory(&scratch);
    PT_ASSERT(locked);
    PT_ASSERT_EQ_INT(init_result, 0);
    PT_ASSERT_EQ_INT(get_result, 0);
    PT_ASSERT_EQ_U32(read.hour_bcd, 0x93); /* 13:45:30 UTC on the first-run seed; PM flag set in 24-hour mode */
    PT_ASSERT(strcmp(text, "0\n") == 0);
}

/* As ../test_rtc.c: the offset file's path and the ".tmp" beside it must fit
 * host_file.h's HOST_FILE_PATH_SIZE. */
#define TMP_SUFFIX_SIZE sizeof ".tmp"
#define LONGEST_PATH (HOST_FILE_PATH_SIZE - TMP_SUFFIX_SIZE)

/* POSIX only: repeated slashes pad a path to the limit and still name the
 * file, which Windows' file calls refuse past MAX_PATH (260 characters). */
PTEST(an_offset_path_just_under_the_limit_is_saved_and_loaded)
{
    static char path[HOST_FILE_PATH_SIZE];
    struct locked_directory scratch = {0};
    int64_t offset = 0;
    int found = 0;

    PT_ASSERT(make_directory(&scratch));
    /* Repeated slashes name the same file, so the directory the test owns
     * reaches the longest accepted path without nesting directories. */
    const size_t dir_length = strlen(scratch.dir);
    const char *const name = "offset";
    (void)snprintf(path, sizeof path, "%s", scratch.dir);
    memset(path + dir_length, '/', LONGEST_PATH - dir_length);
    memcpy(path + LONGEST_PATH - strlen(name), name, strlen(name) + 1);
    const int saved = rtc_offset_store_save(path, 4242);
    const int loaded = rtc_offset_store_load(path, &offset, &found);
    const bool by_short_name = tsup_exists(scratch.path); /* the same file, by its short name */
    (void)tsup_remove_file(scratch.path);
    (void)tsup_remove_directory(scratch.dir);
    PT_ASSERT_EQ_INT((int)strlen(path), (int)LONGEST_PATH);
    PT_ASSERT_EQ_INT(saved, 0);
    PT_ASSERT_EQ_INT(loaded, 0);
    PT_ASSERT_EQ_INT(found, 1);
    PT_ASSERT_EQ_INT((int)offset, 4242);
    PT_ASSERT(by_short_name);
}

PTEST_MAIN()
