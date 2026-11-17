#pragma once
// Minimal HTTP server-side helpers for the setup portal.

#include <cstddef>
#include <optional>
#include <string_view>

namespace tw::web {

struct Request {
    std::string_view method;
    std::string_view path;  // including any query string
    std::string_view body;
};

enum class ParseStatus { Incomplete, Complete, Invalid };

constexpr std::size_t kMaxBody = 1024;

// Parses a raw request buffer. Returns Incomplete until the headers and the
// full Content-Length body have arrived.
ParseStatus parseRequest(std::string_view raw, Request& out);

// Extracts and decodes one field of an application/x-www-form-urlencoded
// body ('+' -> space, %XX escapes). Returns the decoded length; nullopt if
// the key is missing, an escape is malformed, or it doesn't fit in `cap`.
std::optional<std::size_t> formField(std::string_view body, std::string_view key, char* out, std::size_t cap);

// Renders the setup page with the scanned networks as suggestions.
// Network names are HTML-escaped. Returns 0 if `cap` is too small.
std::size_t renderSetupPage(const char* const* networks, std::size_t count, std::string_view errorMessage,
                            char* out, std::size_t cap);

// Renders the "saved, restarting" confirmation page.
std::size_t renderSavedPage(std::string_view ssid, char* out, std::size_t cap);

}  // namespace tw::web
