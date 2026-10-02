// Differential test: OrderBook must behave exactly like the naive
// ReferenceBook on long random sequences of adds and cancels.

#include <cstdint>
#include <iostream>
#include <variant>

#include "order_book.hpp"
#include "order_flow.hpp"
#include "recording_sink.hpp"
#include "reference_book.hpp"
#include "test_harness.hpp"

using namespace test_support;

namespace {

// Runs one random scenario and returns false on the first divergence.
bool runScenario(const order_flow::Config& config, int steps) {
    RecordingSink bookSink;
    RecordingSink referenceSink;
    OrderBook<RecordingSink> book(bookSink);
    ReferenceBook<RecordingSink> reference(referenceSink);
    order_flow::Generator generator(config);

    for (int step = 0; step < steps; ++step) {
        const order_flow::Request request = generator.next();
        bool sameResult = false;
        if (const auto* add = std::get_if<AddOrderRequest>(&request)) {
            sameResult = EXPECT_EQ(book.add(*add), reference.add(*add));
        } else {
            const auto& cancel = std::get<CancelOrderRequest>(request);
            sameResult = EXPECT_EQ(book.cancel(cancel.id), reference.cancel(cancel.id));
        }

        const bool sameEvents = EXPECT_EQ(bookSink.take(), referenceSink.take());
        const bool sameSize = EXPECT_EQ(book.orderCount(), reference.orderCount());
        const bool consistent = EXPECT_EQ(book.checkInvariants(), std::string());
        if (!(sameResult && sameEvents && sameSize && consistent)) {
            std::cerr << "      diverged at step " << step << " (seed " << config.seed
                      << ") on request: " << order_flow::toCsv(request) << '\n';
            return false;
        }
    }
    return true;
}

}  // namespace

TEST(RandomizedMatchesReferenceOnNarrowBook) {
    // Few price levels: deep queues, frequent multi-order sweeps.
    for (std::uint64_t seed = 1; seed <= 300; ++seed) {
        order_flow::Config config;
        config.seed = seed;
        config.halfRangeTicks = 3;
        config.maxQuantity = 20;
        if (!runScenario(config, 400)) return;
    }
}

TEST(RandomizedMatchesReferenceOnWideBook) {
    // Many price levels: frequent level creation and removal.
    for (std::uint64_t seed = 1000; seed < 1300; ++seed) {
        order_flow::Config config;
        config.seed = seed;
        config.halfRangeTicks = 60;
        config.tickRaw = 1;  // 0.0001: exercises 4-decimal prices
        config.maxQuantity = 200;
        if (!runScenario(config, 400)) return;
    }
}

TEST(RandomizedMatchesReferenceUnderHeavyCancels) {
    for (std::uint64_t seed = 5000; seed < 5200; ++seed) {
        order_flow::Config config;
        config.seed = seed;
        config.cancelRatio = 0.6;
        config.unknownCancelRatio = 0.1;
        config.duplicateIdRatio = 0.05;
        if (!runScenario(config, 400)) return;
    }
}
