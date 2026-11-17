#include "tw/captive_net.h"

#include <cstring>

namespace {
uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
}
void put16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}
}  // namespace

namespace tw::dns {

std::size_t buildCaptiveReply(const uint8_t* q, std::size_t len, uint32_t ipv4, uint8_t* out, std::size_t cap) {
    constexpr std::size_t kHeader = 12;
    if (q == nullptr || len < kHeader + 5) return 0;
    const uint16_t flags = be16(q + 2);
    if ((flags & 0x8000) || ((flags >> 11) & 0xF) != 0) return 0;  // must be a standard query
    if (be16(q + 4) != 1) return 0;                               // exactly one question

    // Walk the question name (uncompressed labels).
    std::size_t i = kHeader;
    while (true) {
        if (i >= len) return 0;
        const uint8_t labelLen = q[i];
        if (labelLen == 0) break;
        if (labelLen > 63) return 0;
        i += 1 + labelLen;
    }
    const std::size_t questionEnd = i + 1 + 4;  // terminator + QTYPE + QCLASS
    if (questionEnd > len) return 0;
    const uint16_t qtype = be16(q + i + 1);
    const uint16_t qclass = be16(q + i + 3);
    const bool answer = (qtype == 1 || qtype == 255) && qclass == 1;

    const std::size_t total = questionEnd + (answer ? 16 : 0);
    if (total > cap) return 0;

    std::memcpy(out, q, questionEnd);
    put16(out + 2, static_cast<uint16_t>(0x8400 | (flags & 0x0100)));  // QR, AA, copy RD
    put16(out + 6, answer ? 1 : 0);                                     // ANCOUNT
    put16(out + 8, 0);                                                  // NSCOUNT
    put16(out + 10, 0);                                                 // ARCOUNT (drop EDNS)
    if (answer) {
        uint8_t* a = out + questionEnd;
        put16(a, 0xC00C);  // pointer to the question name
        put16(a + 2, 1);   // A
        put16(a + 4, 1);   // IN
        put32(a + 6, 60);  // TTL
        put16(a + 10, 4);
        put32(a + 12, ipv4);
    }
    return total;
}

}  // namespace tw::dns

namespace tw::dhcp {

namespace {
constexpr std::size_t kOptionsOffset = 240;
constexpr uint32_t kMagicCookie = 0x63825363;
}  // namespace

std::optional<ClientMessage> parseClientMessage(const uint8_t* d, std::size_t len) {
    if (d == nullptr || len < kOptionsOffset + 3) return std::nullopt;
    if (d[0] != 1 || d[1] != 1 || d[2] != 6) return std::nullopt;  // BOOTREQUEST, Ethernet
    if (be32(d + 236) != kMagicCookie) return std::nullopt;

    ClientMessage m;
    m.xid = be32(d + 4);
    m.flags = be16(d + 10);
    std::memcpy(m.mac, d + 28, 6);

    for (std::size_t i = kOptionsOffset; i < len;) {
        const uint8_t code = d[i];
        if (code == 0) {  // pad
            ++i;
            continue;
        }
        if (code == 255) break;
        if (i + 1 >= len || i + 2 + d[i + 1] > len) return std::nullopt;
        if (code == 53 && d[i + 1] == 1) m.type = d[i + 2];
        i += 2 + d[i + 1];
    }
    if (m.type == 0) return std::nullopt;
    return m;
}

std::size_t buildReply(const ClientMessage& msg, MessageType type, uint32_t yiaddr, uint32_t serverIp,
                       uint32_t leaseSeconds, uint8_t* out, std::size_t cap) {
    if (cap < kMinPacket) return 0;
    std::memset(out, 0, kMinPacket);
    out[0] = 2;  // BOOTREPLY
    out[1] = 1;
    out[2] = 6;
    put32(out + 4, msg.xid);
    put16(out + 10, msg.flags);
    put32(out + 16, yiaddr);
    put32(out + 20, serverIp);
    std::memcpy(out + 28, msg.mac, 6);
    put32(out + 236, kMagicCookie);

    uint8_t* o = out + kOptionsOffset;
    auto opt32 = [&](uint8_t code, uint32_t v) {
        o[0] = code;
        o[1] = 4;
        put32(o + 2, v);
        o += 6;
    };
    o[0] = 53;
    o[1] = 1;
    o[2] = type;
    o += 3;
    opt32(54, serverIp);       // server identifier
    opt32(51, leaseSeconds);   // lease time
    opt32(1, 0xFFFFFF00);      // subnet mask /24
    opt32(3, serverIp);        // router
    opt32(6, serverIp);        // DNS server -> our captive DNS
    *o = 255;
    return kMinPacket;
}

std::optional<uint32_t> LeasePool::assign(const uint8_t (&mac)[6]) {
    for (int i = 0; i < kMaxLeases; ++i)
        if (used_[i] && std::memcmp(macs_[i], mac, 6) == 0) return firstIp_ + static_cast<uint32_t>(i);
    for (int i = 0; i < kMaxLeases; ++i) {
        if (!used_[i]) {
            used_[i] = true;
            std::memcpy(macs_[i], mac, 6);
            return firstIp_ + static_cast<uint32_t>(i);
        }
    }
    return std::nullopt;
}

}  // namespace tw::dhcp
