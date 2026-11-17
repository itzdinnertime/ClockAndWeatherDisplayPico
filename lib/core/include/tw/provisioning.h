#pragma once
// Wi-Fi credential storage format and validation for the setup portal.
//
// Credentials live in one 256-byte flash page:
//   [0..3]   magic "TWCF"     [4] version   [5] ssid length   [6] password length
//   [8..39]  ssid (32 bytes)  [40..103] password (64 bytes)
//   [104..107] CRC-32 of bytes 0..103 (little endian); remaining bytes 0xFF
// Erased flash (all 0xFF) and torn writes fail the magic/CRC check.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace tw {

constexpr const char* kSetupApName = "PICO-CLOCK";
constexpr uint32_t kPortalIp = 0xC0A80401;  // 192.168.4.1 (cyw43 AP default)
constexpr const char* kPortalUrl = "http://192.168.4.1/";

struct WifiCredentials {
    char ssid[33] = {};      // 802.11 SSIDs are at most 32 bytes
    char password[64] = {};  // WPA2 passphrases are 8..63 characters

    bool operator==(const WifiCredentials&) const = default;
};

enum class CredentialError { None, SsidEmpty, SsidTooLong, PasswordLength };

CredentialError validateCredentials(std::string_view ssid, std::string_view password);
const char* describe(CredentialError e);
// Validates and copies; nullopt if invalid.
std::optional<WifiCredentials> makeCredentials(std::string_view ssid, std::string_view password);

constexpr std::size_t kCredentialRecordSize = 256;
void encodeCredentials(const WifiCredentials& c, uint8_t (&out)[kCredentialRecordSize]);
std::optional<WifiCredentials> decodeCredentials(const uint8_t* data, std::size_t len);

uint32_t crc32(const uint8_t* data, std::size_t len);

// Fires once when a button is held continuously for `holdMs`.
class LongPressDetector {
public:
    explicit LongPressDetector(uint32_t holdMs) : holdMs_(holdMs) {}

    bool update(bool pressed, uint64_t nowMs) {
        if (!pressed) {
            down_ = false;
            fired_ = false;
            return false;
        }
        if (!down_) {
            down_ = true;
            sinceMs_ = nowMs;
        }
        if (!fired_ && nowMs - sinceMs_ >= holdMs_) {
            fired_ = true;
            return true;
        }
        return false;
    }

private:
    uint32_t holdMs_;
    bool down_ = false;
    bool fired_ = false;
    uint64_t sinceMs_ = 0;
};

}  // namespace tw
