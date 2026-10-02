# Plan: Order Matching Engine (Vatic take-home)

## Context
`/home/arun/development/vatic` is empty apart from the assignment PDF. The task is a C++ program that:
- reads Add and Cancel order messages (CSV) from stdin,
- keeps an in-memory limit order book with price-time priority,
- writes TradeEvent, OrderFullyFilled and OrderPartiallyFilled messages to stdout,
- never crashes on bad input and logs errors to stderr.

A developer will review it, not an automated grader. So the clean design, the tests, the datasets, and a README that explains performance, trade-offs and production improvements all matter as much as correct output.

Decisions made:
- **C++20**
- **Tiny built-in test harness**, so nothing third-party is needed
- **CMake**: 3.22 and g++ 16 / clang 14 are installed

## Layout
```
CMakeLists.txt            # targets: matching_lib (static), matching_engine (app), unit_tests, gen_orders, bench
README.md                 # build/run, design, performance analysis, trade-offs, production enhancements
include/
  types.hpp               # OrderId (uint64), Quantity (uint64), Side enum
  price.hpp               # fixed-point Price: parse/format
  messages.hpp            # AddOrderRequest, CancelOrderRequest, ParseError (std::variant)
  parser.hpp              # parseLine(std::string_view) -> std::variant<Add, Cancel, ParseError>
  event_sink.hpp          # EventSink C++20 concept (onTrade / onFullyFilled / onPartiallyFilled) + NullSink
  csv_event_writer.hpp    # concrete sink that writes the CSV output format to an ostream (no base class)
  order.hpp               # Order (id, side, price, qty, intrusive prev/next links, LevelMap::iterator)
  price_level.hpp         # FIFO intrusive doubly-linked list of orders + aggregate qty
  order_book.hpp          # template <EventSink Sink> class OrderBook: add(), cancel(), result enums (header-only)
src/  price.cpp parser.cpp csv_event_writer.cpp main.cpp
tests/
  test_harness.hpp        # TEST() auto-registration, EXPECT_EQ/EXPECT_TRUE/ASSERT_*, summary + exit code
  test_main.cpp test_price.cpp test_parser.cpp test_order_book.cpp test_randomized.cpp
  recording_sink.hpp      # sink that records events into a std::vector<Event> for assertions
  reference_book.hpp      # naive O(n) vector-based matcher used as an oracle for differential tests
  data/*.in, data/*.out   # end-to-end datasets with expected stdout
  run_e2e.cmake           # runs the app on each .in and compares to .out (wired into ctest)
tools/
  gen_orders.cpp          # seeded random message generator (adds/cancels around a mid; optional garbage lines)
  bench.cpp               # throughput and latency per request type (add without match / matching add / cancel)
```

## Core design
**Price**: fixed-point, following Nasdaq ITCH 5.0's `Price(4)`: a `uint32_t` with **4 implied decimal places** (scale 10,000).
- **Stated assumption (README):** prices are between 0.0001 and 429,496.7295 with at most 4 decimal places.
- Parsing is exact (no floating point). It accepts `1025`, `1025.5` and `1025.0001`, and rejects values ≤ 0, more than 4 decimal places (`1025.00001`), values above the uint32 ceiling, and junk.
- Formatting prints the shortest form: `1025` stays `1025`, `1025.50` becomes `1025.5`, `1025.0050` becomes `1025.005`. This keeps output byte-identical to the spec example.
- Integer prices on a fixed grid also make the tick-indexed price array in the production section realistic, because a price maps directly to an array slot.

**Parser**: works on `std::string_view` and parses integers with `std::from_chars`.
- It tolerates trailing `\r` and whitespace around fields.
- It checks field count per message type, msgtype ∈ {0,1}, side ∈ {0,1}, orderid > 0, and quantity > 0 without overflow.
- It returns a `ParseError` that carries a reason. `main` logs `line N: <reason>: <raw line>` to stderr, and an unknown type reads e.g. `Unknown message type: BADMESSAGE`.
- Blank lines are skipped.

