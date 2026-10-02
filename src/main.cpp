// Order matching engine: reads CSV requests from stdin, writes trade and fill
// events to stdout, and reports rejected input on stderr.

#include <exception>
#include <iostream>

#include "matching/session.hpp"

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    try {
        matching::runSession(std::cin, std::cout, std::cerr);
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
