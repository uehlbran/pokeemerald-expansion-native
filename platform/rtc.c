/*
 * rtc.c: the Seiko Instruments S-3511A real-time clock on the Game Pak.
 *
 * WHAT IT STANDS IN FOR. Ruby, Sapphire and Emerald carry an S-3511A on the
 * cartridge's GPIO port (GBATEK "GBA Cart Real-Time Clock (RTC)",
 * https://problemkaputt.de/gbatek.htm#gbacartrealtimeclockrtc; the chip is
 * almost the NDS one, "DS Real-Time Clock (RTC)",
 * https://problemkaputt.de/gbatek.htm#dsrealtimeclockrtc). The game
 * bit-bangs a 3-wire serial bus (src/siirtc.c) to read and write its
 * registers: status/control, the seven datetime bytes (year, month, day, day
 * of week, hour, minute, second, all BCD), the three time bytes, and the
 * alarm. A battery kept it counting while the console was off.
 *
 * WHAT THIS MODEL DOES. It is the chip at the level of those registers, which
 * is all the game observes through the SiiRtc* API (under PLATFORM_NATIVE
 * src/siirtc.c calls this module instead of toggling GPIO pins). The
 * counters are not stored: the chip time is the host's UTC time plus a
 * persisted offset in seconds, so the clock keeps running while the
 * game is off and "set the time" means "compute the offset". No reference
 * port is copied; this is written from GBATEK and expansion's own sources.
 *
 * DECISIONS.
 *  - UTC plus a fixed offset, initialised from local time. A cartridge
 *    clock is set once by the player to the wall clock in front of them,
 *    knows no time zones and never jumps at daylight saving. So the chip's
 *    seconds are the time source (UTC) plus the persisted offset. On first
 *    run, when no offset file exists (or none is configured), the offset is
 *    initialised to the host's local-minus-UTC difference at that instant
 *    (localtime_r, used only for this) so the in-game clock reads local wall
 *    time, and it is persisted. After that a host zone or DST change does not
 *    move the clock; the player's set operations adjust the offset.
 *  - Year range and the 2099 -> 2000 wrap. The chip's year is two BCD digits
 *    (00h..99h = 2000..2099) with no century, and its leap year rule is
 *    "year divisible by 4", so year 00 is leap. The model's calendar is a
 *    36525-day cycle (25 leap years) from 2000-01-01: 2099-12-31 23:59:59 is
 *    followed by 2000-01-01 00:00:00, and a time before 2000 shows as
 *    2099-style years. Real 2100 (not leap) therefore differs from the chip,
 *    which has a 2100-02-29 (shown as year 00).
 *  - Day of week: 0 = Sunday (expansion's enum Weekday), computed from a
 *    continuously counting day number as the chip's septenary counter does,
 *    so it does not notice the century wrap (2099-12-31 Thursday is followed
 *    by 2000-01-01 Friday). 2000-01-01 itself is a Saturday. The chip lets
 *    software assign any sequence ("custom assignment") and expansion never
 *    sets it.
 *  - Hour register: see rtc.h (PM flag in bit 7, derived).
 *  - Status after init is 24-hour, power-off flag clear: the offset file
 *    stands in for the battery, so the model never loses power (expansion's
 *    RtcCheckInfo would flag RTC_ERR_POWER_FAILURE). Status is not persisted;
 *    expansion writes it on every probe. rtc_reset follows GBATEK's
 *    "setting after Force-Reset is 00h" (12-hour mode); expansion's
 *    SiiRtcReset then writes the 24-hour bit, as on the cartridge.
 *  - After reset the chip's counters read 00h with day and month 01h; the
 *    model shows 2000-01-01 00:00:00 and the computed weekday (Saturday),
 *    where the chip would show weekday 00h.
 *  - The alarm is accepted and stored; nothing fires. Expansion never uses it.
 *  - Not modelled: the interrupt outputs, the test-mode flag (second bit 7),
 *    the serial protocol, and write protection.
 */
#define _FILE_OFFSET_BITS 64 /* with _TIME_BITS: 64-bit time_t on 32-bit builds, */
#define _TIME_BITS 64        /* so the 2038..2099 epoch seconds the tests use convert */

#include "rtc.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "rtc_offset_store.h"

_Static_assert(sizeof(time_t) >= 8, "rtc.c needs a 64-bit time_t (_TIME_BITS=64)");