**OrderBook data structures**
- **Two maps, one per side, of the same type**: `using LevelMap = std::map<Price, PriceLevel>;` (ascending), with `LevelMap bids_, asks_;`.
  - Best ask is `asks_.begin()`; best bid is `std::prev(bids_.end())`.
  - Using one type for both sides means every `Order` can store a `LevelMap::iterator` to its level, valid on either side. An emptied level is then erased with `erase(iterator)`, with no lookup by price.
  - Side-specific logic (where the best price is, and the crossing test buy ≥ ask / sell ≤ bid) lives in small private helpers in `OrderBook`, so the matching loop itself stays the same for both sides.
- `PriceLevel` is an intrusive doubly-linked FIFO with head, tail and total quantity.
  - Append at the tail, match from the head, and unlink anywhere in O(1).
- **Ownership: the index owns each `Order`, and the price level only points at it.**
  - Orders live in `std::unordered_map<OrderId, Order>` (with `reserve`). The same map gives O(1) cancel lookup and duplicate-id checks.
  - Map entries never move in memory, even when the map grows, so the level's `prev`/`next` links can point straight at orders stored in the map.
  - One allocation per order (the map entry). The level allocates nothing per order. There is no separate object pool (see production enhancements).
  - One lifetime rule: an order exists exactly as long as its map entry. All removal goes through one private `removeOrder()`, which unlinks the order from its level and erases an emptied level (through the stored iterator) **before** erasing the map entry. No dangling pointers are left behind.
  - Rejected alternative: the level owns orders in a `std::list<Order>` and the map stores iterators. It needs no custom list code, but costs two allocations per order (list node plus map entry), and every fill touches two separate nodes. A `vector` or `deque` level can't work, because erasing from the middle invalidates the map's references.
- **Events go out through a sink chosen at compile time: `template <EventSink Sink> class OrderBook`.**
  - `EventSink` is a C++20 concept that requires `onTrade(qty, price)`, `onFullyFilled(id)` and `onPartiallyFilled(id, remaining)`. Sinks are plain classes with no base class.
  - The sink is passed once, by reference, to the constructor (`explicit OrderBook(Sink& sink)`), and must outlive the book.
  - The book does no formatting or I/O. For each match it calls `onTrade`, then the aggressor's fill event, then the resting order's fill event, in the order the spec requires.
  - Sinks:
    - `OrderBook<CsvEventWriter>` in `main`
    - `OrderBook<RecordingSink>` in unit and randomized tests
    - `OrderBook<NullSink>` in `bench`
  - Why a template rather than a virtual interface: the code is about the same size, the sink calls on the matching hot path are inlined, the concept documents the contract and gives clear compile errors, and swapping the sink at runtime is never needed.
  - The cost is that `OrderBook`'s implementation lives in `order_book.hpp`. It isn't precompiled per sink type in a `.cpp`, so the library never references test-only sinks.
- `add()` returns `AddResult {Accepted, DuplicateId}` and `cancel()` returns `CancelResult {Cancelled, UnknownId}`. `main` turns these into stderr messages, so the book never does I/O itself.

**Matching (`add`)**
1. Reject the order if its id is already live.
2. Loop while the order has remaining quantity and the opposite side's best price crosses (buy ≥ ask, or sell ≤ bid):
   1. Take the head order of the best level and fill `min(remaining, resting.qty)` at the **resting** price.
   2. Emit, in order: `Trade`, then the aggressive order's Full/Partial message, then the resting order's Full/Partial message.
   3. A fully filled resting order is removed via `removeOrder()`: it is unlinked, an emptied level is erased through its iterator, then its map entry is erased.
3. If quantity remains, place the order in the map with `emplace` and append it to the tail of its price level, creating the level if needed.

