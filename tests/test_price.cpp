#include <optional>
#include <string>

#include "matching/price.hpp"
#include "test_harness.hpp"

using matching::parsePrice;
using matching::Price;
using matching::toString;

namespace {

std::optional<std::int64_t> cents(std::string_view text) {
    const auto price = parsePrice(text);
    return price ? std::optional<std::int64_t>(price->cents()) : std::nullopt;
}

}  // namespace

TEST(PriceParsesWholeAndDecimalValues) {
    EXPECT_EQ(cents("1025"), std::optional<std::int64_t>(102500));
    EXPECT_EQ(cents("1025.5"), std::optional<std::int64_t>(102550));
    EXPECT_EQ(cents("1025.50"), std::optional<std::int64_t>(102550));
    EXPECT_EQ(cents("1025.05"), std::optional<std::int64_t>(102505));
    EXPECT_EQ(cents("0.01"), std::optional<std::int64_t>(1));
    EXPECT_EQ(cents("007"), std::optional<std::int64_t>(700));
}

TEST(PriceAcceptsTrailingZerosBeyondTwoDecimals) {
    EXPECT_EQ(cents("1.500"), std::optional<std::int64_t>(150));
    EXPECT_EQ(cents("1.0000"), std::optional<std::int64_t>(100));
}

TEST(PriceRejectsSubCentPrecision) {
    EXPECT_FALSE(parsePrice("1.005"));
    EXPECT_FALSE(parsePrice("1025.001"));
    EXPECT_FALSE(parsePrice("0.001"));
}

TEST(PriceRejectsNonPositiveValues) {
    EXPECT_FALSE(parsePrice("0"));
    EXPECT_FALSE(parsePrice("0.00"));
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

TEST(PriceRejectsOverflow) {
    EXPECT_FALSE(parsePrice("92233720368547758.08"));  // just above INT64_MAX cents
    EXPECT_FALSE(parsePrice("99999999999999999999999"));
    EXPECT_EQ(cents("92233720368547758.07"), std::optional<std::int64_t>(INT64_MAX));
}

TEST(PriceFormatsShortestExactForm) {
    EXPECT_EQ(toString(Price::fromCents(102500)), std::string("1025"));
    EXPECT_EQ(toString(Price::fromCents(102550)), std::string("1025.5"));
    EXPECT_EQ(toString(Price::fromCents(102505)), std::string("1025.05"));
    EXPECT_EQ(toString(Price::fromCents(1)), std::string("0.01"));
    EXPECT_EQ(toString(Price::fromCents(-150)), std::string("-1.5"));
    EXPECT_EQ(toString(Price::fromCents(INT64_MAX)), std::string("92233720368547758.07"));
    EXPECT_EQ(toString(Price::fromCents(INT64_MIN)), std::string("-92233720368547758.08"));
}

TEST(PriceRoundTripsThroughText) {
    for (const std::int64_t value : {1L, 5L, 10L, 99L, 100L, 101L, 123456L, 100000000L}) {
        const Price price = Price::fromCents(value);
        EXPECT_EQ(parsePrice(toString(price)), std::optional<Price>(price));
    }
}

TEST(PriceOrdersByValue) {
    EXPECT_TRUE(Price::fromCents(100) < Price::fromCents(101));
    EXPECT_TRUE(Price::fromCents(100) == *parsePrice("1.00"));
}
