#pragma once
// Pico implementations of the tw:: driver interfaces.

#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "tw/drivers.h"
#include "tw/provisioning.h"

class PicoI2cBus final : public tw::II2cBus {
public:
    explicit PicoI2cBus(i2c_inst_t* i2c) : i2c_(i2c) {}

    bool write(uint8_t addr, const uint8_t* data, std::size_t len) override {
        return i2c_write_timeout_us(i2c_, addr, data, len, false, kTimeoutUs) == static_cast<int>(len);
    }
    bool writeRead(uint8_t addr, const uint8_t* w, std::size_t wl, uint8_t* r, std::size_t rl) override {
        if (i2c_write_timeout_us(i2c_, addr, w, wl, true, kTimeoutUs) != static_cast<int>(wl)) return false;
        return i2c_read_timeout_us(i2c_, addr, r, rl, false, kTimeoutUs) == static_cast<int>(rl);
    }

private:
    static constexpr unsigned kTimeoutUs = 10'000;  // a missing RTC must never hang the loop
    i2c_inst_t* i2c_;
};

class Cyw43WifiDriver final : public tw::IWifiDriver {
public:
    explicit Cyw43WifiDriver(const tw::WifiCredentials& creds) : creds_(creds) {}

    bool beginConnect() override {
        const bool open = creds_.password[0] == '\0';
        return cyw43_arch_wifi_connect_async(creds_.ssid, open ? nullptr : creds_.password,
                                             open ? CYW43_AUTH_OPEN : CYW43_AUTH_WPA2_MIXED_PSK) == 0;
    }

    tw::LinkState linkState() override {
        switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
            case CYW43_LINK_UP:
                return tw::LinkState::Up;
            case CYW43_LINK_JOIN:
            case CYW43_LINK_NOIP:
                return tw::LinkState::Joining;
            case CYW43_LINK_FAIL:
            case CYW43_LINK_NONET:
            case CYW43_LINK_BADAUTH:
                return tw::LinkState::Failed;
            default:
                return tw::LinkState::Down;
        }
    }

    void disconnect() override { cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA); }

private:
    tw::WifiCredentials creds_;
};