This reproduces the spec example exactly. The aggressive partial message (`4,1000008,1`) is emitted after each trade where the aggressive order still has quantity left.

**Cancel**: look up the id in the index, then call `removeOrder()`. That unlinks the order (O(1)) and erases its map entry (O(1) average). If the order was the last at its price, the level is erased through the stored iterator, with no lookup by price. On success nothing is printed to stdout, because the spec defines no cancel acknowledgement. An unknown id is logged to stderr.

**I/O in `main`**
- Uses `std::getline` with `sync_with_stdio(false)`.
- Output goes to a buffered `std::cout`, written with `'\n'` rather than `endl`.
- It flushes when stdin has no more buffered input (`in_avail()==0`), so piped batches stay fast and interactive use still responds.
- Every line is processed inside a guard, so no input can terminate the process.

**Documented edge-case policy (README)**
- Duplicate ids are rejected only against **live** orders. Tracking every historical id would cost unbounded memory.
- There is no self-trade prevention, because the input has no owner field.
- Quantities and ids are uint64. Values too large for that are rejected.

## Tests
- **Unit tests** (built-in harness):
  - price parse/format round-trips and rejections, including the 4-decimal limit (`1.00005` is rejected) and the uint32 ceiling
  - parser accept/reject cases: wrong field count, non-numeric fields, negative numbers, bad side, zero quantity, overflow, empty fields, `\r`
  - OrderBook cases:
    - the spec example
    - FIFO within a price level
    - price priority
    - sweeping multiple levels
    - an aggressive remainder resting on the book
    - an exact fill where both orders are fully filled
    - sell-side aggression against bids
    - trades executing at the resting price
    - cancelling the head, middle or tail of a level
    - a cancel that empties a level
    - best-price selection: the highest bid and the lowest ask are matched first
    - unknown cancel, duplicate id, decimal prices
    - book state stays consistent after many fills and cancels (the order count matches the index, and no empty levels are left behind)
- **Randomized differential test**:
  - Generate thousands of seeded random add/cancel sequences.
  - Run each through both `OrderBook` and `reference_book.hpp`, a naive vector-based matcher.
  - Assert the two event streams are identical.
- **End-to-end tests** (`tests/data`, run by ctest via `run_e2e.cmake`):
  - `spec_example` (including BADMESSAGE)
  - `multi_level_sweep`
  - `cancels`
  - `malformed_input` (garbage, partial lines, huge numbers, binary-ish text)
  - `decimal_prices`
  - `both_sides_aggressive`
- **Sanitizers**: the CMake option `-DENABLE_SANITIZERS=ON` builds with ASan and UBSan. The whole suite runs under it, and `gen_orders` output with garbage lines is fed through as a fuzz-style smoke test.

## README content
- Build and run instructions.
- Design overview.
- **Performance analysis**, covering:
  - Whether an add matches: O(1). It compares against the cached best of the opposite side.
  - Each fill: O(1). Removing a filled order is a head unlink plus one map erase. Erasing an emptied level is a red-black tree delete through the stored iterator: amortized constant per the standard, O(log L) worst case for a single call (successor search and rebalancing).
  - Inserting a new resting order: O(log L) at a new price level, or O(1) at an existing one.
  - Cancel: O(1) average to find and unlink the order. If it empties a level, removing that level is the same tree delete as above (amortized constant, O(log L) worst case), with no extra lookup by price.
- Measured numbers from `bench`.
- Trade-offs:
  - `std::map` vs. a flat array indexed by price ticks
  - one map type for both sides: an iterator can be stored in `Order`, so no lookup is needed when a level is removed
  - the template sink: inlined event calls in exchange for a header-only `OrderBook`
  - the node-based hash map
  - map-owns/level-points ownership vs. level-owns (above). The take-home still pays one allocation per order; production removes it.
