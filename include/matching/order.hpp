#pragma once

#include <map>

#include "matching/price.hpp"
#include "matching/types.hpp"

namespace matching {

// Order and PriceLevel live in one header because they depend on each other:
// an Order stores an iterator to its level, and a level links Orders together.

struct Order;

// FIFO queue of the resting orders at one price, oldest first.
//
// An intrusive doubly-linked list: the links live inside Order, so the level
// allocates nothing per order and can unlink any order in O(1). The level does
// not own its orders; OrderBook owns them (see order_book.hpp).
class PriceLevel {
public:
    bool empty() const noexcept { return head_ == nullptr; }
    Order* front() const noexcept { return head_; }

    void pushBack(Order& order) noexcept;
    void unlink(Order& order) noexcept;

private:
    Order* head_ = nullptr;
    Order* tail_ = nullptr;
};

// Both sides use this exact type (ascending by price) so that every Order can
// store an iterator to its level regardless of side. Best ask = begin(),
// best bid = std::prev(end()).
using LevelMap = std::map<Price, PriceLevel>;

struct Order {
    OrderId id = 0;
    Side side = Side::Buy;
    Price price;
    Quantity quantity = 0;  // remaining open quantity

    // Intrusive FIFO links and the owning level, managed by PriceLevel / OrderBook.
    Order* prev = nullptr;
    Order* next = nullptr;
    LevelMap::iterator level{};
};

inline void PriceLevel::pushBack(Order& order) noexcept {
    order.prev = tail_;
    order.next = nullptr;
    if (tail_ != nullptr) {
        tail_->next = &order;
    } else {
        head_ = &order;
    }
    tail_ = &order;
}

inline void PriceLevel::unlink(Order& order) noexcept {
    if (order.prev != nullptr) {
        order.prev->next = order.next;
    } else {
        head_ = order.next;
    }
    if (order.next != nullptr) {
        order.next->prev = order.prev;
    } else {
        tail_ = order.prev;
    }
    order.prev = nullptr;
    order.next = nullptr;
}

}  // namespace matching