/* Calendar constants. */
#define SECONDS_PER_MINUTE 60
#define MINUTES_PER_HOUR   60
#define HOURS_PER_DAY      24
#define SECONDS_PER_DAY    ((int64_t)86400)
#define MONTHS_PER_YEAR    12
#define DAYS_PER_YEAR      365
#define DAYS_PER_WEEK      7
#define CHIP_YEARS         100                /* year register 00h..99h */
#define CHIP_LEAP_PERIOD   4                  /* leap if year divisible by 4 */
#define CHIP_CYCLE_DAYS    36525              /* 100 * 365 + 25 leap days */
#define CHIP_WEEKDAY_OF_DAY_ZERO 6            /* 2000-01-01 is a Saturday */
#define UNIX_DAYS_TO_2000  10957              /* 1970-01-01 to 2000-01-01 */
#define TM_YEAR_BASE       1900
#define NOON_HOUR          12
#define CHIP_MAX_WEEKDAY   6
#define BCD_BASE           10
#define BCD_NIBBLE_BITS    4
#define BCD_NIBBLE_MASK    0x0Fu
#define BCD_MAX_DIGIT      9

/* Status bits the game may write: everything but the read-only power flag. */
#define STATUS_WRITABLE (RTC_STATUS_INTFE | RTC_STATUS_INTME | RTC_STATUS_INTAE | RTC_STATUS_24HOUR)
#define STATUS_DEFAULT  RTC_STATUS_24HOUR
#define STATUS_AFTER_RESET 0u

static const uint8_t days_in_month_common[MONTHS_PER_YEAR] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

struct rtc_state {
    bool initialised;
    rtc_time_source source;
    int64_t offset;                  /* seconds added to host local time */
    char offset_path[PATH_MAX];      /* empty: no persistence */
    uint8_t status;                  /* RTC_STATUS_* */
    uint8_t alarm_hour_bcd;          /* with the PM flag */
    uint8_t alarm_minute_bcd;
};

static struct rtc_state chip;

/* Broken-down chip time. */
struct chip_time {
    int year;   /* 0..99 */
    int month;  /* 1..12 */
    int day;    /* 1..31 */
    int weekday;/* 0..6 */
    int hour;   /* 0..23 */
    int minute;
    int second;
};

/* ---- BCD ------------------------------------------------------------------ */

static uint8_t to_bcd(int value)
{
    return (uint8_t)(((value / BCD_BASE) << BCD_NIBBLE_BITS) | (value % BCD_BASE));
}

/* -1 if either nibble is not a decimal digit. */
static int from_bcd(unsigned bcd)
{
    unsigned tens = bcd >> BCD_NIBBLE_BITS;
    unsigned ones = bcd & BCD_NIBBLE_MASK;
    if (tens > BCD_MAX_DIGIT || ones > BCD_MAX_DIGIT || bcd > 0xFFu)
        return -1;
    return (int)(tens * BCD_BASE + ones);
}

/* ---- calendar ------------------------------------------------------------- */

static bool is_chip_leap_year(int year)
{
    return year % CHIP_LEAP_PERIOD == 0;
}

