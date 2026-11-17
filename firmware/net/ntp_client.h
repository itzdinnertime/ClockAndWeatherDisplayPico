#pragma once
// Asynchronous SNTP client on lwIP raw UDP. Runs on core 0; callbacks fire
// from cyw43_arch_poll(), so no locking is needed.

#include <cstdint>
#include <optional>

#include "lwip/ip_addr.h"

struct udp_pcb;
struct pbuf;

class NtpClient {
public:
    struct Result {
        int64_t unixMs;  // UTC at the moment the reply arrived...
        uint64_t monoUs; // ...measured on the shared microsecond timer
    };

    bool begin();
    bool start(const char* host, uint64_t nowMs);
    void tick(uint64_t nowMs);  // enforces the timeout
    bool busy() const { return phase_ != Phase::Idle; }

    std::optional<Result> takeResult();
    bool takeFailure();

private:
    enum class Phase { Idle, Resolving, Waiting };

    void sendTo(const ip_addr_t* addr);
    void fail();
    static void onDnsFound(const char* name, const ip_addr_t* addr, void* arg);
    static void onReceive(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, uint16_t port);

    udp_pcb* pcb_ = nullptr;
    ip_addr_t server_{};
    Phase phase_ = Phase::Idle;
    uint64_t deadlineMs_ = 0;
    uint64_t sentUs_ = 0;
    std::optional<Result> result_;
    bool failed_ = false;
};
