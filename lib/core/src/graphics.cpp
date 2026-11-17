#include "tw/graphics.h"

#include <cstdio>

namespace tw {

namespace {

// Classic 5x7 font, column-major, bit 0 = top row.
struct Glyph {
    char ch;
    uint8_t cols[5];
};

constexpr Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}}, {'*', {0x00, 0x06, 0x09, 0x09, 0x06}},  // degree
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}}, {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}}, {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}}, {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}}, {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}}, {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

const Glyph& glyphFor(char c) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph& g : kFont)
        if (g.ch == c) return g;
    return kFont[4];  // '?'
}

bool isFixedWidth(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '?' || c == '-';
}

// Visible column range [first, first + width) of a glyph.
void glyphSpan(const Glyph& g, int& first, int& width) {
    if (isFixedWidth(g.ch)) {
        first = 0;
        width = 5;
        return;
    }
    if (g.ch == ' ') {
        first = 0;
        width = 2;
        return;
    }
    int lo = 0, hi = 4;
    while (lo < 5 && g.cols[lo] == 0) ++lo;
    while (hi >= 0 && g.cols[hi] == 0) --hi;
    first = lo;
    width = hi >= lo ? hi - lo + 1 : 1;
}

// 8x8 icon layers, row-major, MSB = leftmost pixel.
struct Bitmap8 {
    uint8_t rows[8];
};
constexpr Bitmap8 kSun{{0x18, 0x42, 0x3C, 0xBD, 0xBD, 0x3C, 0x42, 0x18}};
constexpr Bitmap8 kMoon{{0x1C, 0x30, 0x60, 0x60, 0x60, 0x60, 0x30, 0x1C}};
constexpr Bitmap8 kSmallSun{{0x48, 0x30, 0xB4, 0x30, 0x48, 0x00, 0x00, 0x00}};
constexpr Bitmap8 kCloud{{0x00, 0x00, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E}};
constexpr Bitmap8 kCloudTop{{0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x00, 0x00}};
constexpr Bitmap8 kRain{{0, 0, 0, 0, 0, 0, 0x49, 0x92}};
constexpr Bitmap8 kDrizzle{{0, 0, 0, 0, 0, 0, 0x24, 0x00}};
constexpr Bitmap8 kSnow{{0, 0, 0, 0, 0, 0, 0xAA, 0x55}};
constexpr Bitmap8 kBolt{{0, 0, 0, 0, 0, 0x10, 0x08, 0x10}};
constexpr Bitmap8 kFog{{0x00, 0x7E, 0x00, 0xFF, 0x00, 0x7E, 0x00, 0xFF}};

constexpr Rgb kSunColor{255, 200, 0};
constexpr Rgb kMoonColor{200, 200, 140};
constexpr Rgb kCloudColor{180, 180, 190};
constexpr Rgb kRainColor{40, 120, 255};

void drawBitmap(Framebuffer& fb, int x, int y, const Bitmap8& bm, Rgb c) {
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col)
            if (bm.rows[row] & (0x80 >> col)) fb.set(x + col, y + row, c);
}

void drawWeatherIcon(Framebuffer& fb, int x, int y, WeatherIcon icon, bool isDay) {
    switch (icon) {
        case WeatherIcon::Clear:
            drawBitmap(fb, x, y, isDay ? kSun : kMoon, isDay ? kSunColor : kMoonColor);
            break;
        case WeatherIcon::PartlyCloudy:
            drawBitmap(fb, x, y, kSmallSun, isDay ? kSunColor : kMoonColor);
            drawBitmap(fb, x, y, kCloud, kCloudColor);
            break;
        case WeatherIcon::Cloudy:
            drawBitmap(fb, x, y, kCloud, kCloudColor);
            break;
        case WeatherIcon::Fog:
            drawBitmap(fb, x, y, kFog, kCloudColor);
            break;
        case WeatherIcon::Drizzle:
            drawBitmap(fb, x, y, kCloudTop, kCloudColor);
            drawBitmap(fb, x, y, kDrizzle, kRainColor);
            break;
        case WeatherIcon::Rain:
            drawBitmap(fb, x, y, kCloudTop, kCloudColor);
            drawBitmap(fb, x, y, kRain, kRainColor);
            break;
        case WeatherIcon::Snow:
            drawBitmap(fb, x, y, kCloudTop, kCloudColor);
            drawBitmap(fb, x, y, kSnow, colors::kWhite);
            break;
        case WeatherIcon::Thunder:
            drawBitmap(fb, x, y, kCloudTop, kCloudColor);
            drawBitmap(fb, x, y, kBolt, kSunColor);
            break;
        case WeatherIcon::Unknown:
            drawText(fb, x + 1, y, "?", kCloudColor);
            break;
    }
}

