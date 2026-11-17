#include "tw/ntp.h"

#include <cstring>

namespace tw::ntp {

namespace {
uint32_t readBe32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}
}  // namespace

void buildRequest(uint8_t (&packet)[kPacketSize]) {
    std::memset(packet, 0, kPacketSize);
    packet[0] = (0 << 6) | (4 << 3) | 3;
}

std::optional<int64_t> parseTransmitUnixMs(const uint8_t* data, std::size_t len) {
    if (data == nullptr || len < kPacketSize) return std::nullopt;

    const uint8_t leap = data[0] >> 6;
    const uint8_t mode = data[0] & 0x07;
    const uint8_t stratum = data[1];
    if (mode != 4 && mode != 5) return std::nullopt;     // must be server/broadcast
    if (stratum == 0 || stratum > 15) return std::nullopt;  // kiss-of-death / invalid
    if (leap == 3) return std::nullopt;                   // server clock unsynchronised

    const uint32_t secs = readBe32(data + 40);
    const uint32_t frac = readBe32(data + 44);
    if (secs == 0 && frac == 0) return std::nullopt;

    // Era 0 ends in Feb 2036. Timestamps with the MSB clear are taken to be
    // era 1, which keeps this correct from 1968 through 2104.
    int64_t unixSecs = static_cast<int64_t>(secs) - kUnixEpochDelta;
    if ((secs & 0x80000000u) == 0) unixSecs += int64_t{1} << 32;

    const int64_t ms = static_cast<int64_t>((uint64_t{frac} * 1000) >> 32);
    return unixSecs * 1000 + ms;
}

}  // namespace tw::ntp
