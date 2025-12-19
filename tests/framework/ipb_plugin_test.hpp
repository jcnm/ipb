/**
 * @file ipb_plugin_test.hpp
 * @brief Test framework harness for IPB sinks and scoops plugins
 *
 * This header provides common test utilities that can be included by any
 * 3rd party sink or scoop to enable automatic test discovery and execution.
 *
 * Features:
 * - Mock data point generators
 * - Mock sink/scoop base classes
 * - Timing and performance test utilities
 * - Async test helpers
 * - Common test fixtures
 *
 * Usage:
 *   #include <ipb_plugin_test.hpp>
 *
 *   class MySinkTest : public ipb::test::SinkTestBase {
 *   protected:
 *       void SetUp() override {
 *           SinkTestBase::SetUp();
 *           // Additional setup
 *       }
 *   };
 *
 *   TEST_F(MySinkTest, BasicOperation) {
 *       auto dp = create_test_datapoint("sensor/temp", 25.0);
 *       // Test your sink
 *   }
 */

#pragma once

#include <ipb/common/data_point.hpp>
#include <ipb/common/dataset.hpp>
#include <ipb/common/error.hpp>
#include <ipb/common/interfaces.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace ipb::test {

using namespace ipb::common;

// ============================================================================
// Data Point Generators
// ============================================================================

/**
 * @brief Create a test data point with specified values
 */
inline DataPoint create_test_datapoint(
    const std::string& address,
    double value,
    Quality quality = Quality::GOOD,
    uint16_t protocol_id = 0
) {
    DataPoint dp(address);
    dp.set_value(value);
    dp.set_quality(quality);
    dp.set_protocol_id(protocol_id);
    dp.set_timestamp(Timestamp::now());
    return dp;
}

/**
 * @brief Create a test data point with integer value
 */
inline DataPoint create_test_datapoint_int(
    const std::string& address,
    int64_t value,
    Quality quality = Quality::GOOD
) {
    DataPoint dp(address);
    dp.set_value(value);
    dp.set_quality(quality);
    dp.set_timestamp(Timestamp::now());
    return dp;
}

// Note: String values are not supported by Value type.
// Use numeric or boolean types for DataPoint values.

/**
 * @brief Create a test data point with boolean value
 */
inline DataPoint create_test_datapoint_bool(
    const std::string& address,
    bool value,
    Quality quality = Quality::GOOD
) {
    DataPoint dp(address);
    dp.set_value(value);
    dp.set_quality(quality);
    dp.set_timestamp(Timestamp::now());
    return dp;
}

/**
 * @brief Data point generator for batch testing
 */
class DataPointGenerator {
public:
    DataPointGenerator() : rng_(std::random_device{}()) {}

    /**
     * @brief Generate random data points
     */
    std::vector<DataPoint> generate(
        size_t count,
        const std::string& address_prefix = "sensor/test"
    ) {
        std::vector<DataPoint> result;
        result.reserve(count);

        std::uniform_real_distribution<double> value_dist(0.0, 100.0);
        std::uniform_int_distribution<int> quality_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            std::string addr = address_prefix + "/" + std::to_string(i);
            DataPoint dp(addr);
            dp.set_value(value_dist(rng_));
            dp.set_quality(static_cast<Quality>(quality_dist(rng_)));
            dp.set_timestamp(Timestamp::now());
            result.push_back(std::move(dp));
        }

        return result;
    }

    /**
     * @brief Generate data set
     */
    DataSet generate_dataset(
        size_t count,
        const std::string& address_prefix = "sensor/test"
    ) {
        std::vector<DataPoint> points = generate(count, address_prefix);
        DataSet ds(count);  // Use capacity constructor
        for (auto&& dp : points) {
            ds.push_back(std::move(dp));
        }
        return ds;
    }

    /**
     * @brief Generate sequence of data points at different timestamps
     */
    std::vector<DataPoint> generate_time_series(
        const std::string& address,
        size_t count,
        std::chrono::milliseconds interval = std::chrono::milliseconds(100)
    ) {
        std::vector<DataPoint> result;
        result.reserve(count);

        std::uniform_real_distribution<double> value_dist(0.0, 100.0);
        auto base_time = Timestamp::now();

        for (size_t i = 0; i < count; ++i) {
            DataPoint dp(address);
            dp.set_value(value_dist(rng_));
            dp.set_quality(Quality::GOOD);
            dp.set_timestamp(base_time + interval * i);
            result.push_back(std::move(dp));
        }

        return result;
    }

