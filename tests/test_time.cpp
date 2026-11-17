#include <gtest/gtest.h>

#include "tw/time_utils.h"

using namespace tw;

TEST(TimeUtils, CivilFromUnixKnownDates) {
    const CivilTime epoch = civilFromUnix(0);
    EXPECT_EQ(epoch.year, 1970);
    EXPECT_EQ(epoch.month, 1);
    EXPECT_EQ(epoch.day, 1);
    EXPECT_EQ(epoch.weekday, 4);  // Thursday

    // Leap day 2024-02-29 12:34:56 UTC (a Thursday)
    const CivilTime leap = civilFromUnix(1709210096);
    EXPECT_EQ(leap, (CivilTime{2024, 2, 29, 12, 34, 56, 4}));
    EXPECT_EQ(unixFromCivil(2024, 2, 29, 12, 34, 56), 1709210096);

    // 2100 is not a leap year; round-trip across it.
    EXPECT_EQ(unixFromCivil(2100, 1, 1, 0, 0, 0), 4102444800);
    const CivilTime mar = civilFromUnix(unixFromCivil(2100, 2, 28, 23, 59, 59) + 1);
    EXPECT_EQ(mar.month, 3);
    EXPECT_EQ(mar.day, 1);
}

TEST(TimeUtils, LocalTimeAppliesOffsetAcrossMidnightAnd12HourFormat) {
    // 2024-03-01 03:30 UTC in UTC-6 is still Feb 29, 21:30 (9 PM).
    const int64_t utc = unixFromCivil(2024, 3, 1, 3, 30, 0);
    const CivilTime local = localTime(utc, -6 * 3600);
    EXPECT_EQ(local.month, 2);
    EXPECT_EQ(local.day, 29);
    EXPECT_EQ(local.hour, 21);
    EXPECT_STREQ(weekdayAbbrev(local.weekday), "THU");
    EXPECT_STREQ(monthAbbrev(local.month), "FEB");

    EXPECT_EQ(to12Hour(0), 12);
    EXPECT_EQ(to12Hour(12), 12);
    EXPECT_EQ(to12Hour(13), 1);
    EXPECT_EQ(to12Hour(21), 9);
}

TEST(TimeUtils, SoftClockTracksMonotonicTimer) {
    SoftClock clock;
    EXPECT_FALSE(clock.valid());

    clock.set(1'700'000'000'000, 5'000'000);  // anchored at mono t = 5 s
    EXPECT_TRUE(clock.valid());
    EXPECT_EQ(clock.nowUnixMs(5'000'000), 1'700'000'000'000);
    EXPECT_EQ(clock.nowUnixMs(5'000'000 + 86'400'000'000ULL), 1'700'000'000'000 + 86'400'000);  // +1 day
    EXPECT_EQ(clock.nowUnixMs(5'001'999), 1'700'000'000'001);  // truncates sub-ms

    clock.invalidate();
    EXPECT_FALSE(clock.valid());
}
