#include <optional>
#include <string>

#include "price.hpp"
#include "test_harness.hpp"

using matching::parsePrice;
using matching::Price;
using matching::toString;

namespace {

// Raw value (price x 10^4) of a parsed price, or nullopt if rejected.
std::optional<Price::Raw> raw(std::string_view text) {
    const auto price = parsePrice(text);
    return price ? std::optional<Price::Raw>(price->raw()) : std::nullopt;
}

std::optional<Price::Raw> expect(Price::Raw value) { return value; }

}  // namespace

TEST(PriceParsesWholeAndDecimalValues) {
    EXPECT_EQ(raw("1025"), expect(10'250'000));
    EXPECT_EQ(raw("1025.5"), expect(10'255'000));
    EXPECT_EQ(raw("1025.50"), expect(10'255'000));
    EXPECT_EQ(raw("1025.05"), expect(10'250'500));
    EXPECT_EQ(raw("1025.0001"), expect(10'250'001));
    EXPECT_EQ(raw("0.0001"), expect(1));
    EXPECT_EQ(raw("0.01"), expect(100));
    EXPECT_EQ(raw("007"), expect(70'000));
}

TEST(PriceAcceptsTrailingZerosBeyondFourDecimals) {
    EXPECT_EQ(raw("1.50000"), expect(15'000));
    EXPECT_EQ(raw("1.000000"), expect(10'000));
}

TEST(PriceRejectsPrecisionFinerThanFourDecimals) {
    EXPECT_FALSE(parsePrice("1.00005"));
    EXPECT_FALSE(parsePrice("1025.00001"));
    EXPECT_FALSE(parsePrice("0.00001"));
}

TEST(PriceRejectsNonPositiveValues) {
    EXPECT_FALSE(parsePrice("0"));
    EXPECT_FALSE(parsePrice("0.0000"));
    EXPECT_FALSE(parsePrice("-1"));
    EXPECT_FALSE(parsePrice("-0.5"));
}

TEST(PriceRejectsMalformedText) {
    for (const char* text : {"", ".", "1.", ".5", "+1", "1e3", "1,5", "abc", "1.2.3", " 1", "1 ", "0x10", "1.-5"}) {
        if (!EXPECT_FALSE(parsePrice(text))) {
            std::cerr << "      input: '" << text << "'\n";
        }
    }
}

TEST(PriceRangeEndsAtNasdaqItchLimit) {
    // UINT32_MAX raw units with 4 implied decimals: the ceiling of Nasdaq ITCH's Price(4).
    EXPECT_EQ(raw("429496.7295"), expect(Price::kMaxRaw));
    EXPECT_FALSE(parsePrice("429496.7296"));
    EXPECT_FALSE(parsePrice("429497"));
    EXPECT_FALSE(parsePrice("99999999999999999999999"));
    EXPECT_EQ(raw("429496"), expect(4'294'960'000));
}

TEST(PriceFormatsShortestExactForm) {
    EXPECT_EQ(toString(Price::fromRaw(10'250'000)), std::string("1025"));
    EXPECT_EQ(toString(Price::fromRaw(10'255'000)), std::string("1025.5"));
    EXPECT_EQ(toString(Price::fromRaw(10'250'500)), std::string("1025.05"));
    EXPECT_EQ(toString(Price::fromRaw(10'250'050)), std::string("1025.005"));
    EXPECT_EQ(toString(Price::fromRaw(10'250'001)), std::string("1025.0001"));
    EXPECT_EQ(toString(Price::fromRaw(1)), std::string("0.0001"));
    EXPECT_EQ(toString(Price::fromRaw(0)), std::string("0"));
    EXPECT_EQ(toString(Price::fromRaw(Price::kMaxRaw)), std::string("429496.7295"));
}

TEST(PriceRoundTripsThroughText) {
    for (const Price::Raw value : {1U, 5U, 10U, 99U, 100U, 1000U, 10'000U, 10'001U, 123'456U, Price::kMaxRaw}) {
        const Price price = Price::fromRaw(value);
        EXPECT_EQ(parsePrice(toString(price)), std::optional<Price>(price));
    }
}

TEST(PriceOrdersByValue) {
    EXPECT_TRUE(Price::fromRaw(10'000) < Price::fromRaw(10'001));
    EXPECT_TRUE(Price::fromRaw(10'000) == *parsePrice("1.0000"));
}
