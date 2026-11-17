#pragma once
// Calendar math and a monotonic-disciplined software clock.
// Pure C++: no Pico SDK headers, so everything here is host-testable.

#include <cstdint>

namespace tw {

struct CivilTime {
    int year = 1970;
    int month = 1;    // 1..12
    int day = 1;      // 1..31
    int hour = 0;     // 0..23
    int minute = 0;   // 0..59
    int second = 0;   // 0..59
    int weekday = 4;  // 0 = Sunday .. 6 = Saturday

    bool operator==(const CivilTime&) const = default;
};

// Floor division / modulo that behave correctly for negative values.
constexpr int64_t floorDiv(int64_t a, int64_t b) {
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}
constexpr int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

// Proleptic Gregorian conversions (Howard Hinnant's algorithms).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d);
CivilTime civilFromUnix(int64_t unixSeconds);
int64_t unixFromCivil(int year, int month, int day, int hour, int minute, int second);

inline CivilTime localTime(int64_t unixSeconds, int32_t utcOffsetSeconds) {
    return civilFromUnix(unixSeconds + utcOffsetSeconds);
}

// 0 -> 12, 13 -> 1, 12 -> 12.
constexpr int to12Hour(int hour24) {
    int h = hour24 % 12;
    return h == 0 ? 12 : h;
}

const char* weekdayAbbrev(int weekday);  // "SUN".."SAT"
const char* monthAbbrev(int month);      // "JAN".."DEC"

// Wall-clock time anchored to a monotonic timer.
//
// The firmware sets the anchor from NTP (or the DS3231 when offline) and then
// derives "now" from the RP2040's 64-bit microsecond timer, which is shared by
// both cores. Re-anchoring periodically bounds the crystal drift.
class SoftClock {
public:
    void set(int64_t unixMs, uint64_t monoUs) {
        baseUnixMs_ = unixMs;
        baseMonoUs_ = monoUs;
        valid_ = true;
    }
    void invalidate() { valid_ = false; }
    bool valid() const { return valid_; }

    int64_t nowUnixMs(uint64_t monoUs) const {
        return baseUnixMs_ + static_cast<int64_t>(monoUs - baseMonoUs_) / 1000;
    }

private:
    bool valid_ = false;
    int64_t baseUnixMs_ = 0;
    uint64_t baseMonoUs_ = 0;
};

}  // namespace tw