static int days_in_month(int year, int month)
{
    if (month == 2 && is_chip_leap_year(year))
        return days_in_month_common[1] + 1;
    return days_in_month_common[month - 1];
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static int64_t floor_mod(int64_t a, int64_t b)
{
    return a - floor_div(a, b) * b;
}

/* Days since 2000-01-01 of a date in the chip's 2000..2099 window. */
static int days_from_chip_date(int year, int month, int day)
{
    int days = year * DAYS_PER_YEAR + (year + CHIP_LEAP_PERIOD - 1) / CHIP_LEAP_PERIOD;
    int m;
    for (m = 1; m < month; m++)
        days += days_in_month(year, m);
    return days + day - 1;
}

/* Days since 1970-01-01 of a proleptic Gregorian date: the public-domain
 * days_from_civil algorithm (H. Hinnant, "chrono-Compatible Low-Level Date
 * Algorithms", https://howardhinnant.github.io/date_algorithms.html). Years
 * run from 1 March so the leap day is the last day of the year; an era is 400
 * years, 146097 days. */
#define GREGORIAN_ERA_YEARS       400
#define GREGORIAN_ERA_DAYS        146097
#define DAYS_0000_03_01_TO_1970   719468  /* days from 0000-03-01 to 1970-01-01 */
#define MARCH_BASED_MONTH_MAR     3       /* months counted from March: Mar = 0 */
#define MARCH_BASED_MONTH_WRAP    9       /* Jan -> 10, Feb -> 11 */
#define MONTH_DAYS_NUMERATOR      153     /* (153 * m + 2) / 5 = days before month m, March based */
#define MONTH_DAYS_BIAS           2
#define MONTH_DAYS_DENOMINATOR    5
#define LAST_MONTH_OF_OLD_YEAR    2       /* Jan and Feb belong to the previous March-based year */
#define CENTURY_YEARS             100

static int64_t unix_days_from_civil(int64_t year, int month, int day)
{
    int64_t y = month <= LAST_MONTH_OF_OLD_YEAR ? year - 1 : year;
    int64_t era = floor_div(y, GREGORIAN_ERA_YEARS);
    int64_t year_of_era = y - era * GREGORIAN_ERA_YEARS;
    int64_t march_month = month > LAST_MONTH_OF_OLD_YEAR ? month - MARCH_BASED_MONTH_MAR : month + MARCH_BASED_MONTH_WRAP;
    int64_t day_of_year = (MONTH_DAYS_NUMERATOR * march_month + MONTH_DAYS_BIAS) / MONTH_DAYS_DENOMINATOR + day - 1;
    int64_t day_of_era = year_of_era * DAYS_PER_YEAR + year_of_era / CHIP_LEAP_PERIOD
                       - year_of_era / CENTURY_YEARS + day_of_year;
    return era * GREGORIAN_ERA_DAYS + day_of_era - DAYS_0000_03_01_TO_1970;
}

/* Seconds counter value for a chip date and time of day. */
static int64_t chip_seconds_from(int year, int month, int day, int hour, int minute, int second)
{
    int64_t days = UNIX_DAYS_TO_2000 + days_from_chip_date(year, month, day);
    return days * SECONDS_PER_DAY + ((int64_t)hour * MINUTES_PER_HOUR + minute) * SECONDS_PER_MINUTE + second;
}

static struct chip_time chip_time_from_seconds(int64_t chip_seconds)
{
    struct chip_time t;
    int64_t day_count = floor_div(chip_seconds, SECONDS_PER_DAY) - UNIX_DAYS_TO_2000; /* the chip's continuous day counter */
    int64_t day_index = floor_mod(day_count, CHIP_CYCLE_DAYS);
    int64_t second_of_day = floor_mod(chip_seconds, SECONDS_PER_DAY);
    int remaining = (int)day_index;

    /* The weekday counter never sees the century wrap, so it runs on day_count. */
    t.weekday = (int)floor_mod(day_count + CHIP_WEEKDAY_OF_DAY_ZERO, DAYS_PER_WEEK);
    for (t.year = 0; ; t.year++) {
        int year_days = DAYS_PER_YEAR + (is_chip_leap_year(t.year) ? 1 : 0);
        if (remaining < year_days)
            break;
        remaining -= year_days;
    }
    for (t.month = 1; remaining >= days_in_month(t.year, t.month); t.month++)
        remaining -= days_in_month(t.year, t.month);
    t.day = remaining + 1;
    t.hour = (int)(second_of_day / (MINUTES_PER_HOUR * SECONDS_PER_MINUTE));
    t.minute = (int)(second_of_day / SECONDS_PER_MINUTE % MINUTES_PER_HOUR);
    t.second = (int)(second_of_day % SECONDS_PER_MINUTE);
    return t;
}

/* ---- the host clock --------------------------------------------------------- */

static int64_t system_clock(void)
{
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        rtc_report_call_failure("time");
        return INT64_MIN;
    }
    return (int64_t)now;
}

/* The time source's reading, range checked. */
static int host_utc_seconds(int64_t *out)
{
    int64_t utc = chip.source();

    if (utc < -RTC_OFFSET_LIMIT || utc > RTC_OFFSET_LIMIT) {
        fprintf(stderr, "rtc: time source returned %lld, out of range\n", (long long)utc);
        return -1;
    }
    *out = utc;
    return 0;
}

/* Host local wall-clock time minus UTC at the instant utc, in seconds: the
 * zone offset including any DST in force then. Used once, on first run. */
static int local_minus_utc(int64_t utc, int64_t *out)
{
    time_t t = (time_t)utc;
    struct tm local;

    if (localtime_r(&t, &local) == NULL) {
        rtc_report_call_failure("localtime_r");
        return -1;
    }
    *out = unix_days_from_civil((int64_t)local.tm_year + TM_YEAR_BASE, local.tm_mon + 1, local.tm_mday) * SECONDS_PER_DAY
         + ((int64_t)local.tm_hour * MINUTES_PER_HOUR + local.tm_min) * SECONDS_PER_MINUTE + local.tm_sec - utc;
    return 0;
}

