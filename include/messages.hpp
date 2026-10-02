#pragma once

#include <string>
#include <variant>

#include "price.hpp"
#include "types.hpp"

namespace matching {

struct AddOrderRequest {
    OrderId id = 0;
    Side side = Side::Buy;
    Quantity quantity = 0;
    Price price;

    friend bool operator==(const AddOrderRequest&, const AddOrderRequest&) = default;
};

struct CancelOrderRequest {
    OrderId id = 0;

    friend bool operator==(const CancelOrderRequest&, const CancelOrderRequest&) = default;
};

struct ParseError {
    std::string message;
};

using ParsedLine = std::variant<AddOrderRequest, CancelOrderRequest, ParseError>;

}  // namespace matching
