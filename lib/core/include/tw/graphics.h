#pragma once
// Framebuffer, 5x7 font and the 64x32 screen layout.

#include <cstdint>
#include <string_view>

#include "tw/time_utils.h"
#include "tw/weather.h"

namespace tw {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

namespace colors {
constexpr Rgb kBlack{0, 0, 0};
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kTime{255, 190, 120};    // warm white
constexpr Rgb kDate{80, 200, 255};     // cyan
constexpr Rgb kInvalid{255, 40, 40};   // red
constexpr Rgb kStale{110, 110, 110};   // grey
constexpr Rgb kAmber{255, 150, 0};
constexpr Rgb kWifiOk{0, 200, 60};
constexpr Rgb kWifiDown{255, 30, 30};
}  // namespace colors

class Framebuffer {
public:
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 32;

    void clear(Rgb c = colors::kBlack) {
        for (auto& row : px_)
            for (auto& p : row) p = c;
    }
    void set(int x, int y, Rgb c) {
        if (x >= 0 && x < kWidth && y >= 0 && y < kHeight) px_[y][x] = c;
    }
    Rgb get(int x, int y) const {
        return (x >= 0 && x < kWidth && y >= 0 && y < kHeight) ? px_[y][x] : colors::kBlack;
    }
    void fillRect(int x, int y, int w, int h, Rgb c) {
        for (int j = y; j < y + h; ++j)
            for (int i = x; i < x + w; ++i) set(i, j, c);
    }

private:
    Rgb px_[kHeight][kWidth] = {};
};

// --- Text -----------------------------------------------------------------
// Digits and letters are fixed 5 px wide so the clock doesn't jitter;
// punctuation is proportional. '*' renders as a degree sign.
int textWidth(std::string_view text, int scale = 1);
// Returns the x coordinate just past the last glyph.
int drawText(Framebuffer& fb, int x, int y, std::string_view text, Rgb color, int scale = 1);

// --- Screen ---------------------------------------------------------------
struct ScreenModel {
    bool timeValid = false;
    CivilTime local{};
    bool use24h = false;
    bool colonOn = true;

    bool hasWeather = false;
    int temperature = 0;
    char unit = 'F';
    WeatherIcon icon = WeatherIcon::Unknown;
    bool isDay = true;
    bool weatherStale = false;

    bool wifiConnected = false;

    // Wi-Fi setup portal is running: show join instructions instead.
    bool setupMode = false;

    bool operator==(const ScreenModel&) const = default;
};

// Layout (64x32):
//   y 1..14   HH:MM at 2x scale, centred (colon blinks at 1 Hz)
//   y 16..22  "TUE NOV 17"
//   y 24..31  weather icon, temperature, stale dot, Wi-Fi status
// In setup mode: "WIFI SETUP" / "CONNECT TO" / <AP name> / <portal IP>.
void renderScreen(Framebuffer& fb, const ScreenModel& m);

namespace layout {
constexpr int kTimeY = 1;
constexpr int kTimeScale = 2;
constexpr int kDateY = 16;
constexpr int kWeatherY = 24;
constexpr int kTempX = 12;
constexpr int kStaleDotX = 9;
constexpr int kWifiX = 56;
}  // namespace layout

}  // namespace tw
