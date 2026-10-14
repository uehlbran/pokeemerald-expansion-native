// deps: rtc_offset_store.c rtc_calendar.c
/*
 * test_rtc.c: the S-3511A model (rtc.c), written from GBATEK "GBA Cart
 * Real-Time Clock (RTC)" and expansion's use of it (src/siirtc.c, src/rtc.c).
 * Time is injected; TZ is pinned so expected values are exact.
 */
#include "host_file.h"
#include "ptest.h"
#include "rtc.h"
#include "rtc_offset_store.h"
#include "test_support.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define EPOCH_2024_03_15_13_45_30 1710510330LL /* Friday */
#define EPOCH_2000_01_01 946684800LL           /* Saturday */
#define EPOCH_2099_12_31_235959 4102444799LL   /* Thursday */
#define EPOCH_2024_02_29_235959 1709251199LL
#define EPOCH_2023_02_28_235959 1677628799LL
#define EPOCH_2024_04_30_235959 1714521599LL
#define EPOCH_2024_12_31_235959 1735689599LL
#define EPOCH_2024_01_31_235959 1706745599LL
#define EPOCH_2100_02_28_235959 4107542399LL
#define EPOCH_2024_03_15_000000 1710460800LL
#define EPOCH_2024_03_15_120000 1710504000LL
#define EPOCH_2024_03_15_115959 1710503999LL
#define SECONDS_PER_HOUR 3600LL

#define PATH_SIZE 512

static int64_t g_now;

static int64_t fake_source(void)
{
    return g_now;
}

static char g_dir[PATH_SIZE / 2]; /* PATH_SIZE >= TSUP_PATH_SIZE */
static char g_path[PATH_SIZE];
static char g_tmp_path[PATH_SIZE];

static void remove_scratch(void)
{
    if (g_dir[0] == '\0')
        return;
    (void)tsup_remove_file(g_path);
    (void)tsup_remove_file(g_tmp_path);
    (void)tsup_remove_directory(g_tmp_path);
    (void)tsup_remove_directory(g_path);
    (void)tsup_remove_directory(g_dir);
    g_dir[0] = '\0';
}

/* Fresh scratch directory per test; the previous one is removed first. */
static void new_scratch(void)
{
    remove_scratch();
    if (!tsup_make_temp_directory(g_dir, sizeof g_dir, "ptest_rtc")) {
        perror("test_rtc: tsup_make_temp_directory");
        exit(2);
    }
    (void)snprintf(g_path, sizeof g_path, "%s/offset", g_dir);
    (void)snprintf(g_tmp_path, sizeof g_tmp_path, "%s/offset.tmp", g_dir);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f == NULL || fputs(text, f) == EOF || fclose(f) != 0) {
        perror("test_rtc: write_file");
        exit(2);
    }
}

static void read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    size_t n;
    if (f == NULL) {
        buf[0] = '\0';
        return;
    }
    n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    (void)fclose(f);
}

static int file_exists(const char *path)
{
    return tsup_exists(path);
}

static void pin_utc(void)
{
    PT_ASSERT(tsup_set_environment("TZ", "UTC"));
    tzset();
}

static void start_at(int64_t now)
{
    pin_utc();
    g_now = now;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, NULL), 0);
}

static void assert_dt(const struct rtc_datetime *d, uint8_t y, uint8_t mo, uint8_t da, uint8_t dow, uint8_t h,
                      uint8_t mi, uint8_t s)
{
    PT_ASSERT_EQ_U32(d->year_bcd, y);
    PT_ASSERT_EQ_U32(d->month_bcd, mo);
    PT_ASSERT_EQ_U32(d->day_bcd, da);
    PT_ASSERT_EQ_U32(d->day_of_week_bcd, dow);
    PT_ASSERT_EQ_U32(d->hour_bcd, h);
    PT_ASSERT_EQ_U32(d->minute_bcd, mi);
    PT_ASSERT_EQ_U32(d->second_bcd, s);
}

/* Reads the chip at `now` and checks every field. */
static void check_at(int64_t now, uint8_t y, uint8_t mo, uint8_t da, uint8_t dow, uint8_t h, uint8_t mi, uint8_t s)
{
    struct rtc_datetime d;
    start_at(now);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    rtc_shutdown();
    assert_dt(&d, y, mo, da, dow, h, mi, s);
}

/* ---- reading the time ------------------------------------------------- */

PTEST(known_epoch_gives_exact_bcd_fields)
{
    /* 2024-03-15 13:45:30 Friday; hour 13h with the PM flag (bit 7) set. */
    check_at(EPOCH_2024_03_15_13_45_30, 0x24, 0x03, 0x15, 0x05, 0x93, 0x45, 0x30);
}

