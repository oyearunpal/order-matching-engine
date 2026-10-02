// Micro-benchmark for the order book, per request type.
//
//   bench [--orders N] [--seed S]
//
// Workloads (NullSink, so no output cost unless stated):
//   1. passive add     adds that never cross, spread over many price levels
//   2. cancel          cancel every resting order, in random order
//   3. aggressive add  adds that cross and fill several resting orders each
//   4. mixed flow      random adds/cancels from order_flow::Generator
//   5. end-to-end      parse + match + CSV output through runSession (in memory)
//
// Per-operation latency includes ~20 ns of steady_clock overhead per sample.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "matching/event_sink.hpp"
#include "matching/order_book.hpp"
#include "matching/session.hpp"
#include "order_flow.hpp"

using namespace matching;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::int64_t kMidCents = 100'00;
constexpr std::int64_t kLevelsPerSide = 1000;

struct Options {
    std::size_t orders = 1'000'000;
    std::uint64_t seed = 42;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        const std::string_view value = argv[i + 1];
        std::uint64_t number = 0;
        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (ec != std::errc{} || ptr != value.data() + value.size() || number == 0) {
            std::cerr << "bench: invalid value for " << flag << '\n';
            std::exit(2);
        }
        if (flag == "--orders") {
            options.orders = number;
        } else if (flag == "--seed") {
            options.seed = number;
        } else {
            std::cerr << "usage: bench [--orders N] [--seed S]\n";
            std::exit(2);
        }
    }
    return options;
}

void printHeader() {
    std::cout << std::left << std::setw(16) << "workload" << std::right << std::setw(10) << "ops"
              << std::setw(10) << "mean ns" << std::setw(9) << "p50" << std::setw(9) << "p90" << std::setw(9)
              << "p99" << std::setw(10) << "p99.9" << std::setw(10) << "max" << std::setw(14) << "Mops/s"
              << '\n';
}

// Prints latency percentiles for per-operation samples (nanoseconds).
void report(std::string_view name, std::vector<std::int64_t> samples) {
    if (samples.empty()) {
        return;
    }
    const auto total = std::accumulate(samples.begin(), samples.end(), std::int64_t{0});
    std::sort(samples.begin(), samples.end());
    const auto at = [&samples](double q) {
        return samples[std::min(samples.size() - 1, static_cast<std::size_t>(q * static_cast<double>(samples.size())))];
    };
    const double mean = static_cast<double>(total) / static_cast<double>(samples.size());
    std::cout << std::left << std::setw(16) << name << std::right << std::setw(10) << samples.size()
              << std::setw(10) << std::fixed << std::setprecision(1) << mean << std::setw(9) << at(0.50)
              << std::setw(9) << at(0.90) << std::setw(9) << at(0.99) << std::setw(10) << at(0.999)
              << std::setw(10) << samples.back() << std::setw(14) << std::setprecision(2) << 1000.0 / mean
              << '\n';
}

void reportThroughput(std::string_view name, std::size_t ops, Clock::duration elapsed) {
    const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    std::cout << std::left << std::setw(16) << name << std::right << std::setw(10) << ops << std::setw(10)
              << std::fixed << std::setprecision(1) << ns / static_cast<double>(ops) << std::setw(9) << "-"
              << std::setw(9) << "-" << std::setw(9) << "-" << std::setw(10) << "-" << std::setw(10) << "-"
              << std::setw(14) << std::setprecision(2) << static_cast<double>(ops) * 1000.0 / ns << '\n';
}

template <typename F>
std::int64_t timeOne(F&& operation) {
    const auto start = Clock::now();
    operation();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}

// Non-crossing orders: buys below the mid, sells above, over kLevelsPerSide levels each.
std::vector<AddOrderRequest> passiveOrders(std::size_t count, std::mt19937_64& rng) {
    std::uniform_int_distribution<std::int64_t> level(1, kLevelsPerSide);
    std::uniform_int_distribution<Quantity> quantity(1, 100);
    std::vector<AddOrderRequest> orders;
    orders.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Side side = i % 2 == 0 ? Side::Buy : Side::Sell;
        const std::int64_t offset = level(rng);
        const std::int64_t cents = side == Side::Buy ? kMidCents - offset : kMidCents + offset;
        orders.push_back({i + 1, side, quantity(rng), Price::fromCents(cents)});
    }
    return orders;
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    std::mt19937_64 rng(options.seed);
    NullSink sink;

    std::cout << "orders: " << options.orders << ", price levels per side: " << kLevelsPerSide << "\n\n";
    printHeader();

    const std::vector<AddOrderRequest> passive = passiveOrders(options.orders, rng);

    // 1 + 2: passive adds, then cancel everything in random order.
    {
        OrderBook<NullSink> book(sink, options.orders);
        std::vector<std::int64_t> samples;
        samples.reserve(options.orders);
        for (const auto& order : passive) {
            samples.push_back(timeOne([&] { book.add(order); }));
        }
        report("passive add", std::move(samples));

        std::vector<OrderId> ids(options.orders);
        std::iota(ids.begin(), ids.end(), OrderId{1});
        std::shuffle(ids.begin(), ids.end(), rng);
        samples.clear();
        for (const OrderId id : ids) {
            samples.push_back(timeOne([&] { book.cancel(id); }));
        }
        report("cancel", std::move(samples));
    }

    // 3: aggressive adds against a full book; each fills roughly 1-6 resting orders.
    {
        OrderBook<NullSink> book(sink, options.orders);
        for (const auto& order : passive) {
            book.add(order);
        }
        const std::size_t aggressors = std::max<std::size_t>(1, options.orders / 10);
        std::uniform_int_distribution<Quantity> quantity(50, 300);
        std::vector<std::int64_t> samples;
        samples.reserve(aggressors);
        for (std::size_t i = 0; i < aggressors; ++i) {
            const Side side = i % 2 == 0 ? Side::Buy : Side::Sell;
            const std::int64_t limit = side == Side::Buy ? kMidCents + kLevelsPerSide : kMidCents - kLevelsPerSide;
            const AddOrderRequest order{options.orders + i + 1, side, quantity(rng), Price::fromCents(limit)};
            samples.push_back(timeOne([&] { book.add(order); }));
        }
        report("aggressive add", std::move(samples));
    }

    // 4: mixed random flow, measured as overall throughput.
    std::vector<order_flow::Request> flow;
    {
        order_flow::Config config;
        config.seed = options.seed;
        config.halfRangeTicks = 50;
        order_flow::Generator generator(config);
        flow.reserve(options.orders);
        for (std::size_t i = 0; i < options.orders; ++i) {
            flow.push_back(generator.next());
        }

        OrderBook<NullSink> book(sink, options.orders);
        const auto start = Clock::now();
        for (const auto& request : flow) {
            if (const auto* add = std::get_if<AddOrderRequest>(&request)) {
                book.add(*add);
            } else {
                book.cancel(std::get<CancelOrderRequest>(request).id);
            }
        }
        reportThroughput("mixed flow", flow.size(), Clock::now() - start);
    }

    // 5: the same flow as CSV text through the full session (parse, match, format).
    {
        std::string text;
        for (const auto& request : flow) {
            text += order_flow::toCsv(request);
            text += '\n';
        }
        std::istringstream in(text);
        std::ostringstream out;
        std::ostringstream err;
        const auto start = Clock::now();
        const SessionStats stats = runSession(in, out, err);
        reportThroughput("end-to-end", stats.lines, Clock::now() - start);
    }
    return 0;
}
