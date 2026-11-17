#include <gtest/gtest.h>

#include <algorithm>

#include "tw/recovery.h"

using namespace tw;

namespace {
constexpr uint32_t kInitialBackoffMs = 1000;
constexpr uint32_t kMaxBackoffMs = 8000;
constexpr uint32_t kConnectTimeoutMs = 10000;

// Scriptable link state.
class ScriptedWifi : public IWifiDriver {
public:
    LinkState state = LinkState::Joining;
    int connects = 0;
    int disconnects = 0;
    bool beginConnect() override {
        ++connects;
        return true;
    }
    LinkState linkState() override { return state; }
    void disconnect() override { ++disconnects; }
};

// Simulated access point that can disappear. A join takes `joinMs` when the
// AP is up. If the AP is down when a join starts, that join never succeeds
// and only ends via the manager's timeout (the pessimistic case).
class SimulatedWifi : public IWifiDriver {
public:
    explicit SimulatedWifi(const uint64_t& now) : now_(now) {}
    bool apUp = true;
    uint64_t joinMs = 3000;

    bool beginConnect() override {
        joining_ = true;
        joinStart_ = now_;
        joinViable_ = apUp;
        return true;
    }
    LinkState linkState() override {
        if (!apUp) {
            joinViable_ = false;
            if (associated_) associated_ = false;
            return joining_ ? LinkState::Joining : LinkState::Down;
        }
        if (joining_ && joinViable_ && now_ - joinStart_ >= joinMs) {
            joining_ = false;
            associated_ = true;
        }
        if (associated_) return LinkState::Up;
        return joining_ ? LinkState::Joining : LinkState::Down;
    }
    void disconnect() override {
        joining_ = false;
        associated_ = false;
    }

private:
    const uint64_t& now_;
    bool joining_ = false, joinViable_ = false, associated_ = false;
    uint64_t joinStart_ = 0;
};
}  // namespace

TEST(Backoff, DoublesCapsAndResets) {
    Backoff b(kInitialBackoffMs, kMaxBackoffMs);
    EXPECT_EQ(b.nextDelayMs(), 1000u);
    EXPECT_EQ(b.nextDelayMs(), 2000u);
    EXPECT_EQ(b.nextDelayMs(), 4000u);
    EXPECT_EQ(b.nextDelayMs(), 8000u);
    EXPECT_EQ(b.nextDelayMs(), 8000u);
    for (int i = 0; i < 100; ++i) b.nextDelayMs();  // no overflow after many failures
    EXPECT_EQ(b.nextDelayMs(), 8000u);
    b.reset();
    EXPECT_EQ(b.nextDelayMs(), 1000u);
}

TEST(WifiManager, ReconnectsWithExponentialBackoffAfterLinkLoss) {
    ScriptedWifi wifi;
    WifiManager mgr(wifi, Backoff(kInitialBackoffMs, kMaxBackoffMs), kConnectTimeoutMs);

    mgr.tick(0);
    EXPECT_EQ(wifi.connects, 1);
    EXPECT_EQ(mgr.state(), WifiManager::State::Connecting);

    wifi.state = LinkState::Up;
    mgr.tick(100);
    EXPECT_TRUE(mgr.connected());

    wifi.state = LinkState::Down;  // router rebooted
    mgr.tick(200);
    EXPECT_FALSE(mgr.connected());
    EXPECT_EQ(mgr.linkLosses(), 1u);
    EXPECT_EQ(mgr.nextAttemptMs(), 1200u);

    mgr.tick(1199);
    EXPECT_EQ(wifi.connects, 1) << "waits out the backoff";
    mgr.tick(1200);
    EXPECT_EQ(wifi.connects, 2);

    wifi.state = LinkState::Failed;
    mgr.tick(1300);
    EXPECT_EQ(mgr.nextAttemptMs(), 1300u + 2000u) << "second failure doubles the delay";
    wifi.state = LinkState::Joining;
    mgr.tick(3300);
    EXPECT_EQ(wifi.connects, 3);
    mgr.tick(3300 + kConnectTimeoutMs);  // stuck joining -> timeout counts as failure
    EXPECT_EQ(mgr.nextAttemptMs(), 3300u + kConnectTimeoutMs + 4000u);

    mgr.tick(mgr.nextAttemptMs());
    wifi.state = LinkState::Up;
    mgr.tick(mgr.nextAttemptMs() + 50);
    EXPECT_TRUE(mgr.connected());
    wifi.state = LinkState::Down;
    mgr.tick(50'000);
    EXPECT_EQ(mgr.nextAttemptMs(), 51'000u) << "backoff resets after a good connection";
}

TEST(WifiManager, RecoversWithin30SecondsOfNetworkReturning) {
    // Run a 5-minute outage, bring the AP back at many different phase
    // offsets relative to the retry schedule, and measure the worst case.
    uint64_t worstMs = 0;
    for (uint64_t returnOffset = 0; returnOffset < 20'000; returnOffset += 250) {
        uint64_t now = 0;
        SimulatedWifi wifi(now);
        WifiManager mgr(wifi, Backoff(kInitialBackoffMs, kMaxBackoffMs), kConnectTimeoutMs);

        for (; now < 10'000; now += 10) mgr.tick(now);
        ASSERT_TRUE(mgr.connected());

        wifi.apUp = false;
        const uint64_t apBack = now + 300'000 + returnOffset;
        for (; now < apBack; now += 10) mgr.tick(now);
        ASSERT_FALSE(mgr.connected());

        wifi.apUp = true;
        while (!mgr.connected() && now < apBack + 120'000) {
            now += 10;
            mgr.tick(now);
        }
        ASSERT_TRUE(mgr.connected());
        worstMs = std::max(worstMs, now - apBack);
    }
    EXPECT_LE(worstMs, 30'000u);
}

TEST(HeartbeatMonitor, FeedsWatchdogOnlyWhileRenderCoreIsAlive) {
    HeartbeatMonitor hb(1000);
    EXPECT_TRUE(hb.shouldFeed(0, 0));
    EXPECT_TRUE(hb.shouldFeed(1, 500));
    EXPECT_TRUE(hb.shouldFeed(1, 1400));   // silent 900 ms: still within budget
    EXPECT_TRUE(hb.shouldFeed(1, 1500));   // exactly at the limit
    EXPECT_FALSE(hb.shouldFeed(1, 1501));  // core 1 hung -> stop feeding -> reset
    EXPECT_FALSE(hb.shouldFeed(1, 5000));
    EXPECT_TRUE(hb.shouldFeed(2, 5010));   // recovers if it starts beating again
}
