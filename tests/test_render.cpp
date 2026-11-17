#include <gtest/gtest.h>

#include "tw/graphics.h"
#include "tw/hub75_pack.h"

using namespace tw;

namespace {
ScreenModel baseModel() {
    ScreenModel m;
    m.timeValid = true;
    m.local = civilFromUnix(unixFromCivil(2026, 11, 17, 12, 34, 0));
    m.hasWeather = true;
    m.temperature = 72;
    m.icon = WeatherIcon::Clear;
    m.wifiConnected = true;
    return m;
}

int countColor(const Framebuffer& fb, int x0, int y0, int w, int h, Rgb c) {
    int n = 0;
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) n += fb.get(x, y) == c;
    return n;
}
}  // namespace

TEST(Render, DrawsCentredTimeDateAndWifiStatus) {
    Framebuffer fb;
    ScreenModel m = baseModel();
    renderScreen(fb, m);

    // "12:34" at 2x is 52 px wide -> starts at x = 6. Compare against a reference draw.
    EXPECT_EQ(textWidth("12:34", 2), 52);
    Framebuffer ref;
    drawText(ref, 6, layout::kTimeY, "12:34", colors::kTime, 2);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 64; ++x) ASSERT_EQ(fb.get(x, y), ref.get(x, y)) << "x=" << x << " y=" << y;

    EXPECT_GT(countColor(fb, 0, layout::kDateY, 64, 7, colors::kDate), 0);
    EXPECT_GT(countColor(fb, layout::kWifiX, layout::kWeatherY, 8, 8, colors::kWifiOk), 0);

    // Colon blinks; offline shows red Wi-Fi; no time shows red dashes and no date.
    m.colonOn = false;
    m.wifiConnected = false;
    renderScreen(fb, m);
    EXPECT_EQ(countColor(fb, 30, layout::kTimeY, 4, 14, colors::kTime), 0);
    EXPECT_GT(countColor(fb, layout::kWifiX, layout::kWeatherY, 8, 8, colors::kWifiDown), 0);

    m.timeValid = false;
    renderScreen(fb, m);
    EXPECT_GT(countColor(fb, 0, 0, 64, 16, colors::kInvalid), 0);
    EXPECT_EQ(countColor(fb, 0, layout::kDateY, 64, 7, colors::kDate), 0);
}

TEST(Render, StaleWeatherIsDimmedAndFlagged) {
    Framebuffer fb;
    ScreenModel m = baseModel();
    const int tx = layout::kTempX, ty = layout::kWeatherY, tw = 30, th = 8;

    renderScreen(fb, m);
    EXPECT_GT(countColor(fb, tx, ty, tw, th, colors::kWhite), 0);
    EXPECT_EQ(fb.get(layout::kStaleDotX, ty), colors::kBlack);

    m.weatherStale = true;
    renderScreen(fb, m);
    EXPECT_EQ(countColor(fb, tx, ty, tw, th, colors::kWhite), 0);
    EXPECT_GT(countColor(fb, tx, ty, tw, th, colors::kStale), 0);
    EXPECT_EQ(fb.get(layout::kStaleDotX, ty), colors::kAmber);
}

TEST(Hub75, PacksTopAndBottomHalvesIntoBitplanes) {
    const hub75::GammaTable linear = hub75::makeGammaTable(1.0f);
    EXPECT_EQ(linear[0], 0);
    EXPECT_EQ(linear[255], 15);

    Framebuffer fb;
    fb.set(3, 2, {255, 0, 0});    // top half, scan row 2: R0
    fb.set(3, 18, {0, 0, 255});   // bottom half, scan row 2: B1
    fb.set(10, 0, {0, 85, 0});    // level 5 = 0b0101 on G0

    hub75::Bitplanes planes{};
    hub75::packBitplanes(fb, linear, planes);

    for (int p = 0; p < hub75::kColorBits; ++p) {
        EXPECT_EQ(planes.data[p][2][3], 0b100001) << "plane " << p;
        EXPECT_EQ(planes.data[p][2][4], 0);
    }
    EXPECT_EQ(planes.data[0][0][10], 0b10);
    EXPECT_EQ(planes.data[1][0][10], 0);
    EXPECT_EQ(planes.data[2][0][10], 0b10);
    EXPECT_EQ(planes.data[3][0][10], 0);
}
