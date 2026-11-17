#include "services/weather_service.h"

#include <cstdio>

#include "tw/weather.h"

namespace {
constexpr const char* kGeoHost = "ip-api.com";
constexpr const char* kGeoUri = "/json/?fields=status,lat,lon,city,offset";
constexpr const char* kWeatherHost = "api.open-meteo.com";
}  // namespace

WeatherService::WeatherService(SharedState& shared, HttpClient& http)
    : shared_(shared),
      http_(http),
      haveLocation_(cfg::USE_FIXED_LOCATION),
      latitude_(cfg::FIXED_LATITUDE),
      longitude_(cfg::FIXED_LONGITUDE) {}

void WeatherService::tick(uint64_t nowMs, bool online) {
    if (pending_ != Pending::None) {
        const HttpClient::Outcome o = http_.takeOutcome();
        if (o == HttpClient::Outcome::None) return;  // still in flight
        handleResponse(nowMs, o == HttpClient::Outcome::Ok);
    }

    if (online && !wasOnline_) {
        // Back online: refresh right away if what we're showing is old.
        if (!everSucceeded_ || nowMs - lastSuccessMs_ >= cfg::WEATHER_REFRESH_MS) nextFetchMs_ = nowMs;
    }
    wasOnline_ = online;

    if (online && nowMs >= nextFetchMs_) startRequest(nowMs);
}

void WeatherService::startRequest(uint64_t nowMs) {
    bool started;
    if (!haveLocation_) {
        started = http_.get(kGeoHost, kGeoUri);
        pending_ = Pending::Location;
    } else {
        char uri[256];
        std::snprintf(uri, sizeof(uri),
                      "/v1/forecast?latitude=%.4f&longitude=%.4f"
                      "&current=temperature_2m,weather_code,is_day"
                      "&temperature_unit=%s&timezone=auto",
                      latitude_, longitude_, cfg::USE_FAHRENHEIT ? "fahrenheit" : "celsius");
        started = http_.get(kWeatherHost, uri);
        pending_ = Pending::Weather;
    }
    if (!started) {
        pending_ = Pending::None;
        nextFetchMs_ = nowMs + cfg::WEATHER_RETRY_MS;
    }
}

void WeatherService::handleResponse(uint64_t nowMs, bool ok) {
    const Pending which = pending_;
    pending_ = Pending::None;
    nextFetchMs_ = nowMs + cfg::WEATHER_RETRY_MS;  // default: retry soon
    if (!ok) return;

    if (which == Pending::Location) {
        const auto geo = tw::parseIpApi(http_.body());
        if (!geo) return;
        haveLocation_ = true;
        latitude_ = geo->latitude;
        longitude_ = geo->longitude;
        shared_.update([&](SystemSnapshot& s) { s.utcOffsetSeconds = geo->utcOffsetSeconds; });
        std::printf("[weather] located: %s (%.2f, %.2f)\n", geo->city, geo->latitude, geo->longitude);
        nextFetchMs_ = nowMs;  // fetch weather straight away
        return;
    }

    const auto report = tw::parseOpenMeteo(http_.body());
    if (!report) return;
    shared_.update([&](SystemSnapshot& s) {
        s.weather = *report;
        s.hasWeather = true;
        s.weatherFetchedMs = nowMs;
        s.utcOffsetSeconds = report->utcOffsetSeconds;  // tracks DST changes
    });
    lastSuccessMs_ = nowMs;
    everSucceeded_ = true;
    nextFetchMs_ = nowMs + cfg::WEATHER_REFRESH_MS;
    std::printf("[weather] %.1f, code %d\n", report->temperature, report->weatherCode);
}
