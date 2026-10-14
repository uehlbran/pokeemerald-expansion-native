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
 *  - UTC plus a persisted offset. A cartridge clock is set once to the wall
 *    clock in front of the player and never jumps at DST, so the chip's
 *    seconds are the source's UTC plus an offset. On first run the offset is
 *    the host's local-minus-UTC at that instant (host_file_local_time, its
 *    only use), so the game shows wall time; after that only the player's
 *    set operations move it. With no offset file configured, the offset
 *    starts the same way and is not saved.
 *  - Calendar and BCD: rtc_calendar.c (year range and the 2099 -> 2000 wrap,
 *    leap years, the computed day of week).
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
#include "rtc.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "host_file.h"
#include "rtc_calendar.h"
#include "rtc_offset_store.h"

_Static_assert(sizeof(time_t) >= 8, "rtc.c needs a 64-bit time_t (-D_TIME_BITS=64, platform/Makefile)");

#define TM_YEAR_BASE 1900

/* Status bits the game may write: everything but the read-only power flag. */
#define STATUS_WRITABLE (RTC_STATUS_INTFE | RTC_STATUS_INTME | RTC_STATUS_INTAE | RTC_STATUS_24HOUR)
#define STATUS_DEFAULT RTC_STATUS_24HOUR
#define STATUS_AFTER_RESET 0u

struct rtc_state {
    bool initialised;
    rtc_time_source source;
    int64_t offset;                        /* seconds added to the time source's UTC */
    char offset_path[HOST_FILE_PATH_SIZE]; /* empty: no persistence */
    uint8_t status;                        /* RTC_STATUS_* */
    uint8_t alarm_hour_bcd;                /* with the PM flag */
    uint8_t alarm_minute_bcd;
};

static struct rtc_state chip;

/* ---- the host clock --------------------------------------------------------- */

static int64_t system_clock(void)
{
    time_t now = time(NULL);
    if (now == (time_t)-1) {
        rtc_report_call_failure("time");
        return RTC_TIME_SOURCE_FAILED;
    }
    return (int64_t)now;
}

/* The time source's reading, range checked. */
static int host_utc_seconds(int64_t *out)
{
    int64_t utc = chip.source();

    if (utc == RTC_TIME_SOURCE_FAILED)
        return -1; /* the source has said why */
    if (utc < -RTC_OFFSET_LIMIT || utc > RTC_OFFSET_LIMIT) {
        (void)fprintf(stderr, "rtc: time source returned %lld, out of range\n", (long long)utc);
        return -1;
    }
    *out = utc;
    return 0;
}

/* Host local wall-clock time minus UTC at the instant utc, in seconds: the
 * zone offset including any DST in force then. Used once, on first run. */
