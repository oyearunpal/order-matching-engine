#pragma once

// Minimal self-contained test framework (no third-party dependencies).
//
//   TEST(name) { EXPECT_EQ(a, b); ASSERT_TRUE(cond); }
//
// Tests self-register at static-initialization time; test_main.cpp runs them.
// EXPECT_* records a failure and continues; ASSERT_* also returns from the test.

#include <concepts>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    void (*body)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*body)()) { registry().push_back({name, body}); }
};

// Failures recorded by the currently running test.
inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

template <typename T>
concept Streamable = requires(std::ostream& os, const T& value) { os << value; };

template <typename T>
std::string describe(const T& value) {
    std::ostringstream os;
    if constexpr (std::same_as<T, bool>) {
        os << (value ? "true" : "false");
    } else if constexpr (std::same_as<T, std::string>) {
        os << '"' << value << '"';
    } else if constexpr (Streamable<T>) {
        os << value;
    } else if constexpr (requires { value.has_value(); *value; }) {
        os << (value.has_value() ? describe(*value) : std::string("nullopt"));
    } else if constexpr (std::ranges::range<T>) {
        os << '[';
        const char* separator = "";
        for (const auto& element : value) {
            os << separator << describe(element);
            separator = ", ";
        }
        os << ']';
    } else if constexpr (requires { value.first; value.second; }) {
        os << '(' << describe(value.first) << ", " << describe(value.second) << ')';
    } else {
        os << "<unprintable>";
    }
    return os.str();
}

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++currentFailures();
    std::cerr << "    " << file << ':' << line << ": " << message << '\n';
}

template <typename A, typename B>
bool checkEqual(const A& actual, const B& expected, const char* actualText, const char* expectedText,
                const char* file, int line) {
    if (actual == expected) {
        return true;
    }
    reportFailure(file, line,
                  std::string("expected ") + actualText + " == " + expectedText + "\n      actual:   " +
                      describe(actual) + "\n      expected: " + describe(expected));
    return false;
}

inline bool checkTrue(bool condition, const char* text, const char* file, int line) {
    if (!condition) {
        reportFailure(file, line, std::string("expected true: ") + text);
    }
    return condition;
}

}  // namespace testing

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST(name)                                                                         \
    static void name();                                                                    \
    static const ::testing::Registrar TEST_CONCAT(name, _registrar)(#name, &name);         \
    static void name()

#define EXPECT_EQ(actual, expected) \
    ::testing::checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)
#define EXPECT_TRUE(condition) ::testing::checkTrue(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define EXPECT_FALSE(condition) \
    ::testing::checkTrue(!static_cast<bool>(condition), "!(" #condition ")", __FILE__, __LINE__)

#define ASSERT_EQ(actual, expected)             \
    do {                                        \
        if (!EXPECT_EQ(actual, expected)) return; \
    } while (false)
#define ASSERT_TRUE(condition)             \
    do {                                   \
        if (!EXPECT_TRUE(condition)) return; \
    } while (false)