PTEST(y2000_epoch_is_saturday_midnight)
{
    check_at(EPOCH_2000_01_01, 0x00, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00);
}

PTEST(pm_flag_is_set_from_noon_in_24_hour_mode)
{
    check_at(EPOCH_2024_03_15_115959, 0x24, 0x03, 0x15, 0x05, 0x11, 0x59, 0x59);
    check_at(EPOCH_2024_03_15_120000, 0x24, 0x03, 0x15, 0x05, 0x92, 0x00, 0x00);
    check_at(EPOCH_2024_03_15_000000, 0x24, 0x03, 0x15, 0x05, 0x00, 0x00, 0x00);
}

PTEST(twelve_hour_mode_encodes_hour_and_pm_flag)
{
    struct rtc_datetime d;
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_set_status(RTC_STATUS_INTME), 0); /* 24-hour bit clear */
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    PT_ASSERT_EQ_U32(d.hour_bcd, 0x81u); /* 13h is 01 PM */
    g_now = EPOCH_2024_03_15_120000;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    PT_ASSERT_EQ_U32(d.hour_bcd, 0x80u); /* 12h is 00 PM */
    g_now = EPOCH_2024_03_15_115959;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    PT_ASSERT_EQ_U32(d.hour_bcd, 0x11u); /* 11h is 11 AM */
    rtc_shutdown();
}

PTEST(midnight_rollover)
{
    struct rtc_datetime d;
    start_at(EPOCH_2024_03_15_000000 - 1);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    assert_dt(&d, 0x24, 0x03, 0x14, 0x04, 0xA3, 0x59, 0x59);
    g_now = EPOCH_2024_03_15_000000;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    assert_dt(&d, 0x24, 0x03, 0x15, 0x05, 0x00, 0x00, 0x00);
    rtc_shutdown();
}

PTEST(end_of_month_28_29_30_31)
{
    /* 28 days: Feb 2023 -> Mar 1 (Wednesday). */
    check_at(EPOCH_2023_02_28_235959 + 1, 0x23, 0x03, 0x01, 0x03, 0x00, 0x00, 0x00);
    check_at(EPOCH_2023_02_28_235959, 0x23, 0x02, 0x28, 0x02, 0xA3, 0x59, 0x59);
    /* 29 days: leap year 2024, Feb 29 exists. */
    check_at(EPOCH_2024_02_29_235959 - 86400, 0x24, 0x02, 0x28, 0x03, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2024_02_29_235959, 0x24, 0x02, 0x29, 0x04, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2024_02_29_235959 + 1, 0x24, 0x03, 0x01, 0x05, 0x00, 0x00, 0x00);
    /* 30 days: April. */
    check_at(EPOCH_2024_04_30_235959, 0x24, 0x04, 0x30, 0x02, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2024_04_30_235959 + 1, 0x24, 0x05, 0x01, 0x03, 0x00, 0x00, 0x00);
    /* 31 days: January. */
    check_at(EPOCH_2024_01_31_235959, 0x24, 0x01, 0x31, 0x03, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2024_01_31_235959 + 1, 0x24, 0x02, 0x01, 0x04, 0x00, 0x00, 0x00);
}

PTEST(new_year_rolls_year_bcd)
{
    check_at(EPOCH_2024_12_31_235959, 0x24, 0x12, 0x31, 0x02, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2024_12_31_235959 + 1, 0x25, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00);
}

PTEST(year_2099_wraps_to_2000_and_the_chip_treats_2100_as_leap)
{
    /* The chip has a two-digit year and no century: 99 -> 00. */
    check_at(EPOCH_2099_12_31_235959, 0x99, 0x12, 0x31, 0x04, 0xA3, 0x59, 0x59);
    check_at(EPOCH_2099_12_31_235959 + 1, 0x00, 0x01, 0x01, 0x05, 0x00, 0x00, 0x00);
    /* Year 00 divisible by 4: the chip has a Feb 29 in "2100" (a real 2100
     * does not); real 2100-02-28 23:59:59 + 1 s is the chip's 00-02-29. */
    check_at(EPOCH_2100_02_28_235959 + 1, 0x00, 0x02, 0x29, 0x01, 0x00, 0x00, 0x00);
    check_at(EPOCH_2100_02_28_235959 + 1 + 86400, 0x00, 0x03, 0x01, 0x02, 0x00, 0x00, 0x00);
}

PTEST(time_before_2000_wraps_into_2099)
{
    check_at(EPOCH_2000_01_01 - 1, 0x99, 0x12, 0x31, 0x05, 0xA3, 0x59, 0x59);
}

