#pragma once
// Fetches location (once) and current weather (every 10 minutes).
// On failure the last good report stays on screen; the renderer marks it
// stale once it's older than WEATHER_STALE_MS. After a reconnect, a refresh
// is triggered immediately rather than waiting for the next interval.

#include <cstdint>

#include "net/http_client.h"
#include "services/shared_state.h"

class WeatherService {
public:
    WeatherService(SharedState& shared, HttpClient& http);
    void tick(uint64_t nowMs, bool online);

private:
    enum class Pending { None, Location, Weather };

    void startRequest(uint64_t nowMs);
    void handleResponse(uint64_t nowMs, bool ok);

    SharedState& shared_;
    HttpClient& http_;
    Pending pending_ = Pending::None;
    bool haveLocation_;
    double latitude_;
    double longitude_;
    uint64_t nextFetchMs_ = 0;
    uint64_t lastSuccessMs_ = 0;
    bool everSucceeded_ = false;
    bool wasOnline_ = false;
};
