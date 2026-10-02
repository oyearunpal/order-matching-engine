#pragma once

#include <cstdlib>
#include <iostream>
#include <ostream>
#include <string_view>
#include <utility>
#include <vector>

#include "matching/event_sink.hpp"
#include "matching/order_book.hpp"
#include "matching/price.hpp"

namespace test_support {

using namespace matching;

struct Event {
    enum class Kind { Trade, FullyFilled, PartiallyFilled };

    Kind kind = Kind::Trade;
    OrderId id = 0;         // fills only
    Quantity quantity = 0;  // trade quantity, or remaining quantity for partial fills
    Price price;            // trades only

    friend bool operator==(const Event&, const Event&) = default;
};

inline Event trade(Quantity quantity, Price price) { return {Event::Kind::Trade, 0, quantity, price}; }
inline Event filled(OrderId id) { return {Event::Kind::FullyFilled, id, 0, Price{}}; }
inline Event partial(OrderId id, Quantity remaining) { return {Event::Kind::PartiallyFilled, id, remaining, Price{}}; }

// Prints events in the wire format, which makes test failures easy to read.
inline std::ostream& operator<<(std::ostream& os, const Event& event) {
    switch (event.kind) {
        case Event::Kind::Trade:
            return os << "2," << event.quantity << ',' << event.price;
        case Event::Kind::FullyFilled:
            return os << "3," << event.id;
        case Event::Kind::PartiallyFilled:
            return os << "4," << event.id << ',' << event.quantity;
    }
    return os;
}

// Sink that records every event so tests can assert on the exact sequence.
class RecordingSink {
public:
    void onTrade(Quantity quantity, Price price) { events_.push_back(trade(quantity, price)); }
    void onFullyFilled(OrderId id) { events_.push_back(filled(id)); }
    void onPartiallyFilled(OrderId id, Quantity remaining) { events_.push_back(partial(id, remaining)); }

    const std::vector<Event>& events() const noexcept { return events_; }

    // Returns the events recorded so far and clears the log.
    std::vector<Event> take() { return std::exchange(events_, {}); }

private:
    std::vector<Event> events_;
};

static_assert(EventSink<RecordingSink>);

// Price literal for tests: px("1025.5"). Aborts on invalid input.
inline Price px(std::string_view text) {
    const auto price = parsePrice(text);
    if (!price) {
        std::cerr << "test bug: invalid price literal '" << text << "'\n";
        std::abort();
    }
    return *price;
}

inline AddOrderRequest buy(OrderId id, Quantity quantity, std::string_view price) {
    return {id, Side::Buy, quantity, px(price)};
}
inline AddOrderRequest sell(OrderId id, Quantity quantity, std::string_view price) {
    return {id, Side::Sell, quantity, px(price)};
}

inline std::ostream& operator<<(std::ostream& os, AddResult result) {
    switch (result) {
        case AddResult::Accepted: return os << "Accepted";
        case AddResult::DuplicateId: return os << "DuplicateId";
        case AddResult::InvalidOrder: return os << "InvalidOrder";
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, CancelResult result) {
    switch (result) {
        case CancelResult::Cancelled: return os << "Cancelled";
        case CancelResult::UnknownId: return os << "UnknownId";
    }
    return os;
}

}  // namespace test_support
