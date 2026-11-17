// Time & Weather LED display for the Raspberry Pi Pico W.
//
// Core 0: Wi-Fi supervision, NTP, DS3231, weather HTTP, hardware watchdog,
//         and the Wi-Fi setup portal.
// Core 1: renders the screen and refreshes the HUB75 panel at 100 Hz.

#include <cmath>
#include <cstdio>

#include "config.h"
#include "drivers/hub75_panel.h"
#include "drivers/pico_drivers.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "net/http_client.h"
#include "net/ntp_client.h"
#include "net/setup_portal.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "secrets.h"
#include "services/credential_store.h"
#include "services/shared_state.h"
#include "services/time_service.h"
#include "services/weather_service.h"
#include "tw/ds3231.h"
#include "tw/graphics.h"
#include "tw/provisioning.h"
#include "tw/recovery.h"

namespace {

SharedState g_shared;
volatile uint32_t g_renderHeartbeat = 0;  // written by core 1 only

// Watchdog scratch registers survive a watchdog_reboot(); scratch[0] carries
// "boot into setup mode" across the restart triggered by the setup button.
constexpr uint32_t kSetupRequestMagic = 0x53455455;  // "SETU"

// ---------------------------------------------------------------- core 1 ---

Hub75Panel g_panel;
tw::Framebuffer g_framebuffer;

tw::ScreenModel buildScreenModel(const SystemSnapshot& s, uint64_t monoUs) {
    tw::ScreenModel m;
    m.use24h = cfg::USE_24_HOUR;
    m.unit = cfg::USE_FAHRENHEIT ? 'F' : 'C';
    m.wifiConnected = s.wifiUp;

    m.timeValid = s.clock.valid();
    if (m.timeValid) {
        const int64_t unixMs = s.clock.nowUnixMs(monoUs);
        m.local = tw::localTime(tw::floorDiv(unixMs, 1000), s.utcOffsetSeconds);
        m.colonOn = tw::floorMod(unixMs, 1000) < 500;
    }

    m.hasWeather = s.hasWeather;
    if (s.hasWeather) {
        m.temperature = static_cast<int>(std::lround(s.weather.temperature));
        m.icon = tw::iconForWmoCode(s.weather.weatherCode);
        m.isDay = s.weather.isDay;
        m.weatherStale = tw::isWeatherStale(s.weatherFetchedMs, monoUs / 1000, cfg::WEATHER_STALE_MS);
    }

    // In setup mode, alternate instructions with the clock if we know the time.
    if (s.setupMode) {
        const bool instructionsPhase = (monoUs / 1000 / cfg::SETUP_SCREEN_ALTERNATE_MS) % 2 == 0;
        m.setupMode = !m.timeValid || instructionsPhase;
    }
    return m;
}

uint8_t brightnessFor(const tw::ScreenModel& m) {
    if (!m.timeValid) return cfg::BRIGHTNESS_DAY;
    const bool night = m.local.hour >= cfg::NIGHT_START_HOUR || m.local.hour < cfg::NIGHT_END_HOUR;
    return night ? cfg::BRIGHTNESS_NIGHT : cfg::BRIGHTNESS_DAY;
}

void core1Main() {
    // Core 1 executes from flash, so it must be pausable while core 0 writes
    // credentials to flash (see CredentialStore::save / flash_safe_execute).
    multicore_lockout_victim_init();
    g_panel.init();

    tw::ScreenModel shown;
    bool firstFrame = true;
    absolute_time_t nextFrame = get_absolute_time();

    while (true) {
        const tw::ScreenModel model = buildScreenModel(g_shared.get(), time_us_64());
        if (firstFrame || !(model == shown)) {  // re-render only on change
            tw::renderScreen(g_framebuffer, model);
            g_panel.load(g_framebuffer);
            shown = model;
            firstFrame = false;
        }

        g_panel.scanFrame(brightnessFor(model));
        g_renderHeartbeat = g_renderHeartbeat + 1;

        nextFrame = delayed_by_us(nextFrame, cfg::FRAME_PERIOD_US);
        if (time_reached(nextFrame)) {
            nextFrame = get_absolute_time();  // overran; don't try to catch up
        } else {
            busy_wait_until(nextFrame);
        }
    }
}

// ---------------------------------------------------------------- core 0 ---

void initRtcBus() {
    i2c_init(TW_RTC_I2C, cfg::I2C_BAUD);
    gpio_set_function(cfg::PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(cfg::PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(cfg::PIN_I2C_SDA);
    gpio_pull_up(cfg::PIN_I2C_SCL);
}

void initSetupButton() {
    gpio_init(cfg::PIN_SETUP_BUTTON);
    gpio_set_dir(cfg::PIN_SETUP_BUTTON, GPIO_IN);
    gpio_pull_up(cfg::PIN_SETUP_BUTTON);  // pressed = low
}

bool setupButtonPressed() { return !gpio_get(cfg::PIN_SETUP_BUTTON); }

[[noreturn]] void rebootIntoSetupMode() {
    std::printf("[setup] button held, restarting into setup mode\n");
    watchdog_hw->scratch[0] = kSetupRequestMagic;
    watchdog_reboot(0, 0, 0);
    while (true) tight_loop_contents();
}

[[noreturn]] void runSetupMode(CredentialStore& store, TimeService& timeService, bool haveCredentials) {
    g_shared.update([](SystemSnapshot& s) { s.setupMode = true; });

    static SetupPortal portal(store);
    portal.scanNetworks();
    if (!portal.start()) {
        std::printf("[setup] failed to start portal, rebooting\n");
        watchdog_reboot(0, 0, 0);
    }

    tw::HeartbeatMonitor heartbeat(cfg::CORE1_MAX_SILENCE_MS);
    const uint64_t giveUpAt = haveCredentials ? nowMs() + cfg::SETUP_TIMEOUT_MS : UINT64_MAX;

    while (true) {
        const uint64_t now = nowMs();
        cyw43_arch_poll();
        portal.tick(now);
        timeService.tick(now, false);  // keep disciplining from the DS3231

        if (now >= giveUpAt) {
            std::printf("[setup] timed out, resuming with saved network\n");
            watchdog_reboot(0, 0, 0);
        }
        if (heartbeat.shouldFeed(g_renderHeartbeat, now)) watchdog_update();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
    }
}

}  // namespace

int main() {
    stdio_init_all();
    g_shared.init();

    const bool setupRequested = watchdog_hw->scratch[0] == kSetupRequestMagic;
    watchdog_hw->scratch[0] = 0;
    if (watchdog_enable_caused_reboot()) std::printf("[boot] recovered from a watchdog reset\n");

    // From here on, a hang on either core resets the board.
    watchdog_enable(cfg::WATCHDOG_TIMEOUT_MS, true);
    initSetupButton();

    // Restore time from the DS3231 before anything else so the clock is
    // correct from the first frame even with no network.
    initRtcBus();
    static PicoI2cBus i2cBus(TW_RTC_I2C);
    static tw::Ds3231 ds3231(i2cBus);
    tw::IRtc* rtc = ds3231.present() ? &ds3231 : nullptr;

    static NtpClient ntp;
    static TimeService timeService(g_shared, rtc, ntp);
    timeService.bootFromRtc(nowMs());

    multicore_launch_core1(core1Main);
    watchdog_update();

    // Credentials: saved via the setup portal, else the optional build-time ones.
    static CredentialStore credentialStore;
    std::optional<tw::WifiCredentials> credentials = credentialStore.load();
    if (!credentials && WIFI_SSID[0] != '\0') credentials = tw::makeCredentials(WIFI_SSID, WIFI_PASSWORD);

    const bool radioOk = cyw43_arch_init() == 0;
    if (!radioOk) std::printf("[boot] CYW43 init failed; running offline\n");

    if (radioOk && (setupRequested || !credentials)) {
        runSetupMode(credentialStore, timeService, credentials.has_value());
    }

    const bool networkAvailable = radioOk && credentials.has_value();
    if (networkAvailable) {
        cyw43_arch_enable_sta_mode();
        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);  // lower latency, fewer silent drops
        ntp.begin();
        std::printf("[wifi] using network \"%s\"\n", credentials->ssid);
    }

    static Cyw43WifiDriver wifiDriver(credentials.value_or(tw::WifiCredentials{}));
    static tw::WifiManager wifi(wifiDriver, tw::Backoff(cfg::WIFI_BACKOFF_INITIAL_MS, cfg::WIFI_BACKOFF_MAX_MS),
                                cfg::WIFI_CONNECT_TIMEOUT_MS);
    static HttpClient http;
    static WeatherService weather(g_shared, http);
    tw::HeartbeatMonitor heartbeat(cfg::CORE1_MAX_SILENCE_MS);
    tw::LongPressDetector setupButton(cfg::SETUP_HOLD_MS);
    bool wasOnline = false;

    while (true) {
        const uint64_t now = nowMs();
        bool online = false;

        if (radioOk && setupButton.update(setupButtonPressed(), now)) rebootIntoSetupMode();

        if (networkAvailable) {
            cyw43_arch_poll();
            wifi.tick(now);
            online = wifi.connected();
            if (online != wasOnline) {
                std::printf("[wifi] %s (link losses: %lu)\n", online ? "connected" : "disconnected",
                            static_cast<unsigned long>(wifi.linkLosses()));
                g_shared.update([&](SystemSnapshot& s) { s.wifiUp = online; });
                wasOnline = online;
            }
        }

        timeService.tick(now, online);
        if (networkAvailable) weather.tick(now, online);

        if (heartbeat.shouldFeed(g_renderHeartbeat, now)) watchdog_update();

        if (networkAvailable) {
            cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
        } else {
            sleep_ms(10);
        }
    }
}
