#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace matching {

// Fixed-point price, following Nasdaq TotalView-ITCH 5.0's "Price(4)" format:
// an unsigned 32-bit integer with 4 implied decimal places, so the raw value
// 10250000 means 1025.0000.
//
// Range: 0.0001 to 429,496.7295 (UINT32_MAX / 10^4). Prices above that are
// rejected at parse time.
// Integer storage keeps comparisons exact and cheap; no floating point is involved.
class Price {
public:
    using Raw = std::uint32_t;

    static constexpr std::size_t kDecimals = 4;
    static constexpr Raw kScale = 10'000;  // raw units per 1.0
    static constexpr Raw kMaxRaw = std::numeric_limits<Raw>::max();

    constexpr Price() noexcept = default;

    static constexpr Price fromRaw(Raw raw) noexcept { return Price(raw); }

    constexpr Raw raw() const noexcept { return raw_; }

    friend constexpr auto operator<=>(Price, Price) noexcept = default;

private:
    constexpr explicit Price(Raw raw) noexcept : raw_(raw) {}

    Raw raw_ = 0;
};

// Parses a strictly positive decimal such as "1025", "1025.5" or "0.0001".
// Rejects signs, empty parts ("1.", ".5"), non-digits, zero, values above
// 429496.7295, and precision finer than 4 decimals ("1.00005"). Trailing zeros
// beyond 4 decimals ("1.50000") are accepted because they lose no precision.
std::optional<Price> parsePrice(std::string_view text) noexcept;

// Upper bound on characters written by writePrice ("429496.7295" is 11).
inline constexpr std::size_t kMaxPriceChars = 16;

// Writes the shortest exact form ("1025", "1025.5", "0.0001") starting at
// `out`, which must have room for kMaxPriceChars. Returns one past the end.
char* writePrice(char* out, Price price) noexcept;

std::string toString(Price price);
std::ostream& operator<<(std::ostream& os, Price price);

}  // namespace matching