static int chip_seconds_now(int64_t *out)
{
    int64_t utc;
    if (host_utc_seconds(&utc) != 0)
        return -1;
    *out = utc + chip.offset;
    return 0;
}

/* Makes the chip read chip_seconds now, persisting before committing. */
static int set_chip_seconds_at(int64_t chip_seconds, int64_t utc)
{
    int64_t new_offset;

    new_offset = chip_seconds - utc;
    if (new_offset < -RTC_OFFSET_LIMIT || new_offset > RTC_OFFSET_LIMIT) {
        fprintf(stderr, "rtc: offset %lld out of range\n", (long long)new_offset);
        return -1;
    }
    if (chip.offset_path[0] != '\0' && rtc_offset_store_save(chip.offset_path, new_offset) != 0)
        return -1;
    chip.offset = new_offset;
    return 0;
}

static int set_chip_seconds(int64_t chip_seconds)
{
    int64_t utc;

    if (host_utc_seconds(&utc) != 0)
        return -1;
    return set_chip_seconds_at(chip_seconds, utc);
}

/* ---- hour register ------------------------------------------------------------- */

static bool is_24_hour_mode(void)
{
    return (chip.status & RTC_STATUS_24HOUR) != 0;
}

static uint8_t encode_hour(int hour24)
{
    uint8_t pm = hour24 >= NOON_HOUR ? RTC_HOUR_PM : 0;
    return (uint8_t)(to_bcd(is_24_hour_mode() ? hour24 : hour24 % NOON_HOUR) | pm);
}

/* The hour register as written by the game, to a 24-hour value; -1 if invalid. */
static int decode_hour(uint8_t hour_bcd)
{
    int hour;
    if (hour_bcd & ~(RTC_HOUR_PM | RTC_HOUR_MASK))
        return -1;
    hour = from_bcd(hour_bcd & RTC_HOUR_MASK);
    if (is_24_hour_mode())
        return hour < HOURS_PER_DAY ? hour : -1;  /* the PM flag is read only here */
    if (hour < 0 || hour >= NOON_HOUR)
        return -1;
    return hour + ((hour_bcd & RTC_HOUR_PM) ? NOON_HOUR : 0);
}

/* ---- validation of what the game writes ------------------------------------------ */

static int in_range(int value, int low, int high)
{
    return value >= low && value <= high;
}

/* hour24, minute, second of a time write; -1 if any is invalid. */
static int decode_time_fields(const struct rtc_datetime *in, int *hour24, int *minute, int *second)
{
    *hour24 = decode_hour(in->hour_bcd);
    *minute = from_bcd(in->minute_bcd);
    *second = from_bcd(in->second_bcd);
    if (*hour24 < 0 || !in_range(*minute, 0, MINUTES_PER_HOUR - 1) || !in_range(*second, 0, SECONDS_PER_MINUTE - 1))
        return -1;
    return 0;
}

static int decode_date_fields(const struct rtc_datetime *in, int *year, int *month, int *day)
{
    int weekday = from_bcd(in->day_of_week_bcd);
    *year = from_bcd(in->year_bcd);
    *month = from_bcd(in->month_bcd);
    *day = from_bcd(in->day_bcd);
    if (!in_range(*year, 0, CHIP_YEARS - 1) || !in_range(*month, 1, MONTHS_PER_YEAR)
        || !in_range(weekday, 0, CHIP_MAX_WEEKDAY))
        return -1;
    return in_range(*day, 1, days_in_month(*year, *month)) ? 0 : -1;
}

/* ---- public interface ----------------------------------------------------------------- */

void rtc_shutdown(void)
{
    memset(&chip, 0, sizeof chip);
}

int rtc_init(rtc_time_source source, const char *offset_file_path)
{
    int64_t offset = 0;
    int64_t utc;
    int found = 0;

    rtc_shutdown();
    if (offset_file_path != NULL) {
        if (strlen(offset_file_path) >= sizeof chip.offset_path) {
            fprintf(stderr, "rtc: offset file path is longer than %zu bytes\n", sizeof chip.offset_path - 1);
            return -1;
        }
        if (rtc_offset_store_load(offset_file_path, &offset, &found) != 0)
            return -1;
        strcpy(chip.offset_path, offset_file_path);
    }
    chip.source = source != NULL ? source : system_clock;
    if (!found) {
        /* First run: show the host's wall clock, then run on fixed UTC + offset. */
        if (host_utc_seconds(&utc) != 0 || local_minus_utc(utc, &offset) != 0
            || (chip.offset_path[0] != '\0' && rtc_offset_store_save(chip.offset_path, offset) != 0)) {
            rtc_shutdown();
            return -1;
        }
    }
    chip.offset = offset;
    chip.status = STATUS_DEFAULT;
    chip.initialised = true;
    return 0;
}

