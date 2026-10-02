# Order Matching Engine

A single-instrument limit order book with price-time priority, written in C++20 with no third-party dependencies.
It reads `AddOrderRequest` / `CancelOrderRequest` messages from stdin, matches them, and writes `TradeEvent`, `OrderFullyFilled` and `OrderPartiallyFilled` messages to stdout. Rejected input is reported on stderr and never stops processing.

---

## Build and run

**Requirements**

| Tool | Minimum version | Tested with |
|---|---|---|
| Linux | any recent distribution | Ubuntu 22.04 |
| C++20 compiler | GCC 10 or Clang 12 | GCC 11, GCC 16, Clang 14 (with the GCC 11 standard library) |
| CMake | 3.20 | 3.22 |

No third-party libraries are needed. To install the tools:

```bash
sudo apt install g++ cmake        # Ubuntu 22.04+ / Debian 12+
sudo dnf install gcc-c++ cmake    # Fedora / RHEL 9+
```

**Build**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run on the example from the assignment
./build/matching_engine < tests/data/spec_example.in

# Or interactively: output appears as soon as each line is processed
./build/matching_engine
```

**Run every test** (unit, randomized differential, and end-to-end):

```bash
ctest --test-dir build --output-on-failure
./build/unit_tests Cancel          # run only tests whose name contains "Cancel"
```

**Sanitizers** (AddressSanitizer + UndefinedBehaviorSanitizer):

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_SANITIZERS=ON
cmake --build build-asan -j && ctest --test-dir build-asan
```

**Tools:**

```bash
# Reproducible random input, optionally with malformed lines mixed in
./build/gen_orders --count 1000000 --seed 7 --garbage-ratio 0.05 > orders.csv
./build/matching_engine < orders.csv > events.csv 2> errors.log

# Latency and throughput per request type
./build/bench --orders 1000000
```

