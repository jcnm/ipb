#pragma once

/**
 * @file testing.hpp
 * @brief IPB Testing Module - Main Header
 *
 * Provides comprehensive testing infrastructure:
 *
 * - Concurrency Testing: Race condition detection, stress testing
 * - Fuzz Testing: Property-based testing, input mutation, shrinking
 * - Integration Testing: E2E tests with fixtures and assertions
 *
 * Quick Start:
 * @code
 * #include <ipb/testing/testing.hpp>
 *
 * using namespace ipb::testing;
 *
 * // Simple test
 * TEST(MyTests, BasicTest) {
 *     ASSERT_EQ(1 + 1, 2);
 * }
 *
 * // Concurrency test
 * void test_thread_safety() {
 *     ConcurrencyTest test;
 *     test.add_thread([](size_t id) { ... }, 4);
 *     auto result = test.run();
 *     ASSERT_TRUE(result.success);
 * }
 *
 * // Fuzz test
 * void test_parser_fuzz() {
 *     FuzzTest<std::string> fuzz;
 *     fuzz.test([](const std::string& input) {
 *         Parser p;
 *         p.parse(input);  // Should not crash
 *     });
 *     auto result = fuzz.run(10000);
 *     ASSERT_TRUE(result.success);
 * }
 * @endcode
 */

#include "concurrency_test.hpp"
#include "fuzz_test.hpp"
#include "integration_test.hpp"

namespace ipb::testing {

//=============================================================================
// Test Utilities
//=============================================================================

/**
 * @brief Temporary directory for test files
 */
class TempDirectory {
public:
    TempDirectory() {
        // Create unique temp directory
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        path_    = std::filesystem::temp_directory_path() / ("ipb_test_" + std::to_string(now));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() {
        if (!path_.empty() && std::filesystem::exists(path_)) {
            std::filesystem::remove_all(path_);
        }
    }

    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path file(const std::string& name) const { return path_ / name; }

    // Non-copyable
    TempDirectory(const TempDirectory&)            = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

private:
    std::filesystem::path path_;
};

/**
 * @brief Wait condition with timeout
 */
class WaitCondition {
public:
    bool wait_for(std::function<bool()> condition,
                  std::chrono::milliseconds timeout       = std::chrono::seconds(5),
                  std::chrono::milliseconds poll_interval = std::chrono::milliseconds(10)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(poll_interval);
        }

        return false;
    }
};

/**
 * @brief Test mock for simple function mocking
 */
template <typename Ret, typename... Args>
class MockFunction {
public:
    using FuncType = std::function<Ret(Args...)>;

    void set(FuncType func) { func_ = std::move(func); }

    Ret operator()(Args... args) {
        ++call_count_;
        if (func_) {
            return func_(std::forward<Args>(args)...);
        }
        if constexpr (!std::is_void_v<Ret>) {
            return Ret{};
        }
    }

    size_t call_count() const { return call_count_; }

    void reset() {
        call_count_ = 0;
        func_       = nullptr;
    }

private:
    FuncType func_;
    size_t call_count_{0};
};

/**
 * @brief Capture stdout/stderr for testing
 */
class OutputCapture {
public:
    OutputCapture() {
        old_cout_ = std::cout.rdbuf(cout_buffer_.rdbuf());
        old_cerr_ = std::cerr.rdbuf(cerr_buffer_.rdbuf());
    }

    ~OutputCapture() {
        std::cout.rdbuf(old_cout_);
        std::cerr.rdbuf(old_cerr_);
    }

    std::string stdout_str() const { return cout_buffer_.str(); }
    std::string stderr_str() const { return cerr_buffer_.str(); }

private:
    std::ostringstream cout_buffer_;
    std::ostringstream cerr_buffer_;
    std::streambuf* old_cout_;
    std::streambuf* old_cerr_;
};

/**
 * @brief Benchmark within tests
 */
class TestBenchmark {
public:
    explicit TestBenchmark(const std::string& name)
        : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

    ~TestBenchmark() {
        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::cout << "[BENCH] " << name_ << ": " << duration.count() << "us\n";
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_;
};

#define BENCHMARK_SCOPE(name) ::ipb::testing::TestBenchmark benchmark_##__LINE__(name)

//=============================================================================
// Test Data Generators
//=============================================================================

/**
 * @brief Generate test data
 */
class TestData {
public:
    // Generate vector of sequential integers
    static std::vector<int> sequence(int start, int count) {
        std::vector<int> result(count);
        for (int i = 0; i < count; ++i) {
            result[i] = start + i;
        }
        return result;
    }

    // Generate random vector
    template <typename T>
    static std::vector<T> random_vector(size_t size, T min, T max) {
        RandomGen rng;
        std::vector<T> result(size);
        for (auto& v : result) {
            v = rng.integer(min, max);
        }
        return result;
    }

    // Generate lorem ipsum text
    static std::string lorem_ipsum(size_t words = 50) {
        static const char* lorem[] = {"lorem",       "ipsum",      "dolor",      "sit",   "amet",
                                      "consectetur", "adipiscing", "elit",       "sed",   "do",
                                      "eiusmod",     "tempor",     "incididunt", "ut",    "labore",
                                      "et",          "dolore",     "magna",      "aliqua"};

        RandomGen rng;
        std::ostringstream oss;
        for (size_t i = 0; i < words; ++i) {
            if (i > 0)
                oss << " ";
            oss << lorem[rng.integer<size_t>(0, sizeof(lorem) / sizeof(lorem[0]) - 1)];
        }
        return oss.str();
    }

    // Generate JSON-like test data
    static std::string json_object(size_t fields = 5) {
        RandomGen rng;
        std::ostringstream oss;
        oss << "{";
        for (size_t i = 0; i < fields; ++i) {
            if (i > 0)
                oss << ",";
            oss << "\"field" << i << "\":";
            switch (rng.integer(0, 2)) {
                case 0:
                    oss << rng.integer<int>(-1000, 1000);
                    break;
                case 1:
                    oss << "\"" << rng.string(5, 20) << "\"";
                    break;
                case 2:
                    oss << (rng.boolean() ? "true" : "false");
                    break;
            }
        }
        oss << "}";
        return oss.str();
    }
};

}  // namespace ipb::testing
