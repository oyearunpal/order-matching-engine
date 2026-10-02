#include "price.hpp"

#include <algorithm>
#include <charconv>
#include <ostream>
#include <system_error>

namespace matching {
namespace {

// True for an ASCII decimal digit.
bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// True if every character is a digit (also true for an empty view; callers check emptiness).
bool allDigits(std::string_view s) noexcept { return std::all_of(s.begin(), s.end(), isDigit); }

// Largest whole part a valid price can have (429496 for 429496.7295).
constexpr std::uint64_t kMaxWhole = Price::kMaxRaw / Price::kScale;

}  // namespace

// Splits the text at '.', validates both parts, and combines them into raw units
// (price x 10^4), rejecting zero and anything above Price::kMaxRaw.
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
    // Digits past the 4th decimal must be zero; anything else is finer than the price grid.
    if (fracPart.size() > Price::kDecimals &&
        !std::all_of(fracPart.begin() + Price::kDecimals, fracPart.end(), [](char c) { return c == '0'; })) {
        return std::nullopt;
    }

    std::uint64_t whole = 0;
    const auto [ptr, ec] = std::from_chars(wholePart.data(), wholePart.data() + wholePart.size(), whole);
    if (ec != std::errc{} || ptr != wholePart.data() + wholePart.size() || whole > kMaxWhole) {
        return std::nullopt;
    }

    std::uint64_t fraction = 0;
    for (std::size_t i = 0; i < Price::kDecimals; ++i) {
        fraction = fraction * 10 + (i < fracPart.size() ? static_cast<std::uint64_t>(fracPart[i] - '0') : 0);
    }

    // Cannot overflow: whole <= kMaxWhole, so whole * kScale + fraction < 2^33.
    const std::uint64_t raw = whole * Price::kScale + fraction;
    if (raw == 0 || raw > Price::kMaxRaw) {
        return std::nullopt;
    }
    return Price::fromRaw(static_cast<Price::Raw>(raw));
}

// Writes the whole part, then the fraction (if any) with trailing zeros dropped.
char* writePrice(char* out, Price price) noexcept {
    const Price::Raw raw = price.raw();
    out = std::to_chars(out, out + kMaxPriceChars, raw / Price::kScale).ptr;

    Price::Raw fraction = raw % Price::kScale;
    if (fraction != 0) {
        // Write all 4 decimals (zero-padded), then drop trailing zeros: 5000 -> ".5", 50 -> ".005".
        *out++ = '.';
        char digits[Price::kDecimals];
        for (std::size_t i = Price::kDecimals; i-- > 0;) {
            digits[i] = static_cast<char>('0' + fraction % 10);
            fraction /= 10;
        }
        std::size_t length = Price::kDecimals;
        while (digits[length - 1] == '0') {
            --length;
        }
        out = std::copy(digits, digits + length, out);
    }
    return out;
}

// Formats a price as a std::string, in its shortest exact form.
std::string toString(Price price) {
    char buffer[kMaxPriceChars];
    return std::string(buffer, writePrice(buffer, price));
}

// Streams a price in its shortest exact form.
std::ostream& operator<<(std::ostream& os, Price price) {
    char buffer[kMaxPriceChars];
    return os.write(buffer, writePrice(buffer, price) - buffer);
}

}  // namespace matching
