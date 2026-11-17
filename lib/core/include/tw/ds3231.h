#pragma once
// Maxim DS3231 temperature-compensated RTC (+-2 ppm, ~0.17 s/day).
// Stores UTC in 24-hour mode; years 2000-2199 via the century bit.

#include "tw/drivers.h"

namespace tw {

class Ds3231 final : public IRtc {
public:
    static constexpr uint8_t kAddress = 0x68;
    static constexpr uint8_t kRegSeconds = 0x00;
    static constexpr uint8_t kRegStatus = 0x0F;
    static constexpr uint8_t kStatusOsf = 0x80;  // oscillator-stop flag
    static constexpr uint8_t kMonthCentury = 0x80;

    explicit Ds3231(II2cBus& bus) : bus_(bus) {}

    bool present();
    std::optional<int64_t> readUnix() override;
    // Writing the seconds register also resets the DS3231's sub-second
    // countdown, so callers should write exactly on a second boundary.
    bool writeUnix(int64_t unixSeconds) override;

    static constexpr uint8_t toBcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
    static constexpr int fromBcd(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }

private:
    bool readStatus(uint8_t& status);
    II2cBus& bus_;
};

}  // namespace tw