int rtc_get_status(uint8_t *status)
{
    if (!chip.initialised || status == NULL)
        return -1;
    *status = chip.status; /* the power-off flag is never set, so nothing to clear */
    return 0;
}

int rtc_set_status(uint8_t status)
{
    uint8_t writable = (uint8_t)(status & ~RTC_STATUS_POWER);
    if (!chip.initialised || (writable & ~STATUS_WRITABLE) != 0)
        return -1;
    chip.status = writable;
    return 0;
}

int rtc_get_datetime(struct rtc_datetime *out)
{
    int64_t seconds;
    struct chip_time t;

    if (!chip.initialised || out == NULL || chip_seconds_now(&seconds) != 0)
        return -1;
    t = chip_time_from_seconds(seconds);
    out->year_bcd = to_bcd(t.year);
    out->month_bcd = to_bcd(t.month);
    out->day_bcd = to_bcd(t.day);
    out->day_of_week_bcd = to_bcd(t.weekday);
    out->hour_bcd = encode_hour(t.hour);
    out->minute_bcd = to_bcd(t.minute);
    out->second_bcd = to_bcd(t.second);
    return 0;
}

int rtc_set_datetime(const struct rtc_datetime *in)
{
    int year, month, day, hour, minute, second;

    if (!chip.initialised || in == NULL || decode_date_fields(in, &year, &month, &day) != 0
        || decode_time_fields(in, &hour, &minute, &second) != 0)
        return -1;
    return set_chip_seconds(chip_seconds_from(year, month, day, hour, minute, second));
}

int rtc_get_time(struct rtc_datetime *out)
{
    struct rtc_datetime full;

    if (out == NULL || rtc_get_datetime(&full) != 0)
        return -1;
    out->hour_bcd = full.hour_bcd;
    out->minute_bcd = full.minute_bcd;
    out->second_bcd = full.second_bcd;
    return 0;
}

int rtc_set_time(const struct rtc_datetime *in)
{
    int hour, minute, second;
    int64_t utc;

    if (!chip.initialised || in == NULL || decode_time_fields(in, &hour, &minute, &second) != 0
        || host_utc_seconds(&utc) != 0)
        return -1;
    /* One time-source sample for both the current date and the new offset. */
    return set_chip_seconds_at(floor_div(utc + chip.offset, SECONDS_PER_DAY) * SECONDS_PER_DAY
                               + ((int64_t)hour * MINUTES_PER_HOUR + minute) * SECONDS_PER_MINUTE + second, utc);
}

int rtc_reset(void)
{
    if (!chip.initialised)
        return -1;
    if (set_chip_seconds(chip_seconds_from(0, 1, 1, 0, 0, 0)) != 0)
        return -1;
    chip.status = STATUS_AFTER_RESET;
    chip.alarm_hour_bcd = 0;
    chip.alarm_minute_bcd = 0;
    return 0;
}

int rtc_set_alarm(const struct rtc_datetime *in)
{
    int hour, minute;

    if (!chip.initialised || in == NULL)
        return -1;
    if (in->hour_bcd & ~(RTC_HOUR_PM | RTC_HOUR_MASK))
        return -1;
    hour = from_bcd(in->hour_bcd & RTC_HOUR_MASK);
    minute = from_bcd(in->minute_bcd);
    if (!in_range(hour, 0, HOURS_PER_DAY - 1) || !in_range(minute, 0, MINUTES_PER_HOUR - 1))
        return -1;
    chip.alarm_hour_bcd = (uint8_t)(to_bcd(hour) | (hour >= NOON_HOUR ? RTC_HOUR_PM : 0));
    chip.alarm_minute_bcd = to_bcd(minute);
    return 0;
}

int rtc_get_alarm(struct rtc_datetime *out)
{
    if (!chip.initialised || out == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    out->hour_bcd = chip.alarm_hour_bcd;
    out->minute_bcd = chip.alarm_minute_bcd;
    return 0;
}
