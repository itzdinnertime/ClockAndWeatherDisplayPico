#include "net/http_client.h"

#include <cstdio>

#include "lwip/altcp.h"
#include "lwip/pbuf.h"
#include "tw/http_util.h"

bool HttpClient::get(const char* host, const char* uri) {
    if (busy_) return false;
    len_ = 0;
    overflow_ = false;
    chunked_ = false;
    outcome_ = Outcome::None;

    settings_ = {};
    settings_.use_proxy = 0;
    settings_.headers_done_fn = &HttpClient::onHeaders;
    settings_.result_fn = &HttpClient::onResult;

    httpc_state_t* state = nullptr;
    busy_ = true;
    if (httpc_get_file_dns(host, HTTP_DEFAULT_PORT, uri, &settings_, &HttpClient::onBody, this, &state) != ERR_OK) {
        busy_ = false;
        return false;
    }
    return true;
}

HttpClient::Outcome HttpClient::takeOutcome() {
    const Outcome o = outcome_;
    outcome_ = Outcome::None;
    return o;
}

err_t HttpClient::onHeaders(httpc_state_t*, void* arg, pbuf* hdr, u16_t hdrLen, u32_t) {
    auto* self = static_cast<HttpClient*>(arg);
    char headers[512];
    const u16_t n = pbuf_copy_partial(hdr, headers, hdrLen < sizeof(headers) ? hdrLen : sizeof(headers), 0);
    self->chunked_ = tw::http::headersIndicateChunked({headers, n});
    return ERR_OK;
}

err_t HttpClient::onBody(void* arg, altcp_pcb* conn, pbuf* p, err_t) {
    if (p == nullptr) return ERR_OK;
    auto* self = static_cast<HttpClient*>(arg);

    const std::size_t room = kBodyCapacity - self->len_;
    if (p->tot_len > room) self->overflow_ = true;
    const u16_t take = static_cast<u16_t>(p->tot_len < room ? p->tot_len : room);
    self->len_ += pbuf_copy_partial(p, self->buf_ + self->len_, take, 0);

    altcp_recved(conn, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void HttpClient::onResult(void* arg, httpc_result_t result, u32_t, u32_t status, err_t) {
    auto* self = static_cast<HttpClient*>(arg);
    bool ok = result == HTTPC_RESULT_OK && status == 200 && !self->overflow_;

    if (ok && self->chunked_) {
        const auto n = tw::http::dechunk({self->buf_, self->len_}, self->buf_, kBodyCapacity);
        ok = n.has_value();
        self->len_ = n.value_or(0);
    }
    if (!ok) std::printf("[http] failed: result=%d status=%lu\n", static_cast<int>(result), static_cast<unsigned long>(status));

    self->buf_[self->len_] = '\0';
    self->outcome_ = ok ? Outcome::Ok : Outcome::Failed;
    self->busy_ = false;
}
