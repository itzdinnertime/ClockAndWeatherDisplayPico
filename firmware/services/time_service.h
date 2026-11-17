#pragma once
// Keeps the shared SoftClock correct:
//   * NTP every hour while online (and immediately after reconnecting),
//     writing the result to the DS3231 on an exact second boundary.
//   * While NTP is unavailable, re-anchors from the DS3231 every 10 minutes
//     so the Pico's ~30 ppm crystal never accumulates meaningful drift;
//     the DS3231's own +-2 ppm (~0.17 s/day) then bounds offline error.

#include <cstdint>

#include "net/ntp_client.h"
#include "services/shared_state.h"
#include "tw/drivers.h"

class TimeService {
public:
    TimeService(SharedState& shared, tw::IRtc* rtc, NtpClient& ntp) : shared_(shared), rtc_(rtc), ntp_(ntp) {}

    void bootFromRtc(uint64_t nowMs);
    void tick(uint64_t nowMs, bool online);

private:
    bool readRtcAligned(int64_t& unixSeconds, uint64_t& monoUs);
    void writeRtcAligned(const tw::SoftClock& clock);
    bool ntpFresh(uint64_t nowMs) const;

    SharedState& shared_;
    tw::IRtc* rtc_;  // null when no DS3231 is fitted
    NtpClient& ntp_;

    uint64_t nextNtpMs_ = 0;
    uint64_t nextRtcReadMs_ = 0;
    uint64_t lastNtpOkMs_ = 0;
    bool ntpEverOk_ = false;
    bool wasOnline_ = false;
    unsigned serverIndex_ = 0;
};
