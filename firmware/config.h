#pragma once
// User-tunable settings: wiring, units and timing.

#include <cstdint>

#include "hardware/i2c.h"

namespace cfg {

// ---- HUB75 wiring ---------------------------------------------------------
// Data pins must be consecutive in the order R0 G0 B0 R1 G1 B1.
constexpr unsigned PIN_RGB_BASE = 0;   // GP0..GP5
// Row address pins A B C D must be consecutive (1/16 scan => 4 pins).
constexpr unsigned PIN_ROW_BASE = 6;   // GP6..GP9
constexpr unsigned PIN_CLK = 11;
// LAT and OE must be consecutive: OE = PIN_LATCH + 1.
constexpr unsigned PIN_LATCH = 12;
constexpr unsigned PIN_OE = 13;
static_assert(PIN_OE == PIN_LATCH + 1, "PIO side-set drives LAT and OE as a pair");

constexpr float PANEL_PIXEL_CLOCK_HZ = 15'000'000.0f;  // lower this if the image has glitches
constexpr float PANEL_GAMMA = 2.2f;

// ---- DS3231 RTC -----------------------------------------------------------
#define TW_RTC_I2C i2c0
constexpr unsigned PIN_I2C_SDA = 20;
constexpr unsigned PIN_I2C_SCL = 21;
constexpr unsigned I2C_BAUD = 400'000;

// ---- Display --------------------------------------------------------------
constexpr bool USE_24_HOUR = false;
constexpr bool USE_FAHRENHEIT = true;
constexpr uint8_t BRIGHTNESS_DAY = 255;
constexpr uint8_t BRIGHTNESS_NIGHT = 48;
constexpr int NIGHT_START_HOUR = 22;
constexpr int NIGHT_END_HOUR = 7;
constexpr uint32_t FRAME_PERIOD_US = 10'000;  // 100 Hz refresh

// Used until the first successful geolocation / weather fetch.
constexpr int32_t DEFAULT_UTC_OFFSET_S = 0;

// Set to true to skip IP geolocation (e.g. when behind a VPN).
constexpr bool USE_FIXED_LOCATION = false;
constexpr double FIXED_LATITUDE = 0.0;
constexpr double FIXED_LONGITUDE = 0.0;

// ---- Networking & recovery ------------------------------------------------
constexpr uint32_t WIFI_BACKOFF_INITIAL_MS = 1'000;
constexpr uint32_t WIFI_BACKOFF_MAX_MS = 8'000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10'000;

constexpr uint32_t NTP_INTERVAL_MS = 60 * 60 * 1000;      // re-sync hourly
constexpr uint32_t NTP_RETRY_MS = 15'000;
constexpr uint32_t NTP_TIMEOUT_MS = 5'000;
constexpr uint32_t RTC_DISCIPLINE_MS = 10 * 60 * 1000;    // re-anchor from DS3231 when NTP is stale

constexpr uint32_t WEATHER_REFRESH_MS = 10 * 60 * 1000;
constexpr uint32_t WEATHER_RETRY_MS = 30'000;
constexpr uint32_t WEATHER_STALE_MS = 30 * 60 * 1000;

constexpr uint32_t WATCHDOG_TIMEOUT_MS = 8'000;           // RP2040 max is ~8.3 s
constexpr uint32_t CORE1_MAX_SILENCE_MS = 1'000;          // 100 missed frames => core 1 is hung

}  // namespace cfg
