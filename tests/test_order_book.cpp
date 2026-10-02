#include <optional>
#include <utility>
#include <vector>

#include "order_book.hpp"
#include "recording_sink.hpp"
#include "test_harness.hpp"

using namespace test_support;

namespace {

using Queue = std::vector<std::pair<OrderId, Quantity>>;

// A book wired to a recording sink, plus helpers that assert each add is accepted.
struct Fixture {
    RecordingSink sink;
    OrderBook<RecordingSink> book{sink};

    void place(const AddOrderRequest& request) {
        EXPECT_EQ(book.add(request), AddResult::Accepted);
    }

    // Places `request` and returns only the events it produced.
    std::vector<Event> placeAndTake(const AddOrderRequest& request) {
        sink.take();
        place(request);
        return sink.take();
    }

    // Builds the resting book from the assignment's Logic section:
    //   1075 S 1 | 1050 S 10 | 1025 S 2,5 || 1000 B 9,1 | 975 B 30
    void buildAssignmentBook() {
        place(sell(1000000, 1, "1075"));
        place(buy(1000001, 9, "1000"));
        place(buy(1000002, 30, "975"));
        place(sell(1000003, 10, "1050"));
        place(buy(1000004, 10, "950"));
        place(sell(1000005, 2, "1025"));
        place(buy(1000006, 1, "1000"));
        EXPECT_EQ(book.cancel(1000004), CancelResult::Cancelled);
        place(sell(1000007, 5, "1025"));
        EXPECT_TRUE(sink.take().empty());
    }

    void expectConsistent() { EXPECT_EQ(book.checkInvariants(), std::string()); }
};

}  // namespace

TEST(AssignmentExampleProducesExpectedEvents) {
    Fixture f;
    f.buildAssignmentBook();

    const std::vector<Event> expected = {
        trade(2, px("1025")), partial(1000008, 1), filled(1000005),
        trade(1, px("1025")), filled(1000008), partial(1000007, 4),
    };
    EXPECT_EQ(f.placeAndTake(buy(1000008, 3, "1050")), expected);

    // Resulting book matches the assignment's "updated list of standing orders".
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("1025")), (Queue{{1000007, 4}}));
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("1050")), (Queue{{1000003, 10}}));
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("1075")), (Queue{{1000000, 1}}));
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("1000")), (Queue{{1000001, 9}, {1000006, 1}}));
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("975")), (Queue{{1000002, 30}}));
    EXPECT_FALSE(f.book.contains(1000008));
    f.expectConsistent();
}

TEST(NonCrossingOrdersRestWithoutEvents) {
    Fixture f;
    f.place(buy(1, 10, "99"));
    f.place(sell(2, 10, "101"));
    EXPECT_TRUE(f.sink.events().empty());
    EXPECT_EQ(f.book.bestBid(), std::optional<Price>(px("99")));
    EXPECT_EQ(f.book.bestAsk(), std::optional<Price>(px("101")));
    EXPECT_EQ(f.book.orderCount(), std::size_t{2});
    f.expectConsistent();
}

TEST(NewOrderAtExistingPriceJoinsBackOfQueue) {
    Fixture f;
    f.place(sell(1, 2, "1025"));
    f.place(sell(2, 5, "1025"));
    f.place(sell(3, 4, "1025"));
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("1025")), (Queue{{1, 2}, {2, 5}, {3, 4}}));
    EXPECT_EQ(f.book.levelCount(Side::Sell), std::size_t{1});
}

TEST(SamePriceMatchesOldestFirst) {
    Fixture f;
    f.place(sell(1, 5, "100"));
    f.place(sell(2, 5, "100"));
    const std::vector<Event> expected = {trade(5, px("100")), partial(3, 2), filled(1),
                                         trade(2, px("100")), filled(3), partial(2, 3)};
    EXPECT_EQ(f.placeAndTake(buy(3, 7, "100")), expected);
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("100")), (Queue{{2, 3}}));
}

TEST(BetterPriceMatchesBeforeOlderOrder) {
    Fixture f;
    f.place(sell(1, 5, "101"));  // older but worse price
    f.place(sell(2, 5, "100"));
    const std::vector<Event> expected = {trade(5, px("100")), filled(3), filled(2)};
    EXPECT_EQ(f.placeAndTake(buy(3, 5, "101")), expected);
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("101")), (Queue{{1, 5}}));
}

