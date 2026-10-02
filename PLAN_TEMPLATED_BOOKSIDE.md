# Variant Plan: Templated `BookSide<Side>`

To be built **after** the main implementation in `PLAN.md`, so the two order-book designs can be compared side by side.
Everything else stays the same as `PLAN.md`: parser, `Price` stored as cents, the `EventSink` concept, `PriceLevel` FIFO, the hash map that owns each `Order`, and the CSV writer.

## Idea
The main plan keeps two maps of the same type `std::map<Price, PriceLevel>`, both sorted ascending. The best bid is `std::prev(bids_.end())`, and side-specific helpers in `OrderBook` decide where the best price is and whether an order crosses.

This variant turns each side into a template whose sort order and crossing rule are fixed at compile time:

```cpp
template <Side S> struct SideTraits;

template <> struct SideTraits<Side::Buy> {
    using Compare = std::greater<Price>;   // highest bid first
    static bool crosses(Price bestBid, Price incomingSell) { return bestBid >= incomingSell; }
};
template <> struct SideTraits<Side::Sell> {
    using Compare = std::less<Price>;      // lowest ask first
    static bool crosses(Price bestAsk, Price incomingBuy) { return bestAsk <= incomingBuy; }
};

template <Side S>
class BookSide {
    std::map<Price, PriceLevel, typename SideTraits<S>::Compare> levels_;
public:
    PriceLevel* best();                  // levels_.begin(), or nullptr if empty
    bool crosses(Price incoming) const;  // non-empty && SideTraits<S>::crosses(best, incoming)
    void append(Order&);                 // find or create the level at order.price, push to its tail
    void remove(Order&);                 // unlink; erase the level by key if now empty
    void popBestIfEmpty();               // fills: erase(begin())
};
```

- `OrderBook` holds `BookSide<Side::Buy> bids_` and `BookSide<Side::Sell> asks_`. These are two maps of **different** types.
- "Best" is `begin()` on both sides. The comparator encodes each side's priority, so the `std::prev(end())` special case goes away.
- The matching loop is written once, as `template <Side S> void match(Order& incoming, BookSide<opposite(S)>& book)`, with no side checks at runtime. `add()` makes one branch on `req.side` to choose the instantiation, and `removeOrder()` makes one branch on `order.side` to call the right `BookSide::remove()`.

## The trade-off being tested
- Because the two maps have different iterator types, `Order` can't hold one iterator field for either side. It stores a `PriceLevel*` instead, which stays valid because `std::map` nodes never move.
- Unlinking an order from its level is O(1), the same as the main plan.
- A fill empties the best level, which is always `begin()`, so `erase(begin())` costs the same as the main plan.
- A **cancel that empties a level** must call `levels_.erase(order.price)`. That is one O(log L) lookup by price before the same red-black tree delete the main plan does. This lookup is the only extra cost. The tree delete is O(log L) worst case in both designs.

## Files (separate namespace, so both versions build together)
```
include/matching/templated/
  side_traits.hpp         # SideTraits<Buy|Sell>, opposite(Side)
  order.hpp               # Order with PriceLevel* instead of LevelMap::iterator
  book_side.hpp           # template <Side S> class BookSide
  order_book.hpp          # matching::templated::OrderBook<Sink>, same public API as the main OrderBook
```
- The public API is identical: `add(const AddOrderRequest&) -> AddResult`, `cancel(OrderId) -> CancelResult`, and the constructor takes `Sink&`. Tests and the benchmark can therefore switch between the two books by type alone.
- `PriceLevel`, `Price`, the sinks and the parser are shared, not duplicated.
- A build option, or a second app target `matching_engine_templated`, selects this book in `main`.

## How to compare
1. **Correctness**
   - Turn the OrderBook unit tests into templates over the book type, and run them against both books.
   - Add both books to the randomized differential test: main vs. reference, templated vs. reference, and main vs. templated. All event streams must be identical.
   - Run the e2e datasets through both app targets, which must produce byte-identical stdout.
2. **Performance**: run `bench` for both books on the same `gen_orders` datasets, and report:
   - add with no match, matching add (sweeping several levels), and cancel
   - **cancel-heavy workloads that empty many levels**, which is the case the variant should lose on
   - p50, p99 and throughput for each
3. **Readability**: compare lines of code in each `OrderBook` and the number of side-specific branches, and give a short qualitative note.
4. Write the results in a README section titled "Design comparison: same-type maps vs. templated BookSide" and say which design is kept as the default.