- Production enhancements:
  - **PriceLevel redesign.** The intrusive doubly-linked FIFO would not stay as it is. It is O(1) for every operation and easy to review, which is why the take-home uses it. Its weakness is the matching loop: each step follows a pointer to an order that may sit anywhere in the heap, so it can miss the cache on every order. Production would replace it with:
    - **A contiguous per-level queue** (a ring buffer of compact `{OrderId, qty}` slots). Matching then walks memory sequentially.
    - **Lazy cancellation.** A cancel finds the order's `{level, slot}` handle in O(1) and marks the slot dead (qty = 0). Matching skips dead slots, and a level compacts itself once dead slots pass a threshold such as 50%. Cancel stays O(1), matching stays cache-friendly, and compaction cost is amortized. The catch is that a heavily cancelled level holds stale slots until the next compaction.
    - **Hot/cold split of `Order`.** The matching loop touches only id and qty, so those stay in the queue slot. Side, price and any future metadata live in separate cold storage.
    - **Ownership moves into the level.** Once a level's storage is contiguous, the level should own the hot data, since walking it in order is the whole point. The index maps an id to a `{level, slot}` handle, the reverse of the take-home's choice.
    - **32-bit indices instead of pointers**, if linked structures remain anywhere. Nodes get smaller.
  - a tick-indexed array of levels (prices are integers on a fixed grid), with a bitmap or summary words to find the next non-empty level, replacing `std::map`
  - **An open-addressing hash map plus an order pool.** A flat-array hash map is faster, but it moves entries when it grows, so it can no longer own orders. Cold order data then moves to a preallocated pool (sized from config, possibly in huge pages, addressed by 32-bit indices), which removes the per-order heap allocation the take-home still pays.
  - a binary protocol and zero-copy parsing
  - preallocation sized from config
  - one book per symbol with symbol sharding
  - lock-free SPSC queues between the network, matcher and publisher threads
  - journaling and recovery
  - latency histograms

## Verification
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j` builds cleanly with `-Wall -Wextra -Wpedantic -Werror`.
2. `ctest --test-dir build --output-on-failure` passes the unit, randomized differential and e2e tests.
3. Pipe the spec example into the app: `./build/matching_engine < tests/data/spec_example.in`. Stdout must be byte-identical to the 6 expected lines, and stderr must show the BADMESSAGE error.
4. Rebuild with `-DENABLE_SANITIZERS=ON` and rerun ctest. Then run `gen_orders --garbage` (1M lines) through the app and confirm there are no sanitizer reports and no crash.
5. Run `./build/bench` and put the results in the README.

## Implementation notes (differences from the plan above)
- **`Order` and `PriceLevel` share `order.hpp`.** They depend on each other: `Order` stores a `LevelMap::iterator`, and `PriceLevel`'s methods dereference `Order`. So `price_level.hpp` was folded into `order.hpp`.
- **`PriceLevel` holds only head and tail.** The planned aggregate quantity wasn't needed for matching, and summing uint64 quantities could overflow.
- **`csv_event_writer.hpp` is header-only**, so its calls inline into the matching loop like the other sinks. There is no `csv_event_writer.cpp`.
- **New `session.hpp` / `session.cpp`.** The read-parse-dispatch-log loop moved out of `main.cpp` into `runSession(istream&, ostream&, ostream&)`, so `tests/test_session.cpp` can test the full pipeline on string streams. `main.cpp` is now about 10 lines.
- **Line reading is capped at 1024 characters.** Longer lines are skipped without being buffered, so huge or binary input can't exhaust memory.
- **`AddResult::InvalidOrder` was added** (zero quantity or non-positive price), so the book's API rejects bad input on its own and doesn't rely on the parser.
- **Trailing zeros past 4 decimals are accepted**, e.g. `1.50000`. Only precision finer than 0.0001 is rejected.
- **The e2e tests also compare stderr** when a `<case>.err` file exists.
- **`tools/order_flow.hpp` is one seeded generator shared** by `gen_orders`, `bench` and the randomized test.
