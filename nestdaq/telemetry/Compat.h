#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 11)
#include <charconv>
#include <system_error>
#endif

namespace nestdaq::telemetry::compat {

inline auto parseDouble(std::string_view token, double& value) -> bool
{
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 11)
    const auto* first = token.data();
    const auto* last = std::next(first, static_cast<std::ptrdiff_t>(token.size()));
    const auto kResult = std::from_chars(first, last, value);
    return kResult.ec == std::errc{} && kResult.ptr == last && std::isfinite(value);
#else
    auto buffer = std::string {token};
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtod(buffer.c_str(), &end);
    const auto* last = std::next(buffer.c_str(), static_cast<std::ptrdiff_t>(buffer.size()));
    if (errno == ERANGE || end == buffer.c_str() || end != last || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
#endif
}

template <typename Integer>
inline auto parseInteger(std::string_view token, Integer& value) -> bool
{
    static_assert(std::is_integral_v<Integer>, "parseInteger requires an integral type");

#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ >= 11)
    const auto* first = token.data();
    const auto* last = std::next(first, static_cast<std::ptrdiff_t>(token.size()));
    const auto kResult = std::from_chars(first, last, value);
    return kResult.ec == std::errc{} && kResult.ptr == last;
#else
    if (token.empty()) {
        return false;
    }

    auto buffer = std::string{token};
    char* end = nullptr;
    errno = 0;

    const auto* last = std::next(buffer.c_str(), static_cast<std::ptrdiff_t>(buffer.size()));

    if constexpr (std::is_signed_v<Integer>) {
        const auto parsed = std::strtoll(buffer.c_str(), &end, 10);
        if (errno == ERANGE || end == buffer.c_str() || end != last) {
            return false;
        }
        if (parsed < static_cast<long long>(std::numeric_limits<Integer>::min()) ||
                parsed > static_cast<long long>(std::numeric_limits<Integer>::max())) {
            return false;
        }
        value = static_cast<Integer>(parsed);
        return true;
    } else {
        if (token.front() == '+' || token.front() == '-') {
            return false;
        }
        const auto parsed = std::strtoull(buffer.c_str(), &end, 10);
        if (errno == ERANGE || end == buffer.c_str() || end != last) {
            return false;
        }
        if (parsed > static_cast<unsigned long long>(std::numeric_limits<Integer>::max())) {
            return false;
        }
        value = static_cast<Integer>(parsed);
        return true;
    }
#endif
}

} // namespace nestdaq::telemetry::compat
