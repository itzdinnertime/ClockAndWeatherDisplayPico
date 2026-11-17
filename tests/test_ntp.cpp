#include <gtest/gtest.h>

#include "tw/ntp.h"

using namespace tw;

namespace {
void putBe32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

void makeReply(uint8_t (&pkt)[ntp::kPacketSize], uint32_t secs, uint32_t frac) {
    for (auto& b : pkt) b = 0;
    pkt[0] = (0 << 6) | (4 << 3) | 4;  // LI 0, v4, server
    pkt[1] = 2;                        // stratum
    putBe32(pkt + 40, secs);
    putBe32(pkt + 44, frac);
}
}  // namespace

TEST(Ntp, ParsesTransmitTimestamp) {
    uint8_t req[ntp::kPacketSize];
    ntp::buildRequest(req);
    EXPECT_EQ(req[0], 0x23);  // v4 client

    uint8_t pkt[ntp::kPacketSize];
    makeReply(pkt, 1709210096u + ntp::kUnixEpochDelta, 0x80000000u);  // +0.5 s
    const auto ms = ntp::parseTransmitUnixMs(pkt, sizeof(pkt));
    ASSERT_TRUE(ms.has_value());
    EXPECT_EQ(*ms, 1709210096LL * 1000 + 500);

    // After the 2036 rollover NTP seconds wrap to small values (era 1).
    makeReply(pkt, 100u, 0);
    EXPECT_EQ(*ntp::parseTransmitUnixMs(pkt, sizeof(pkt)), ((int64_t{1} << 32) - ntp::kUnixEpochDelta + 100) * 1000);
    EXPECT_EQ(ntp::parseTransmitUnixMs(pkt, sizeof(pkt)).value() / 1000 / 86400 / 365 + 1970, 2036);
}

TEST(Ntp, RejectsInvalidPackets) {
    uint8_t pkt[ntp::kPacketSize];

    makeReply(pkt, 3900000000u, 0);
    EXPECT_FALSE(ntp::parseTransmitUnixMs(pkt, 47).has_value()) << "short packet";
    EXPECT_FALSE(ntp::parseTransmitUnixMs(nullptr, 48).has_value());

    makeReply(pkt, 3900000000u, 0);
    pkt[1] = 0;  // kiss-of-death
    EXPECT_FALSE(ntp::parseTransmitUnixMs(pkt, sizeof(pkt)).has_value());

    makeReply(pkt, 3900000000u, 0);
    pkt[0] = 0x23;  // client mode echoed back
    EXPECT_FALSE(ntp::parseTransmitUnixMs(pkt, sizeof(pkt)).has_value());

    makeReply(pkt, 3900000000u, 0);
    pkt[0] |= 0xC0;  // LI = 3, unsynchronised
    EXPECT_FALSE(ntp::parseTransmitUnixMs(pkt, sizeof(pkt)).has_value());

    makeReply(pkt, 0, 0);
    EXPECT_FALSE(ntp::parseTransmitUnixMs(pkt, sizeof(pkt)).has_value());
}

TEST(Ntp, CompensatesRoundTripDelay) {
    // Sent at mono 1.0 s, reply received at 1.2 s: server time + 100 ms.
    EXPECT_EQ(ntp::compensateRoundTrip(1'000'000, 1'000'000, 1'200'000), 1'000'100);
    // Clock anomalies never move time backwards.
    EXPECT_EQ(ntp::compensateRoundTrip(1'000'000, 2'000'000, 1'000'000), 1'000'000);
}
