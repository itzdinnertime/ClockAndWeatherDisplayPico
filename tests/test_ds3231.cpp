#include <gtest/gtest.h>

#include <array>

#include "tw/ds3231.h"
#include "tw/time_utils.h"

using namespace tw;

namespace {
// Emulates the DS3231's auto-incrementing register pointer.
class FakeI2cBus : public II2cBus {
public:
    std::array<uint8_t, 0x13> regs{};
    bool fail = false;

    bool write(uint8_t addr, const uint8_t* data, std::size_t len) override {
        if (fail || addr != Ds3231::kAddress || len == 0) return false;
        uint8_t ptr = data[0];
        for (std::size_t i = 1; i < len; ++i) regs[ptr++ % regs.size()] = data[i];
        return true;
    }
    bool writeRead(uint8_t addr, const uint8_t* w, std::size_t wl, uint8_t* r, std::size_t rl) override {
        if (fail || addr != Ds3231::kAddress || wl != 1) return false;
        uint8_t ptr = w[0];
        for (std::size_t i = 0; i < rl; ++i) r[i] = regs[ptr++ % regs.size()];
        return true;
    }
};
}  // namespace

TEST(Ds3231, WritesBcdRegistersAndClearsOscillatorStop) {
    FakeI2cBus bus;
    bus.regs[Ds3231::kRegStatus] = Ds3231::kStatusOsf | 0x08;  // OSF + EN32kHz
    Ds3231 rtc(bus);

    ASSERT_TRUE(rtc.writeUnix(1709210096));  // 2024-02-29 12:34:56 Thu
    EXPECT_EQ(bus.regs[0], 0x56);
    EXPECT_EQ(bus.regs[1], 0x34);
    EXPECT_EQ(bus.regs[2], 0x12);
    EXPECT_EQ(bus.regs[3], 5);  // weekday 4 (Thu) stored 1-based
    EXPECT_EQ(bus.regs[4], 0x29);
    EXPECT_EQ(bus.regs[5], 0x02);
    EXPECT_EQ(bus.regs[6], 0x24);
    EXPECT_EQ(bus.regs[Ds3231::kRegStatus], 0x08) << "OSF cleared, other bits kept";

    ASSERT_TRUE(rtc.writeUnix(unixFromCivil(2101, 12, 31, 23, 59, 58)));
    EXPECT_EQ(bus.regs[5], 0x12 | Ds3231::kMonthCentury);
    EXPECT_EQ(bus.regs[6], 0x01);
}

TEST(Ds3231, ReadsTimeAndRejectsPowerLossOrBusFailure) {
    FakeI2cBus bus;
    Ds3231 rtc(bus);
    ASSERT_TRUE(rtc.writeUnix(1709210096));
    EXPECT_TRUE(rtc.present());
    EXPECT_EQ(rtc.readUnix(), 1709210096);

    ASSERT_TRUE(rtc.writeUnix(unixFromCivil(2101, 12, 31, 23, 59, 58)));
    EXPECT_EQ(rtc.readUnix(), unixFromCivil(2101, 12, 31, 23, 59, 58));

    bus.regs[Ds3231::kRegStatus] |= Ds3231::kStatusOsf;  // battery died at some point
    EXPECT_FALSE(rtc.readUnix().has_value());

    bus.regs[Ds3231::kRegStatus] = 0;
    bus.regs[1] = 0x7A;  // corrupt minutes
    EXPECT_FALSE(rtc.readUnix().has_value());

    bus.fail = true;  // module unplugged
    EXPECT_FALSE(rtc.present());
    EXPECT_FALSE(rtc.readUnix().has_value());
}