Warnings are strict (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`) and are errors by default. To relax this, pass `-DWARNINGS_AS_ERRORS=OFF`.

---

## Input, output and assumptions

| Message | Format | Direction |
|---|---|---|
| AddOrderRequest | `0,orderid,side,quantity,price` (side: 0 buy, 1 sell) | stdin |
| CancelOrderRequest | `1,orderid` | stdin |
| TradeEvent | `2,quantity,price` | stdout |
| OrderFullyFilled | `3,orderid` | stdout |
| OrderPartiallyFilled | `4,orderid,remaining_quantity` | stdout |

For every match the engine writes the `TradeEvent`, then the aggressive order's fill message, then the resting order's fill message. Trades execute at the resting order's price.

**Assumptions and policies**

- **Prices follow Nasdaq TotalView-ITCH 5.0's `Price(4)` format:** an unsigned 32-bit integer with 4 implied decimal places. The raw value `10250000` means `1025.0000`. There is no floating point anywhere.
  - **Range:** `0.0001` to `429496.7295` (`UINT32_MAX / 10^4`). Higher prices are rejected.
  - `1025`, `1025.5`, `1025.0001` are accepted. Trailing zeros past 4 decimals (`1025.50000`) are accepted because no precision is lost.
  - Prices finer than 4 decimals, such as `1025.00001`, are rejected.
  - Output uses the shortest exact form: `1025`, `1025.5`, `1025.005`.
- Order ids and quantities are unsigned 64-bit integers and must be positive. Values that overflow are rejected.
- **Duplicate ids are checked against live orders only.** An id can be reused once its order has filled or been cancelled. Remembering every id ever seen would cost memory without bound.
- A cancel prints nothing on success, because the protocol has no cancel acknowledgement. A cancel for an unknown, filled or already-cancelled id is reported on stderr.
- There is no self-trade prevention, because messages carry no owner or account.
- Blank lines are ignored. Whitespace around fields and a trailing `\r` (Windows line endings) are tolerated.

**Error reporting.** Each rejected line produces exactly one message on stderr with its line number, and processing continues:

```
error: line 6: Unknown message type: BADMESSAGE
error: line 9: invalid price '1.00005': expected a positive number up to 429496.7295 with at most 4 decimal places
error: line 12: cancel for unknown order id 999
error: line 17: line exceeds 1024 characters
```

Non-printable bytes in messages are escaped as `\xNN`. Lines longer than 1024 characters are skipped without being buffered, so a huge line or a binary file cannot exhaust memory. A valid request is under 60 characters.

---

## Design

```
                         ┌──────────────────────────────────────────────┐
 stdin ─► runSession ─►  │ parseLine ─► OrderBook<CsvEventWriter>       │ ─► stdout
          (session.cpp)  │              add() / cancel()                │
                         │                   │ events                   │
                         │                   ▼                          │
                         │          CsvEventWriter (EventSink)          │
                         └──────────────────────────────────────────────┘
                           errors (parse, duplicate id, unknown cancel) ─► stderr
```

### Order book data structures (`include/order_book.hpp`, `order.hpp`)

```
orders_ : unordered_map<OrderId, Order>    ← owns every resting order (one allocation each)

asks_   : map<Price, PriceLevel>  ascending, best ask = begin()
bids_   : map<Price, PriceLevel>  ascending, best bid = prev(end())

PriceLevel 1025:  head ─► [id 5, qty 2] ⇄ [id 7, qty 5] ◄─ tail     (FIFO, oldest first)
                           Order.level ──► iterator to its map entry
```

- **The hash map owns each `Order`, and the price level only points at it.**
  - Entries in `std::unordered_map` never move in memory, even when it rehashes. So an `Order` can be linked straight into a price level.
  - Each order costs exactly one allocation, and the same map gives the O(1) lookup for cancels and duplicate-id checks.
- **`PriceLevel` is an intrusive doubly-linked FIFO.** The `prev`/`next` links live inside `Order`.
  - Appending at the tail, matching from the head and unlinking from the middle are all O(1).
  - The level allocates nothing per order.
- **Both sides use the same map type**, sorted ascending.
  - Because the type is shared, every `Order` can store an iterator to its level whichever side it is on. An emptied level is then erased through that iterator, with no search by price.
  - The two differences between the sides (which end holds the best price, and the `buy ≥ ask` / `sell ≤ bid` crossing test) are kept in two small helpers. The matching loop is the same for both sides.
- **There is one removal path.** Fills and cancels both go through `removeOrder()`. It unlinks the order from its level, erases the level if it is now empty, and only then erases the map entry. This order means a level never points at a deleted order.
- **The output sink is chosen at compile time:** `template <EventSink Sink> class OrderBook`.
  - `EventSink` is a C++20 concept. The CSV writer, the test recording sink and the benchmark's `NullSink` are plain classes with no base class.
  - Sink calls in the matching loop are inlined, with no virtual dispatch.
  - The book does no I/O at all.

### Matching

```
add(order):
  reject if quantity == 0, price == 0, or the id is live
  while order has quantity and the best opposite level crosses its limit:
      resting = oldest order at that level
      fill    = min(order.qty, resting.qty)          # trade at resting.price
      emit Trade, then aggressor fill/partial, then resting fill/partial
      if resting is empty: removeOrder(resting)
  if quantity remains: rest it at the tail of its price level
```

---

## Performance

### Cost of the three operations the assignment asks about

L = number of price levels on one side.

| Operation | Cost | How |
|---|---|---|
| **Does an `AddOrderRequest` match?** | **O(1)** | Compare the limit with the best opposite level: `asks_.begin()` or `prev(bids_.end())`. Both are constant time on `std::map`. A non-matching add pays nothing else for matching. |
| **Removing a filled order** | **O(1)**, plus amortized O(1) for its level | Pop the head of the FIFO, then one hash erase. If that empties the level, it is erased through the stored iterator. That is amortized constant per the standard, though a single red-black-tree delete can touch O(log L) nodes. |
| **Removing a cancelled order** | **O(1)** on average, plus amortized O(1) for its level | One hash lookup, one O(1) unlink from anywhere in the FIFO, one hash erase. An emptied level is erased through the stored iterator, as above. |
| Resting a new order | O(log L) to find or create its price level, then O(1) to append | `try_emplace` on the level map always searches the tree. A new price also allocates and inserts a tree node. The tail append is O(1). |

An aggressive order that generates k fills costs O(k): each fill is the O(1) removal described above. No operation ever scans the book.

### Measured

`./build/bench --orders 1000000` in a Release build with GCC 16 on an AMD Ryzen 5 3600, with 1000 price levels per side and `NullSink` (no output cost). These figures are typical of three runs. Latency samples include about 20 ns of clock overhead.

| Workload | Mean | p50 | p90 | p99 | p99.9 | Throughput |
|---|---|---|---|---|---|---|
| Passive add (no match, 1M orders) | 140 ns | 110 ns | 130 ns | 1.6 µs | 3.2 µs | 7.1 M/s |
| Cancel (1M orders, random order) | 240 ns | 220 ns | 330 ns | 600 ns | 720 ns | 4.1 M/s |
| Aggressive add (about 3.5 fills each) | 435 ns | 400 ns | 760 ns | 1.2 µs | 1.5 µs | 2.3 M/s |
| Mixed random flow (book stays small) | 53 ns | | | | | 19 M/s |
| End-to-end: parse, match and CSV output | 280 ns | | | | | 3.6 M lines/s |

**What the numbers show:**
- **Cache misses dominate, not instruction count.**
  - The mixed flow keeps the book small enough to stay in cache and runs at about 50 ns per request.
  - With 1M resting orders, each cancel touches about four unrelated cache lines: the hash bucket, the order node, and the neighbouring orders it relinks. That costs about 240 ns.
- **The passive-add tail (p99 at 1.6 µs) comes from allocation**, not matching: the map node and new memory pages being touched for the first time.

The production enhancements below target both of these.

### Trade-offs

- **32-bit prices (Nasdaq ITCH style) vs. 64-bit.**
  - With 32-bit prices `Order` is 48 bytes and its hash-map entry 64 bytes. With 64-bit prices they would be 56 and 72 bytes, about 8 MB more per million resting orders.
  - The cost is the 429,496.7295 ceiling.
  - The engine only compares prices and never does arithmetic on them, so the narrower type changes no latency measurably.
  - Widening later is contained: only `Price`, the parser's range check, and the price tests depend on the width.
  - CME takes the other route: `int64` prices with 9 implied decimals.
- **`std::map` for price levels vs. a flat array indexed by price.** The map handles any price with no configuration and keeps the code small. The cost is a node allocation per new level and pointer-chasing.
- **Same map type for both sides.** It lets `Order` store a level iterator, so an emptied level is never searched for by price. The cost is the `prev(end())` asymmetry on the bid side.
- **Template sink vs. a virtual interface.** Event calls are inlined, at the price of `OrderBook` being a header-only template. Swapping the sink at runtime is never needed.
- **Node-based `std::unordered_map`.** Its stable node addresses are what let it own the orders. The cost is one heap allocation per order and a pointer hop per lookup.
- **The hash map owns orders vs. the level owning them.** One allocation per order instead of two, for example with a `std::list` per level.
- **Which paths are favoured:** none at another's expense. All three operations are O(1) in the common case. Matching benefits most from best-price-first ordering, while cancel pays for one hash lookup.
- **Output buffering.** Output goes through a buffered `std::cout` and is flushed only when stdin has no more buffered input. Piped batches stay fast, and interactive use still sees results immediately.

---

## Production enhancements

What I would change for a production engine, roughly in order of impact:

1. **Redesign the price level for cache locality.** The intrusive linked FIFO is O(1) everywhere, but each matching step follows a pointer to an order that can be anywhere in the heap.
   - **A contiguous per-level queue:** a ring buffer of compact `{OrderId, qty}` slots, so matching walks memory in order.
   - **Lazy cancellation:** a cancel looks up the order's `{level, slot}` handle in O(1) and marks the slot dead. Matching skips dead slots, and a level compacts itself once dead slots pass a threshold (about 50%). The catch is that stale slots linger until the next compaction.
   - **Hot/cold split:** the matching loop only touches id and quantity, so only those go in the slot. Side, price and any metadata live elsewhere.
   - **Ownership moves into the level**, the reverse of this version: the index maps an id to a slot handle. If linked structures remain anywhere, they use 32-bit indices instead of pointers.
2. **A price-indexed array of levels instead of `std::map`.** Prices are integers on a fixed 0.0001 grid, so a level is `levels[(price - base) / tick]` within a band around the market. A bitmap or summary words find the next non-empty level. Insert, best-price lookup and level removal all become O(1) with no allocation.
3. **An open-addressing hash map plus an order pool.** A flat hash map such as Robin Hood or Swiss-table is much faster than node-based buckets, but it moves entries when it grows. So orders move to a preallocated pool addressed by 32-bit indices, sized from configuration and possibly backed by huge pages. That removes the per-order heap allocation and the allocation tail visible in the passive-add p99.
4. **Preallocate everything at startup**, sized from configuration, so the steady state never calls `malloc`.
5. **Binary protocol and zero-copy parsing.** Replace CSV with fixed-layout binary messages read straight from the network buffer. Batch output writes instead of going through `iostream`.
6. **Threading:** network I/O → SPSC lock-free queue → single-threaded matcher pinned to a core → SPSC queue → publisher. Matching stays single-threaded per book, which keeps it deterministic and lock-free.
7. **Scale out by symbol.** Run one book per instrument and shard instruments across matcher cores.
8. **Reliability:** journal inputs for deterministic replay and recovery, add sequence numbers on output, and snapshot books.
9. **Observability:** latency histograms (HDR) per request type, queue-depth metrics, and rate-limited error logging instead of one stderr line per bad message.

---

## Testing

| Suite | What it covers |
|---|---|
| `tests/test_price.cpp` | Parsing and formatting, the 4-decimal limit, the `429496.7295` uint32 ceiling, round-trips |
| `tests/test_parser.cpp` | Every accept/reject path: field counts, signs, overflow, bad side, zero quantity, whitespace, `\r`, binary bytes |
| `tests/test_order_book.cpp` | The assignment example; FIFO and price priority; multi-level sweeps; resting remainders; exact fills; sell-side aggression; cancel at the head, middle and tail; emptied levels; duplicate and unknown ids; uint64 quantities; invariant checks |
| `tests/test_session.cpp` | Full pipeline on string streams: line numbers in errors, blank lines, CRLF, a final line without a newline, over-long and exactly-max-length lines, binary garbage |
| `tests/test_randomized.cpp` | **Differential test:** 800 seeded scenarios of 400 operations each, run through both `OrderBook` and a deliberately naive O(n) `ReferenceBook`. Result codes, event streams and book size must match after every step, and `checkInvariants()` must pass. |
| `tests/data/*.in` | End-to-end cases. Each `.in` is piped through the binary and compared byte for byte with `.out` and `.err`. The expected files were worked out by hand from the matching rules, not captured from the program. |

The randomized flow comes from `tools/order_flow.hpp`, which `gen_orders` and `bench` also use. A 1M-line `gen_orders --garbage-ratio 0.05` run through the sanitizer build finishes with no sanitizer reports.

---

## Project layout

```
include/
  types.hpp              OrderId, Quantity, Side, MessageType
  price.hpp              Price (uint32, 4 implied decimals), parsePrice, writePrice
  messages.hpp           AddOrderRequest, CancelOrderRequest, ParseError
  parser.hpp             parseLine, trim, escapeForLog
  event_sink.hpp         EventSink concept, NullSink
  csv_event_writer.hpp   CSV output sink
  order.hpp              Order, PriceLevel (intrusive FIFO), LevelMap
  order_book.hpp         OrderBook<Sink> (header-only)
  session.hpp            runSession: stream → book → stream
src/                     price.cpp, parser.cpp, session.cpp, main.cpp
tests/                   test harness, unit/session/randomized tests, reference book, e2e data
tools/                   order_flow.hpp (generator), gen_orders.cpp, bench.cpp
```
