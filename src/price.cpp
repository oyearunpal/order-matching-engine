#include "matching/price.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <ostream>
#include <system_error>

namespace matching {
namespace {

bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

bool allDigits(std::string_view s) noexcept { return std::all_of(s.begin(), s.end(), isDigit); }

constexpr std::int64_t kMaxCents = std::numeric_limits<std::int64_t>::max();

}  // namespace

std::optional<Price> parsePrice(std::string_view text) noexcept {
    const auto dot = text.find('.');
    const std::string_view wholePart = text.substr(0, dot);
    const std::string_view fracPart =
        dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);

    if (wholePart.empty() || !allDigits(wholePart)) {
        return std::nullopt;
    }
    if (dot != std::string_view::npos && (fracPart.empty() || !allDigits(fracPart))) {
        return std::nullopt;
    }
    // Digits past the second decimal must be zero; anything else is sub-cent precision.
    if (fracPart.size() > 2 &&
        !std::all_of(fracPart.begin() + 2, fracPart.end(), [](char c) { return c == '0'; })) {
        return std::nullopt;
    }

    std::int64_t whole = 0;
    const auto [ptr, ec] = std::from_chars(wholePart.data(), wholePart.data() + wholePart.size(), whole);
    if (ec != std::errc{} || ptr != wholePart.data() + wholePart.size() ||
        whole > kMaxCents / Price::kCentsPerUnit) {
        return std::nullopt;
    }

    std::int64_t cents = 0;
    for (std::size_t i = 0; i < 2; ++i) {
        cents = cents * 10 + (i < fracPart.size() ? fracPart[i] - '0' : 0);
    }

    if (whole * Price::kCentsPerUnit > kMaxCents - cents) {
        return std::nullopt;  // would overflow int64 cents
    }
    const std::int64_t total = whole * Price::kCentsPerUnit + cents;
    if (total <= 0) {
        return std::nullopt;
    }
    return Price::fromCents(total);
}

char* writePrice(char* out, Price price) noexcept {
    const std::int64_t cents = price.cents();
    // Negate in unsigned arithmetic so INT64_MIN is handled without overflow.
    const std::uint64_t magnitude =
        cents < 0 ? 0 - static_cast<std::uint64_t>(cents) : static_cast<std::uint64_t>(cents);
    if (cents < 0) {
        *out++ = '-';
    }

    out = std::to_chars(out, out + kMaxPriceChars, magnitude / Price::kCentsPerUnit).ptr;

    const auto fraction = static_cast<unsigned>(magnitude % Price::kCentsPerUnit);
    if (fraction != 0) {
        *out++ = '.';
        *out++ = static_cast<char>('0' + fraction / 10);
        if (fraction % 10 != 0) {
            *out++ = static_cast<char>('0' + fraction % 10);
        }
    }
    return out;
}

std::string toString(Price price) {
    char buffer[kMaxPriceChars];
    return std::string(buffer, writePrice(buffer, price));
}

std::ostream& operator<<(std::ostream& os, Price price) {
    char buffer[kMaxPriceChars];
    return os.write(buffer, writePrice(buffer, price) - buffer);
}

}  // namespace matching