PTEST(get_time_reports_hour_minute_second_only)
{
    struct rtc_datetime d;
    memset(&d, 0xEE, sizeof d);
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_get_time(&d), 0);
    rtc_shutdown();
    PT_ASSERT_EQ_U32(d.hour_bcd, 0x93u);
    PT_ASSERT_EQ_U32(d.minute_bcd, 0x45u);
    PT_ASSERT_EQ_U32(d.second_bcd, 0x30u);
    PT_ASSERT_EQ_U32(d.year_bcd, 0xEEu);
    PT_ASSERT_EQ_U32(d.month_bcd, 0xEEu);
    PT_ASSERT_EQ_U32(d.day_bcd, 0xEEu);
    PT_ASSERT_EQ_U32(d.day_of_week_bcd, 0xEEu);
}

PTEST(first_run_chip_time_is_host_local_time)
{
    struct rtc_datetime d;
    /* POSIX TZ "XXX-2" is two hours east of UTC: no tz database needed. */
    PT_ASSERT(tsup_set_environment("TZ", "XXX-2"));
    tzset();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, NULL), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    rtc_shutdown();
    pin_utc();
    assert_dt(&d, 0x24, 0x03, 0x15, 0x05, 0x95, 0x45, 0x30); /* 15:45:30 */
}

PTEST(rtc_init_utc_first_run_ignores_the_host_zone)
{
    struct rtc_datetime d;
    PT_ASSERT(tsup_set_environment("TZ", "XXX-2"));
    tzset();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init_utc(fake_source, NULL), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    rtc_shutdown();
    pin_utc();
    assert_dt(&d, 0x24, 0x03, 0x15, 0x05, 0x93, 0x45, 0x30); /* 13:45:30 UTC (PM flag), not 15:45:30 */
}

PTEST(source_out_of_range_is_an_error)
{
    struct rtc_datetime d;
    pin_utc();
    g_now = INT64_MIN + 1;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, NULL), -1); /* first-run offset needs a valid reading */
    start_at(EPOCH_2000_01_01);
    g_now = INT64_MIN + 1;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), -1);
    g_now = INT64_MAX;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), -1);
    rtc_shutdown();
}

static void read_with_failed_source(void *context)
{
    g_now = RTC_TIME_SOURCE_FAILED;
    PT_ASSERT_EQ_INT(rtc_get_datetime(context), -1);
}

/* The source reports its own failure; the chip fails without a second,
 * misleading "out of range" report of the sentinel. */
PTEST(a_source_that_failed_is_an_error_reported_once_by_the_source)
{
    struct rtc_datetime d;
    char message[256];
    start_at(EPOCH_2000_01_01);
    PT_ASSERT(tsup_capture_stderr(read_with_failed_source, &d, message, sizeof message));
    rtc_shutdown();
    PT_ASSERT_EQ_STR(message, "");
}

/* ---- setting the time -------------------------------------------------- */

static struct rtc_datetime sample(void)
{
    struct rtc_datetime d = {0x31, 0x07, 0x04, 0x00, 0x16, 0x20, 0x05};
    return d;
}

PTEST(set_datetime_then_get_returns_the_set_time_and_it_keeps_running)
{
    struct rtc_datetime in = sample(), out;
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    /* 2031-07-04 is a Friday; 16h gets the PM flag. */
    assert_dt(&out, 0x31, 0x07, 0x04, 0x05, 0x96, 0x20, 0x05);
    g_now += 61;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x31, 0x07, 0x04, 0x05, 0x96, 0x21, 0x06);
    rtc_shutdown();
}

PTEST(set_datetime_persists_the_offset_in_seconds)
{
    struct rtc_datetime in = sample();
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    rtc_shutdown();
    /* 2031-07-04 16:20:05 UTC = 1940948405; minus 1710510330. */
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "230438075\n");
    PT_ASSERT(!file_exists(g_tmp_path));
}

PTEST(second_init_with_the_offset_file_continues_from_the_offset)
{
    struct rtc_datetime in = sample(), out;
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    rtc_shutdown();
    g_now += 120; /* the host clock moved on while the game was off */
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    rtc_shutdown();
    assert_dt(&out, 0x31, 0x07, 0x04, 0x05, 0x96, 0x22, 0x05);
}

PTEST(negative_offset_round_trips)
{
    struct rtc_datetime in = {0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00}, out;
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    rtc_shutdown();
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "-732203130\n"); /* 978307200 - 1710510330 */
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    rtc_shutdown();
    assert_dt(&out, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00);
}

PTEST(missing_offset_file_is_first_run)
{
    struct rtc_datetime out;
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    rtc_shutdown();
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x93, 0x45, 0x30);
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "0\n"); /* TZ=UTC: local minus UTC is 0, persisted on first run */
}

