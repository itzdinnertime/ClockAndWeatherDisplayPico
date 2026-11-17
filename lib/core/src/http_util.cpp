#include "tw/http_util.h"

#include <cstring>

namespace tw::http {

namespace {
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::size_t findCaseInsensitive(std::string_view hay, std::string_view needle, std::size_t from = 0) {
    if (needle.size() > hay.size()) return std::string_view::npos;
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(hay[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return i;
    }
    return std::string_view::npos;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
}  // namespace

bool headersIndicateChunked(std::string_view headers) {
    const std::size_t at = findCaseInsensitive(headers, "transfer-encoding:");
    if (at == std::string_view::npos) return false;
    std::size_t eol = headers.find('\n', at);
    if (eol == std::string_view::npos) eol = headers.size();
    return findCaseInsensitive(headers.substr(at, eol - at), "chunked") != std::string_view::npos;
}

std::optional<std::size_t> dechunk(std::string_view in, char* out, std::size_t capacity) {
    std::size_t r = 0;  // read cursor into `in`
    std::size_t w = 0;  // write cursor into `out`
    while (true) {
        // Chunk size line: hex digits, optional ";extension", CRLF.
        std::size_t size = 0;
        int digits = 0;
        while (r < in.size() && hexValue(in[r]) >= 0) {
            size = size * 16 + static_cast<std::size_t>(hexValue(in[r++]));
            if (++digits > 8) return std::nullopt;
        }
        if (digits == 0) return std::nullopt;
        const std::size_t eol = in.find("\r\n", r);
        if (eol == std::string_view::npos) return std::nullopt;
        r = eol + 2;

        if (size == 0) return w;  // last chunk (trailers ignored)
        if (r + size + 2 > in.size() || w + size > capacity) return std::nullopt;

        std::memmove(out + w, in.data() + r, size);
        w += size;
        r += size;
        if (in[r] != '\r' || in[r + 1] != '\n') return std::nullopt;
        r += 2;
    }
}

}  // namespace tw::http
