/*
 * rtc_calendar.c: the S-3511A's BCD registers and calendar, as pure
 * arithmetic (interface: rtc_calendar.h). Split from rtc.c so the chip's
 * calendar rules can be tested without a clock or a file.
 *
 * Follows GBATEK "GBA Cart Real-Time Clock (RTC)",
 * https://problemkaputt.de/gbatek.htm#gbacartrealtimeclockrtc, and
 * "DS Real-Time Clock (RTC)", https://problemkaputt.de/gbatek.htm#dsrealtimeclockrtc.
 *
 *  - Year range and the 2099 -> 2000 wrap. The chip's year is two BCD digits
 *    (00h..99h = 2000..2099) with no century, and its leap year rule is
 *    "year divisible by 4", so year 00 is leap. The calendar is a 36525-day
 *    cycle (25 leap years) from 2000-01-01: 2099-12-31 23:59:59 is followed
 *    by 2000-01-01 00:00:00, and a time before 2000 shows as 2099-style
 *    years. Real 2100 (not leap) therefore differs from the chip, which has
 *    a 2100-02-29 (shown as year 00).
 *  - Day of week: 0 = Sunday (expansion's enum Weekday), computed from a
 *    continuously counting day number as the chip's septenary counter does,
 *    so it does not notice the century wrap (2099-12-31 Thursday is followed
 *    by 2000-01-01 Friday). 2000-01-01 itself is a Saturday. The chip lets
 *    software assign any sequence ("custom assignment") and expansion never
 *    sets it.
 */
#include "rtc_calendar.h"

#include <stdbool.h>

#define DAYS_PER_YEAR      365
#define DAYS_PER_WEEK      7
#define CHIP_LEAP_PERIOD   4                  /* leap if year divisible by 4 */
#define CHIP_CYCLE_DAYS    36525              /* 100 * 365 + 25 leap days */
#define CHIP_WEEKDAY_OF_DAY_ZERO 6            /* 2000-01-01 is a Saturday */
#define UNIX_DAYS_TO_2000  10957              /* 1970-01-01 to 2000-01-01 */
#define BCD_BASE           10
#define BCD_NIBBLE_BITS    4
#define BCD_NIBBLE_MASK    0x0Fu
#define BCD_MAX_DIGIT      9

static const uint8_t days_in_month_common[RTC_MONTHS_PER_YEAR] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

uint8_t rtc_calendar_to_bcd(int value)
{
    return (uint8_t)(((value / BCD_BASE) << BCD_NIBBLE_BITS) | (value % BCD_BASE));
}

int rtc_calendar_from_bcd(unsigned bcd)
{
    unsigned tens = bcd >> BCD_NIBBLE_BITS;
    unsigned ones = bcd & BCD_NIBBLE_MASK;
    if (tens > BCD_MAX_DIGIT || ones > BCD_MAX_DIGIT || bcd > 0xFFu)
        return -1;
    return (int)(tens * BCD_BASE + ones);
}

static bool is_chip_leap_year(int year)
{
    return year % CHIP_LEAP_PERIOD == 0;
}

int rtc_calendar_days_in_month(int year, int month)
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
        days += rtc_calendar_days_in_month(year, m);
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

int64_t rtc_calendar_unix_days_from_civil(int64_t year, int month, int day)
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

int64_t rtc_calendar_seconds_of_day(int hour, int minute, int second)
{
    return ((int64_t)hour * RTC_MINUTES_PER_HOUR + minute) * RTC_SECONDS_PER_MINUTE + second;
}

int64_t rtc_calendar_midnight_before(int64_t chip_seconds)
{
    return floor_div(chip_seconds, RTC_SECONDS_PER_DAY) * RTC_SECONDS_PER_DAY;
}

int64_t rtc_calendar_seconds_from(const struct rtc_calendar_time *t)
{
    int64_t days = UNIX_DAYS_TO_2000 + days_from_chip_date(t->year, t->month, t->day);
    return days * RTC_SECONDS_PER_DAY + rtc_calendar_seconds_of_day(t->hour, t->minute, t->second);
}

struct rtc_calendar_time rtc_calendar_time_from_seconds(int64_t chip_seconds)
{
    struct rtc_calendar_time t;
    int64_t day_count = floor_div(chip_seconds, RTC_SECONDS_PER_DAY) - UNIX_DAYS_TO_2000; /* the chip's continuous day counter */
    int64_t day_index = floor_mod(day_count, CHIP_CYCLE_DAYS);
    int64_t second_of_day = floor_mod(chip_seconds, RTC_SECONDS_PER_DAY);
    int remaining = (int)day_index;

    /* The weekday counter never sees the century wrap, so it runs on day_count. */
    t.weekday = (int)floor_mod(day_count + CHIP_WEEKDAY_OF_DAY_ZERO, DAYS_PER_WEEK);
    for (t.year = 0; ; t.year++) {
        int year_days = DAYS_PER_YEAR + (is_chip_leap_year(t.year) ? 1 : 0);
        if (remaining < year_days)
            break;
        remaining -= year_days;
    }
    for (t.month = 1; remaining >= rtc_calendar_days_in_month(t.year, t.month); t.month++)
        remaining -= rtc_calendar_days_in_month(t.year, t.month);
    t.day = remaining + 1;
    t.hour = (int)(second_of_day / (RTC_MINUTES_PER_HOUR * RTC_SECONDS_PER_MINUTE));
    t.minute = (int)(second_of_day / RTC_SECONDS_PER_MINUTE % RTC_MINUTES_PER_HOUR);
    t.second = (int)(second_of_day % RTC_SECONDS_PER_MINUTE);
    return t;
}
