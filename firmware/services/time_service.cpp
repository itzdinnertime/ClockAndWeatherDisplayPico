#include "services/time_service.h"

#include <cstdio>

#include "pico/time.h"

namespace {
constexpr const char* kNtpServers[] = {"pool.ntp.org", "time.google.com", "time.cloudflare.com"};
constexpr unsigned kNtpServerCount = sizeof(kNtpServers) / sizeof(kNtpServers[0]);
}  // namespace

bool TimeService::ntpFresh(uint64_t nowMs) const {
    return ntpEverOk_ && nowMs - lastNtpOkMs_ < 2ull * cfg::NTP_INTERVAL_MS;
}

void TimeService::bootFromRtc(uint64_t nowMs) {
    if (rtc_ == nullptr) {
        std::printf("[time] no DS3231 found\n");
        return;
    }
    int64_t secs;
    uint64_t mono;
    if (readRtcAligned(secs, mono)) {
        shared_.update([&](SystemSnapshot& s) { s.clock.set(secs * 1000, mono); });
        std::printf("[time] restored from DS3231: %lld\n", static_cast<long long>(secs));
    } else {
        std::printf("[time] DS3231 time invalid (battery lost?), waiting for NTP\n");
    }
    nextRtcReadMs_ = nowMs + cfg::RTC_DISCIPLINE_MS;
}

void TimeService::tick(uint64_t nowMs, bool online) {
    ntp_.tick(nowMs);

    if (const auto r = ntp_.takeResult()) {
        tw::SoftClock fresh;
        fresh.set(r->unixMs, r->monoUs);
        shared_.update([&](SystemSnapshot& s) { s.clock = fresh; });
        lastNtpOkMs_ = nowMs;
        ntpEverOk_ = true;
        nextNtpMs_ = nowMs + cfg::NTP_INTERVAL_MS;
        std::printf("[time] NTP sync from %s\n", kNtpServers[serverIndex_]);
        if (rtc_ != nullptr) writeRtcAligned(fresh);
    }
    if (ntp_.takeFailure()) {
        std::printf("[time] NTP failed (%s)\n", kNtpServers[serverIndex_]);
        serverIndex_ = (serverIndex_ + 1) % kNtpServerCount;
        nextNtpMs_ = nowMs + cfg::NTP_RETRY_MS;
    }

    if (online && !wasOnline_ && !ntpFresh(nowMs)) nextNtpMs_ = nowMs;  // just reconnected
    wasOnline_ = online;

    if (online && !ntp_.busy() && nowMs >= nextNtpMs_) {
        nextNtpMs_ = nowMs + cfg::NTP_RETRY_MS;  // overwritten on success
        ntp_.start(kNtpServers[serverIndex_], nowMs);
    }

    // Offline (or NTP failing): discipline the software clock from the DS3231.
    if (!ntpFresh(nowMs) && rtc_ != nullptr && nowMs >= nextRtcReadMs_) {
        nextRtcReadMs_ = nowMs + cfg::RTC_DISCIPLINE_MS;
        int64_t secs;
        uint64_t mono;
        if (readRtcAligned(secs, mono)) {
            shared_.update([&](SystemSnapshot& s) { s.clock.set(secs * 1000, mono); });
        }
    }
}

// The DS3231 only exposes whole seconds. Polling until the seconds register
// ticks over pins the reading to the start of a second (+-~1 ms) instead of
// carrying up to a full second of error.
bool TimeService::readRtcAligned(int64_t& unixSeconds, uint64_t& monoUs) {
    const auto first = rtc_->readUnix();
    if (!first) return false;
    const uint64_t deadline = time_us_64() + 1'200'000;
    while (time_us_64() < deadline) {
        const auto now = rtc_->readUnix();
        if (!now) return false;
        if (*now != *first) {
            monoUs = time_us_64();
            unixSeconds = *now;
            return true;
        }
        sleep_us(500);
    }
    return false;
}

// Writing the seconds register restarts the DS3231's sub-second divider, so
// write exactly when the true time crosses a whole second.
void TimeService::writeRtcAligned(const tw::SoftClock& clock) {
    const uint64_t mono = time_us_64();
    const int64_t nowUnixMs = clock.nowUnixMs(mono);
    const int64_t nextSecond = tw::floorDiv(nowUnixMs, 1000) + 1;
    const uint64_t target = mono + static_cast<uint64_t>(nextSecond * 1000 - nowUnixMs) * 1000;
    busy_wait_until(from_us_since_boot(target));
    if (!rtc_->writeUnix(nextSecond)) std::printf("[time] DS3231 write failed\n");
}