static void expect_first_run_offset(const char *tz, int64_t now, const char *expected_file_text)
{
    char text[64];
    new_scratch();
    PT_ASSERT(tsup_set_environment("TZ", tz));
    tzset();
    g_now = now;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    rtc_shutdown();
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, expected_file_text);
}

PTEST(first_run_offset_is_local_minus_utc_for_the_injected_instant)
{
    expect_first_run_offset("UTC+3", EPOCH_2024_03_15_13_45_30, "-10800\n");
    expect_first_run_offset("XXX-2", EPOCH_2024_03_15_13_45_30, "7200\n");
    /* A zone with DST: EST in January, EDT in July. */
    expect_first_run_offset("EST5EDT,M3.2.0,M11.1.0", 1705320000LL, "-18000\n");
    expect_first_run_offset("EST5EDT,M3.2.0,M11.1.0", 1719835200LL, "-14400\n");
    pin_utc();
}

PTEST(existing_offset_file_wins_over_the_host_zone)
{
    struct rtc_datetime out;
    new_scratch();
    write_file(g_path, "3600\n");
    PT_ASSERT(tsup_set_environment("TZ", "UTC+3"));
    tzset();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    rtc_shutdown();
    pin_utc();
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x94, 0x45, 0x30);
}

PTEST(dst_change_does_not_move_the_chip_time)
{
    struct rtc_datetime out;
    /* 2024-03-09 12:00 UTC is 07:00 EST; US DST begins on 03-10. */
    PT_ASSERT(tsup_set_environment("TZ", "EST5EDT,M3.2.0,M11.1.0"));
    tzset();
    g_now = 1709985600LL;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, NULL), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x09, 0x06, 0x07, 0x00, 0x00);
    g_now += INT64_C(2) * 86400; /* the host zone is now EDT; the chip must not follow */
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x11, 0x01, 0x07, 0x00, 0x00);
    rtc_shutdown();
    pin_utc();
}

PTEST(corrupt_offset_file_is_an_error)
{
    static const char *const bad[] = {
        "",
        "\n",
        "abc\n",
        "12abc\n",
        "12\n34\n",
        "12",
        "1 2\n",
        "-\n",
        "+5\n",
        "99999999999999999999999\n",
        "-99999999999999999999999\n",
        "0000000000000000000000000000005\n", /* 32 bytes, valid prefix: file may continue unseen */
        "5\n000000000000000000000000000000000000",
        "9223372036854775807\n",
        "1125899906842625\n", /* beyond +-2^50 */
        "123456789012345678901234567890123456789012345678901234567890\n",
    };
    size_t i;
    pin_utc();
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        new_scratch();
        write_file(g_path, bad[i]);
        g_now = EPOCH_2024_03_15_13_45_30;
        if (rtc_init(fake_source, g_path) != -1)
            PT_FAIL(bad[i]);
        {
            struct rtc_datetime d;
            PT_ASSERT_EQ_INT(rtc_get_datetime(&d), -1); /* failed init leaves it uninitialised */
        }
        rtc_shutdown();
    }
}

PTEST(unreadable_offset_path_is_an_error)
{
    new_scratch();
    pin_utc();
    PT_ASSERT(tsup_make_directory(g_path)); /* a directory where the file should be */
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), -1);
    rtc_shutdown();
}

PTEST(overlong_offset_path_is_an_error)
{
    static char path[8192];
    memset(path, 'a', sizeof path - 1);
    path[0] = '/';
    path[sizeof path - 1] = '\0';
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, path), -1);
    rtc_shutdown();
}

/* One limit for the offset file's path, the save file's: the path and the
 * ".tmp" beside it must fit host_file.h's HOST_FILE_PATH_SIZE. */
#define TMP_SUFFIX_SIZE sizeof ".tmp"
#define LONGEST_PATH (HOST_FILE_PATH_SIZE - TMP_SUFFIX_SIZE)

static void make_path_of_length(char *path, size_t length)
{
    memset(path, 'a', length);
    path[0] = '/';
    path[length] = '\0';
}

// cppcheck-suppress constParameterCallback ; tsup_capture_stderr's action takes a non-const void *
static void save_to_overlong_path(void *context)
{
    PT_ASSERT_EQ_INT(rtc_offset_store_save(context, 0), -1);
}

