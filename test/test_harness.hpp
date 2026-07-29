// Minimal dependency-free test harness.
//
// Deliberately not GoogleTest: this driver is dropped into projects (and onto boards)
// that do not always have gtest installed, and the tests should build with nothing but
// a C++17 compiler.
//
//     TEST_CASE(my_case)
//     {
//         CHECK(1 + 1 == 2);
//         CHECK_EQ(port.baudrate(), 115200);
//     }
//
// Link one translation unit that defines TEST_MAIN (see test_serial_linux.cpp).
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace testing
{

struct TestCase
{
    const char *name;
    void (*fn)();
};

inline std::vector<TestCase> &registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

inline int &currentFailures()
{
    static int failures = 0;
    return failures;
}

struct Registrar
{
    Registrar(const char *name, void (*fn)())
    {
        registry().push_back({name, fn});
    }
};

inline void reportFailure(const char *file, int line, const std::string &expression)
{
    ++currentFailures();
    std::printf("    %s:%d: failed: %s\n", file, line, expression.c_str());
}

template <typename A, typename B>
void reportComparison(
    const char *file, int line, const char *expression, const A &lhs, const B &rhs)
{
    std::ostringstream out;
    out << expression << "  (left = " << lhs << ", right = " << rhs << ")";
    reportFailure(file, line, out.str());
}

/// Run every registered case, optionally filtered by a substring of its name.
inline int runAll(const std::string &filter = "")
{
    int failed_cases = 0;
    int run_cases = 0;

    for (const TestCase &test : registry()) {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) {
            continue;
        }
        ++run_cases;
        currentFailures() = 0;
        std::printf("[ RUN  ] %s\n", test.name);
        test.fn();
        if (currentFailures() == 0) {
            std::printf("[  OK  ] %s\n", test.name);
        } else {
            std::printf("[ FAIL ] %s (%d check(s) failed)\n", test.name, currentFailures());
            ++failed_cases;
        }
    }

    std::printf("\n%d case(s) run, %d failed\n", run_cases, failed_cases);
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace testing

#define TEST_CASE(name)                                            \
    static void name();                                            \
    static const ::testing::Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(expression)                                                  \
    do {                                                                   \
        if (!(expression)) {                                               \
            ::testing::reportFailure(__FILE__, __LINE__, #expression);     \
        }                                                                  \
    } while (0)

#define CHECK_EQ(lhs, rhs)                                                              \
    do {                                                                                \
        const auto &check_lhs = (lhs);                                                  \
        const auto &check_rhs = (rhs);                                                  \
        if (!(check_lhs == check_rhs)) {                                                 \
            ::testing::reportComparison(                                                \
                __FILE__, __LINE__, #lhs " == " #rhs, check_lhs, check_rhs);            \
        }                                                                               \
    } while (0)

/// Abort the current case (the rest of it cannot work) without killing the run.
#define REQUIRE(expression)                                            \
    do {                                                               \
        if (!(expression)) {                                           \
            ::testing::reportFailure(__FILE__, __LINE__, #expression); \
            return;                                                    \
        }                                                              \
    } while (0)

#define TEST_MAIN()                                                    \
    int main(int argc, char **argv)                                    \
    {                                                                  \
        return ::testing::runAll(argc > 1 ? argv[1] : "");             \
    }
