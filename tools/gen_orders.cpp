// Generates a random but reproducible stream of input requests on stdout.
//
//   gen_orders [--count N] [--seed S] [--cancel-ratio R] [--garbage-ratio R]
//
// --garbage-ratio mixes in malformed lines (junk text, binary bytes, bad
// fields, huge numbers, over-long lines) to exercise error handling.

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <string_view>

#include "order_flow.hpp"

namespace {

struct Options {
    std::uint64_t count = 100'000;
    std::uint64_t seed = 1;
    double cancelRatio = 0.3;
    double garbageRatio = 0.0;
};

[[noreturn]] void usage(std::string_view problem) {
    std::cerr << "gen_orders: " << problem << "\n"
              << "usage: gen_orders [--count N] [--seed S] [--cancel-ratio R] [--garbage-ratio R]\n";
    std::exit(2);
}

template <typename T>
T parseNumber(std::string_view flag, std::string_view text) {
    T value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        usage("invalid value for " + std::string(flag) + ": " + std::string(text));
    }
    return value;
}

double parseRatio(std::string_view flag, std::string_view text) {
    char* end = nullptr;
    const std::string copy(text);
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !(value >= 0.0 && value <= 1.0)) {
        usage("value for " + std::string(flag) + " must be between 0 and 1");
    }
    return value;
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (i + 1 >= argc) {
            usage("missing value for " + std::string(flag));
        }
        const std::string_view value = argv[++i];
        if (flag == "--count") {
            options.count = parseNumber<std::uint64_t>(flag, value);
        } else if (flag == "--seed") {
            options.seed = parseNumber<std::uint64_t>(flag, value);
        } else if (flag == "--cancel-ratio") {
            options.cancelRatio = parseRatio(flag, value);
        } else if (flag == "--garbage-ratio") {
            options.garbageRatio = parseRatio(flag, value);
        } else {
            usage("unknown option " + std::string(flag));
        }
    }
    return options;
}

// Produces one malformed line, covering the error paths of the parser and session.
std::string garbageLine(std::mt19937_64& rng) {
    auto pick = [&rng](std::uint64_t n) { return std::uniform_int_distribution<std::uint64_t>(0, n - 1)(rng); };
    switch (pick(12)) {
        case 0: return "BADMESSAGE";
        case 1: return "0,1,0,5";                                  // too few fields
        case 2: return "0,1,0,5,100,9";                            // too many fields
        case 3: return "0,abc,0,5,100";                            // non-numeric id
        case 4: return "0,7,3,5,100";                              // bad side
        case 5: return "0,7,0,-5,100";                             // negative quantity
        case 6: return "0,7,0,5,1.005";                            // sub-cent price
        case 7: return "0,99999999999999999999999,0,5,100";        // id overflow
        case 8: return "9," + std::to_string(pick(1000));          // unknown type
        case 9: return std::string(2000 + pick(3000), '7');        // over-long line
        case 10: {                                                 // random bytes (no newline)
            std::string bytes(1 + pick(40), '\0');
            for (char& c : bytes) {
                c = static_cast<char>(pick(256));
                if (c == '\n') c = '?';
            }
            return bytes;
        }
        default: return ",,,,";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    const Options options = parseOptions(argc, argv);

    order_flow::Config config;
    config.seed = options.seed;
    config.cancelRatio = options.cancelRatio;
    order_flow::Generator generator(config);
    std::mt19937_64 garbageRng(options.seed ^ 0x9e3779b97f4a7c15ULL);
    std::bernoulli_distribution isGarbage(options.garbageRatio);

    for (std::uint64_t i = 0; i < options.count; ++i) {
        if (isGarbage(garbageRng)) {
            std::cout << garbageLine(garbageRng) << '\n';
        } else {
            std::cout << order_flow::toCsv(generator.next()) << '\n';
        }
    }
    return 0;
}
