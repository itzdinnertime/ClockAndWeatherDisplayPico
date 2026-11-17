#include "tw/json_lite.h"

#include <cstdlib>
#include <cstring>

namespace tw::json {

namespace {

constexpr std::size_t npos = std::string_view::npos;

std::size_t skipWs(std::string_view s, std::size_t i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    return i;
}

// `i` points at an opening quote; returns the index of the closing quote.
std::size_t endOfString(std::string_view s, std::size_t i) {
    for (std::size_t j = i + 1; j < s.size(); ++j) {
        if (s[j] == '\\') {
            ++j;
        } else if (s[j] == '"') {
            return j;
        }
    }
    return npos;
}

// `i` points at '{' or '['; returns the index of the matching close bracket.
std::size_t endOfContainer(std::string_view s, std::size_t i) {
    int depth = 0;
    for (std::size_t j = i; j < s.size(); ++j) {
        const char c = s[j];
        if (c == '"') {
            j = endOfString(s, j);
            if (j == npos) return npos;
        } else if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (--depth == 0) return j;
        }
    }
    return npos;
}

// Returns the index where the value of direct member `key` starts.
std::size_t findMemberValue(std::string_view obj, std::string_view key) {
    std::size_t i = skipWs(obj, 0);
    if (i >= obj.size() || obj[i] != '{') return npos;

    int depth = 0;
    bool expectKey = false;
    for (; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '"') {
            const std::size_t end = endOfString(obj, i);
            if (end == npos) return npos;
            if (depth == 1 && expectKey) {
                const std::string_view k = obj.substr(i + 1, end - i - 1);
                const std::size_t colon = skipWs(obj, end + 1);
                if (colon >= obj.size() || obj[colon] != ':') return npos;
                if (k == key) return skipWs(obj, colon + 1);
                expectKey = false;
                i = colon;
            } else {
                i = end;
            }
        } else if (c == '{' || c == '[') {
            ++depth;
            if (depth == 1) expectKey = true;
        } else if (c == '}' || c == ']') {
            if (--depth <= 0) return npos;
        } else if (c == ',' && depth == 1) {
            expectKey = true;
        }
    }
    return npos;
}

}  // namespace

std::optional<std::string_view> getObject(std::string_view obj, std::string_view key) {
    const std::size_t start = findMemberValue(obj, key);
    if (start == npos || start >= obj.size() || obj[start] != '{') return std::nullopt;
    const std::size_t end = endOfContainer(obj, start);
    if (end == npos) return std::nullopt;
    return obj.substr(start, end - start + 1);
}

std::optional<double> getNumber(std::string_view obj, std::string_view key) {
    const std::size_t start = findMemberValue(obj, key);
    if (start == npos) return std::nullopt;

    char buf[32];
    std::size_t n = 0;
    for (std::size_t i = start; i < obj.size() && n < sizeof(buf) - 1; ++i) {
        const char c = obj[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
            buf[n++] = c;
        } else {
            break;
        }
    }
    if (n == 0) return std::nullopt;
    buf[n] = '\0';

    char* endp = nullptr;
    const double v = std::strtod(buf, &endp);
    if (endp != buf + n) return std::nullopt;
    return v;
}

std::optional<std::string_view> getString(std::string_view obj, std::string_view key) {
    const std::size_t start = findMemberValue(obj, key);
    if (start == npos || start >= obj.size() || obj[start] != '"') return std::nullopt;
    const std::size_t end = endOfString(obj, start);
    if (end == npos) return std::nullopt;
    return obj.substr(start + 1, end - start - 1);
}

}  // namespace tw::json
