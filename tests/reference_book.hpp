#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "matching/event_sink.hpp"
#include "matching/messages.hpp"
#include "matching/order_book.hpp"

namespace test_support {

using namespace matching;

// Deliberately naive order book used as a test oracle.
//
// Every operation scans one flat vector, so it is O(n) but obviously correct:
// the best resting order is simply the one with the best price, then the
// lowest arrival sequence number. The randomized test checks that OrderBook
// produces exactly the same results and events.
template <EventSink Sink>
class ReferenceBook {
public:
    explicit ReferenceBook(Sink& sink) : sink_(sink) {}

    AddResult add(const AddOrderRequest& request) {
        if (request.quantity == 0 || request.price <= Price{}) {
            return AddResult::InvalidOrder;
        }
        if (find(request.id) != orders_.end()) {
            return AddResult::DuplicateId;
        }

        Quantity remaining = request.quantity;
        while (remaining > 0) {
            const auto best = bestCrossing(request);
            if (best == orders_.end()) {
                break;
            }
            const Quantity fill = std::min(remaining, best->quantity);
            remaining -= fill;
            best->quantity -= fill;

            sink_.onTrade(fill, best->price);
            report(request.id, remaining);
            report(best->id, best->quantity);
            if (best->quantity == 0) {
                orders_.erase(best);
            }
        }

        if (remaining > 0) {
            orders_.push_back({request.id, request.side, request.price, remaining, nextSequence_++});
        }
        return AddResult::Accepted;
    }

    CancelResult cancel(OrderId id) {
        const auto it = find(id);
        if (it == orders_.end()) {
            return CancelResult::UnknownId;
        }
        orders_.erase(it);
        return CancelResult::Cancelled;
    }

    std::size_t orderCount() const noexcept { return orders_.size(); }

private:
    struct Resting {
        OrderId id;
        Side side;
        Price price;
        Quantity quantity;
        std::uint64_t sequence;
    };
    using Iterator = typename std::vector<Resting>::iterator;

    Iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(), [id](const Resting& o) { return o.id == id; });
    }

    Iterator bestCrossing(const AddOrderRequest& request) {
        const bool aggressorBuys = request.side == Side::Buy;
        Iterator best = orders_.end();
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if (it->side == request.side) continue;
            const bool crosses = aggressorBuys ? it->price <= request.price : it->price >= request.price;
            if (!crosses) continue;
            if (best == orders_.end()) {
                best = it;
                continue;
            }
            const bool betterPrice = aggressorBuys ? it->price < best->price : it->price > best->price;
            const bool samePriceOlder = it->price == best->price && it->sequence < best->sequence;
            if (betterPrice || samePriceOlder) {
                best = it;
            }
        }
        return best;
    }

    void report(OrderId id, Quantity remaining) {
        if (remaining == 0) {
            sink_.onFullyFilled(id);
        } else {
            sink_.onPartiallyFilled(id, remaining);
        }
    }

    Sink& sink_;
    std::vector<Resting> orders_;
    std::uint64_t nextSequence_ = 0;
};

}  // namespace test_support
