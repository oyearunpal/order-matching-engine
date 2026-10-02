// Runs every registered test. An optional argument filters tests by substring:
//   ./unit_tests            run all
//   ./unit_tests Cancel     run tests whose name contains "Cancel"

#include <cstring>
#include <iostream>

#include "test_harness.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : "";
    int run = 0;
    int failed = 0;

    for (const auto& test : testing::registry()) {
        if (std::strstr(test.name, filter) == nullptr) {
            continue;
        }
        ++run;
        testing::currentFailures() = 0;
        test.body();
        if (testing::currentFailures() > 0) {
            ++failed;
            std::cerr << "[ FAIL ] " << test.name << '\n';
        } else {
            std::cout << "[  OK  ] " << test.name << '\n';
        }
    }

    std::cout << '\n' << run - failed << '/' << run << " tests passed\n";
    if (run == 0) {
        std::cerr << "no tests matched filter '" << filter << "'\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
