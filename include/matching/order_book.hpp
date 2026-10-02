#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "matching/event_sink.hpp"
#include "matching/messages.hpp"
#include "matching/order.hpp"

namespace matching {

enum class AddResult {
    Accepted,      // matched and/or rested; events (if any) were sent to the sink
    DuplicateId,   // an order with this id is still live in the book
    InvalidOrder,  // zero quantity or non-positive price
};

enum class CancelResult {
    Cancelled,
    UnknownId,  // never seen, already filled, or already cancelled
};

// Single-instrument limit order book with price-time priority.
//
// Data structures:
//   - orders_: hash map that OWNS every resting Order. Its nodes never move,
//     so price levels can link straight to the Orders inside it.
//   - bids_ / asks_: one LevelMap per side, price -> FIFO PriceLevel.
//
// Costs (L = price levels on a side):
//   - does an add match?       O(1): compare with the best opposite level
//   - each fill                O(1): unlink head, erase from orders_
//   - cancel                   O(1) average: hash lookup + unlink
//   - erase an emptied level   amortized O(1) via the stored iterator
//   - rest at a new price      O(log L); at an existing price O(1) after the lookup
//
// The book performs no I/O; all output goes to the EventSink.
template <EventSink Sink>
class OrderBook {
public:
    explicit OrderBook(Sink& sink, std::size_t expectedOrders = 0) : sink_(sink) {
        orders_.reserve(expectedOrders);
    }

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    AddResult add(const AddOrderRequest& request) {
        if (request.quantity == 0 || request.price <= Price{}) {
            return AddResult::InvalidOrder;
        }
        if (orders_.contains(request.id)) {
            return AddResult::DuplicateId;
        }

        const Quantity remaining = match(request);
        if (remaining > 0) {
            rest(request, remaining);
        }
        return AddResult::Accepted;
    }

    CancelResult cancel(OrderId id) {
        const auto it = orders_.find(id);
        if (it == orders_.end()) {
            return CancelResult::UnknownId;
        }
        removeOrder(it->second);
        return CancelResult::Cancelled;
    }

    // ---- Read-only inspection, used by tests and diagnostics. ----

    std::size_t orderCount() const noexcept { return orders_.size(); }
    std::size_t levelCount(Side side) const noexcept { return levels(side).size(); }
    bool contains(OrderId id) const { return orders_.contains(id); }

    std::optional<Price> bestBid() const {
        return bids_.empty() ? std::nullopt : std::optional<Price>(std::prev(bids_.end())->first);
    }
    std::optional<Price> bestAsk() const {
        return asks_.empty() ? std::nullopt : std::optional<Price>(asks_.begin()->first);
    }

    // Resting orders at one price, oldest first, as (id, remaining quantity).
    std::vector<std::pair<OrderId, Quantity>> ordersAt(Side side, Price price) const {
        std::vector<std::pair<OrderId, Quantity>> result;
        const LevelMap& sideLevels = levels(side);
        if (const auto level = sideLevels.find(price); level != sideLevels.end()) {
            for (const Order* order = level->second.front(); order != nullptr; order = order->next) {
                result.emplace_back(order->id, order->quantity);
            }
        }
        return result;
    }

    // Walks the whole book and returns a description of the first broken
    // invariant, or an empty string if the book is consistent. O(n); tests only.
    std::string checkInvariants() const {
        std::size_t linked = 0;
        for (const Side side : {Side::Buy, Side::Sell}) {
            const LevelMap& sideLevels = levels(side);
            for (auto level = sideLevels.begin(); level != sideLevels.end(); ++level) {
                if (level->second.empty()) {
                    return "empty level left in book at " + toString(level->first);
                }
                const Order* previous = nullptr;
                for (const Order* order = level->second.front(); order != nullptr; order = order->next) {
                    if (order->prev != previous) return "broken prev link at order " + std::to_string(order->id);
                    if (order->side != side) return "order on wrong side: " + std::to_string(order->id);
                    if (order->price != level->first) return "order at wrong price: " + std::to_string(order->id);
                    if (order->level != level) return "stale level iterator: " + std::to_string(order->id);
                    if (order->quantity == 0) return "zero-quantity resting order: " + std::to_string(order->id);
                    const auto owned = orders_.find(order->id);
                    if (owned == orders_.end() || &owned->second != order) {
                        return "linked order not owned by index: " + std::to_string(order->id);
                    }
                    previous = order;
                    ++linked;
                }
            }
        }
        if (linked != orders_.size()) {
            return "index holds " + std::to_string(orders_.size()) + " orders but levels link " +
                   std::to_string(linked);
        }
        if (bestBid() && bestAsk() && *bestBid() >= *bestAsk()) {
            return "book is crossed: bid " + toString(*bestBid()) + " >= ask " + toString(*bestAsk());
        }
        return {};
    }

private:
    LevelMap& levels(Side side) noexcept { return side == Side::Buy ? bids_ : asks_; }
    const LevelMap& levels(Side side) const noexcept { return side == Side::Buy ? bids_ : asks_; }

    // Best level of a non-empty side: highest bid or lowest ask.
    static LevelMap::iterator bestLevel(LevelMap& sideLevels, Side side) noexcept {
        return side == Side::Buy ? std::prev(sideLevels.end()) : sideLevels.begin();
    }

    // Can an aggressive order on `aggressorSide` with `limit` trade at `restingPrice`?
    static bool crosses(Side aggressorSide, Price limit, Price restingPrice) noexcept {
        return aggressorSide == Side::Buy ? restingPrice <= limit : restingPrice >= limit;
    }

    // Matches `request` against the opposite side in price-then-time order and
    // returns the quantity left unfilled.
    Quantity match(const AddOrderRequest& request) {
        const Side restingSide = opposite(request.side);
        LevelMap& restingLevels = levels(restingSide);
        Quantity remaining = request.quantity;

        while (remaining > 0 && !restingLevels.empty()) {
            const auto level = bestLevel(restingLevels, restingSide);
            if (!crosses(request.side, request.price, level->first)) {
                break;
            }

            Order& resting = *level->second.front();
            const Quantity fill = std::min(remaining, resting.quantity);
            remaining -= fill;
            resting.quantity -= fill;

            sink_.onTrade(fill, resting.price);
            reportFill(request.id, remaining);
            reportFill(resting.id, resting.quantity);

            if (resting.quantity == 0) {
                removeOrder(resting);
            }
        }
        return remaining;
    }

    void reportFill(OrderId id, Quantity remaining) {
        if (remaining == 0) {
            sink_.onFullyFilled(id);
        } else {
            sink_.onPartiallyFilled(id, remaining);
        }
    }

    void rest(const AddOrderRequest& request, Quantity remaining) {
        LevelMap& sideLevels = levels(request.side);
        const auto level = sideLevels.try_emplace(request.price).first;

        Order& order = orders_.try_emplace(request.id).first->second;
        order.id = request.id;
        order.side = request.side;
        order.price = request.price;
        order.quantity = remaining;
        order.level = level;
        level->second.pushBack(order);
    }

    // The single removal path for fills and cancels. Order matters: unlink and
    // drop the level while the Order is still alive, then destroy it.
    void removeOrder(Order& order) {
        const auto level = order.level;
        level->second.unlink(order);
        if (level->second.empty()) {
            levels(order.side).erase(level);
        }
        const OrderId id = order.id;  // copy: erasing destroys `order`
        orders_.erase(id);
    }

    Sink& sink_;
    std::unordered_map<OrderId, Order> orders_;
    LevelMap bids_;
    LevelMap asks_;
};

}  // namespace matching
