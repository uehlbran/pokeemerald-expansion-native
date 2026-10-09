// deps: rtc_calendar.c
/*
 * test_rtc_calendar.c: rtc_calendar.c, the S-3511A's BCD and calendar
 * arithmetic, written from GBATEK "GBA Cart Real-Time Clock (RTC)". Expected
 * values are Unix epoch seconds (the chip's count) worked out by hand; the
 * chip's year 00h..99h is 2000..2099 and every year divisible by 4 is leap.
 */
#include "ptest.h"
#include "rtc_calendar.h"

#define EPOCH_2000_01_01 946684800LL        /* Saturday */
#define EPOCH_1999_12_31_235959 946684799LL /* Friday */
#define EPOCH_2024_02_29_235959 1709251199LL
#define EPOCH_2099_12_31_235959 4102444799LL /* Thursday */
#define EPOCH_2100_01_01 4102444800LL        /* the chip wraps to 2000-01-01 */
#define SATURDAY 6
#define FRIDAY 5
#define THURSDAY 4

static struct rtc_calendar_time make(int year, int month, int day, int hour, int minute, int second)
{
    return (struct rtc_calendar_time){
        .year = year, .month = month, .day = day, .hour = hour, .minute = minute, .second = second};
}

PTEST(bcd_round_trips_every_value_from_0_to_99)
{
    for (int value = 0; value < RTC_CHIP_YEARS; value++)
        PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(rtc_calendar_to_bcd(value)), value);
}

PTEST(bcd_encodes_tens_in_the_high_nibble)
{
    PT_ASSERT_EQ_U32(rtc_calendar_to_bcd(0), 0x00);
    PT_ASSERT_EQ_U32(rtc_calendar_to_bcd(9), 0x09);
    PT_ASSERT_EQ_U32(rtc_calendar_to_bcd(10), 0x10);
    PT_ASSERT_EQ_U32(rtc_calendar_to_bcd(59), 0x59);
    PT_ASSERT_EQ_U32(rtc_calendar_to_bcd(99), 0x99);
}

PTEST(bcd_rejects_a_nibble_above_9_and_anything_past_a_byte)
{
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0x0A), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0xA0), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0x1F), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0xFF), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0x100), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_from_bcd(0x99), 99);
}

PTEST(months_have_their_usual_lengths_in_a_common_year)
{
    static const int expected[RTC_MONTHS_PER_YEAR] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int month = 1; month <= RTC_MONTHS_PER_YEAR; month++)
        PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(23, month), expected[month - 1]);
}

PTEST(february_has_29_days_in_every_fourth_year_including_year_00)
{
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(0, 2), 29); /* 2000 */
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(24, 2), 29);
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(96, 2), 29);
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(1, 2), 28);
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(99, 2), 28);
    PT_ASSERT_EQ_INT(rtc_calendar_days_in_month(24, 3), 31); /* only February changes */
}

PTEST(the_chip_epoch_is_2000_01_01_a_saturday)
{
    const struct rtc_calendar_time start = make(0, 1, 1, 0, 0, 0);
    PT_ASSERT_EQ_INT(rtc_calendar_seconds_from(&start), EPOCH_2000_01_01);
    const struct rtc_calendar_time t = rtc_calendar_time_from_seconds(EPOCH_2000_01_01);
    PT_ASSERT_EQ_INT(t.year, 0);
    PT_ASSERT_EQ_INT(t.month, 1);
    PT_ASSERT_EQ_INT(t.day, 1);
    PT_ASSERT_EQ_INT(t.weekday, SATURDAY);
    PT_ASSERT_EQ_INT(t.hour + t.minute + t.second, 0);
}

PTEST(the_leap_day_of_2024_is_the_60th_day_and_followed_by_march)
{
    const struct rtc_calendar_time leap_day = make(24, 2, 29, 23, 59, 59);
    PT_ASSERT_EQ_INT(rtc_calendar_seconds_from(&leap_day), EPOCH_2024_02_29_235959);
    const struct rtc_calendar_time next = rtc_calendar_time_from_seconds(EPOCH_2024_02_29_235959 + 1);
    PT_ASSERT_EQ_INT(next.month, 3);
    PT_ASSERT_EQ_INT(next.day, 1);
    PT_ASSERT_EQ_INT(next.second, 0);
}