TEST(BestBidIsHighestAndBestAskIsLowest) {
    Fixture f;
    f.place(buy(1, 1, "98"));
    f.place(buy(2, 1, "99.5"));
    f.place(buy(3, 1, "97"));
    f.place(sell(4, 1, "102"));
    f.place(sell(5, 1, "100.25"));
    f.place(sell(6, 1, "101"));
    EXPECT_EQ(f.book.bestBid(), std::optional<Price>(px("99.5")));
    EXPECT_EQ(f.book.bestAsk(), std::optional<Price>(px("100.25")));

    // An incoming sell hits the highest bid first, an incoming buy the lowest ask.
    EXPECT_EQ(f.placeAndTake(sell(7, 1, "90")), (std::vector<Event>{trade(1, px("99.5")), filled(7), filled(2)}));
    EXPECT_EQ(f.placeAndTake(buy(8, 1, "200")),
              (std::vector<Event>{trade(1, px("100.25")), filled(8), filled(5)}));
}

TEST(AggressiveOrderSweepsMultipleLevels) {
    Fixture f;
    f.place(sell(1, 3, "100"));
    f.place(sell(2, 4, "101"));
    f.place(sell(3, 5, "102"));
    f.place(sell(4, 1, "103"));
    const std::vector<Event> expected = {
        trade(3, px("100")), partial(5, 7), filled(1),
        trade(4, px("101")), partial(5, 3), filled(2),
        trade(3, px("102")), filled(5), partial(3, 2),
    };
    EXPECT_EQ(f.placeAndTake(buy(5, 10, "102")), expected);
    EXPECT_EQ(f.book.bestAsk(), std::optional<Price>(px("102")));
    EXPECT_EQ(f.book.levelCount(Side::Sell), std::size_t{2});
    f.expectConsistent();
}

TEST(UnfilledRemainderRestsAtItsLimitPrice) {
    Fixture f;
    f.place(sell(1, 3, "100"));
    const std::vector<Event> expected = {trade(3, px("100")), partial(2, 7), filled(1)};
    EXPECT_EQ(f.placeAndTake(buy(2, 10, "105")), expected);
    EXPECT_EQ(f.book.bestBid(), std::optional<Price>(px("105")));
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("105")), (Queue{{2, 7}}));
    EXPECT_EQ(f.book.bestAsk(), std::optional<Price>());
    f.expectConsistent();
}

TEST(ExactMatchFillsBothOrders) {
    Fixture f;
    f.place(buy(1, 5, "100"));
    EXPECT_EQ(f.placeAndTake(sell(2, 5, "100")), (std::vector<Event>{trade(5, px("100")), filled(2), filled(1)}));
    EXPECT_EQ(f.book.orderCount(), std::size_t{0});
    EXPECT_EQ(f.book.levelCount(Side::Buy), std::size_t{0});
    f.expectConsistent();
}

TEST(SellAggressorTradesAtRestingBidPrice) {
    Fixture f;
    f.place(buy(1, 4, "101"));
    f.place(buy(2, 4, "100"));
    const std::vector<Event> expected = {trade(4, px("101")), partial(3, 2), filled(1),
                                         trade(2, px("100")), filled(3), partial(2, 2)};
    EXPECT_EQ(f.placeAndTake(sell(3, 6, "99")), expected);
}

TEST(TradesExecuteAtRestingPriceNotAggressorLimit) {
    Fixture f;
    f.place(sell(1, 1, "100.05"));
    EXPECT_EQ(f.placeAndTake(buy(2, 1, "250")), (std::vector<Event>{trade(1, px("100.05")), filled(2), filled(1)}));
}

TEST(PartialFillOfRestingOrderKeepsItsQueuePosition) {
    Fixture f;
    f.place(sell(1, 10, "100"));
    f.place(sell(2, 10, "100"));
    f.placeAndTake(buy(3, 4, "100"));
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("100")), (Queue{{1, 6}, {2, 10}}));
    EXPECT_EQ(f.placeAndTake(buy(4, 1, "100")), (std::vector<Event>{trade(1, px("100")), filled(4), partial(1, 5)}));
}

TEST(CancelRemovesHeadMiddleAndTail) {
    Fixture f;
    for (OrderId id = 1; id <= 5; ++id) {
        f.place(buy(id, id, "100"));
    }
    EXPECT_EQ(f.book.cancel(3), CancelResult::Cancelled);  // middle
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("100")), (Queue{{1, 1}, {2, 2}, {4, 4}, {5, 5}}));
    EXPECT_EQ(f.book.cancel(1), CancelResult::Cancelled);  // head
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("100")), (Queue{{2, 2}, {4, 4}, {5, 5}}));
    EXPECT_EQ(f.book.cancel(5), CancelResult::Cancelled);  // tail
    EXPECT_EQ(f.book.ordersAt(Side::Buy, px("100")), (Queue{{2, 2}, {4, 4}}));
    f.expectConsistent();

    // FIFO still holds after the cancels.
    EXPECT_EQ(f.placeAndTake(sell(6, 3, "100")),
              (std::vector<Event>{trade(2, px("100")), partial(6, 1), filled(2), trade(1, px("100")), filled(6),
                                  partial(4, 3)}));
    EXPECT_TRUE(f.sink.events().empty());
}

