#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "matching/messages.hpp"

namespace matching {

// Parses one input line into a request, or a ParseError describing what is wrong.
//
// Accepted forms (whitespace around fields and a trailing '\r' are tolerated):
//   AddOrderRequest:    0,orderid,side,quantity,price
//   CancelOrderRequest: 1,orderid
// Never throws except std::bad_alloc while building an error message.
ParsedLine parseLine(std::string_view line);

// Strips leading and trailing whitespace (space, \t, \r, \n, \v, \f).
std::string_view trim(std::string_view text) noexcept;

// Makes arbitrary input safe to print in a log line: non-printable bytes become
// \xNN escapes and the result is truncated to about `maxLength` characters.
std::string escapeForLog(std::string_view text, std::size_t maxLength = 80);

}  // namespace matching