PTEST(the_offset_path_limit_is_the_host_file_limit_with_room_for_the_tmp_name)
{
    static char path[HOST_FILE_PATH_SIZE];
    static char message[256];
    char expected[64];

    make_path_of_length(path, LONGEST_PATH + 1);
    PT_ASSERT(tsup_capture_stderr(save_to_overlong_path, path, message, sizeof message));
    (void)snprintf(expected, sizeof expected, "path is longer than %llu bytes", (unsigned long long)LONGEST_PATH);
    PT_ASSERT(strstr(message, expected) != NULL);

    make_path_of_length(path, LONGEST_PATH); /* fits; it names no directory, so only the open fails */
    PT_ASSERT(tsup_capture_stderr(save_to_overlong_path, path, message, sizeof message));
    PT_ASSERT(strstr(message, "path is longer") == NULL);
}

PTEST(an_offset_path_just_under_the_limit_is_saved_and_loaded)
{
    static char path[HOST_FILE_PATH_SIZE];
    int64_t offset = 0;
    int found = 0;

    new_scratch();
    /* Repeated slashes name the same file, so the directory the test owns
     * reaches the longest accepted path without nesting directories. */
    const size_t dir_length = strlen(g_dir);
    const char *const name = "offset";
    memset(path, '/', LONGEST_PATH);
    memcpy(path, g_dir, dir_length);
    memcpy(path + LONGEST_PATH - strlen(name), name, strlen(name) + 1);
    PT_ASSERT_EQ_INT((int)strlen(path), (int)LONGEST_PATH);

    PT_ASSERT_EQ_INT(rtc_offset_store_save(path, 4242), 0);
    PT_ASSERT_EQ_INT(rtc_offset_store_load(path, &offset, &found), 0);
    PT_ASSERT_EQ_INT(found, 1);
    PT_ASSERT_EQ_INT((int)offset, 4242);
    PT_ASSERT(file_exists(g_path)); /* the same file, by its short name */
}

/* ---- invalid input ------------------------------------------------------ */

PTEST(invalid_bcd_and_ranges_are_rejected_without_changing_the_offset)
{
    static const struct rtc_datetime bad[] = {
        {0x1A, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00},                                             /* year digit > 9 */
        {0xA0, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00}, {0x24, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, /* month 0 */
        {0x24, 0x13, 0x01, 0x00, 0x00, 0x00, 0x00},                                             /* month 13 */
        {0x24, 0x0A, 0x01, 0x00, 0x00, 0x00, 0x00},                                             /* month not BCD */
        {0x24, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},                                             /* day 0 */
        {0x24, 0x01, 0x32, 0x00, 0x00, 0x00, 0x00},                                             /* day 32 */
        {0x24, 0x04, 0x31, 0x00, 0x00, 0x00, 0x00},                                             /* April 31 */
        {0x23, 0x02, 0x29, 0x00, 0x00, 0x00, 0x00}, /* Feb 29 in a common year */
        {0x24, 0x02, 0x30, 0x00, 0x00, 0x00, 0x00}, /* Feb 30 */
        {0x24, 0x01, 0x1F, 0x00, 0x00, 0x00, 0x00}, /* day not BCD */
        {0x24, 0x01, 0x01, 0x07, 0x00, 0x00, 0x00}, /* weekday 7 */
        {0x24, 0x01, 0x01, 0x0A, 0x00, 0x00, 0x00}, /* weekday not BCD */
        {0x24, 0x01, 0x01, 0x00, 0x24, 0x00, 0x00}, /* hour 24 */
        {0x24, 0x01, 0x01, 0x00, 0x1A, 0x00, 0x00}, /* hour not BCD */
        {0x24, 0x01, 0x01, 0x00, 0x40, 0x00, 0x00}, /* hour bit 6 */
        {0x24, 0x01, 0x01, 0x00, 0x00, 0x60, 0x00}, /* minute 60 */
        {0x24, 0x01, 0x01, 0x00, 0x00, 0x0F, 0x00}, /* minute not BCD */
        {0x24, 0x01, 0x01, 0x00, 0x00, 0x00, 0x60}, /* second 60 */
        {0x24, 0x01, 0x01, 0x00, 0x00, 0x00, 0x80}, /* test-mode bit */
    };
    size_t i;
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        struct rtc_datetime d = bad[i];
        char label[32];
        (void)snprintf(label, sizeof label, "bad datetime %zu", i);
        if (rtc_set_datetime(&d) != -1)
            PT_FAIL(label);
    }
    {
        struct rtc_datetime out;
        PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
        assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x93, 0x45, 0x30);
        char text[64];
        read_file(g_path, text, sizeof text);
        PT_ASSERT_EQ_STR(text, "0\n"); /* still the first-run file */
    }
    rtc_shutdown();
}

