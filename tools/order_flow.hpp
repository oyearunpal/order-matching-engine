#pragma once

// Seeded random order-flow generator shared by gen_orders, bench and the
// randomized differential test. Produces adds clustered around a mid price
// (so roughly half of them cross the spread) plus cancels of earlier ids.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "messages.hpp"

namespace order_flow {

using matching::AddOrderRequest;
using matching::CancelOrderRequest;
using matching::OrderId;
using matching::Price;
using matching::Quantity;
using matching::Side;

using Request = std::variant<AddOrderRequest, CancelOrderRequest>;

struct Config {
    std::uint64_t seed = 1;
    std::int64_t midRaw = 100'0000;   // 100.0000 (raw Price units, 4 decimals)
    std::int64_t tickRaw = 500;       // 0.05: prices are mid +/- k * tick
    std::int64_t halfRangeTicks = 20; // k in [-halfRangeTicks, +halfRangeTicks]
    Quantity maxQuantity = 100;
    double cancelRatio = 0.3;          // share of requests that are cancels
    double unknownCancelRatio = 0.02;  // share of cancels for ids never issued
    double duplicateIdRatio = 0.01;    // share of adds that reuse an issued id
};

class Generator {
public:
    explicit Generator(const Config& config) : config_(config), rng_(config.seed) {}

    Request next() {
        if (!issued_.empty() && chance(config_.cancelRatio)) {
            return nextCancel();
        }
        return nextAdd();
    }

private:
    bool chance(double probability) { return std::bernoulli_distribution(probability)(rng_); }

    template <typename T>
    T uniform(T low, T high) {
        return std::uniform_int_distribution<T>(low, high)(rng_);
    }

    AddOrderRequest nextAdd() {
        OrderId id = nextId_++;
        if (!issued_.empty() && chance(config_.duplicateIdRatio)) {
            id = issued_[uniform<std::size_t>(0, issued_.size() - 1)];
        } else {
            issued_.push_back(id);
        }
        const Side side = chance(0.5) ? Side::Buy : Side::Sell;
        const std::int64_t offset = uniform<std::int64_t>(-config_.halfRangeTicks, config_.halfRangeTicks);
        const std::int64_t raw = std::clamp<std::int64_t>(config_.midRaw + offset * config_.tickRaw, 1, Price::kMaxRaw);
        return {id, side, uniform<Quantity>(1, config_.maxQuantity), Price::fromRaw(static_cast<Price::Raw>(raw))};
    }

    CancelOrderRequest nextCancel() {
        if (chance(config_.unknownCancelRatio)) {
            return {nextId_ + 1'000'000'000};
        }
        // Swap-remove a random issued id so each id is cancelled at most once.
        const std::size_t index = uniform<std::size_t>(0, issued_.size() - 1);
        const OrderId id = issued_[index];
        issued_[index] = issued_.back();
        issued_.pop_back();
        return {id};
    }

    Config config_;
    std::mt19937_64 rng_;
    OrderId nextId_ = 1;
    std::vector<OrderId> issued_;  // ids added and not yet chosen for cancel
};

// Formats a request in the input wire format, without a trailing newline.
inline std::string toCsv(const Request& request) {
    if (const auto* add = std::get_if<AddOrderRequest>(&request)) {
        return "0," + std::to_string(add->id) + ',' + (add->side == Side::Buy ? "0" : "1") + ',' +
               std::to_string(add->quantity) + ',' + matching::toString(add->price);
    }
    return "1," + std::to_string(std::get<CancelOrderRequest>(request).id);
}

}  // namespace order_flow
