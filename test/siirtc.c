#include "global.h"
#include "siirtc.h"
#include "test/test.h"

// Native only: a failed read of the clock answers with zeros, not with the
// bytes the caller's struct held, because RtcGetRawInfo ignores the result.
// SiiRtcProtect locks the chip, which makes every read fail.
#if PLATFORM_NATIVE

#define UNTOUCHED 0xAA

static void FillWithUntouched(struct SiiRtcInfo *rtc)
{
    memset(rtc, UNTOUCHED, sizeof(*rtc));
}

TEST("SiiRtcGetDateTime on a locked chip returns FALSE and zeroes the date and time")
{
    struct SiiRtcInfo rtc;

    FillWithUntouched(&rtc);
    SiiRtcProtect();
    bool8 result = SiiRtcGetDateTime(&rtc);
    SiiRtcUnprotect();

    EXPECT_EQ(result, FALSE);
    EXPECT_EQ(rtc.year, 0);
    EXPECT_EQ(rtc.month, 0);
    EXPECT_EQ(rtc.day, 0);
    EXPECT_EQ(rtc.dayOfWeek, 0);
    EXPECT_EQ(rtc.hour, 0);
    EXPECT_EQ(rtc.minute, 0);
    EXPECT_EQ(rtc.second, 0);
    EXPECT_EQ(rtc.status, UNTOUCHED);
}

TEST("SiiRtcGetTime on a locked chip returns FALSE and zeroes the time, leaving the date")
{
    struct SiiRtcInfo rtc;

    FillWithUntouched(&rtc);
    SiiRtcProtect();
    bool8 result = SiiRtcGetTime(&rtc);
    SiiRtcUnprotect();

    EXPECT_EQ(result, FALSE);
    EXPECT_EQ(rtc.hour, 0);
    EXPECT_EQ(rtc.minute, 0);
    EXPECT_EQ(rtc.second, 0);
    EXPECT_EQ(rtc.year, UNTOUCHED);
    EXPECT_EQ(rtc.day, UNTOUCHED);
}

#endif