private:
    std::mt19937 rng_;
};

// ============================================================================
// Mock Sink for Testing
// ============================================================================

/**
 * @brief Mock sink for testing routing and data flow
 */
class MockSink : public ISink {
public:
    MockSink(const std::string& id = "mock-sink") : id_(id) {}

    Result<void> initialize(const std::string&) override {
        initialized_ = true;
        return ok();
    }

    Result<void> start() override {
        running_ = true;
        return ok();
    }

    Result<void> stop() override {
        running_ = false;
        return ok();
    }

    Result<void> shutdown() override {
        running_ = false;
        initialized_ = false;
        return ok();
    }

    Result<void> send_data_point(const DataPoint& dp) override {
        std::lock_guard<std::mutex> lock(mutex_);
        received_points_.push_back(dp);
        points_count_++;
        if (on_receive_) {
            on_receive_(dp);
        }
        return ok();
    }

    Result<void> send_data_set(const DataSet& ds) override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& dp : ds) {
            received_points_.push_back(dp);
            points_count_++;
        }
        return ok();
    }

    bool is_connected() const override { return connected_; }
    bool is_healthy() const override { return healthy_; }

    SinkMetrics get_metrics() const override {
        SinkMetrics metrics;
        metrics.sink_id = id_;
        metrics.messages_sent = points_count_.load();
        metrics.is_connected = connected_;
        metrics.is_healthy = healthy_;
        return metrics;
    }

    std::string get_sink_info() const override {
        return "MockSink[" + id_ + "]";
    }

    // Test helpers
    size_t received_count() const { return points_count_.load(); }

    std::vector<DataPoint> received_points() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return received_points_;
    }

    void clear_received() {
        std::lock_guard<std::mutex> lock(mutex_);
        received_points_.clear();
        points_count_ = 0;
    }

    void set_connected(bool c) { connected_ = c; }
    void set_healthy(bool h) { healthy_ = h; }

    void set_on_receive(std::function<void(const DataPoint&)> cb) {
        on_receive_ = std::move(cb);
    }

    bool wait_for_count(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (points_count_.load() >= count) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

private:
    std::string id_;
    bool initialized_ = false;
    bool running_ = false;
    bool connected_ = true;
    bool healthy_ = true;

    mutable std::mutex mutex_;
    std::vector<DataPoint> received_points_;
    std::atomic<size_t> points_count_{0};
    std::function<void(const DataPoint&)> on_receive_;
};

// ============================================================================
// Test Fixtures
// ============================================================================

/**
 * @brief Base test fixture for sink tests
 */
class SinkTestBase : public ::testing::Test {
protected:
    DataPointGenerator generator_;

    void SetUp() override {
        // Override in derived classes
    }

    void TearDown() override {
        // Override in derived classes
    }

    DataPoint create_datapoint(
        const std::string& address = "test/sensor",
        double value = 25.0
    ) {
        return create_test_datapoint(address, value);
    }

    std::vector<DataPoint> create_datapoints(
        size_t count,
        const std::string& prefix = "test/sensor"
    ) {
        return generator_.generate(count, prefix);
    }

    DataSet create_dataset(
        size_t count,
        const std::string& prefix = "test/sensor"
    ) {
        return generator_.generate_dataset(count, prefix);
    }
};

/**
 * @brief Base test fixture for scoop tests
 */
class ScoopTestBase : public ::testing::Test {
protected:
    DataPointGenerator generator_;

    void SetUp() override {
        // Override in derived classes
    }

    void TearDown() override {
        // Override in derived classes
    }
};

// ============================================================================
// Performance Testing Utilities
// ============================================================================

/**
 * @brief Performance measurement helper
 */
class PerformanceTimer {
public:
    void start() {
        start_time_ = std::chrono::steady_clock::now();
    }

    void stop() {
        end_time_ = std::chrono::steady_clock::now();
    }

    std::chrono::nanoseconds elapsed() const {
        return end_time_ - start_time_;
    }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(elapsed()).count();
    }

    double elapsed_us() const {
        return std::chrono::duration<double, std::micro>(elapsed()).count();
    }

    double throughput(size_t operations) const {
        auto ms = elapsed_ms();
        return ms > 0 ? (operations / ms) * 1000.0 : 0.0;
    }