PTEST(set_datetime_accepts_leap_day_and_boundaries)
{
    static const struct rtc_datetime good[] = {
        {0x24, 0x02, 0x29, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x02, 0x29, 0x00, 0x00, 0x00, 0x00}, /* year 00 is leap on the chip */
        {0x99, 0x12, 0x31, 0x06, 0x23, 0x59, 0x59},
        {0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00},
    };
    size_t i;
    start_at(EPOCH_2024_03_15_13_45_30);
    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        struct rtc_datetime out;
        PT_ASSERT_EQ_INT(rtc_set_datetime(&good[i]), 0);
        PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
        PT_ASSERT_EQ_U32(out.year_bcd, good[i].year_bcd);
        PT_ASSERT_EQ_U32(out.month_bcd, good[i].month_bcd);
        PT_ASSERT_EQ_U32(out.day_bcd, good[i].day_bcd);
        PT_ASSERT_EQ_U32(out.minute_bcd, good[i].minute_bcd);
    }
    rtc_shutdown();
}

PTEST(set_datetime_in_12_hour_mode_takes_hour_and_pm_flag)
{
    struct rtc_datetime in = {0x24, 0x03, 0x15, 0x00, 0x81, 0x00, 0x00}, out;
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_set_status(0), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0); /* 01 PM */
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x81u);
    PT_ASSERT_EQ_INT(rtc_set_status(RTC_STATUS_24HOUR), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x93u); /* the same instant, 13h */
    in.hour_bcd = 0x12;                    /* 12 is not a valid 12-hour-mode hour (00..11) */
    PT_ASSERT_EQ_INT(rtc_set_status(0), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), -1);
    rtc_shutdown();
}

PTEST(set_datetime_in_24_hour_mode_ignores_the_read_only_pm_flag)
{
    struct rtc_datetime in = {0x24, 0x03, 0x15, 0x00, 0x03, 0x00, 0x00}, out;
    start_at(EPOCH_2024_03_15_13_45_30);
    in.hour_bcd = 0x83; /* PM flag set but hour 03: the flag is derived, not stored */
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x03u);
    rtc_shutdown();
}

PTEST(set_time_keeps_the_date)
{
    struct rtc_datetime in = {0, 0, 0, 0, 0x08, 0x30, 0x15}, out;
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_set_time(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x08, 0x30, 0x15);
    in.minute_bcd = 0x61;
    PT_ASSERT_EQ_INT(rtc_set_time(&in), -1);
    in.minute_bcd = 0x30;
    in.hour_bcd = 0x24;
    PT_ASSERT_EQ_INT(rtc_set_time(&in), -1);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x08, 0x30, 0x15);
    rtc_shutdown();
}

/* The date kept is the chip's, not UTC's: with a nonzero offset the two
 * differ near midnight. */
PTEST(set_time_keeps_the_chip_date_when_a_positive_offset_crosses_midnight)
{
    /* UTC 2024-03-15 23:30:00; the chip is set to 2024-03-16 00:30:00 (offset +3600). */
    struct rtc_datetime date = {0x24, 0x03, 0x16, 0, 0x00, 0x30, 0x00}, out;
    struct rtc_datetime time = {0, 0, 0, 0, 0x01, 0x15, 0x00};
    start_at(EPOCH_2024_03_15_000000 + 23 * SECONDS_PER_HOUR + 1800);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&date), 0);
    PT_ASSERT_EQ_INT(rtc_set_time(&time), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x16, 0x06, 0x01, 0x15, 0x00);
    rtc_shutdown();
}

PTEST(set_time_keeps_the_chip_date_when_a_negative_offset_crosses_midnight)
{
    /* UTC 2024-03-16 00:30:00; the chip is set to 2024-03-15 23:30:00 (offset -3600). */
    struct rtc_datetime date = {0x24, 0x03, 0x15, 0, 0x23, 0x30, 0x00}, out;
    struct rtc_datetime time = {0, 0, 0, 0, 0x22, 0x10, 0x00};
    start_at(EPOCH_2024_03_15_000000 + 24 * SECONDS_PER_HOUR + 1800);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&date), 0);
    PT_ASSERT_EQ_INT(rtc_set_time(&time), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0xA2, 0x10, 0x00); /* 22h with the PM flag */
    rtc_shutdown();
}

PTEST(set_time_persists_the_offset)
{
    struct rtc_datetime in = {0, 0, 0, 0, 0x14, 0x45, 0x30};
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_time(&in), 0);
    rtc_shutdown();
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "3600\n");
}

/* ---- status, reset, alarm ------------------------------------------------ */

PTEST(status_defaults_to_24_hour_with_power_flag_clear)
{
    uint8_t s = 0xFF;
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
    PT_ASSERT_EQ_U32(s, RTC_STATUS_24HOUR);
    rtc_shutdown();
}

