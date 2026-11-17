#include "tw/recovery.h"

namespace tw {

void WifiManager::scheduleRetry(uint64_t nowMs) {
    nextAttemptMs_ = nowMs + backoff_.nextDelayMs();
    state_ = State::Waiting;
}

void WifiManager::tick(uint64_t nowMs) {
    switch (state_) {
        case State::Waiting:
            if (nowMs >= nextAttemptMs_) {
                if (driver_.beginConnect()) {
                    state_ = State::Connecting;
                    attemptStartMs_ = nowMs;
                } else {
                    scheduleRetry(nowMs);
                }
            }
            break;

        case State::Connecting: {
            const LinkState ls = driver_.linkState();
            if (ls == LinkState::Up) {
                state_ = State::Connected;
                backoff_.reset();
            } else if (ls == LinkState::Failed || nowMs - attemptStartMs_ >= connectTimeoutMs_) {
                driver_.disconnect();
                scheduleRetry(nowMs);
            }
            break;
        }

        case State::Connected:
            if (driver_.linkState() != LinkState::Up) {
                ++linkLosses_;
                driver_.disconnect();
                scheduleRetry(nowMs);
            }
            break;
    }
}

}  // namespace tw
