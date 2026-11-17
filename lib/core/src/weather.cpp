#include "tw/weather.h"

#include <cstring>

#include "tw/json_lite.h"

namespace tw {

WeatherIcon iconForWmoCode(int code) {
    switch (code) {
        case 0:
        case 1:
            return WeatherIcon::Clear;
        case 2:
            return WeatherIcon::PartlyCloudy;
        case 3:
            return WeatherIcon::Cloudy;
        case 45:
        case 48:
            return WeatherIcon::Fog;
        case 51: case 53: case 55: case 56: case 57:
            return WeatherIcon::Drizzle;
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82:
            return WeatherIcon::Rain;
        case 71: case 73: case 75: case 77:
        case 85: case 86:
            return WeatherIcon::Snow;
        case 95: case 96: case 99:
            return WeatherIcon::Thunder;
        default:
            return WeatherIcon::Unknown;
    }
}

std::optional<WeatherReport> parseOpenMeteo(std::string_view body) {
    const auto current = json::getObject(body, "current");
    if (!current) return std::nullopt;

    const auto temp = json::getNumber(*current, "temperature_2m");
    const auto code = json::getNumber(*current, "weather_code");
    const auto offset = json::getNumber(body, "utc_offset_seconds");
    if (!temp || !code || !offset) return std::nullopt;

    WeatherReport r;
    r.temperature = static_cast<float>(*temp);
    r.weatherCode = static_cast<int>(*code);
    r.utcOffsetSeconds = static_cast<int32_t>(*offset);
    r.isDay = json::getNumber(*current, "is_day").value_or(1.0) != 0.0;
    return r;
}

std::optional<GeoLocation> parseIpApi(std::string_view body) {
    const auto status = json::getString(body, "status");
    if (!status || *status != "success") return std::nullopt;

    const auto lat = json::getNumber(body, "lat");
    const auto lon = json::getNumber(body, "lon");
    if (!lat || !lon) return std::nullopt;

    GeoLocation g;
    g.latitude = *lat;
    g.longitude = *lon;
    g.utcOffsetSeconds = static_cast<int32_t>(json::getNumber(body, "offset").value_or(0.0));
    if (const auto city = json::getString(body, "city")) {
        const std::size_t n = city->size() < sizeof(g.city) - 1 ? city->size() : sizeof(g.city) - 1;
        std::memcpy(g.city, city->data(), n);
        g.city[n] = '\0';
    }
    return g;
}

}  // namespace tw