static int local_minus_utc(int64_t utc, int64_t *out)
{
    struct tm local;

    if (host_file_local_time(utc, &local) != 0) {
        rtc_report_call_failure("localtime_r");
        return -1;
    }
    *out = rtc_calendar_unix_days_from_civil((int64_t)local.tm_year + TM_YEAR_BASE, local.tm_mon + 1, local.tm_mday)
               * RTC_SECONDS_PER_DAY
           + rtc_calendar_seconds_of_day(local.tm_hour, local.tm_min, local.tm_sec) - utc;
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

/* Makes the chip read chip_seconds now, persisting before committing. A file
 * that was replaced but not yet synced is the offset on disk, so the chip
 * takes it too: running on the old one would disagree with the next start. */
static int set_chip_seconds_at(int64_t chip_seconds, int64_t utc)
{
    int64_t new_offset;

    new_offset = chip_seconds - utc;
    if (new_offset < -RTC_OFFSET_LIMIT || new_offset > RTC_OFFSET_LIMIT) {
        (void)fprintf(stderr, "rtc: offset %lld out of range\n", (long long)new_offset);
        return -1;
    }
    if (chip.offset_path[0] != '\0' && rtc_offset_store_save(chip.offset_path, new_offset) < 0)
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
    uint8_t pm = hour24 >= RTC_NOON_HOUR ? RTC_HOUR_PM : 0;
    return (uint8_t)(rtc_calendar_to_bcd(is_24_hour_mode() ? hour24 : hour24 % RTC_NOON_HOUR) | pm);
}

/* The hour register as written by the game, to a 24-hour value; -1 if invalid. */
static int decode_hour(uint8_t hour_bcd)
{
    int hour;
    if (hour_bcd & ~(RTC_HOUR_PM | RTC_HOUR_MASK))
        return -1;
    hour = rtc_calendar_from_bcd(hour_bcd & RTC_HOUR_MASK);
    if (is_24_hour_mode())
        return hour < RTC_HOURS_PER_DAY ? hour : -1; /* the PM flag is read only here */
    if (hour < 0 || hour >= RTC_NOON_HOUR)
        return -1;
    return hour + ((hour_bcd & RTC_HOUR_PM) ? RTC_NOON_HOUR : 0);
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
    *minute = rtc_calendar_from_bcd(in->minute_bcd);
    *second = rtc_calendar_from_bcd(in->second_bcd);
    if (*hour24 < 0 || !in_range(*minute, 0, RTC_MINUTES_PER_HOUR - 1)
        || !in_range(*second, 0, RTC_SECONDS_PER_MINUTE - 1))
        return -1;
    return 0;
}

static int decode_date_fields(const struct rtc_datetime *in, int *year, int *month, int *day)
{
    int weekday = rtc_calendar_from_bcd(in->day_of_week_bcd);
    *year = rtc_calendar_from_bcd(in->year_bcd);
    *month = rtc_calendar_from_bcd(in->month_bcd);
    *day = rtc_calendar_from_bcd(in->day_bcd);
    if (!in_range(*year, 0, RTC_CHIP_YEARS - 1) || !in_range(*month, 1, RTC_MONTHS_PER_YEAR)
        || !in_range(weekday, 0, RTC_CHIP_MAX_WEEKDAY))
        return -1;
    return in_range(*day, 1, rtc_calendar_days_in_month(*year, *month)) ? 0 : -1;
}

/* ---- public interface ----------------------------------------------------------------- */

void rtc_shutdown(void)
{
    memset(&chip, 0, sizeof chip);
}

static int init_with_seed(rtc_time_source source, const char *offset_file_path, bool seed_from_host_zone)
{
    int64_t offset = 0;
    int64_t utc;
    int found = 0;

    rtc_shutdown();
    if (offset_file_path != NULL) {
        if (rtc_offset_store_load(offset_file_path, &offset, &found) != 0)
            return -1;
        (void)snprintf(chip.offset_path, sizeof chip.offset_path, "%s", offset_file_path); /* fits: the load checked */
    }
    chip.source = source != NULL ? source : system_clock;
    if (!found) {
        /* First run: show the host's wall clock (or, for rtc_init_utc, UTC), then run on fixed UTC + offset. */
        if (host_utc_seconds(&utc) != 0 || (seed_from_host_zone && local_minus_utc(utc, &offset) != 0)
            || (chip.offset_path[0] != '\0' && rtc_offset_store_save(chip.offset_path, offset) < 0)) {
            rtc_shutdown();
            return -1;
        }
    }
    chip.offset = offset;
    chip.status = STATUS_DEFAULT;
    chip.initialised = true;
    return 0;
}

int rtc_init(rtc_time_source source, const char *offset_file_path)
{
    return init_with_seed(source, offset_file_path, true);
}

int rtc_init_utc(rtc_time_source source, const char *offset_file_path)
{
    return init_with_seed(source, offset_file_path, false);
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
    struct rtc_calendar_time t;

    if (!chip.initialised || out == NULL || chip_seconds_now(&seconds) != 0)
        return -1;
    t = rtc_calendar_time_from_seconds(seconds);
    out->year_bcd = rtc_calendar_to_bcd(t.year);
    out->month_bcd = rtc_calendar_to_bcd(t.month);
    out->day_bcd = rtc_calendar_to_bcd(t.day);
    out->day_of_week_bcd = rtc_calendar_to_bcd(t.weekday);
    out->hour_bcd = encode_hour(t.hour);
    out->minute_bcd = rtc_calendar_to_bcd(t.minute);
    out->second_bcd = rtc_calendar_to_bcd(t.second);
    return 0;
}

int rtc_set_datetime(const struct rtc_datetime *in)
{
    int year, month, day, hour, minute, second;

    if (!chip.initialised || in == NULL || decode_date_fields(in, &year, &month, &day) != 0
        || decode_time_fields(in, &hour, &minute, &second) != 0)
        return -1;
    return set_chip_seconds(rtc_calendar_seconds_from(&(struct rtc_calendar_time){
        .year = year, .month = month, .day = day, .hour = hour, .minute = minute, .second = second}));
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
    return set_chip_seconds_at(
        rtc_calendar_midnight_before(utc + chip.offset) + rtc_calendar_seconds_of_day(hour, minute, second), utc);
}

int rtc_reset(void)
{
    if (!chip.initialised)
        return -1;
    if (set_chip_seconds(rtc_calendar_seconds_from(&(struct rtc_calendar_time){.year = 0, .month = 1, .day = 1})) != 0)
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
    hour = rtc_calendar_from_bcd(in->hour_bcd & RTC_HOUR_MASK);
    minute = rtc_calendar_from_bcd(in->minute_bcd);
    if (!in_range(hour, 0, RTC_HOURS_PER_DAY - 1) || !in_range(minute, 0, RTC_MINUTES_PER_HOUR - 1))
        return -1;
    chip.alarm_hour_bcd = (uint8_t)(rtc_calendar_to_bcd(hour) | (hour >= RTC_NOON_HOUR ? RTC_HOUR_PM : 0));
    chip.alarm_minute_bcd = rtc_calendar_to_bcd(minute);
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
