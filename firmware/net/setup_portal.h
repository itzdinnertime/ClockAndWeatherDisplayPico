#pragma once
// Wi-Fi setup ("captive portal") mode.
//
// The Pico becomes an open access point named tw::kSetupApName and runs:
//   * a DHCP server  - hands out 192.168.4.16-23, advertising itself as DNS
//   * a DNS server   - resolves every name to 192.168.4.1
//   * an HTTP server - serves the setup form; any other URL redirects to it
// Because phones probe a known URL after joining a network, the DNS + redirect
// combination makes the OS pop up the setup page automatically.

#include <cstdint>

#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "services/credential_store.h"
#include "tw/captive_net.h"
#include "tw/web.h"

struct tcp_pcb;
struct udp_pcb;
struct pbuf;

class SetupPortal {
public:
    explicit SetupPortal(CredentialStore& store) : store_(store) {}

    // Scan for nearby networks (STA mode) to offer as suggestions. ~3-5 s.
    void scanNetworks();
    // Switch to AP mode and start the DHCP, DNS and HTTP servers.
    bool start();
    // Reboots shortly after credentials are saved.
    void tick(uint64_t nowMs);

private:
    static constexpr int kMaxNetworks = 16;
    static constexpr int kMaxConnections = 4;
    static constexpr std::size_t kRequestCapacity = 1536;

    struct Connection {
        SetupPortal* owner = nullptr;
        tcp_pcb* pcb = nullptr;
        char buf[kRequestCapacity];
        std::size_t len = 0;
    };

    void handleRequest(Connection& c, const tw::web::Request& req);
    err_t sendResponse(Connection& c, const char* status, const char* extraHeaders, const char* body, std::size_t len);
    static err_t closeConnection(Connection& c);

    static int onScanResult(void* env, const struct _cyw43_ev_scan_result_t* result);
    static err_t onAccept(void* arg, tcp_pcb* pcb, err_t err);
    static err_t onReceive(void* arg, tcp_pcb* pcb, pbuf* p, err_t err);
    static void onError(void* arg, err_t err);
    static void onDhcp(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, uint16_t port);
    static void onDns(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, uint16_t port);

    CredentialStore& store_;
    tw::dhcp::LeasePool leases_{0xC0A80410};  // 192.168.4.16

    char networks_[kMaxNetworks][33] = {};
    const char* networkPtrs_[kMaxNetworks] = {};
    int networkCount_ = 0;

    Connection connections_[kMaxConnections];
    char page_[4096] = {};
    uint64_t rebootAtMs_ = 0;
};
