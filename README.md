# Time & Weather LED Display

Dual-core C++ firmware for a **Raspberry Pi Pico W** that drives a **64x32 HUB75 RGB LED matrix**
showing the local time, date and current weather. It works out where it is automatically and
**keeps working without Wi-Fi**.

```
+--------------------------------------------------------------+
|                         12:34                                |   time (colon blinks at 1 Hz)
|                      TUE NOV 17                              |   date
| [sun]  72°F                                     [wifi]       |   weather, stale dot, Wi-Fi status
+--------------------------------------------------------------+
```

## Features

- **Dual-core design.** Core 1 does nothing but render and refresh the panel through two PIO state
  machines at a fixed **100 Hz**, using 4-bit binary-coded modulation (4096 colours, gamma-corrected).
  Core 0 runs Wi-Fi, NTP, the DS3231 and weather fetching. The cores share state through a
  hardware-spinlock-protected snapshot.
- **Automatic location.** IP geolocation ([ip-api.com](https://ip-api.com)) gives latitude and longitude,
  and [Open-Meteo](https://open-meteo.com) provides current conditions plus the local UTC offset
  (with DST) through `timezone=auto`. Neither service needs an API key.
- **Accurate time, online or offline.**
  - NTP sync every hour, corrected for half the round-trip time.
  - Each sync is written to a **DS3231 RTC** over I2C, exactly on a second boundary.
  - When NTP is unavailable, the software clock re-anchors to the DS3231 every 10 minutes. Reads are
    aligned to the RTC's seconds edge (to about 1 ms), so the error is set by the DS3231's ±2 ppm
    (about 0.17 s/day) rather than by the Pico's crystal (about 30 ppm, or 2.6 s/day).
  - On boot the time is restored from the DS3231 before Wi-Fi comes up.
- **Failure recovery.**
  - Wi-Fi reconnects with exponential backoff (1 s → 8 s cap, 10 s join timeout). The worst-case time
    to reconnect after the access point returns is checked by a unit test.
  - The last good weather stays on screen. After 30 minutes without an update it turns grey and shows
    an amber dot.
  - The weather refreshes as soon as the connection comes back.
  - Core 0 feeds the **hardware watchdog** only while core 1's frame heartbeat keeps advancing, so a
    hang on either core resets the board.
- **Testable by design.** All hardware sits behind interfaces (`II2cBus`, `IRtc`, `IWifiDriver`).
  `lib/core` has no Pico SDK dependencies and is covered by **20 GTest unit tests** that run on the host.
  These cover parsing, time logic, rendering and recovery.

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43439 Wi-Fi |
| 64x32 HUB75 RGB matrix, 1/16 scan | P3/P4/P5 panels all work |
| DS3231 RTC module with CR2032 | e.g. ZS-042 or the Adafruit DS3231 |
| 5 V supply, ≥ 2 A | Powers the panel; share GND with the Pico |

### Wiring (edit `firmware/config.h` to change it)

| HUB75 | Pico GPIO | | DS3231 | Pico GPIO |
|---|---|---|---|---|
| R0 G0 B0 | GP0 GP1 GP2 | | SDA | GP20 |
| R1 G1 B1 | GP3 GP4 GP5 | | SCL | GP21 |
| A B C D | GP6 GP7 GP8 GP9 | | VCC | 3V3 |
| CLK | GP11 | | GND | GND |
| LAT | GP12 | | | |
| OE | GP13 | | | |
| GND | GND | | | |

The data pins and the address pins must each be consecutive, and OE must be LAT + 1. The PIO
programs depend on this.

## Building

### VS Code (Raspberry Pi Pico extension)

Open the folder, then pass your Wi-Fi credentials. You can either set the `WIFI_SSID` and
`WIFI_PASSWORD` environment variables before launching VS Code, or add this to `.vscode/settings.json`:

```json
"cmake.configureArgs": ["-DWIFI_SSID=MyNetwork", "-DWIFI_PASSWORD=hunter2"]
```

Then click **Compile** and hold BOOTSEL while plugging in the Pico to flash `build/firmware/time_weather_display.uf2`.

### Command line

```sh
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk -DWIFI_SSID=MyNetwork -DWIFI_PASSWORD=secret
cmake --build build
```

Credentials are written to a generated header inside `build/` and are never committed. The build
falls back to offline (RTC-only) mode if `WIFI_SSID` is empty.

### Unit tests (host)

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

GitHub Actions runs the tests and builds the `.uf2` on every push (see `.github/workflows/ci.yml`).

## Layout

```
lib/core/            Portable C++20, no SDK headers. Built for both the Pico and the host.
  time_utils         Calendar math, SoftClock (wall time anchored to the µs timer)
  ntp                SNTP packet build/parse, era rollover, RTT compensation
  json_lite, weather Allocation-free JSON reader, Open-Meteo / ip-api parsers
  http_util          Chunked transfer decoding (lwIP's httpc does not do it)
  ds3231             RTC driver on the II2cBus interface
  recovery           Backoff, WifiManager state machine, HeartbeatMonitor
  graphics           Framebuffer, 5x7 font, weather icons, screen layout
  hub75_pack         Framebuffer → BCM bitplanes for the PIO
firmware/            Pico-only code
  main.cpp           Core 0 network loop and core 1 render loop
  drivers/           hub75.pio, Hub75Panel, PicoI2cBus, Cyw43WifiDriver
  net/               Async NTP (lwIP UDP) and HTTP (lwIP httpc) clients
  services/          TimeService, WeatherService, cross-core SharedState
tests/               20 GTest tests with fake I2C / Wi-Fi drivers
```

## Configuration

`firmware/config.h` holds the settings: 12/24-hour time, °F/°C, day and night brightness, the
fixed-location override (useful behind a VPN), refresh intervals and pin assignments. If your panel
shows ghosting or noise, lower `PANEL_PIXEL_CLOCK_HZ`.

The serial log is available over USB at 115200 baud and shows Wi-Fi, NTP and weather events.
