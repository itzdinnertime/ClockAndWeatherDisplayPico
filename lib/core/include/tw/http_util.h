#pragma once
// HTTP/1.1 helpers. lwIP's httpc client hands us the raw body and does not
// decode "Transfer-Encoding: chunked", so we do it ourselves.

#include <cstddef>
#include <optional>
#include <string_view>

namespace tw::http {

// Case-insensitive check of a raw header block for chunked transfer encoding.
bool headersIndicateChunked(std::string_view headers);

// Decodes a chunked body into `out`. `out` may alias `in` (in-place decode).
// Returns the decoded length, or nullopt on malformed input / overflow.
std::optional<std::size_t> dechunk(std::string_view in, char* out, std::size_t capacity);

}  // namespace tw::http
