#pragma once

#include <concepts>

#include "price.hpp"
#include "types.hpp"

namespace matching {

// Receiver of the order book's output events, chosen at compile time so the
// calls on the matching hot path can be inlined.
//
// For every match the book calls, in order:
//   1. onTrade
//   2. onFullyFilled / onPartiallyFilled for the aggressive order
//   3. onFullyFilled / onPartiallyFilled for the resting order
template <typename S>
concept EventSink = requires(S& sink, OrderId id, Quantity quantity, Price price) {
    { sink.onTrade(quantity, price) } -> std::same_as<void>;
    { sink.onFullyFilled(id) } -> std::same_as<void>;
    { sink.onPartiallyFilled(id, quantity) } -> std::same_as<void>;
};

// Discards every event. Used to benchmark the book without output cost.
struct NullSink {
    void onTrade(Quantity, Price) noexcept {}
    void onFullyFilled(OrderId) noexcept {}
    void onPartiallyFilled(OrderId, Quantity) noexcept {}
};

static_assert(EventSink<NullSink>);

}  // namespace matching