private:
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
};

/**
 * @brief Scoped timer that records elapsed time on destruction
 */
class ScopedTimer {
public:
    explicit ScopedTimer(std::chrono::nanoseconds& result)
        : result_(result), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        result_ = std::chrono::steady_clock::now() - start_;
    }

private:
    std::chrono::nanoseconds& result_;
    std::chrono::steady_clock::time_point start_;
};

/**
 * @brief Run performance test and calculate statistics
 */
template<typename Func>
struct PerformanceStats {
    double min_us;
    double max_us;
    double avg_us;
    double median_us;
    double p95_us;
    double p99_us;
    size_t iterations;
};

template<typename Func>
PerformanceStats<Func> measure_performance(
    Func func,
    size_t iterations,
    size_t warmup_iterations = 10
) {
    // Warmup
    for (size_t i = 0; i < warmup_iterations; ++i) {
        func();
    }

    // Measure
    std::vector<double> timings;
    timings.reserve(iterations);

    for (size_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::steady_clock::now();
        func();
        auto end = std::chrono::steady_clock::now();
        auto us = std::chrono::duration<double, std::micro>(end - start).count();
        timings.push_back(us);
    }

    // Calculate stats
    std::sort(timings.begin(), timings.end());

    PerformanceStats<Func> stats;
    stats.iterations = iterations;
    stats.min_us = timings.front();
    stats.max_us = timings.back();
    stats.median_us = timings[iterations / 2];
    stats.p95_us = timings[static_cast<size_t>(iterations * 0.95)];
    stats.p99_us = timings[static_cast<size_t>(iterations * 0.99)];

    double sum = 0;
    for (auto t : timings) sum += t;
    stats.avg_us = sum / iterations;

    return stats;
}

// ============================================================================
// Async Testing Utilities
// ============================================================================

/**
 * @brief Synchronization primitive for async tests
 */
class TestLatch {
public:
    explicit TestLatch(int count = 1) : count_(count) {}

    void count_down() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (--count_ == 0) {
            cv_.notify_all();
        }
    }

    bool wait(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return count_ == 0; });
    }

    void reset(int count = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        count_ = count;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
};

/**
 * @brief Wait for condition with timeout
 */
template<typename Pred>
bool wait_for(Pred pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// ============================================================================
// Test Assertions for IPB Types
// ============================================================================

/**
 * @brief Assert that a Result is successful
 */
#define ASSERT_RESULT_OK(result) \
    ASSERT_TRUE((result).is_success()) << "Expected success, got error: " << (result).error().message()

/**
 * @brief Assert that a Result has an error
 */
#define ASSERT_RESULT_ERROR(result) \
    ASSERT_TRUE((result).is_error()) << "Expected error, got success"

/**
 * @brief Assert that a Result has a specific error code
 */
#define ASSERT_RESULT_ERROR_CODE(result, expected_code) \
    do { \
        ASSERT_TRUE((result).is_error()); \
        ASSERT_EQ((result).error().code(), (expected_code)); \
    } while(0)

/**
 * @brief Expect that a Result is successful
 */
#define EXPECT_RESULT_OK(result) \
    EXPECT_TRUE((result).is_success()) << "Expected success, got error: " << (result).error().message()

/**
 * @brief Expect that a Result has an error
 */
#define EXPECT_RESULT_ERROR(result) \
    EXPECT_TRUE((result).is_error()) << "Expected error, got success"

/**
 * @brief Assert data point equality
 */
inline void ASSERT_DATAPOINT_EQ(const DataPoint& a, const DataPoint& b) {
    ASSERT_EQ(a.address(), b.address());
    ASSERT_EQ(a.quality(), b.quality());
    ASSERT_EQ(a.protocol_id(), b.protocol_id());
    ASSERT_EQ(a.is_valid(), b.is_valid());
}

/**
 * @brief Expect data point equality
 */
inline void EXPECT_DATAPOINT_EQ(const DataPoint& a, const DataPoint& b) {
    EXPECT_EQ(a.address(), b.address());
    EXPECT_EQ(a.quality(), b.quality());
    EXPECT_EQ(a.protocol_id(), b.protocol_id());
    EXPECT_EQ(a.is_valid(), b.is_valid());
}

}  // namespace ipb::test