constexpr Bitmap8 kWifiGlyph{{0x00, 0x7E, 0x81, 0x3C, 0x42, 0x18, 0x00, 0x18}};

void drawWifi(Framebuffer& fb, int x, int y, bool connected) {
    drawBitmap(fb, x, y, kWifiGlyph, connected ? colors::kWifiOk : colors::kWifiDown);
    if (!connected) {
        for (int i = 0; i < 8; ++i) fb.set(x + i, y + 7 - i, colors::kWifiDown);  // slash
    }
}

}  // namespace

int textWidth(std::string_view text, int scale) {
    int w = 0;
    for (char c : text) {
        int first, width;
        glyphSpan(glyphFor(c), first, width);
        w += (width + 1) * scale;
    }
    return w > 0 ? w - scale : 0;  // no trailing gap
}

int drawText(Framebuffer& fb, int x, int y, std::string_view text, Rgb color, int scale) {
    for (char c : text) {
        const Glyph& g = glyphFor(c);
        int first, width;
        glyphSpan(g, first, width);
        for (int col = 0; col < width; ++col) {
            const uint8_t bits = (first + col < 5) ? g.cols[first + col] : 0;
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) fb.fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += (width + 1) * scale;
    }
    return x;
}

void renderScreen(Framebuffer& fb, const ScreenModel& m) {
    using namespace layout;
    fb.clear();

    // Time
    char hh[3], mm[3];
    if (m.timeValid) {
        const int hour = m.use24h ? m.local.hour : to12Hour(m.local.hour);
        std::snprintf(hh, sizeof(hh), m.use24h ? "%02d" : "%d", hour);
        std::snprintf(mm, sizeof(mm), "%02d", m.local.minute);
    } else {
        std::snprintf(hh, sizeof(hh), "--");
        std::snprintf(mm, sizeof(mm), "--");
    }
    const Rgb timeColor = m.timeValid ? colors::kTime : colors::kInvalid;
    const int colonW = textWidth(":", kTimeScale);
    const int gap = kTimeScale;
    const int total = textWidth(hh, kTimeScale) + gap + colonW + gap + textWidth(mm, kTimeScale);
    int x = (Framebuffer::kWidth - total) / 2;
    x = drawText(fb, x, kTimeY, hh, timeColor, kTimeScale);
    drawText(fb, x, kTimeY, ":", m.colonOn ? timeColor : colors::kBlack, kTimeScale);
    x += colonW + gap;
    drawText(fb, x, kTimeY, mm, timeColor, kTimeScale);

    // Date
    if (m.timeValid) {
        char date[16];
        std::snprintf(date, sizeof(date), "%s %s %d", weekdayAbbrev(m.local.weekday), monthAbbrev(m.local.month),
                      m.local.day);
        drawText(fb, (Framebuffer::kWidth - textWidth(date)) / 2, kDateY, date, colors::kDate);
    }

    // Weather
    if (m.hasWeather) {
        drawWeatherIcon(fb, 0, kWeatherY, m.icon, m.isDay);
        char temp[12];
        std::snprintf(temp, sizeof(temp), "%d*%c", m.temperature, m.unit);
        drawText(fb, kTempX, kWeatherY + 1, temp, m.weatherStale ? colors::kStale : colors::kWhite);
        if (m.weatherStale) fb.fillRect(kStaleDotX, kWeatherY, 2, 2, colors::kAmber);
    } else {
        drawText(fb, kTempX, kWeatherY + 1, "--*", colors::kStale);
    }

    drawWifi(fb, kWifiX, kWeatherY, m.wifiConnected);
}

}  // namespace tw
