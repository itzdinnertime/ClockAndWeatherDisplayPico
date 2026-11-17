#include "net/setup_portal.h"

#include <cstdio>
#include <cstring>

#include "hardware/watchdog.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "tw/provisioning.h"

namespace {
uint64_t nowMs() { return time_us_64() / 1000; }

void sendUdp(udp_pcb* pcb, const uint8_t* data, std::size_t len, const ip_addr_t* to, uint16_t port, netif* via) {
    pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<u16_t>(len), PBUF_RAM);
    if (p == nullptr) return;
    pbuf_take(p, data, static_cast<u16_t>(len));
    if (via != nullptr) {
        udp_sendto_if(pcb, p, to, port, via);
    } else {
        udp_sendto(pcb, p, to, port);
    }
    pbuf_free(p);
}
}  // namespace

// ------------------------------------------------------------- scanning ---

int SetupPortal::onScanResult(void* env, const cyw43_ev_scan_result_t* r) {
    auto* self = static_cast<SetupPortal*>(env);
    if (r == nullptr || r->ssid_len == 0 || r->ssid_len > 32) return 0;  // hidden network

    char name[33] = {};
    std::memcpy(name, r->ssid, r->ssid_len);
    for (int i = 0; i < self->networkCount_; ++i)
        if (std::strcmp(self->networks_[i], name) == 0) return 0;  // seen on another channel/AP
    if (self->networkCount_ < kMaxNetworks) {
        std::memcpy(self->networks_[self->networkCount_], name, sizeof(name));
        self->networkPtrs_[self->networkCount_] = self->networks_[self->networkCount_];
        ++self->networkCount_;
    }
    return 0;
}

void SetupPortal::scanNetworks() {
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_scan_options_t opts = {};
    if (cyw43_wifi_scan(&cyw43_state, &opts, this, &SetupPortal::onScanResult) == 0) {
        const uint64_t deadline = nowMs() + 8000;
        while (cyw43_wifi_scan_active(&cyw43_state) && nowMs() < deadline) {
            cyw43_arch_poll();
            watchdog_update();  // bounded wait; render core is still checked afterwards
            sleep_ms(10);
        }
    }
    cyw43_arch_disable_sta_mode();
    std::printf("[setup] found %d networks\n", networkCount_);
}

// --------------------------------------------------------------- servers ---

bool SetupPortal::start() {
    cyw43_arch_enable_ap_mode(tw::kSetupApName, nullptr, CYW43_AUTH_OPEN);

    udp_pcb* dhcp = udp_new();
    udp_pcb* dns = udp_new();
    tcp_pcb* http = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (dhcp == nullptr || dns == nullptr || http == nullptr) return false;

    ip_set_option(dhcp, SOF_BROADCAST);
    if (udp_bind(dhcp, IP_ADDR_ANY, tw::dhcp::kServerPort) != ERR_OK) return false;
    udp_recv(dhcp, &SetupPortal::onDhcp, this);

    if (udp_bind(dns, IP_ADDR_ANY, tw::dns::kPort) != ERR_OK) return false;
    udp_recv(dns, &SetupPortal::onDns, this);

    if (tcp_bind(http, IP_ADDR_ANY, 80) != ERR_OK) return false;
    tcp_pcb* listener = tcp_listen_with_backlog(http, kMaxConnections);
    if (listener == nullptr) return false;
    tcp_arg(listener, this);
    tcp_accept(listener, &SetupPortal::onAccept);

    std::printf("[setup] portal up: join \"%s\" and open %s\n", tw::kSetupApName, tw::kPortalUrl);
    return true;
}

void SetupPortal::tick(uint64_t now) {
    if (rebootAtMs_ != 0 && now >= rebootAtMs_) {
        std::printf("[setup] credentials saved, rebooting\n");
        watchdog_reboot(0, 0, 0);
        while (true) tight_loop_contents();
    }
}

void SetupPortal::onDhcp(void* arg, udp_pcb* pcb, pbuf* p, const ip_addr_t*, uint16_t) {
    auto* self = static_cast<SetupPortal*>(arg);
    uint8_t in[548];
    const u16_t n = pbuf_copy_partial(p, in, sizeof(in), 0);
    pbuf_free(p);

    const auto msg = tw::dhcp::parseClientMessage(in, n);
    if (!msg) return;
    tw::dhcp::MessageType reply;
    if (msg->type == tw::dhcp::Discover) {
        reply = tw::dhcp::Offer;
    } else if (msg->type == tw::dhcp::Request) {
        reply = tw::dhcp::Ack;
    } else {
        return;
    }
    const auto ip = self->leases_.assign(msg->mac);
    if (!ip) return;

    uint8_t out[tw::dhcp::kMinPacket];
    const std::size_t len = tw::dhcp::buildReply(*msg, reply, *ip, tw::kPortalIp, 3600, out, sizeof(out));
    if (len > 0) sendUdp(pcb, out, len, IP_ADDR_BROADCAST, tw::dhcp::kClientPort, &cyw43_state.netif[CYW43_ITF_AP]);
}

