/**
 * @file test_timestamp.cpp
 * @brief Comprehensive unit tests for ipb::common::Timestamp
 */

#include <gtest/gtest.h>
#include <ipb/common/data_point.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

using namespace ipb::common;
using namespace std::chrono_literals;

class TimestampTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(TimestampTest, DefaultConstruction) {
    Timestamp ts;
    EXPECT_EQ(ts.nanoseconds(), 0);
    EXPECT_EQ(ts.microseconds(), 0);
    EXPECT_EQ(ts.milliseconds(), 0);
    EXPECT_EQ(ts.seconds(), 0);
}

TEST_F(TimestampTest, ConstructFromDuration) {
    Timestamp ts(std::chrono::nanoseconds(1000000000));  // 1 second
    EXPECT_EQ(ts.nanoseconds(), 1000000000);
    EXPECT_EQ(ts.microseconds(), 1000000);
    EXPECT_EQ(ts.milliseconds(), 1000);
    EXPECT_EQ(ts.seconds(), 1);
}

TEST_F(TimestampTest, NowIsNonZero) {
    Timestamp ts = Timestamp::now();
    EXPECT_GT(ts.nanoseconds(), 0);
}

TEST_F(TimestampTest, NowIsMonotonic) {
    Timestamp ts1 = Timestamp::now();
    std::this_thread::sleep_for(1ms);
    Timestamp ts2 = Timestamp::now();

    EXPECT_GT(ts2.nanoseconds(), ts1.nanoseconds());
}

TEST_F(TimestampTest, FromSystemTime) {
    Timestamp ts = Timestamp::from_system_time();
    EXPECT_GT(ts.nanoseconds(), 0);

    // System time should be relatively recent (post-2020)
    // 2020-01-01 00:00:00 UTC in nanoseconds since epoch
    constexpr int64_t jan_2020_ns = 1577836800LL * 1000000000LL;
    EXPECT_GT(ts.nanoseconds(), jan_2020_ns);
}

// ============================================================================
// Accessor Tests
// ============================================================================

TEST_F(TimestampTest, NanosecondsConversion) {
    Timestamp ts(std::chrono::nanoseconds(123456789));
    EXPECT_EQ(ts.nanoseconds(), 123456789);
}

TEST_F(TimestampTest, MicrosecondsConversion) {
    Timestamp ts(std::chrono::nanoseconds(1234567890));
    EXPECT_EQ(ts.microseconds(), 1234567);  // Truncated
}

TEST_F(TimestampTest, MillisecondsConversion) {
    Timestamp ts(std::chrono::nanoseconds(1234567890000));
    EXPECT_EQ(ts.milliseconds(), 1234567);  // Truncated
}

