#pragma once
// Hardware driver interfaces. Firmware provides Pico implementations;
// tests provide fakes. Nothing in lib/core talks to hardware directly.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace tw {

class II2cBus {
public:
    virtual ~II2cBus() = default;
    virtual bool write(uint8_t addr, const uint8_t* data, std::size_t len) = 0;
    // Write (typically a register pointer) then repeated-start read.
    virtual bool writeRead(uint8_t addr, const uint8_t* wdata, std::size_t wlen, uint8_t* rdata,
                           std::size_t rlen) = 0;
};

// A battery-backed real-time clock that keeps UTC.
class IRtc {
public:
    virtual ~IRtc() = default;
    // nullopt if the bus fails or the clock lost power since it was last set.
    virtual std::optional<int64_t> readUnix() = 0;
    virtual bool writeUnix(int64_t unixSeconds) = 0;
};

enum class LinkState : uint8_t { Down, Joining, Up, Failed };

class IWifiDriver {
public:
    virtual ~IWifiDriver() = default;
    virtual bool beginConnect() = 0;  // non-blocking join
    virtual LinkState linkState() = 0;
    virtual void disconnect() = 0;
};

}  // namespace tw
