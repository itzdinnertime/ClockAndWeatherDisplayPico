#include "tw/ds3231.h"

#include "tw/time_utils.h"

namespace tw {

bool Ds3231::readStatus(uint8_t& status) {
    const uint8_t reg = kRegStatus;
    return bus_.writeRead(kAddress, &reg, 1, &status, 1);
}

bool Ds3231::present() {
    uint8_t status = 0;
    return readStatus(status);
}

std::optional<int64_t> Ds3231::readUnix() {
    uint8_t status = 0;
    if (!readStatus(status) || (status & kStatusOsf)) return std::nullopt;

    const uint8_t reg = kRegSeconds;
    uint8_t r[7] = {};
    if (!bus_.writeRead(kAddress, &reg, 1, r, sizeof(r))) return std::nullopt;

    const int second = fromBcd(r[0] & 0x7F);
    const int minute = fromBcd(r[1] & 0x7F);
    const int hour = fromBcd(r[2] & 0x3F);  // 24-hour mode
    const int day = fromBcd(r[4] & 0x3F);
    const int month = fromBcd(r[5] & 0x1F);
    const int year = 2000 + fromBcd(r[6]) + ((r[5] & kMonthCentury) ? 100 : 0);

    if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 || month < 1 || month > 12) {
        return std::nullopt;
    }
    return unixFromCivil(year, month, day, hour, minute, second);
}

bool Ds3231::writeUnix(int64_t unixSeconds) {
    const CivilTime t = civilFromUnix(unixSeconds);
    if (t.year < 2000 || t.year > 2199) return false;

    const int yy = t.year % 100;
    const uint8_t frame[8] = {
        kRegSeconds,
        toBcd(t.second),
        toBcd(t.minute),
        toBcd(t.hour),  // bit 6 clear = 24-hour mode
        static_cast<uint8_t>(t.weekday + 1),
        toBcd(t.day),
        static_cast<uint8_t>(toBcd(t.month) | (t.year >= 2100 ? kMonthCentury : 0)),
        toBcd(yy),
    };
    if (!bus_.write(kAddress, frame, sizeof(frame))) return false;

    // Clear the oscillator-stop flag so future reads are trusted.
    uint8_t status = 0;
    if (!readStatus(status)) return false;
    const uint8_t clear[2] = {kRegStatus, static_cast<uint8_t>(status & ~kStatusOsf)};
    return bus_.write(kAddress, clear, sizeof(clear));
}

}  // namespace tw
