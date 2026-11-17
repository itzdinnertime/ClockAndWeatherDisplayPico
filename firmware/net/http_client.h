#pragma once
// One-request-at-a-time HTTP GET on lwIP's httpc, buffering the body.

#include <cstddef>
#include <string_view>

#include "lwip/apps/http_client.h"

class HttpClient {
public:
    static constexpr std::size_t kBodyCapacity = 2048;
    enum class Outcome { None, Ok, Failed };

    bool get(const char* host, const char* uri);
    bool busy() const { return busy_; }
    Outcome takeOutcome();
    std::string_view body() const { return {buf_, len_}; }

private:
    static err_t onHeaders(httpc_state_t* conn, void* arg, struct pbuf* hdr, u16_t hdrLen, u32_t contentLen);
    static err_t onBody(void* arg, struct altcp_pcb* conn, struct pbuf* p, err_t err);
    static void onResult(void* arg, httpc_result_t result, u32_t rxLen, u32_t status, err_t err);

    httpc_connection_t settings_{};
    char buf_[kBodyCapacity + 1] = {};
    std::size_t len_ = 0;
    bool overflow_ = false;
    bool chunked_ = false;
    bool busy_ = false;
    Outcome outcome_ = Outcome::None;
};
