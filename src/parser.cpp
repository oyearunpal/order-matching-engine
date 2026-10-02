#include "parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace matching {
namespace {

constexpr std::size_t kAddFieldCount = 5;
constexpr std::size_t kCancelFieldCount = 2;

// Fields beyond this are counted but not stored; only the count matters for errors.
constexpr std::size_t kMaxStoredFields = kAddFieldCount;

struct Fields {
    std::array<std::string_view, kMaxStoredFields> values{};
    std::size_t count = 0;
};

// Splits a line on commas and trims each field. Keeps the first kMaxStoredFields
// fields but counts all of them, so a wrong field count can be reported.
Fields splitFields(std::string_view line) noexcept {
    Fields fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = line.find(',', start);
        const std::string_view field =
            line.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (fields.count < fields.values.size()) {
            fields.values[fields.count] = trim(field);
        }
        ++fields.count;
        if (comma == std::string_view::npos) {
            return fields;
        }
        start = comma + 1;
    }
}

enum class UintStatus { Ok, Invalid, OutOfRange };

// Parses an unsigned decimal integer occupying the whole field (no sign, no spaces).
UintStatus parseUint(std::string_view field, std::uint64_t& value) noexcept {
    const char* const end = field.data() + field.size();
    const auto [ptr, ec] = std::from_chars(field.data(), end, value);
    if (ec == std::errc::result_out_of_range) {
        return UintStatus::OutOfRange;
    }
    if (ec != std::errc{} || ptr != end) {
        return UintStatus::Invalid;
    }
    return UintStatus::Ok;
}

// Wraps a field in single quotes for an error message, escaping unsafe bytes.
std::string quoted(std::string_view field) { return "'" + escapeForLog(field) + "'"; }

// Parses a strictly positive integer field, or explains why it is not one.
std::optional<std::uint64_t> parsePositive(std::string_view name, std::string_view field,
                                           std::string& error) {
    std::uint64_t value = 0;
    switch (parseUint(field, value)) {
        case UintStatus::Ok:
            if (value > 0) {
                return value;
            }
            break;
        case UintStatus::OutOfRange:
            error = std::string(name) + " " + quoted(field) + " is out of range";
            return std::nullopt;
        case UintStatus::Invalid:
            break;
    }
    error = "invalid " + std::string(name) + " " + quoted(field) + ": expected a positive integer";
    return std::nullopt;
}

// Builds the error for a message with the wrong number of fields.
std::string fieldCountError(std::string_view message, std::string_view layout, std::size_t expected,
                            std::size_t actual) {
    return std::string(message) + " expects " + std::to_string(expected) + " fields (" +
           std::string(layout) + "), got " + std::to_string(actual);
}

// Validates and converts the fields of an AddOrderRequest: orderid, side, quantity, price.
ParsedLine parseAdd(const Fields& fields) {
    if (fields.count != kAddFieldCount) {
        return ParseError{fieldCountError("AddOrderRequest", "msgtype,orderid,side,quantity,price",
                                          kAddFieldCount, fields.count)};
    }

    std::string error;
    const auto id = parsePositive("orderid", fields.values[1], error);
    if (!id) {
        return ParseError{std::move(error)};
    }

    const std::string_view sideField = fields.values[2];
    if (sideField != "0" && sideField != "1") {
        return ParseError{"invalid side " + quoted(sideField) + ": expected 0 (buy) or 1 (sell)"};
    }

    const auto quantity = parsePositive("quantity", fields.values[3], error);
    if (!quantity) {
        return ParseError{std::move(error)};
    }

    const auto price = parsePrice(fields.values[4]);
    if (!price) {
        return ParseError{"invalid price " + quoted(fields.values[4]) +
                          ": expected a positive number up to 429496.7295 with at most 4 decimal places"};
    }

    return AddOrderRequest{*id, sideField == "0" ? Side::Buy : Side::Sell, *quantity, *price};
}

// Validates and converts the fields of a CancelOrderRequest: orderid.
ParsedLine parseCancel(const Fields& fields) {
    if (fields.count != kCancelFieldCount) {
        return ParseError{
            fieldCountError("CancelOrderRequest", "msgtype,orderid", kCancelFieldCount, fields.count)};
    }

    std::string error;
    const auto id = parsePositive("orderid", fields.values[1], error);
    if (!id) {
        return ParseError{std::move(error)};
    }
    return CancelOrderRequest{*id};
}

}  // namespace

// Returns the view without surrounding whitespace, or an empty view if nothing else is left.
std::string_view trim(std::string_view text) noexcept {
    constexpr std::string_view kWhitespace = " \t\r\n\v\f";
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

// Copies printable ASCII as is, writes every other byte (and the backslash) as \xNN,
// and stops with "..." once the output reaches maxLength.
std::string escapeForLog(std::string_view text, std::size_t maxLength) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(std::min(text.size(), maxLength) + 8);
    for (const char c : text) {
        if (out.size() >= maxLength) {
            out += "...";
            break;
        }
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte < 0x7f && byte != '\\') {
            out += c;
        } else {
            out += "\\x";
            out += kHex[byte >> 4];
            out += kHex[byte & 0x0f];
        }
    }
    return out;
}

// Reads the message type from the first field, then hands the fields to that type's parser.
ParsedLine parseLine(std::string_view line) {
    const Fields fields = splitFields(trim(line));
    const std::string_view typeField = fields.values[0];

    if (typeField.empty()) {
        return ParseError{"missing message type"};
    }

    std::uint64_t type = 0;
    if (parseUint(typeField, type) == UintStatus::Ok) {
        if (type == static_cast<std::uint64_t>(MessageType::AddOrder)) {
            return parseAdd(fields);
        }
        if (type == static_cast<std::uint64_t>(MessageType::CancelOrder)) {
            return parseCancel(fields);
        }
    }
    return ParseError{"Unknown message type: " + escapeForLog(typeField)};
}

}  // namespace matching
