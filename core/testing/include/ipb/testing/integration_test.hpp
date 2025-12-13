#pragma once

/**
 * @file integration_test.hpp
 * @brief End-to-end integration testing framework
 *
 * Features:
 * - Test fixtures with setup/teardown
 * - Test suites and grouping
 * - Assertions with detailed messages
 * - Test discovery and registration
 * - Timeout handling
 * - Resource cleanup
 * - Test isolation
 *
 * Usage:
 * @code
 * class MyIntegrationTest : public TestFixture {
 * public:
 *     void SetUp() override {
 *         // Setup resources
 *     }
 *
 *     void TearDown() override {
 *         // Cleanup resources
 *     }
 * };
 *
 * TEST_F(MyIntegrationTest, TestName) {
 *     EXPECT_TRUE(some_condition);
 *     ASSERT_EQ(actual, expected);
 * }
 * @endcode
 */

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ipb::testing {

//=============================================================================
// Test Result Types
//=============================================================================

enum class TestStatus { PASSED, FAILED, SKIPPED, TIMEOUT, ERROR };

inline std::string status_string(TestStatus status) {
    switch (status) {
        case TestStatus::PASSED:
            return "PASSED";
        case TestStatus::FAILED:
            return "FAILED";
        case TestStatus::SKIPPED:
            return "SKIPPED";
        case TestStatus::TIMEOUT:
            return "TIMEOUT";
        case TestStatus::ERROR:
            return "ERROR";
    }
    return "UNKNOWN";
}

struct TestResult {
    std::string name;
    TestStatus status{TestStatus::PASSED};
    std::string message;
    std::chrono::milliseconds duration{0};
    std::string file;
    int line{0};

    bool passed() const { return status == TestStatus::PASSED; }
};

struct SuiteResult {
    std::string name;
    std::vector<TestResult> tests;
    size_t passed{0};
    size_t failed{0};
    size_t skipped{0};
    std::chrono::milliseconds total_duration{0};

    void add(const TestResult& result) {
        tests.push_back(result);
        total_duration += result.duration;

        switch (result.status) {
            case TestStatus::PASSED:
                ++passed;
                break;
            case TestStatus::FAILED:
            case TestStatus::TIMEOUT:
            case TestStatus::ERROR:
                ++failed;
                break;
            case TestStatus::SKIPPED:
                ++skipped;
                break;
        }
    }
};

//=============================================================================
// Test Assertions
//=============================================================================

class AssertionFailure : public std::exception {
public:
    AssertionFailure(const std::string& msg, const char* file, int line)
        : message_(msg), file_(file), line_(line) {
        what_ = message_ + " at " + file_ + ":" + std::to_string(line_);
    }

    const char* what() const noexcept override { return what_.c_str(); }
    const std::string& message() const { return message_; }
    const std::string& file() const { return file_; }
    int line() const { return line_; }

private:
    std::string message_;
    std::string file_;
    int line_;
    std::string what_;
};

class TestSkipped : public std::exception {
public:
    explicit TestSkipped(const std::string& reason) : reason_(reason) {}
    const char* what() const noexcept override { return reason_.c_str(); }

private:
    std::string reason_;
};

/**
 * @brief Assertion helper class
 */
class Assert {
public:
    static void True(bool condition, const char* expr, const char* file, int line) {
        if (!condition) {
            throw AssertionFailure(std::string("Expected true: ") + expr, file, line);
        }
    }

    static void False(bool condition, const char* expr, const char* file, int line) {
        if (condition) {
            throw AssertionFailure(std::string("Expected false: ") + expr, file, line);
        }
    }

    template <typename T, typename U>
    static void Equal(const T& expected, const U& actual, const char* expr_expected,
                      const char* expr_actual, const char* file, int line) {
        if (!(expected == actual)) {
            std::ostringstream oss;
            oss << "Expected " << expr_expected << " == " << expr_actual << "\n"
                << "  Expected: " << expected << "\n"
                << "  Actual:   " << actual;
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T, typename U>
    static void NotEqual(const T& expected, const U& actual, const char* expr_expected,
                         const char* expr_actual, const char* file, int line) {
        if (expected == actual) {
            std::ostringstream oss;
            oss << "Expected " << expr_expected << " != " << expr_actual << "\n"
                << "  Both are: " << expected;
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T, typename U>
    static void Less(const T& a, const U& b, const char* expr_a, const char* expr_b,
                     const char* file, int line) {
        if (!(a < b)) {
            std::ostringstream oss;
            oss << "Expected " << expr_a << " < " << expr_b << "\n"
                << "  Left:  " << a << "\n"
                << "  Right: " << b;
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T, typename U>
    static void LessOrEqual(const T& a, const U& b, const char* expr_a, const char* expr_b,
                            const char* file, int line) {
        if (!(a <= b)) {
            std::ostringstream oss;
            oss << "Expected " << expr_a << " <= " << expr_b << "\n"
                << "  Left:  " << a << "\n"
                << "  Right: " << b;
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T, typename U>
    static void Greater(const T& a, const U& b, const char* expr_a, const char* expr_b,
                        const char* file, int line) {
        if (!(a > b)) {
            std::ostringstream oss;
            oss << "Expected " << expr_a << " > " << expr_b << "\n"
                << "  Left:  " << a << "\n"
                << "  Right: " << b;
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T>
    static void NotNull(const T* ptr, const char* expr, const char* file, int line) {
        if (ptr == nullptr) {
            throw AssertionFailure(std::string("Expected non-null: ") + expr, file, line);
        }
    }

    template <typename T>
    static void IsNull(const T* ptr, const char* expr, const char* file, int line) {
        if (ptr != nullptr) {
            throw AssertionFailure(std::string("Expected null: ") + expr, file, line);
        }
    }

    template <typename Exception, typename Func>
    static void Throws(Func&& func, const char* expr, const char* file, int line) {
        try {
            func();
            throw AssertionFailure(std::string("Expected exception not thrown: ") + expr, file,
                                   line);
        } catch (const Exception&) {
            // Expected
        } catch (...) {
            throw AssertionFailure(std::string("Wrong exception type thrown: ") + expr, file, line);
        }
    }

    template <typename Func>
    static void NoThrow(Func&& func, const char* expr, const char* file, int line) {
        try {
            func();
        } catch (const std::exception& e) {
            throw AssertionFailure(std::string("Unexpected exception: ") + e.what() + " in " + expr,
                                   file, line);
        } catch (...) {
            throw AssertionFailure(std::string("Unknown exception in: ") + expr, file, line);
        }
    }

    static void StringContains(const std::string& haystack, const std::string& needle,
                               const char* file, int line) {
        if (haystack.find(needle) == std::string::npos) {
            std::ostringstream oss;
            oss << "Expected string to contain: \"" << needle << "\"\n"
                << "  Actual: \"" << haystack << "\"";
            throw AssertionFailure(oss.str(), file, line);
        }
    }

    template <typename T>
    static void Near(T expected, T actual, T epsilon, const char* file, int line) {
        T diff = expected > actual ? expected - actual : actual - expected;
        if (diff > epsilon) {
            std::ostringstream oss;
            oss << "Expected values to be near (epsilon=" << epsilon << ")\n"
                << "  Expected: " << expected << "\n"
                << "  Actual:   " << actual << "\n"
                << "  Diff:     " << diff;
            throw AssertionFailure(oss.str(), file, line);
        }
    }
};

//=============================================================================
// Assertion Macros
//=============================================================================

#define ASSERT_TRUE(condition) \
    ::ipb::testing::Assert::True(condition, #condition, __FILE__, __LINE__)

#define ASSERT_FALSE(condition) \
    ::ipb::testing::Assert::False(condition, #condition, __FILE__, __LINE__)

#define ASSERT_EQ(expected, actual) \
    ::ipb::testing::Assert::Equal(expected, actual, #expected, #actual, __FILE__, __LINE__)

#define ASSERT_NE(expected, actual) \
    ::ipb::testing::Assert::NotEqual(expected, actual, #expected, #actual, __FILE__, __LINE__)

#define ASSERT_LT(a, b) ::ipb::testing::Assert::Less(a, b, #a, #b, __FILE__, __LINE__)

#define ASSERT_LE(a, b) ::ipb::testing::Assert::LessOrEqual(a, b, #a, #b, __FILE__, __LINE__)

#define ASSERT_GT(a, b) ::ipb::testing::Assert::Greater(a, b, #a, #b, __FILE__, __LINE__)

#define ASSERT_NOT_NULL(ptr) ::ipb::testing::Assert::NotNull(ptr, #ptr, __FILE__, __LINE__)

#define ASSERT_NULL(ptr) ::ipb::testing::Assert::IsNull(ptr, #ptr, __FILE__, __LINE__)

#define ASSERT_THROWS(exception_type, expr) \
    ::ipb::testing::Assert::Throws<exception_type>([&] { expr; }, #expr, __FILE__, __LINE__)

#define ASSERT_NO_THROW(expr) \
    ::ipb::testing::Assert::NoThrow([&] { expr; }, #expr, __FILE__, __LINE__)

#define ASSERT_STR_CONTAINS(haystack, needle) \
    ::ipb::testing::Assert::StringContains(haystack, needle, __FILE__, __LINE__)

#define ASSERT_NEAR(expected, actual, epsilon) \
    ::ipb::testing::Assert::Near(expected, actual, epsilon, __FILE__, __LINE__)

#define EXPECT_TRUE(condition)                                                                  \
    do {                                                                                        \
        try {                                                                                   \
            ASSERT_TRUE(condition);                                                             \
        } catch (const ::ipb::testing::AssertionFailure&) {                                     \
            ::ipb::testing::TestContext::current().add_failure(__FILE__, __LINE__, #condition); \
        }                                                                                       \
    } while (0)

#define EXPECT_FALSE(condition)                                                                 \
    do {                                                                                        \
        try {                                                                                   \
            ASSERT_FALSE(condition);                                                            \
        } catch (const ::ipb::testing::AssertionFailure&) {                                     \
            ::ipb::testing::TestContext::current().add_failure(__FILE__, __LINE__, #condition); \
        }                                                                                       \
    } while (0)

#define EXPECT_EQ(expected, actual)                                                       \
    do {                                                                                  \
        try {                                                                             \
            ASSERT_EQ(expected, actual);                                                  \
        } catch (const ::ipb::testing::AssertionFailure&) {                               \
            ::ipb::testing::TestContext::current().add_failure(__FILE__, __LINE__,        \
                                                               #expected " == " #actual); \
        }                                                                                 \
    } while (0)

#define SKIP_TEST(reason) throw ::ipb::testing::TestSkipped(reason)

//=============================================================================
// Test Context
//=============================================================================

/**
 * @brief Thread-local test context
 */
class TestContext {
public:
    static TestContext& current() {
        thread_local TestContext ctx;
        return ctx;
    }

    void reset() {
        failures_.clear();
        has_failure_ = false;
    }

    void add_failure(const char* file, int line, const char* expr) {
        has_failure_ = true;
        std::ostringstream oss;
        oss << file << ":" << line << ": " << expr;
        failures_.push_back(oss.str());
    }

    bool has_failure() const { return has_failure_; }
    const std::vector<std::string>& failures() const { return failures_; }

private:
    std::vector<std::string> failures_;
    bool has_failure_{false};
};

//=============================================================================
// Test Fixture
//=============================================================================

/**
 * @brief Base class for test fixtures
 */
class TestFixture {
public:
    virtual ~TestFixture() = default;

    virtual void SetUp() {}
    virtual void TearDown() {}

    // Environment setup (once per suite)
    static void SetUpTestSuite() {}
    static void TearDownTestSuite() {}
};

//=============================================================================
// Test Case
//=============================================================================

/**
 * @brief Represents a single test case
 */
struct TestCase {
    std::string name;
    std::string suite;
    std::function<void()> test_func;
    std::function<std::unique_ptr<TestFixture>()> fixture_factory;
    std::chrono::seconds timeout{30};
    bool enabled{true};
};

//=============================================================================
// Test Registry
//=============================================================================

/**
 * @brief Global test registry
 */
class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry registry;
        return registry;
    }

    void register_test(TestCase test) { tests_.push_back(std::move(test)); }

    const std::vector<TestCase>& tests() const { return tests_; }

    void clear() { tests_.clear(); }

private:
    std::vector<TestCase> tests_;
};

//=============================================================================
// Test Runner
//=============================================================================

/**
 * @brief Test runner configuration
 */
struct RunnerConfig {
    bool verbose{false};
    bool stop_on_failure{false};
    std::string filter;  // Run only tests matching this pattern
    bool shuffle{false};
    size_t repeat{1};
    std::chrono::seconds default_timeout{30};
};

/**
 * @brief Test runner
 */
class TestRunner {
public:
    explicit TestRunner(RunnerConfig config = {}) : config_(std::move(config)) {}

    /**
     * @brief Run all registered tests
     */
    std::vector<SuiteResult> run() {
        std::map<std::string, SuiteResult> suites;
        auto& tests = TestRegistry::instance().tests();

        // Group by suite
        for (const auto& test : tests) {
            if (!matches_filter(test.name))
                continue;
            if (!test.enabled)
                continue;

            if (suites.find(test.suite) == suites.end()) {
                suites[test.suite].name = test.suite;
            }

            auto result = run_test(test);
            suites[test.suite].add(result);

            if (config_.stop_on_failure && !result.passed()) {
                break;
            }
        }

        std::vector<SuiteResult> results;
        for (auto& [_, suite] : suites) {
            results.push_back(std::move(suite));
        }

        return results;
    }

    /**
     * @brief Run single test
     */
    TestResult run_test(const TestCase& test) {
        TestResult result;
        result.name = test.name;

        TestContext::current().reset();

        auto start = std::chrono::steady_clock::now();

        try {
            // Create fixture
            std::unique_ptr<TestFixture> fixture;
            if (test.fixture_factory) {
                fixture = test.fixture_factory();
                fixture->SetUp();
            }

            // Run test
            test.test_func();

            // Teardown
            if (fixture) {
                fixture->TearDown();
            }

            // Check for EXPECT failures
            if (TestContext::current().has_failure()) {
                result.status = TestStatus::FAILED;
                std::ostringstream oss;
                for (const auto& f : TestContext::current().failures()) {
                    oss << f << "\n";
                }
                result.message = oss.str();
            } else {
                result.status = TestStatus::PASSED;
            }

        } catch (const TestSkipped& e) {
            result.status  = TestStatus::SKIPPED;
            result.message = e.what();

        } catch (const AssertionFailure& e) {
            result.status  = TestStatus::FAILED;
            result.message = e.message();
            result.file    = e.file();
            result.line    = e.line();

        } catch (const std::exception& e) {
            result.status  = TestStatus::ERROR;
            result.message = std::string("Exception: ") + e.what();

        } catch (...) {
            result.status  = TestStatus::ERROR;
            result.message = "Unknown exception";
        }

        auto end        = std::chrono::steady_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        // Print result
        if (config_.verbose) {
            print_result(result);
        }

        return result;
    }

    /**
     * @brief Print test result
     */
    void print_result(const TestResult& result) {
        const char* status_icon = "";
        switch (result.status) {
            case TestStatus::PASSED:
                status_icon = "[PASS]";
                break;
            case TestStatus::FAILED:
                status_icon = "[FAIL]";
                break;
            case TestStatus::SKIPPED:
                status_icon = "[SKIP]";
                break;
            case TestStatus::TIMEOUT:
                status_icon = "[TIME]";
                break;
            case TestStatus::ERROR:
                status_icon = "[ERR ]";
                break;
        }

        std::cout << status_icon << " " << result.name << " (" << result.duration.count() << "ms)";

        if (!result.message.empty() && result.status != TestStatus::PASSED) {
            std::cout << "\n  " << result.message;
        }
        std::cout << "\n";
    }

    /**
     * @brief Print summary
     */
    void print_summary(const std::vector<SuiteResult>& suites) {
        size_t total_passed = 0, total_failed = 0, total_skipped = 0;
        std::chrono::milliseconds total_duration{0};

        for (const auto& suite : suites) {
            total_passed += suite.passed;
            total_failed += suite.failed;
            total_skipped += suite.skipped;
            total_duration += suite.total_duration;
        }

        std::cout << "\n========================================\n";
        std::cout << "Test Summary\n";
        std::cout << "========================================\n";
        std::cout << "Passed:  " << total_passed << "\n";
        std::cout << "Failed:  " << total_failed << "\n";
        std::cout << "Skipped: " << total_skipped << "\n";
        std::cout << "Total:   " << (total_passed + total_failed + total_skipped) << "\n";
        std::cout << "Duration: " << total_duration.count() << "ms\n";
        std::cout << "========================================\n";

        if (total_failed > 0) {
            std::cout << "\nFailed tests:\n";
            for (const auto& suite : suites) {
                for (const auto& test : suite.tests) {
                    if (!test.passed()) {
                        std::cout << "  - " << test.name << "\n";
                        if (!test.message.empty()) {
                            std::cout << "    " << test.message << "\n";
                        }
                    }
                }
            }
        }
    }

private:
    bool matches_filter(const std::string& name) {
        if (config_.filter.empty())
            return true;
        return name.find(config_.filter) != std::string::npos;
    }

    RunnerConfig config_;
};

//=============================================================================
// Test Registration Macros
//=============================================================================

#define TEST(suite_name, test_name)                                                \
    void suite_name##_##test_name##_impl();                                        \
    namespace {                                                                    \
    struct suite_name##_##test_name##_registrar {                                  \
        suite_name##_##test_name##_registrar() {                                   \
            ::ipb::testing::TestCase tc;                                           \
            tc.name      = #suite_name "." #test_name;                             \
            tc.suite     = #suite_name;                                            \
            tc.test_func = suite_name##_##test_name##_impl;                        \
            ::ipb::testing::TestRegistry::instance().register_test(std::move(tc)); \
        }                                                                          \
    } suite_name##_##test_name##_registrar_instance;                               \
    }                                                                              \
    void suite_name##_##test_name##_impl()

#define TEST_F(fixture_class, test_name)                                                \
    class fixture_class##_##test_name : public fixture_class {                          \
    public:                                                                             \
        void TestBody();                                                                \
    };                                                                                  \
    namespace {                                                                         \
    struct fixture_class##_##test_name##_registrar {                                    \
        fixture_class##_##test_name##_registrar() {                                     \
            ::ipb::testing::TestCase tc;                                                \
            tc.name            = #fixture_class "." #test_name;                         \
            tc.suite           = #fixture_class;                                        \
            tc.fixture_factory = []() -> std::unique_ptr<::ipb::testing::TestFixture> { \
                return std::make_unique<fixture_class##_##test_name>();                 \
            };                                                                          \
            tc.test_func = []() {                                                       \
                fixture_class##_##test_name instance;                                   \
                instance.SetUp();                                                       \
                instance.TestBody();                                                    \
                instance.TearDown();                                                    \
            };                                                                          \
            ::ipb::testing::TestRegistry::instance().register_test(std::move(tc));      \
        }                                                                               \
    } fixture_class##_##test_name##_registrar_instance;                                 \
    }                                                                                   \
    void fixture_class##_##test_name::TestBody()

//=============================================================================
// Main Entry Point
//=============================================================================

/**
 * @brief Run all tests and return exit code
 */
inline int run_all_tests(int argc = 0, char** argv = nullptr) {
    RunnerConfig config;
    config.verbose = true;

    // Parse command line
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg.starts_with("--filter=")) {
            config.filter = arg.substr(9);
        } else if (arg == "--stop-on-failure") {
            config.stop_on_failure = true;
        }
    }

    TestRunner runner(config);
    auto results = runner.run();
    runner.print_summary(results);

    size_t failed = 0;
    for (const auto& suite : results) {
        failed += suite.failed;
    }

    return failed > 0 ? 1 : 0;
}

}  // namespace ipb::testing