TEST(CancelOfLastOrderRemovesLevel) {
    Fixture f;
    f.place(sell(1, 5, "101"));
    f.place(sell(2, 5, "102"));
    EXPECT_EQ(f.book.cancel(1), CancelResult::Cancelled);
    EXPECT_EQ(f.book.levelCount(Side::Sell), std::size_t{1});
    EXPECT_EQ(f.book.bestAsk(), std::optional<Price>(px("102")));
    f.expectConsistent();
}

TEST(CancelledOrderNoLongerMatches) {
    Fixture f;
    f.place(sell(1, 5, "100"));
    EXPECT_EQ(f.book.cancel(1), CancelResult::Cancelled);
    EXPECT_TRUE(f.placeAndTake(buy(2, 5, "100")).empty());
    EXPECT_EQ(f.book.bestBid(), std::optional<Price>(px("100")));
}

TEST(CancelUnknownOrFilledIdIsRejected) {
    Fixture f;
    EXPECT_EQ(f.book.cancel(42), CancelResult::UnknownId);
    f.place(sell(1, 5, "100"));
    f.place(buy(2, 5, "100"));  // fills order 1
    EXPECT_EQ(f.book.cancel(1), CancelResult::UnknownId);
    EXPECT_EQ(f.book.cancel(2), CancelResult::UnknownId);  // never rested
    f.place(buy(3, 5, "90"));
    EXPECT_EQ(f.book.cancel(3), CancelResult::Cancelled);
    EXPECT_EQ(f.book.cancel(3), CancelResult::UnknownId);  // double cancel
}

TEST(DuplicateLiveIdIsRejectedWithoutSideEffects) {
    Fixture f;
    f.place(sell(1, 5, "100"));
    EXPECT_EQ(f.book.add(buy(1, 5, "100")), AddResult::DuplicateId);
    EXPECT_TRUE(f.sink.events().empty());
    EXPECT_EQ(f.book.ordersAt(Side::Sell, px("100")), (Queue{{1, 5}}));
}

TEST(IdCanBeReusedOnceOrderIsGone) {
    Fixture f;
    f.place(sell(1, 5, "100"));
    EXPECT_EQ(f.book.cancel(1), CancelResult::Cancelled);
    EXPECT_EQ(f.book.add(sell(1, 3, "100")), AddResult::Accepted);
}

TEST(InvalidOrdersAreRejected) {
    Fixture f;
    EXPECT_EQ(f.book.add({1, Side::Buy, 0, px("100")}), AddResult::InvalidOrder);
    EXPECT_EQ(f.book.add({2, Side::Buy, 5, Price::fromRaw(0)}), AddResult::InvalidOrder);
    EXPECT_EQ(f.book.orderCount(), std::size_t{0});
}

TEST(DecimalPricesMatchExactly) {
    Fixture f;
    f.place(sell(1, 5, "100.0001"));
    EXPECT_TRUE(f.placeAndTake(buy(2, 5, "100")).empty());  // one raw unit (0.0001) short
    EXPECT_EQ(f.placeAndTake(buy(3, 5, "100.0001")),
              (std::vector<Event>{trade(5, px("100.0001")), filled(3), filled(1)}));
}

TEST(LargeQuantitiesDoNotOverflow) {
    Fixture f;
    const Quantity big = UINT64_MAX;
    f.place({1, Side::Sell, big, px("100")});
    const std::vector<Event> expected = {trade(big - 1, px("100")), filled(2), partial(1, 1)};
    EXPECT_EQ(f.placeAndTake({2, Side::Buy, big - 1, px("100")}), expected);
}

TEST(BookStaysConsistentThroughManyOperations) {
    Fixture f;
    OrderId id = 1;
    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 20; ++i) {
            const auto raw = static_cast<Price::Raw>(1'000'000 + (i % 7) * 1000);  // 100.00 .. 100.60
            f.place({id++, Side::Sell, static_cast<Quantity>(1 + i % 4), Price::fromRaw(raw)});
            f.place({id++, Side::Buy, static_cast<Quantity>(1 + i % 3), Price::fromRaw(raw - 8000)});
        }
        f.place({id++, Side::Buy, 25, Price::fromRaw(1'003'000)});  // 100.30: sweeps several ask levels
        f.book.cancel(id - 5);
        f.book.cancel(id - 10);
        f.expectConsistent();
    }
}
