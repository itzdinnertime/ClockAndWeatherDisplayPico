#pragma once
// SNTP (RFC 4330) packet building and parsing.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tw::ntp {

constexpr std::size_t kPacketSize = 48;
constexpr uint16_t kPort = 123;
constexpr uint32_t kUnixEpochDelta = 2208988800u;  // seconds from 1900-01-01 to 1970-01-01

// Client request: LI = 0, VN = 4, Mode = 3 (client).
void buildRequest(uint8_t (&packet)[kPacketSize]);

// Validates a server reply and returns its transmit timestamp as Unix ms.
// Rejects short packets, non-server modes, kiss-of-death (stratum 0),
// unsynchronised servers (LI = 3) and zero timestamps.
// Handles the 2036 NTP era rollover.
std::optional<int64_t> parseTransmitUnixMs(const uint8_t* data, std::size_t len);

// The server stamped its reply roughly half a round trip before we received
// it, so the time *at receive* is transmit + RTT / 2.
constexpr int64_t compensateRoundTrip(int64_t serverTxUnixMs, uint64_t sentMonoUs, uint64_t recvMonoUs) {
    const uint64_t rttUs = recvMonoUs >= sentMonoUs ? recvMonoUs - sentMonoUs : 0;
    return serverTxUnixMs + static_cast<int64_t>(rttUs / 2000);
}

}  // namespace tw::ntp
