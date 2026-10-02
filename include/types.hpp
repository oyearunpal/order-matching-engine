#pragma once

#include <cstdint>

namespace matching {

using OrderId = std::uint64_t;
using Quantity = std::uint64_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side side) noexcept {
    return side == Side::Buy ? Side::Sell : Side::Buy;
}

// Wire message type ids, as defined by the protocol.
enum class MessageType : std::uint8_t {
    AddOrder = 0,
    CancelOrder = 1,
    Trade = 2,
    OrderFullyFilled = 3,
    OrderPartiallyFilled = 4,
};

}  // namespace matching
