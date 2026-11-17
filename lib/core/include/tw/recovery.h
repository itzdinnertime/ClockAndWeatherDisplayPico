#pragma once
// Failure-recovery building blocks: exponential backoff, the Wi-Fi
// reconnect state machine, and the dual-core watchdog heartbeat check.

#include <cstdint>

#include "tw/drivers.h"

namespace tw {

class Backoff {
public:
    constexpr Backoff(uint32_t initialMs, uint32_t maxMs) : initialMs_(initialMs), maxMs_(maxMs) {}

    // initial, 2x, 4x, ... capped at maxMs.
    uint32_t nextDelayMs() {
        uint64_t d = uint64_t{initialMs_} << attempts_;
        if (attempts_ < 31) ++attempts_;
        return d > maxMs_ ? maxMs_ : static_cast<uint32_t>(d);
    }
    void reset() { attempts_ = 0; }
    uint32_t attempts() const { return attempts_; }

private:
    uint32_t initialMs_;
    uint32_t maxMs_;
    uint32_t attempts_ = 0;
};

// Non-blocking Wi-Fi supervisor; call tick() from the network loop.
//
//   Waiting --(retry time reached)--> Connecting --(link up)--> Connected
//      ^                                  |                         |
//      +-------(failed / timed out)-------+------(link lost)--------+
//
// Worst-case recovery after the AP returns is bounded by
// connectTimeout + maxBackoff + join time.
class WifiManager {
public:
    enum class State : uint8_t { Waiting, Connecting, Connected };

    WifiManager(IWifiDriver& driver, Backoff backoff, uint32_t connectTimeoutMs)
        : driver_(driver), backoff_(backoff), connectTimeoutMs_(connectTimeoutMs) {}

    void tick(uint64_t nowMs);

    State state() const { return state_; }
    bool connected() const { return state_ == State::Connected; }
    uint32_t linkLosses() const { return linkLosses_; }
    uint64_t nextAttemptMs() const { return nextAttemptMs_; }

private:
    void scheduleRetry(uint64_t nowMs);

    IWifiDriver& driver_;
    Backoff backoff_;
    uint32_t connectTimeoutMs_;
    State state_ = State::Waiting;
    uint64_t nextAttemptMs_ = 0;
    uint64_t attemptStartMs_ = 0;
    uint32_t linkLosses_ = 0;
};

// Core 0 owns the hardware watchdog. It only feeds it while core 1's render
// heartbeat keeps advancing, so a hang on *either* core triggers a reset.
class HeartbeatMonitor {
public:
    explicit HeartbeatMonitor(uint32_t maxSilenceMs) : maxSilenceMs_(maxSilenceMs) {}

    bool shouldFeed(uint32_t heartbeat, uint64_t nowMs) {
        if (!seen_ || heartbeat != lastBeat_) {
            seen_ = true;
            lastBeat_ = heartbeat;
            lastChangeMs_ = nowMs;
            return true;
        }
        return (nowMs - lastChangeMs_) <= maxSilenceMs_;
    }

private:
    uint32_t maxSilenceMs_;
    bool seen_ = false;
    uint32_t lastBeat_ = 0;
    uint64_t lastChangeMs_ = 0;
};

}  // namespace tw
