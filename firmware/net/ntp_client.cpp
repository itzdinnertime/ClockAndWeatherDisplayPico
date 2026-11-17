#include "net/ntp_client.h"

#include "config.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/time.h"
#include "tw/ntp.h"

bool NtpClient::begin() {
    pcb_ = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb_ == nullptr) return false;
    udp_recv(pcb_, &NtpClient::onReceive, this);
    return true;
}

bool NtpClient::start(const char* host, uint64_t nowMs) {
    if (pcb_ == nullptr || busy()) return false;
    deadlineMs_ = nowMs + cfg::NTP_TIMEOUT_MS;
    phase_ = Phase::Resolving;

    const err_t err = dns_gethostbyname(host, &server_, &NtpClient::onDnsFound, this);
    if (err == ERR_OK) {
        sendTo(&server_);  // cached
    } else if (err != ERR_INPROGRESS) {
        fail();
        return false;
    }
    return true;
}

void NtpClient::tick(uint64_t nowMs) {
    if (busy() && nowMs >= deadlineMs_) fail();
}

std::optional<NtpClient::Result> NtpClient::takeResult() {
    auto r = result_;
    result_.reset();
    return r;
}

bool NtpClient::takeFailure() {
    const bool f = failed_;
    failed_ = false;
    return f;
}

void NtpClient::fail() {
    phase_ = Phase::Idle;
    failed_ = true;
}

void NtpClient::onDnsFound(const char*, const ip_addr_t* addr, void* arg) {
    auto* self = static_cast<NtpClient*>(arg);
    if (self->phase_ != Phase::Resolving) return;  // already timed out
    if (addr == nullptr) {
        self->fail();
        return;
    }
    self->server_ = *addr;
    self->sendTo(&self->server_);
}

void NtpClient::sendTo(const ip_addr_t* addr) {
    pbuf* p = pbuf_alloc(PBUF_TRANSPORT, tw::ntp::kPacketSize, PBUF_RAM);
    if (p == nullptr) {
        fail();
        return;
    }
    uint8_t request[tw::ntp::kPacketSize];
    tw::ntp::buildRequest(request);
    pbuf_take(p, request, sizeof(request));

    phase_ = Phase::Waiting;
    sentUs_ = time_us_64();
    const err_t err = udp_sendto(pcb_, p, addr, tw::ntp::kPort);
    pbuf_free(p);
    if (err != ERR_OK) fail();
}

void NtpClient::onReceive(void* arg, udp_pcb*, pbuf* p, const ip_addr_t* addr, uint16_t port) {
    const uint64_t recvUs = time_us_64();
    auto* self = static_cast<NtpClient*>(arg);

    if (self->phase_ == Phase::Waiting && port == tw::ntp::kPort && ip_addr_eq(addr, &self->server_)) {
        uint8_t buf[tw::ntp::kPacketSize];
        const uint16_t n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
        if (const auto txMs = tw::ntp::parseTransmitUnixMs(buf, n)) {
            self->result_ = Result{tw::ntp::compensateRoundTrip(*txMs, self->sentUs_, recvUs), recvUs};
            self->phase_ = Phase::Idle;
        } else {
            self->fail();
        }
    }
    pbuf_free(p);
}