void SetupPortal::onDns(void*, udp_pcb* pcb, pbuf* p, const ip_addr_t* addr, uint16_t port) {
    uint8_t in[512];
    const u16_t n = pbuf_copy_partial(p, in, sizeof(in), 0);
    pbuf_free(p);

    uint8_t out[512 + 16];
    const std::size_t len = tw::dns::buildCaptiveReply(in, n, tw::kPortalIp, out, sizeof(out));
    if (len > 0) sendUdp(pcb, out, len, addr, port, nullptr);
}

// ------------------------------------------------------------------ HTTP ---

err_t SetupPortal::onAccept(void* arg, tcp_pcb* pcb, err_t err) {
    auto* self = static_cast<SetupPortal*>(arg);
    if (err != ERR_OK || pcb == nullptr) return ERR_VAL;

    for (Connection& c : self->connections_) {
        if (c.pcb == nullptr) {
            c.owner = self;
            c.pcb = pcb;
            c.len = 0;
            tcp_arg(pcb, &c);
            tcp_recv(pcb, &SetupPortal::onReceive);
            tcp_err(pcb, &SetupPortal::onError);
            return ERR_OK;
        }
    }
    tcp_abort(pcb);  // all slots busy
    return ERR_ABRT;
}

void SetupPortal::onError(void* arg, err_t) {
    // lwIP has already freed the pcb.
    if (arg != nullptr) static_cast<Connection*>(arg)->pcb = nullptr;
}

err_t SetupPortal::closeConnection(Connection& c) {
    tcp_pcb* pcb = c.pcb;
    c.pcb = nullptr;
    if (pcb == nullptr) return ERR_OK;
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

err_t SetupPortal::onReceive(void* arg, tcp_pcb* pcb, pbuf* p, err_t) {
    auto& c = *static_cast<Connection*>(arg);
    if (p == nullptr) return closeConnection(c);  // client closed

    const std::size_t room = kRequestCapacity - c.len;
    const bool overflow = p->tot_len > room;
    c.len += pbuf_copy_partial(p, c.buf + c.len, static_cast<u16_t>(overflow ? room : p->tot_len), 0);
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    tw::web::Request req;
    const auto status = tw::web::parseRequest({c.buf, c.len}, req);
    if (status == tw::web::ParseStatus::Incomplete && !overflow) return ERR_OK;
    if (status != tw::web::ParseStatus::Complete) return closeConnection(c);

    c.owner->handleRequest(c, req);
    if (c.pcb == nullptr) return ERR_OK;
    return closeConnection(c);
}

void SetupPortal::handleRequest(Connection& c, const tw::web::Request& req) {
    const bool isRoot = req.path == "/" || req.path.substr(0, 2) == "/?";

    if (req.method == "POST" && req.path == "/save") {
        // Buffers are larger than the limits so over-long input reaches
        // validateCredentials() and gets a specific error message.
        char ssid[128] = {};
        char password[128] = {};
        const auto ssidLen = tw::web::formField(req.body, "ssid", ssid, sizeof(ssid));
        const auto passLen = tw::web::formField(req.body, "password", password, sizeof(password));

        tw::CredentialError error;
        if (!ssidLen) {
            error = tw::CredentialError::SsidEmpty;
        } else if (!passLen) {
            error = tw::CredentialError::PasswordLength;
        } else {
            error = tw::validateCredentials({ssid, *ssidLen}, {password, *passLen});
        }

        const char* message = tw::describe(error);
        if (error == tw::CredentialError::None) {
            const auto creds = tw::makeCredentials({ssid, *ssidLen}, {password, *passLen});
            if (creds && store_.save(*creds)) {
                const std::size_t len = tw::web::renderSavedPage(ssid, page_, sizeof(page_));
                sendResponse(c, "200 OK", "", page_, len);
                rebootAtMs_ = nowMs() + 2000;  // let the response reach the phone first
                return;
            }
            message = "Could not save to flash. Please try again.";
        }
        const std::size_t len = tw::web::renderSetupPage(networkPtrs_, networkCount_, message, page_, sizeof(page_));
        sendResponse(c, "200 OK", "", page_, len);
        return;
    }

    if (req.method == "GET" && isRoot) {
        const std::size_t len = tw::web::renderSetupPage(networkPtrs_, networkCount_, "", page_, sizeof(page_));
        sendResponse(c, "200 OK", "", page_, len);
        return;
    }

    // Anything else (OS connectivity checks, favicon, ...) goes to the form.
    char location[64];
    std::snprintf(location, sizeof(location), "Location: %s\r\n", tw::kPortalUrl);
    sendResponse(c, "302 Found", location, "", 0);
}

err_t SetupPortal::sendResponse(Connection& c, const char* status, const char* extraHeaders, const char* body,
                                std::size_t len) {
    char header[256];
    const int n = std::snprintf(header, sizeof(header),
                                "HTTP/1.1 %s\r\n%sContent-Type: text/html; charset=utf-8\r\n"
                                "Content-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                                status, extraHeaders, static_cast<unsigned>(len));
    err_t err = tcp_write(c.pcb, header, static_cast<u16_t>(n), TCP_WRITE_FLAG_COPY | (len ? TCP_WRITE_FLAG_MORE : 0));
    if (err == ERR_OK && len > 0) err = tcp_write(c.pcb, body, static_cast<u16_t>(len), TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) tcp_output(c.pcb);
    return err;
}