PTEST(status_round_trips_the_writable_bits)
{
    static const uint8_t values[] = {
        0,
        RTC_STATUS_24HOUR,
        RTC_STATUS_INTFE,
        RTC_STATUS_INTME,
        RTC_STATUS_INTAE,
        RTC_STATUS_INTFE | RTC_STATUS_INTME | RTC_STATUS_INTAE | RTC_STATUS_24HOUR,
    };
    size_t i;
    start_at(EPOCH_2000_01_01);
    for (i = 0; i < sizeof values / sizeof values[0]; i++) {
        uint8_t s = 0xFF;
        PT_ASSERT_EQ_INT(rtc_set_status(values[i]), 0);
        PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
        PT_ASSERT_EQ_U32(s, values[i]);
    }
    rtc_shutdown();
}

PTEST(status_power_bit_is_read_only_and_other_bits_are_rejected)
{
    uint8_t s;
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_set_status(RTC_STATUS_24HOUR | RTC_STATUS_POWER), 0);
    PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
    PT_ASSERT_EQ_U32(s, RTC_STATUS_24HOUR);
    PT_ASSERT_EQ_INT(rtc_set_status(0x08), -1);
    PT_ASSERT_EQ_INT(rtc_set_status(0x10), -1);
    PT_ASSERT_EQ_INT(rtc_set_status(0x20), -1);
    PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
    PT_ASSERT_EQ_U32(s, RTC_STATUS_24HOUR); /* unchanged by the rejected writes */
    rtc_shutdown();
}

PTEST(reset_clears_status_and_sets_the_time_to_2000_01_01)
{
    struct rtc_datetime out;
    uint8_t s;
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_status(RTC_STATUS_24HOUR | RTC_STATUS_INTAE), 0);
    PT_ASSERT_EQ_INT(rtc_reset(), 0);
    PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
    PT_ASSERT_EQ_U32(s, 0u); /* GBATEK: setting after Force-Reset is 00h */
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x00, 0x01, 0x01, 0x06, 0x00, 0x00, 0x00);
    g_now += 5;
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    PT_ASSERT_EQ_U32(out.second_bcd, 0x05u);
    rtc_shutdown();
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "-763825530\n"); /* 946684800 - 1710510330 */
}

PTEST(alarm_is_validated_and_stored)
{
    struct rtc_datetime in = {0, 0, 0, 0, 0x17, 0x30, 0}, out;
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x97u); /* PM flag derived from the hour */
    PT_ASSERT_EQ_U32(out.minute_bcd, 0x30u);
    /* The PM flag is set from hour 12 on, not from hour 13. */
    in.hour_bcd = 0x11;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x11u);
    in.hour_bcd = 0x12;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x92u);
    in.hour_bcd = 0x00;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x00u);
    in.hour_bcd = 0x17;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), 0);
    in.hour_bcd = 0x24;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), -1);
    in.hour_bcd = 0x05;
    in.minute_bcd = 0x7A;
    PT_ASSERT_EQ_INT(rtc_set_alarm(&in), -1);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&out), 0);
    PT_ASSERT_EQ_U32(out.hour_bcd, 0x97u); /* unchanged by the rejected write */
    rtc_shutdown();
}

/* ---- lifecycle ------------------------------------------------------------ */

PTEST(everything_fails_before_init)
{
    struct rtc_datetime d = sample();
    uint8_t s;
    rtc_shutdown();
    PT_ASSERT_EQ_INT(rtc_get_status(&s), -1);
    PT_ASSERT_EQ_INT(rtc_set_status(0), -1);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), -1);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&d), -1);
    PT_ASSERT_EQ_INT(rtc_get_time(&d), -1);
    PT_ASSERT_EQ_INT(rtc_set_time(&d), -1);
    PT_ASSERT_EQ_INT(rtc_reset(), -1);
    PT_ASSERT_EQ_INT(rtc_set_alarm(&d), -1);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&d), -1);
}

PTEST(everything_fails_after_shutdown)
{
    struct rtc_datetime d;
    start_at(EPOCH_2000_01_01);
    rtc_shutdown();
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), -1);
    rtc_shutdown(); /* twice is fine */
}

PTEST(shutdown_without_init_is_safe)
{
    rtc_shutdown();
    rtc_shutdown();
}

PTEST(null_pointers_are_errors)
{
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_get_status(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_get_datetime(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_set_datetime(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_get_time(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_set_time(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_set_alarm(NULL), -1);
    PT_ASSERT_EQ_INT(rtc_get_alarm(NULL), -1);
    rtc_shutdown();
}

PTEST(init_restores_the_default_status_and_clears_the_alarm)
{
    uint8_t s;
    struct rtc_datetime alarm = {0, 0, 0, 0, 0x17, 0x30, 0};
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_set_status(RTC_STATUS_INTFE), 0);
    PT_ASSERT_EQ_INT(rtc_set_alarm(&alarm), 0);
    rtc_shutdown();
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_get_status(&s), 0);
    PT_ASSERT_EQ_U32(s, RTC_STATUS_24HOUR);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&alarm), 0);
    assert_dt(&alarm, 0, 0, 0, 0, 0, 0, 0);
    rtc_shutdown();
}

