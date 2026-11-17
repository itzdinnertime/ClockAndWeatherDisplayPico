#pragma once
// Weather/location models and API response parsers.
//
// Location:  http://ip-api.com/json/?fields=status,lat,lon,city,offset
// Weather:   http://api.open-meteo.com/v1/forecast?...&timezone=auto
// Both are free, keyless and served over plain HTTP, which keeps TLS off the Pico.

#include <cstdint>
#include <optional>
#include <string_view>

namespace tw {

enum class WeatherIcon : uint8_t { Unknown, Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Thunder };

// Maps a WMO weather interpretation code (used by Open-Meteo) to an icon.
WeatherIcon iconForWmoCode(int code);

struct WeatherReport {
    float temperature = 0.0f;  // in the unit requested from the API
    int weatherCode = -1;      // WMO code
    bool isDay = true;
    int32_t utcOffsetSeconds = 0;  // local offset incl. DST, from timezone=auto

    bool operator==(const WeatherReport&) const = default;
};

struct GeoLocation {
    double latitude = 0.0;
    double longitude = 0.0;
    int32_t utcOffsetSeconds = 0;
    char city[32] = {};
};

std::optional<WeatherReport> parseOpenMeteo(std::string_view body);
std::optional<GeoLocation> parseIpApi(std::string_view body);

// Weather older than `maxAgeMs` is still shown, but flagged as stale.
constexpr bool isWeatherStale(uint64_t fetchedAtMs, uint64_t nowMs, uint64_t maxAgeMs) {
    return nowMs > fetchedAtMs && (nowMs - fetchedAtMs) > maxAgeMs;
}

}  // namespace tw
