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

PTEST_MAIN()
