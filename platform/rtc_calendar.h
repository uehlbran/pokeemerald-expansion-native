/*
 * rtc_calendar.h: the S-3511A's BCD registers and calendar arithmetic, with
 * no state and no clock. Internal to the RTC model (rtc.c); see the header
 * comment of rtc_calendar.c for the chip's calendar rules and the choices made.
 *
 * "Chip seconds" are seconds on the chip's own count: the Unix epoch's scale
 * (1970-01-01 00:00:00 is 0) with the chip's 2000..2099 calendar laid over it.
 */
#ifndef PLATFORM_RTC_CALENDAR_H
#define PLATFORM_RTC_CALENDAR_H

#include <stdint.h>

#define RTC_SECONDS_PER_MINUTE 60
#define RTC_MINUTES_PER_HOUR 60
#define RTC_HOURS_PER_DAY 24
#define RTC_NOON_HOUR 12
#define RTC_SECONDS_PER_DAY ((int64_t)86400)
#define RTC_MONTHS_PER_YEAR 12
#define RTC_CHIP_YEARS 100     /* the year register, 00h..99h = 2000..2099 */
#define RTC_CHIP_MAX_WEEKDAY 6 /* the weekday register, 0 = Sunday */

/* Broken-down chip time, plain integers (not BCD). */
struct rtc_calendar_time {
    int year;    /* 0..99 */
    int month;   /* 1..12 */
    int day;     /* 1..31 */
    int weekday; /* 0..6; ignored by rtc_calendar_seconds_from */
    int hour;    /* 0..23 */
    int minute;  /* 0..59 */
    int second;  /* 0..59 */
};

/* value (0..99) as two BCD digits. */
uint8_t rtc_calendar_to_bcd(int value);

/* The value of two BCD digits, or -1 if either nibble is not a decimal digit. */
int rtc_calendar_from_bcd(unsigned bcd);

/* Days in month (1..12) of year (0..99) on the chip's calendar. */
int rtc_calendar_days_in_month(int year, int month);

/* Chip seconds of the date and time of day in t; t must hold valid fields. */
int64_t rtc_calendar_seconds_from(const struct rtc_calendar_time *t);

/* The chip's broken-down time for any chip seconds, negative or beyond 2099
 * included: the calendar repeats every 36525 days. */
struct rtc_calendar_time rtc_calendar_time_from_seconds(int64_t chip_seconds);

/* Seconds into the day of a time of day; fields must be valid. */
int64_t rtc_calendar_seconds_of_day(int hour, int minute, int second);

/* chip_seconds with the time of day cleared (rounded down to midnight). */
int64_t rtc_calendar_midnight_before(int64_t chip_seconds);

/* Days from 1970-01-01 to a proleptic Gregorian date; any year. */
int64_t rtc_calendar_unix_days_from_civil(int64_t year, int month, int day);

#endif /* PLATFORM_RTC_CALENDAR_H */
