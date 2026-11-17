#include "tw/web.h"

#include <cstring>

namespace tw::web {

namespace {

char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (lower(s[i]) != lower(prefix[i])) return false;
    return true;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Bounded string builder: tracks overflow instead of writing past `cap`.
class Writer {
public:
    Writer(char* out, std::size_t cap) : out_(out), cap_(cap) {}

    void raw(std::string_view s) {
        if (len_ + s.size() >= cap_) {
            overflow_ = true;
            return;
        }
        std::memcpy(out_ + len_, s.data(), s.size());
        len_ += s.size();
    }

    void escaped(std::string_view s) {
        for (char c : s) {
            switch (c) {
                case '&': raw("&amp;"); break;
                case '<': raw("&lt;"); break;
                case '>': raw("&gt;"); break;
                case '"': raw("&quot;"); break;
                case '\'': raw("&#39;"); break;
                default: raw(std::string_view(&c, 1));
            }
        }
    }

    std::size_t finish() {
        if (overflow_ || cap_ == 0) return 0;
        out_[len_] = '\0';
        return len_;
    }

private:
    char* out_;
    std::size_t cap_;
    std::size_t len_ = 0;
    bool overflow_ = false;
};

constexpr std::string_view kHead =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Clock Wi-Fi Setup</title><style>"
    "body{font-family:system-ui,sans-serif;max-width:420px;margin:2em auto;padding:0 1em;"
    "background:#111;color:#eee}"
    "input,button{width:100%;padding:.7em;margin:.3em 0 1em;font-size:1em;box-sizing:border-box;"
    "border-radius:6px;border:1px solid #555;background:#222;color:#eee}"
    "button{background:#f90;color:#000;border:0;font-weight:bold}"
    ".err{color:#f66}</style></head><body>";

}  // namespace

ParseStatus parseRequest(std::string_view raw, Request& out) {
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) return ParseStatus::Incomplete;

    const std::size_t lineEnd = raw.find("\r\n");
    const std::string_view line = raw.substr(0, lineEnd);
    const std::size_t sp1 = line.find(' ');
    const std::size_t sp2 = sp1 == std::string_view::npos ? sp1 : line.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos || sp1 == 0 || sp2 == sp1 + 1) {
        return ParseStatus::Invalid;
    }
    out.method = line.substr(0, sp1);
    out.path = line.substr(sp1 + 1, sp2 - sp1 - 1);

    std::size_t contentLength = 0;
    std::size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        std::size_t eol = raw.find("\r\n", pos);
        const std::string_view header = raw.substr(pos, eol - pos);
        if (startsWithNoCase(header, "content-length:")) {
            std::size_t i = 15;
            while (i < header.size() && header[i] == ' ') ++i;
            if (i == header.size()) return ParseStatus::Invalid;
            for (; i < header.size(); ++i) {
                if (header[i] < '0' || header[i] > '9') return ParseStatus::Invalid;
                contentLength = contentLength * 10 + static_cast<std::size_t>(header[i] - '0');
                if (contentLength > kMaxBody) return ParseStatus::Invalid;
            }
        }
        pos = eol + 2;
    }

    const std::size_t bodyStart = headerEnd + 4;
    if (raw.size() - bodyStart < contentLength) return ParseStatus::Incomplete;
    out.body = raw.substr(bodyStart, contentLength);
    return ParseStatus::Complete;
}

std::optional<std::size_t> formField(std::string_view body, std::string_view key, char* out, std::size_t cap) {
    std::size_t pos = 0;
    while (pos <= body.size()) {
        std::size_t amp = body.find('&', pos);
        if (amp == std::string_view::npos) amp = body.size();
        const std::string_view pair = body.substr(pos, amp - pos);
        const std::size_t eq = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key) {
            const std::string_view value = pair.substr(eq + 1);
            std::size_t n = 0;
            for (std::size_t i = 0; i < value.size(); ++i) {
                char c = value[i];
                if (c == '+') {
                    c = ' ';
                } else if (c == '%') {
                    if (i + 2 >= value.size()) return std::nullopt;  // truncated escape
                    const int hi = hexValue(value[i + 1]);
                    const int lo = hexValue(value[i + 2]);
                    if (hi < 0 || lo < 0) return std::nullopt;
                    c = static_cast<char>((hi << 4) | lo);
                    i += 2;
                }
                if (n + 1 >= cap) return std::nullopt;
                out[n++] = c;
            }
            if (cap == 0) return std::nullopt;
            out[n] = '\0';
            return n;
        }
        pos = amp + 1;
    }
    return std::nullopt;
}

std::size_t renderSetupPage(const char* const* networks, std::size_t count, std::string_view errorMessage,
                            char* out, std::size_t cap) {
    Writer w(out, cap);
    w.raw(kHead);
    w.raw("<h2>Clock Wi-Fi setup</h2>");
    if (!errorMessage.empty()) {
        w.raw("<p class=err>");
        w.escaped(errorMessage);
        w.raw("</p>");
    }
    w.raw("<form method=post action=/save>"
          "<label>Network</label><input name=ssid list=nets maxlength=32 required autocomplete=off>"
          "<datalist id=nets>");
    for (std::size_t i = 0; i < count; ++i) {
        w.raw("<option value=\"");
        w.escaped(networks[i]);
        w.raw("\">");
    }
    w.raw("</datalist><label>Password</label><input name=password type=password maxlength=63>"
          "<button>Save &amp; restart</button></form>"
          "<p>The clock keeps time from its backup RTC while offline.</p></body></html>");
    return w.finish();
}

std::size_t renderSavedPage(std::string_view ssid, char* out, std::size_t cap) {
    Writer w(out, cap);
    w.raw(kHead);
    w.raw("<h2>Saved</h2><p>The clock is restarting and will connect to <b>");
    w.escaped(ssid);
    w.raw("</b>. You can close this page.</p></body></html>");
    return w.finish();
}

}  // namespace tw::web
