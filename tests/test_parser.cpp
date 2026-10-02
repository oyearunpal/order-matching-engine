#include <string>
#include <variant>

#include "parser.hpp"
#include "test_harness.hpp"

using namespace matching;

namespace {

// Returns the error message, or "<parsed>" if the line was accepted.
std::string errorOf(std::string_view line) {
    const ParsedLine parsed = parseLine(line);
    if (const auto* error = std::get_if<ParseError>(&parsed)) {
        return error->message;
    }
    return "<parsed>";
}

bool isError(std::string_view line) { return std::holds_alternative<ParseError>(parseLine(line)); }

}  // namespace

TEST(ParserReadsAddOrder) {
    const ParsedLine parsed = parseLine("0,123,0,9,1000");
    const auto* add = std::get_if<AddOrderRequest>(&parsed);
    ASSERT_TRUE(add != nullptr);
    EXPECT_EQ(add->id, OrderId{123});
    EXPECT_TRUE(add->side == Side::Buy);
    EXPECT_EQ(add->quantity, Quantity{9});
    EXPECT_EQ(add->price, Price::fromRaw(10'000'000));
}

TEST(ParserReadsSellWithDecimalPrice) {
    const ParsedLine parsed = parseLine("0,7,1,5,1025.2575");
    const auto* add = std::get_if<AddOrderRequest>(&parsed);
    ASSERT_TRUE(add != nullptr);
    EXPECT_TRUE(add->side == Side::Sell);
    EXPECT_EQ(add->price, Price::fromRaw(10'252'575));
}

TEST(ParserReadsCancel) {
    const ParsedLine parsed = parseLine("1,123");
    const auto* cancel = std::get_if<CancelOrderRequest>(&parsed);
    ASSERT_TRUE(cancel != nullptr);
    EXPECT_EQ(cancel->id, OrderId{123});
}

TEST(ParserToleratesWhitespaceAndCarriageReturn) {
    EXPECT_FALSE(isError(" 0 , 1 , 1 , 2 , 3.5 "));
    EXPECT_FALSE(isError("1,5\r"));
    EXPECT_FALSE(isError("\t0,1,0,1,1\r\n"));
}

TEST(ParserReportsUnknownMessageType) {
    EXPECT_EQ(errorOf("BADMESSAGE"), std::string("Unknown message type: BADMESSAGE"));
    EXPECT_EQ(errorOf("2,1,1025"), std::string("Unknown message type: 2"));
    EXPECT_EQ(errorOf("-1,5"), std::string("Unknown message type: -1"));
    EXPECT_EQ(errorOf(",1,0,1,1"), std::string("missing message type"));
}

TEST(ParserChecksFieldCount) {
    EXPECT_EQ(errorOf("0,1,0,5"),
              std::string("AddOrderRequest expects 5 fields (msgtype,orderid,side,quantity,price), got 4"));
    EXPECT_EQ(errorOf("0,1,0,5,100,7"),
              std::string("AddOrderRequest expects 5 fields (msgtype,orderid,side,quantity,price), got 6"));
    EXPECT_EQ(errorOf("1"), std::string("CancelOrderRequest expects 2 fields (msgtype,orderid), got 1"));
    EXPECT_EQ(errorOf("1,5,"), std::string("CancelOrderRequest expects 2 fields (msgtype,orderid), got 3"));
}

TEST(ParserRejectsBadOrderId) {
    EXPECT_EQ(errorOf("0,abc,0,1,1"), std::string("invalid orderid 'abc': expected a positive integer"));
    EXPECT_EQ(errorOf("0,0,0,1,1"), std::string("invalid orderid '0': expected a positive integer"));
    EXPECT_EQ(errorOf("0,-5,0,1,1"), std::string("invalid orderid '-5': expected a positive integer"));
    EXPECT_EQ(errorOf("1,"), std::string("invalid orderid '': expected a positive integer"));
    EXPECT_EQ(errorOf("1,99999999999999999999"), std::string("orderid '99999999999999999999' is out of range"));
    EXPECT_TRUE(isError("1,12abc"));
    EXPECT_TRUE(isError("1,+5"));
    EXPECT_TRUE(isError("1,1 2"));
}

TEST(ParserRejectsBadSide) {
    EXPECT_EQ(errorOf("0,1,2,1,1"), std::string("invalid side '2': expected 0 (buy) or 1 (sell)"));
    EXPECT_TRUE(isError("0,1,B,1,1"));
    EXPECT_TRUE(isError("0,1,,1,1"));
    EXPECT_TRUE(isError("0,1,00,1,1"));
}

TEST(ParserRejectsBadQuantity) {
    EXPECT_EQ(errorOf("0,1,0,0,1"), std::string("invalid quantity '0': expected a positive integer"));
    EXPECT_TRUE(isError("0,1,0,-3,1"));
    EXPECT_TRUE(isError("0,1,0,1.5,1"));
    EXPECT_TRUE(isError("0,1,0,99999999999999999999999,1"));
}

TEST(ParserRejectsBadPrice) {
    EXPECT_EQ(errorOf("0,1,0,1,1.00005"),
              std::string("invalid price '1.00005': expected a positive number up to 429496.7295 with at most 4 decimal places"));
    EXPECT_TRUE(isError("0,1,0,1,429496.7296"));  // above the uint32 ceiling
    EXPECT_TRUE(isError("0,1,0,1,0"));
    EXPECT_TRUE(isError("0,1,0,1,-10"));
    EXPECT_TRUE(isError("0,1,0,1,abc"));
    EXPECT_TRUE(isError("0,1,0,1,"));
}

TEST(ParserEscapesBinaryInErrors) {
    const std::string line("\x01\xff" "abc", 5);
    EXPECT_EQ(errorOf(line), std::string("Unknown message type: \\x01\\xffabc"));
    EXPECT_EQ(errorOf(std::string("0,1,0,1,1\0", 10)),
              std::string("invalid price '1\\x00': expected a positive number up to 429496.7295 with at most 4 decimal places"));
}

TEST(EscapeForLogTruncatesLongInput) {
    const std::string longText(500, 'x');
    const std::string escaped = escapeForLog(longText, 10);
    EXPECT_EQ(escaped, std::string("xxxxxxxxxx..."));
}

TEST(TrimStripsWhitespaceOnly) {
    EXPECT_EQ(trim("  a b \r\n"), std::string_view("a b"));
    EXPECT_EQ(trim(" \t\r\n"), std::string_view(""));
    EXPECT_EQ(trim(""), std::string_view(""));
}
