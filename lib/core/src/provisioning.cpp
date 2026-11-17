#include "tw/provisioning.h"

#include <cstring>

namespace tw {

namespace {
constexpr uint8_t kMagic[4] = {'T', 'W', 'C', 'F'};
constexpr uint8_t kVersion = 1;
constexpr std::size_t kSsidOffset = 8;
constexpr std::size_t kPasswordOffset = 40;
constexpr std::size_t kCrcOffset = 104;

uint8_t boundedLength(const char* s, std::size_t max) {
    std::size_t n = 0;
    while (n < max && s[n] != '\0') ++n;
    return static_cast<uint8_t>(n);
}
}  // namespace

CredentialError validateCredentials(std::string_view ssid, std::string_view password) {
    if (ssid.empty()) return CredentialError::SsidEmpty;
    if (ssid.size() > 32) return CredentialError::SsidTooLong;
    if (!password.empty() && (password.size() < 8 || password.size() > 63)) return CredentialError::PasswordLength;
    return CredentialError::None;
}

const char* describe(CredentialError e) {
    switch (e) {
        case CredentialError::None:
            return "";
        case CredentialError::SsidEmpty:
            return "Please enter a network name.";
        case CredentialError::SsidTooLong:
            return "Network names are at most 32 characters.";
        case CredentialError::PasswordLength:
            return "Wi-Fi passwords are 8 to 63 characters (leave empty for an open network).";
    }
    return "";
}

std::optional<WifiCredentials> makeCredentials(std::string_view ssid, std::string_view password) {
    if (validateCredentials(ssid, password) != CredentialError::None) return std::nullopt;
    WifiCredentials c;
    std::memcpy(c.ssid, ssid.data(), ssid.size());
    std::memcpy(c.password, password.data(), password.size());
    return c;
}

uint32_t crc32(const uint8_t* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void encodeCredentials(const WifiCredentials& c, uint8_t (&out)[kCredentialRecordSize]) {
    std::memset(out, 0xFF, sizeof(out));
    std::memset(out, 0, kCrcOffset);
    std::memcpy(out, kMagic, sizeof(kMagic));
    out[4] = kVersion;
    out[5] = boundedLength(c.ssid, 32);
    out[6] = boundedLength(c.password, 63);
    std::memcpy(out + kSsidOffset, c.ssid, out[5]);
    std::memcpy(out + kPasswordOffset, c.password, out[6]);
    const uint32_t crc = crc32(out, kCrcOffset);
    for (int i = 0; i < 4; ++i) out[kCrcOffset + i] = static_cast<uint8_t>(crc >> (8 * i));
}

std::optional<WifiCredentials> decodeCredentials(const uint8_t* data, std::size_t len) {
    if (data == nullptr || len < kCrcOffset + 4) return std::nullopt;
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 || data[4] != kVersion) return std::nullopt;

    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) stored |= uint32_t{data[kCrcOffset + i]} << (8 * i);
    if (stored != crc32(data, kCrcOffset)) return std::nullopt;

    const std::string_view ssid(reinterpret_cast<const char*>(data + kSsidOffset), data[5] <= 32 ? data[5] : 0);
    const std::string_view pass(reinterpret_cast<const char*>(data + kPasswordOffset), data[6] <= 63 ? data[6] : 0);
    return makeCredentials(ssid, pass);
}

}  // namespace tw
