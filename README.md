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
- **Wi-Fi setup on the device, no rebuild needed.** With no saved network, or after the setup button is
  held for 3 s, the clock opens a `PICO-CLOCK` hotspot and runs its own DHCP and DNS servers. Most phones
  then show the setup page automatically. Pick a network from the scanned list, enter the password, and
  the details are saved to flash with a CRC check. Core 1 is paused safely during the write.
- **Testable by design.** All hardware sits behind interfaces (`II2cBus`, `IRtc`, `IWifiDriver`).
  `lib/core` has no Pico SDK dependencies and is covered by **28 GTest unit tests** that run on the host.
  These cover parsing, time logic, rendering, recovery and the setup portal's DHCP, DNS and HTTP handling.

## Hardware

| Part | Notes |
|---|---|
| Raspberry Pi Pico W | RP2040 + CYW43439 Wi-Fi |
| 64x32 HUB75 RGB matrix, 1/16 scan | P3/P4/P5 panels all work |
| DS3231 RTC module with CR2032 | e.g. ZS-042 or the Adafruit DS3231 |
| 5 V supply, ≥ 2 A | Powers the panel; share GND with the Pico |
| Momentary push button | Optional; between GP15 and GND. Hold it for 3 s to re-run Wi-Fi setup |

### Wiring (edit `firmware/config.h` to change it)

| HUB75 | Pico GPIO | | DS3231 | Pico GPIO |
|---|---|---|---|---|
| R0 G0 B0 | GP0 GP1 GP2 | | SDA | GP20 |
| R1 G1 B1 | GP3 GP4 GP5 | | SCL | GP21 |
| A B C D | GP6 GP7 GP8 GP9 | | VCC | 3V3 |
| CLK | GP11 | | GND | GND |
| LAT | GP12 | | | |
| OE | GP13 | | Setup button | GP15 → GND |
| GND | GND | | | |

The data pins and the address pins must each be consecutive, and OE must be LAT + 1. The PIO
programs depend on this.

## Wi-Fi setup

1. Flash the firmware and power it on. With no network saved, the panel shows
   `WIFI SETUP / CONNECT TO / PICO-CLOCK / 192.168.4.1`.
2. On a phone or laptop, join the open `PICO-CLOCK` network. The setup page should pop up by itself;
   if it doesn't, browse to `http://192.168.4.1`.
3. Choose your network (nearby ones are suggested), enter the password, and tap **Save & restart**.

To change networks later, hold the GP15 button for 3 seconds. If a network is already saved, setup
mode gives up after 10 minutes and goes back to it. The clock keeps time from the DS3231 the whole
time, and in setup mode the screen alternates between the instructions and the clock.

## Building

### VS Code (Raspberry Pi Pico extension)

Open the folder, click **Compile**, then hold BOOTSEL while plugging in the Pico and copy
`build/firmware/time_weather_display.uf2` onto it.

### Command line

```sh
cmake -S . -B build -G Ninja -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

For development you can also build credentials in with `-DWIFI_SSID=... -DWIFI_PASSWORD=...` (or
matching environment variables). These are only a fallback when nothing is saved in flash. They go
into a generated header inside `build/`, which is never committed.

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
  provisioning       CRC-protected credential flash record, validation, long-press detection
  web, captive_net   HTTP request/form parsing, setup page, DHCP and captive-DNS packets
firmware/            Pico-only code
  main.cpp           Core 0 network/setup loop and core 1 render loop
  drivers/           hub75.pio, Hub75Panel, PicoI2cBus, Cyw43WifiDriver
  net/               Async NTP (lwIP UDP), HTTP client (lwIP httpc), setup portal servers
  services/          TimeService, WeatherService, CredentialStore, cross-core SharedState
tests/               28 GTest tests with fake I2C / Wi-Fi drivers
```

## Configuration

`firmware/config.h` holds the settings: 12/24-hour time, °F/°C, day and night brightness, the
fixed-location override (useful behind a VPN), refresh intervals and pin assignments. If your panel
shows ghosting or noise, lower `PANEL_PIXEL_CLOCK_HZ`.

The serial log is available over USB at 115200 baud and shows Wi-Fi, NTP and weather events.
