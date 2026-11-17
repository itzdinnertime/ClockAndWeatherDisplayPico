#pragma once
// A tiny, allocation-free JSON reader: just enough to pull scalar members out
// of known API responses. Lookups are scoped to the *direct* members of one
// object, so "current_units.temperature_2m" never shadows
// "current.temperature_2m".

#include <optional>
#include <string_view>

namespace tw::json {

// Returns the member object `key` of `obj` (including its braces).
std::optional<std::string_view> getObject(std::string_view obj, std::string_view key);
std::optional<double> getNumber(std::string_view obj, std::string_view key);
// Returns the raw string contents (escape sequences are not decoded).
std::optional<std::string_view> getString(std::string_view obj, std::string_view key);

}  // namespace tw::json
