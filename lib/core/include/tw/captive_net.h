#pragma once
// Tiny DHCP and DNS servers' packet logic for the setup access point.
// IPv4 addresses are host-order uint32_t (192.168.4.1 == 0xC0A80401).

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tw::dns {

constexpr uint16_t kPort = 53;

// Answers every A/IN question with `ipv4` so phones' connectivity checks land
// on the portal (which triggers the "sign in to network" prompt). Other query
// types get an empty answer. Returns 0 if `query` is not a valid DNS query.
std::size_t buildCaptiveReply(const uint8_t* query, std::size_t len, uint32_t ipv4, uint8_t* out, std::size_t cap);

}  // namespace tw::dns

namespace tw::dhcp {

constexpr uint16_t kServerPort = 67;
constexpr uint16_t kClientPort = 68;
constexpr std::size_t kMinPacket = 300;  // BOOTP minimum

enum MessageType : uint8_t { Discover = 1, Offer = 2, Request = 3, Decline = 4, Ack = 5, Nak = 6, Release = 7, Inform = 8 };

struct ClientMessage {
    uint8_t type = 0;
    uint32_t xid = 0;
    uint16_t flags = 0;
    uint8_t mac[6] = {};
};

std::optional<ClientMessage> parseClientMessage(const uint8_t* data, std::size_t len);

// Builds an OFFER/ACK assigning `yiaddr`, advertising us as router and DNS.
std::size_t buildReply(const ClientMessage& msg, MessageType type, uint32_t yiaddr, uint32_t serverIp,
                       uint32_t leaseSeconds, uint8_t* out, std::size_t cap);

// Hands out addresses first..first+count-1, stable per MAC address.
class LeasePool {
public:
    static constexpr int kMaxLeases = 8;
    explicit LeasePool(uint32_t firstIp) : firstIp_(firstIp) {}
    std::optional<uint32_t> assign(const uint8_t (&mac)[6]);

private:
    uint32_t firstIp_;
    uint8_t macs_[kMaxLeases][6] = {};
    bool used_[kMaxLeases] = {};
};

}  // namespace tw::dhcp
