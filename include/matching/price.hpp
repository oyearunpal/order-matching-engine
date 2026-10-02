#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace matching {

// Fixed-point price stored as an integer number of cents.
//
// Assumption: prices have at most 2 decimal places (tick size = 0.01).
// Integer cents keep comparisons exact and cheap; no floating point is involved.
class Price {
public:
    static constexpr std::int64_t kCentsPerUnit = 100;

    constexpr Price() noexcept = default;

    static constexpr Price fromCents(std::int64_t cents) noexcept { return Price(cents); }

    constexpr std::int64_t cents() const noexcept { return cents_; }

    friend constexpr auto operator<=>(Price, Price) noexcept = default;

private:
    constexpr explicit Price(std::int64_t cents) noexcept : cents_(cents) {}

    std::int64_t cents_ = 0;
};

// Parses a strictly positive decimal such as "1025", "1025.5" or "1025.50".
// Rejects signs, empty parts ("1.", ".5"), non-digits, overflow, zero, and
// sub-cent precision ("1.005"). Trailing zeros beyond 2 decimals ("1.500") are
// accepted because they lose no precision.
std::optional<Price> parsePrice(std::string_view text) noexcept;

// Upper bound on characters written by writePrice ("-" + 17 digits + ".dd").
inline constexpr std::size_t kMaxPriceChars = 24;

// Writes the shortest exact form ("1025", "1025.5", "1025.05") starting at
// `out`, which must have room for kMaxPriceChars. Returns one past the end.
char* writePrice(char* out, Price price) noexcept;

std::string toString(Price price);
std::ostream& operator<<(std::ostream& os, Price price);

}  // namespace matching
