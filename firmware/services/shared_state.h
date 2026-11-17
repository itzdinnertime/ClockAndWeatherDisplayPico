#pragma once
// State shared between core 0 (network) and core 1 (display).
// Core 0 writes, core 1 copies a snapshot once per frame. A hardware
// spinlock (via critical_section) makes each copy atomic across cores.

#include <cstdint>

#include "config.h"
#include "pico/critical_section.h"
#include "pico/time.h"
#include "tw/time_utils.h"
#include "tw/weather.h"

struct SystemSnapshot {
    tw::SoftClock clock;
    int32_t utcOffsetSeconds = cfg::DEFAULT_UTC_OFFSET_S;
    bool hasWeather = false;
    tw::WeatherReport weather{};
    uint64_t weatherFetchedMs = 0;
    bool wifiUp = false;
    bool setupMode = false;
};

class SharedState {
public:
    void init() { critical_section_init(&cs_); }

    SystemSnapshot get() {
        critical_section_enter_blocking(&cs_);
        const SystemSnapshot copy = s_;
        critical_section_exit(&cs_);
        return copy;
    }

    template <typename F>
    void update(F&& f) {
        critical_section_enter_blocking(&cs_);
        f(s_);
        critical_section_exit(&cs_);
    }

private:
    critical_section_t cs_{};
    SystemSnapshot s_{};
};

inline uint64_t nowMs() { return time_us_64() / 1000; }
