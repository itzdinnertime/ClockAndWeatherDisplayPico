#include <gtest/gtest.h>

#include <string>

#include "tw/http_util.h"
#include "tw/json_lite.h"
#include "tw/weather.h"

using namespace tw;

namespace {
// Trimmed real Open-Meteo response. Note "current_units" also contains
// "temperature_2m", which must not be confused with the actual reading.
constexpr const char* kOpenMeteo = R"({
  "latitude": 30.27, "longitude": -97.74, "utc_offset_seconds": -21600,
  "timezone": "America/Chicago",
  "current_units": {"time": "iso8601", "temperature_2m": "°F", "weather_code": "wmo code"},
  "current": {"time": "2026-11-17T10:00", "interval": 900,
              "temperature_2m": 61.3, "weather_code": 63, "is_day": 1}
})";
}  // namespace

TEST(WeatherParser, ParsesOpenMeteoCurrentObjectOnly) {
    const auto r = parseOpenMeteo(kOpenMeteo);
    ASSERT_TRUE(r.has_value());
    EXPECT_FLOAT_EQ(r->temperature, 61.3f);
    EXPECT_EQ(r->weatherCode, 63);
    EXPECT_TRUE(r->isDay);
    EXPECT_EQ(r->utcOffsetSeconds, -21600);

    EXPECT_EQ(iconForWmoCode(r->weatherCode), WeatherIcon::Rain);
    EXPECT_EQ(iconForWmoCode(0), WeatherIcon::Clear);
    EXPECT_EQ(iconForWmoCode(2), WeatherIcon::PartlyCloudy);
    EXPECT_EQ(iconForWmoCode(75), WeatherIcon::Snow);
    EXPECT_EQ(iconForWmoCode(95), WeatherIcon::Thunder);
    EXPECT_EQ(iconForWmoCode(12345), WeatherIcon::Unknown);
}

TEST(WeatherParser, MissingOrMalformedFieldsReturnNullopt) {
    EXPECT_FALSE(parseOpenMeteo("").has_value());
    EXPECT_FALSE(parseOpenMeteo("not json").has_value());
    // Truncated mid-response (e.g. connection dropped)
    const std::string full = kOpenMeteo;
    EXPECT_FALSE(parseOpenMeteo(full.substr(0, full.find("\"current\":") + 30)).has_value());
    // Missing weather_code
    EXPECT_FALSE(parseOpenMeteo(R"({"utc_offset_seconds":0,"current":{"temperature_2m":1}})").has_value());
    // Key only present in a nested object must not match at the top level
    EXPECT_FALSE(json::getNumber(R"({"a":{"x":1}})", "x").has_value());
    // Strings with braces/escaped quotes don't confuse the scanner
    EXPECT_EQ(json::getNumber(R"({"s":"}{\",","x":-4.5e1})", "x").value(), -45.0);
}

TEST(WeatherParser, ParsesIpApiLocation) {
    const auto g = parseIpApi(R"({"status":"success","city":"Austin","lat":30.2672,"lon":-97.7431,"offset":-21600})");
    ASSERT_TRUE(g.has_value());
    EXPECT_DOUBLE_EQ(g->latitude, 30.2672);
    EXPECT_DOUBLE_EQ(g->longitude, -97.7431);
    EXPECT_EQ(g->utcOffsetSeconds, -21600);
    EXPECT_STREQ(g->city, "Austin");

    EXPECT_FALSE(parseIpApi(R"({"status":"fail","message":"private range"})").has_value());
}

TEST(HttpUtil, DechunksBodyInPlace) {
    EXPECT_TRUE(http::headersIndicateChunked("HTTP/1.1 200 OK\r\nTRANSFER-ENCODING:  Chunked\r\n\r\n"));
    EXPECT_FALSE(http::headersIndicateChunked("HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\n"));

    std::string body = "4\r\n{\"a\"\r\nA;ext=1\r\n:12345678}\r\n0\r\n\r\n";
    const auto n = http::dechunk(body, body.data(), body.size());
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(body.substr(0, *n), "{\"a\":12345678}");

    std::string bad = "zz\r\nabc";
    EXPECT_FALSE(http::dechunk(bad, bad.data(), bad.size()).has_value());
    std::string overflow = "5\r\nhello\r\n0\r\n\r\n";
    char small[3];
    EXPECT_FALSE(http::dechunk(overflow, small, sizeof(small)).has_value());
}

TEST(WeatherStaleness, FlagsStaleOnlyAfterMaxAge) {
    constexpr uint64_t kMaxAge = 30 * 60 * 1000;
    EXPECT_FALSE(isWeatherStale(1000, 1000, kMaxAge));
    EXPECT_FALSE(isWeatherStale(1000, 1000 + kMaxAge, kMaxAge));
    EXPECT_TRUE(isWeatherStale(1000, 1001 + kMaxAge, kMaxAge));
    EXPECT_FALSE(isWeatherStale(5000, 1000, kMaxAge));  // clock never goes backwards into "stale"
}