TEST_F(TimestampTest, SecondsConversion) {
    Timestamp ts(std::chrono::nanoseconds(5000000000));  // 5 seconds
    EXPECT_EQ(ts.seconds(), 5);
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(TimestampTest, EqualityOperator) {
    Timestamp ts1(std::chrono::nanoseconds(1000));
    Timestamp ts2(std::chrono::nanoseconds(1000));
    Timestamp ts3(std::chrono::nanoseconds(2000));

    EXPECT_TRUE(ts1 == ts2);
    EXPECT_FALSE(ts1 == ts3);
}

TEST_F(TimestampTest, LessThanOperator) {
    Timestamp ts1(std::chrono::nanoseconds(1000));
    Timestamp ts2(std::chrono::nanoseconds(2000));

    EXPECT_TRUE(ts1 < ts2);
    EXPECT_FALSE(ts2 < ts1);
    EXPECT_FALSE(ts1 < ts1);
}

TEST_F(TimestampTest, LessThanOrEqualOperator) {
    Timestamp ts1(std::chrono::nanoseconds(1000));
    Timestamp ts2(std::chrono::nanoseconds(2000));
    Timestamp ts3(std::chrono::nanoseconds(1000));

    EXPECT_TRUE(ts1 <= ts2);
    EXPECT_TRUE(ts1 <= ts3);
    EXPECT_FALSE(ts2 <= ts1);
}

TEST_F(TimestampTest, GreaterThanOperator) {
    Timestamp ts1(std::chrono::nanoseconds(2000));
    Timestamp ts2(std::chrono::nanoseconds(1000));

    EXPECT_TRUE(ts1 > ts2);
    EXPECT_FALSE(ts2 > ts1);
    EXPECT_FALSE(ts1 > ts1);
}

TEST_F(TimestampTest, GreaterThanOrEqualOperator) {
    Timestamp ts1(std::chrono::nanoseconds(2000));
    Timestamp ts2(std::chrono::nanoseconds(1000));
    Timestamp ts3(std::chrono::nanoseconds(2000));

    EXPECT_TRUE(ts1 >= ts2);
    EXPECT_TRUE(ts1 >= ts3);
    EXPECT_FALSE(ts2 >= ts1);
}

// ============================================================================
// Arithmetic Tests
// ============================================================================

TEST_F(TimestampTest, AdditionOperator) {
    Timestamp ts(std::chrono::nanoseconds(1000));
    Timestamp result = ts + std::chrono::nanoseconds(500);

    EXPECT_EQ(result.nanoseconds(), 1500);
}

TEST_F(TimestampTest, SubtractionOperator) {
    Timestamp ts1(std::chrono::nanoseconds(2000));
    Timestamp ts2(std::chrono::nanoseconds(500));

    auto diff = ts1 - ts2;
    EXPECT_EQ(diff.count(), 1500);
}

TEST_F(TimestampTest, SubtractionWithChronoDuration) {
    Timestamp ts1(std::chrono::nanoseconds(5000000));  // 5ms
    Timestamp ts2(std::chrono::nanoseconds(2000000));  // 2ms

    auto diff = ts1 - ts2;
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(diff).count(), 3);
}

// ============================================================================
// Constexpr Tests
// ============================================================================

TEST_F(TimestampTest, ConstexprConstruction) {
    constexpr Timestamp ts;
    EXPECT_EQ(ts.nanoseconds(), 0);
}

TEST_F(TimestampTest, ConstexprComparison) {
    constexpr Timestamp ts1;
    constexpr Timestamp ts2;
    constexpr bool are_equal = (ts1 == ts2);
    EXPECT_TRUE(are_equal);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(TimestampTest, NowPerformance) {
    const size_t iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        volatile auto ts = Timestamp::now();
        (void)ts;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    // Should be fast - less than 1 microsecond per call
    EXPECT_LT(ns_per_op, 1000);

    std::cout << "Timestamp::now() performance: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(TimestampTest, ComparisonPerformance) {
    const size_t iterations = 1000000;
    Timestamp ts1 = Timestamp::now();
    Timestamp ts2 = ts1 + std::chrono::nanoseconds(1);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        volatile bool result = ts1 < ts2;
        (void)result;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    // Comparison should be extremely fast
    EXPECT_LT(ns_per_op, 50);

    std::cout << "Timestamp comparison performance: " << ns_per_op << " ns/op" << std::endl;
}

// ============================================================================
// Sorting Tests
// ============================================================================

TEST_F(TimestampTest, Sortable) {
    std::vector<Timestamp> timestamps;
    for (int i = 0; i < 100; ++i) {
        timestamps.push_back(Timestamp(std::chrono::nanoseconds(rand() % 1000000)));
    }

    std::sort(timestamps.begin(), timestamps.end());

    for (size_t i = 1; i < timestamps.size(); ++i) {
        EXPECT_LE(timestamps[i-1], timestamps[i]);
    }
}

// ============================================================================
// Stream Output Tests
// ============================================================================

TEST_F(TimestampTest, StreamOutput) {
    Timestamp ts(std::chrono::nanoseconds(1234567890));

    std::ostringstream oss;
    oss << ts;

    std::string output = oss.str();
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("ns"), std::string::npos);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