PTEST(reset_clears_the_alarm)
{
    struct rtc_datetime alarm = {0, 0, 0, 0, 0x17, 0x30, 0};
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_set_alarm(&alarm), 0);
    PT_ASSERT_EQ_INT(rtc_reset(), 0);
    PT_ASSERT_EQ_INT(rtc_get_alarm(&alarm), 0);
    assert_dt(&alarm, 0, 0, 0, 0, 0, 0, 0);
    rtc_shutdown();
}

PTEST(alarm_hour_bit_6_is_rejected)
{
    struct rtc_datetime alarm = {0, 0, 0, 0, 0x40, 0x00, 0};
    start_at(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(rtc_set_alarm(&alarm), -1);
    rtc_shutdown();
}

PTEST(save_over_a_longer_stale_temporary_file_writes_exactly_the_offset)
{
    struct rtc_datetime in = sample();
    char text[256];
    new_scratch();
    pin_utc();
    write_file(g_tmp_path, "stale stale stale stale stale stale stale stale stale stale stale stale\n");
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    read_file(g_path, text, sizeof text); /* the first run's offset, saved over the stale file */
    PT_ASSERT_EQ_STR(text, "0\n");
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    rtc_shutdown();
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "230438075\n");
}

PTEST(null_source_uses_the_system_clock)
{
    struct rtc_datetime d;
    pin_utc();
    PT_ASSERT_EQ_INT(rtc_init(NULL, NULL), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&d), 0);
    rtc_shutdown();
    PT_ASSERT(d.year_bcd >= 0x24 && d.year_bcd <= 0x99); /* the build was written in 2026 or later */
}

/* ---- atomic persistence ---------------------------------------------------- */

PTEST(failed_write_leaves_the_old_file_and_the_offset_intact)
{
    struct rtc_datetime first = sample(), second = {0x25, 0x01, 0x01, 0, 0, 0, 0}, out;
    char text[64];
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&first), 0);
    /* A directory squats on the temporary name, so opening it for writing fails. */
    PT_ASSERT(tsup_make_directory(g_tmp_path));
    PT_ASSERT_EQ_INT(rtc_set_datetime(&second), -1);
    PT_ASSERT_EQ_INT(rtc_set_time(&first), -1);
    PT_ASSERT_EQ_INT(rtc_reset(), -1);
    read_file(g_path, text, sizeof text);
    PT_ASSERT_EQ_STR(text, "230438075\n");
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x31, 0x07, 0x04, 0x05, 0x96, 0x20, 0x05); /* in-memory offset unchanged too */
    rtc_shutdown();
}

PTEST(failed_rename_removes_the_temporary_file_and_reports_an_error)
{
    struct rtc_datetime first = sample(), second = {0x25, 0x01, 0x01, 0, 0, 0, 0};
    new_scratch();
    pin_utc();
    g_now = EPOCH_2024_03_15_13_45_30;
    PT_ASSERT_EQ_INT(rtc_init(fake_source, g_path), 0);
    /* The target becomes a directory after init: rename(2) onto it fails. */
    PT_ASSERT(tsup_exists(g_path));
    PT_ASSERT(tsup_remove_file(g_path));
    PT_ASSERT(tsup_make_directory(g_path));
    PT_ASSERT_EQ_INT(rtc_set_datetime(&first), -1);
    PT_ASSERT(!file_exists(g_tmp_path));
    PT_ASSERT_EQ_INT(rtc_set_datetime(&second), -1);
    PT_ASSERT(!file_exists(g_tmp_path));
    rtc_shutdown();
}

PTEST(no_persistence_without_a_path)
{
    struct rtc_datetime in = sample(), out;
    start_at(EPOCH_2024_03_15_13_45_30);
    PT_ASSERT_EQ_INT(rtc_set_datetime(&in), 0);
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x31, 0x07, 0x04, 0x05, 0x96, 0x20, 0x05);
    rtc_shutdown();
    start_at(EPOCH_2024_03_15_13_45_30); /* offset lived in memory only */
    PT_ASSERT_EQ_INT(rtc_get_datetime(&out), 0);
    assert_dt(&out, 0x24, 0x03, 0x15, 0x05, 0x93, 0x45, 0x30);
    rtc_shutdown();
}

PTEST(cleanup)
{
    remove_scratch();
}

PTEST_MAIN()