PTEST(2099_12_31_235959_is_followed_by_2000_01_01_and_the_weekday_does_not_wrap_with_it)
{
    const struct rtc_calendar_time last = rtc_calendar_time_from_seconds(EPOCH_2099_12_31_235959);
    PT_ASSERT_EQ_INT(last.year, 99);
    PT_ASSERT_EQ_INT(last.month, 12);
    PT_ASSERT_EQ_INT(last.day, 31);
    PT_ASSERT_EQ_INT(last.hour, 23);
    PT_ASSERT_EQ_INT(last.minute, 59);
    PT_ASSERT_EQ_INT(last.second, 59);
    PT_ASSERT_EQ_INT(last.weekday, THURSDAY);

    const struct rtc_calendar_time wrapped = rtc_calendar_time_from_seconds(EPOCH_2100_01_01);
    PT_ASSERT_EQ_INT(wrapped.year, 0);
    PT_ASSERT_EQ_INT(wrapped.month, 1);
    PT_ASSERT_EQ_INT(wrapped.day, 1);
    PT_ASSERT_EQ_INT(wrapped.weekday, FRIDAY); /* Thursday plus one day, not 2000's Saturday */
}

PTEST(a_time_before_2000_shows_as_the_end_of_the_cycle)
{
    const struct rtc_calendar_time t = rtc_calendar_time_from_seconds(EPOCH_1999_12_31_235959);
    PT_ASSERT_EQ_INT(t.year, 99);
    PT_ASSERT_EQ_INT(t.month, 12);
    PT_ASSERT_EQ_INT(t.day, 31);
    PT_ASSERT_EQ_INT(t.second, 59);
    PT_ASSERT_EQ_INT(t.weekday, FRIDAY);
}

PTEST(seconds_to_time_to_seconds_round_trips_across_the_cycle)
{
    /* A step that is not a multiple of a day or an hour walks through every
     * month length, a leap day and the wrap. */
    for (int64_t seconds = EPOCH_2000_01_01; seconds < EPOCH_2100_01_01; seconds += 1234567) {
        const struct rtc_calendar_time t = rtc_calendar_time_from_seconds(seconds);
        PT_ASSERT_EQ_INT(rtc_calendar_seconds_from(&t), seconds);
    }
}

PTEST(seconds_of_day_and_midnight_split_a_time_of_day)
{
    PT_ASSERT_EQ_INT(rtc_calendar_seconds_of_day(0, 0, 0), 0);
    PT_ASSERT_EQ_INT(rtc_calendar_seconds_of_day(23, 59, 59), RTC_SECONDS_PER_DAY - 1);
    PT_ASSERT_EQ_INT(rtc_calendar_seconds_of_day(1, 2, 3), 3723);
    PT_ASSERT_EQ_INT(rtc_calendar_midnight_before(EPOCH_2024_02_29_235959),
                     EPOCH_2024_02_29_235959 - (RTC_SECONDS_PER_DAY - 1));
    PT_ASSERT_EQ_INT(rtc_calendar_midnight_before(EPOCH_2000_01_01), EPOCH_2000_01_01);
}

PTEST(midnight_before_rounds_down_for_negative_seconds)
{
    PT_ASSERT_EQ_INT(rtc_calendar_midnight_before(-1), -RTC_SECONDS_PER_DAY);
    PT_ASSERT_EQ_INT(rtc_calendar_midnight_before(-RTC_SECONDS_PER_DAY), -RTC_SECONDS_PER_DAY);
    PT_ASSERT_EQ_INT(rtc_calendar_midnight_before(-RTC_SECONDS_PER_DAY - 1), -2 * RTC_SECONDS_PER_DAY);
}

PTEST(civil_days_count_from_1970_01_01_in_the_gregorian_calendar)
{
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(1970, 1, 1), 0);
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(1969, 12, 31), -1);
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(2000, 1, 1), 10957);
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(2000, 3, 1) - rtc_calendar_unix_days_from_civil(2000, 2, 28),
                     2); /* 2000 is leap */
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(2100, 3, 1) - rtc_calendar_unix_days_from_civil(2100, 2, 28),
                     1); /* Gregorian 2100 is not */
    PT_ASSERT_EQ_INT(rtc_calendar_unix_days_from_civil(1900, 3, 1) - rtc_calendar_unix_days_from_civil(1900, 2, 28),
                     1); /* neither is 1900 */
}

PTEST_MAIN()
