/*
 * rtc_siirtc.c: expansion's SiiRtc API (include/siirtc.h), served by the
 * platform's model of the S-3511A (rtc.h).
 *
 * Stands in for src/siirtc.c, which the native build leaves out
 * (Makefile.native): that file reaches the chip by bit-banging the
 * cartridge's GPIO port at 0x080000C4-0x080000C8 (GBATEK "GBA Cart
 * Real-Time Clock (RTC)", https://problemkaputt.de/gbatek.htm#gbacartrealtimeclockrtc),
 * which the native build does not have. The calls here are the file's own
 * API, with the same results: struct SiiRtcInfo is filled and read in BCD as
 * the chip delivers it, the chip's status bits are in the SIIRTCINFO_*
 * order, and SiiRtcProtect locks the chip (every call but the lock itself
 * then fails) until SiiRtcUnprotect. A date and time read that fails
 * (locked, or no time source) answers with zeros, as the chip always
 * answers and RtcGetRawInfo (src/rtc.c) ignores the result. The original's
 * SiiRtcSetAlarm is static and unused (it is not in siirtc.h), so it is not
 * carried over; rtc_set_alarm remains in rtc.h. Followed: src/siirtc.c.
 *
 * This file is compiled with the game's flags (platform/Makefile, as
 * inspect.c is), because it fills the game's struct SiiRtcInfo. Module
 * state: whether the chip is locked.
 */
#include "rtc.h"

#include <stdint.h>

#include "config/general.h"
#include "siirtc.h"

/* The platform's copy of the status bit layout must not drift from the
 * game's. */
_Static_assert(RTC_STATUS_INTFE == SIIRTCINFO_INTFE, "RTC_STATUS_INTFE != SIIRTCINFO_INTFE");
_Static_assert(RTC_STATUS_INTME == SIIRTCINFO_INTME, "RTC_STATUS_INTME != SIIRTCINFO_INTME");
_Static_assert(RTC_STATUS_INTAE == SIIRTCINFO_INTAE, "RTC_STATUS_INTAE != SIIRTCINFO_INTAE");
_Static_assert(RTC_STATUS_24HOUR == SIIRTCINFO_24HOUR, "RTC_STATUS_24HOUR != SIIRTCINFO_24HOUR");
_Static_assert(RTC_STATUS_POWER == SIIRTCINFO_POWER, "RTC_STATUS_POWER != SIIRTCINFO_POWER");

/* The second register's top bit is the chip's test-mode flag
 * (GBATEK "GBA Cart Real-Time Clock (RTC)"). */
#define SECOND_TEST_MODE 0x80u

/* The writable status bits; the 24-hour bit is always written set. */
#define STATUS_WRITABLE_MASK (SIIRTCINFO_INTAE | SIIRTCINFO_INTME | SIIRTCINFO_INTFE)

/* What SiiRtcProbe's result carries: bit 0 the chip answered, bits 4-7 the
 * number of resets it needed. */
#define PROBE_OK 1
#define PROBE_RESETS_SHIFT 4
#define PROBE_RESETS_MASK 0xF0

static bool8 sLocked;

static void ClearTime(struct SiiRtcInfo *rtc)
{
    rtc->hour = rtc->minute = rtc->second = 0;
}

static void ClearDateTime(struct SiiRtcInfo *rtc)
{
    rtc->year = rtc->month = rtc->day = rtc->dayOfWeek = 0;
    ClearTime(rtc);
}

/* The hour register's PM flag (bit 7) is not part of the game's hour. */
static void StoreTime(struct SiiRtcInfo *rtc, const struct rtc_datetime *dateTime)
{
    rtc->hour = (u8)(dateTime->hour_bcd & ~RTC_HOUR_PM);
    rtc->minute = dateTime->minute_bcd;
    rtc->second = dateTime->second_bcd;
}

void SiiRtcUnprotect(void)
{
    sLocked = FALSE;
}

void SiiRtcProtect(void)
{
    sLocked = TRUE;
}

u8 SiiRtcProbe(void)
{
    u8 errorCode;
    struct SiiRtcInfo rtc;

    if (!SiiRtcGetStatus(&rtc))
        return 0;

    errorCode = 0;

#ifdef BUGFIX
    if (!(rtc.status & SIIRTCINFO_24HOUR) || (rtc.status & SIIRTCINFO_POWER))
#else
    if ((rtc.status & (SIIRTCINFO_POWER | SIIRTCINFO_24HOUR)) == SIIRTCINFO_POWER
        || (rtc.status & (SIIRTCINFO_POWER | SIIRTCINFO_24HOUR)) == 0)
#endif
    {
        /* The RTC is in 12-hour mode (or, with BUGFIX, lost power): reset it
         * and switch to 24-hour mode. */
        if (!SiiRtcReset())
            return 0;

        errorCode++;
    }

    SiiRtcGetTime(&rtc);

    if (rtc.second & SECOND_TEST_MODE) {
        /* The RTC is in test mode. Reset it to leave test mode. */
        if (!SiiRtcReset())
            return (u8)((errorCode << PROBE_RESETS_SHIFT) & PROBE_RESETS_MASK);

        errorCode++;
    }

    return (u8)((errorCode << PROBE_RESETS_SHIFT) | PROBE_OK);
}

bool8 SiiRtcReset(void)
{
    struct SiiRtcInfo rtc;

    if (sLocked == TRUE)
        return FALSE;

    if (rtc_reset() != 0)
        return FALSE;

    rtc.status = SIIRTCINFO_24HOUR;

    return SiiRtcSetStatus(&rtc);
}

bool8 SiiRtcGetStatus(struct SiiRtcInfo *rtc)
{
    uint8_t status;

    if (sLocked == TRUE || rtc_get_status(&status) != 0)
        return FALSE;

    rtc->status = status;

    return TRUE;
}

// cppcheck-suppress constParameterPointer ; siirtc.h declares a non-const parameter
bool8 SiiRtcSetStatus(struct SiiRtcInfo *rtc)
{
    if (sLocked == TRUE)
        return FALSE;

    return rtc_set_status((uint8_t)(SIIRTCINFO_24HOUR | (rtc->status & STATUS_WRITABLE_MASK))) == 0;
}

bool8 SiiRtcGetDateTime(struct SiiRtcInfo *rtc)
{
    struct rtc_datetime dateTime;

    if (sLocked == TRUE || rtc_get_datetime(&dateTime) != 0) {
        ClearDateTime(rtc);
        return FALSE;
    }

    rtc->year = dateTime.year_bcd;
    rtc->month = dateTime.month_bcd;
    rtc->day = dateTime.day_bcd;
    rtc->dayOfWeek = dateTime.day_of_week_bcd;
    StoreTime(rtc, &dateTime);

    return TRUE;
}

bool8 SiiRtcGetTime(struct SiiRtcInfo *rtc)
{
    struct rtc_datetime dateTime;

    if (sLocked == TRUE || rtc_get_time(&dateTime) != 0) {
        ClearTime(rtc);
        return FALSE;
    }

    StoreTime(rtc, &dateTime);

    return TRUE;
}
