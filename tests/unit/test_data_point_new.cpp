/**
 * @file test_data_point_new.cpp
 * @brief Comprehensive unit tests for ipb::common::DataPoint
 *
 * Tests aligned with the current API (v1.5.0)
 */

#include <gtest/gtest.h>
#include <ipb/common/data_point.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>
#include <unordered_set>

using namespace ipb::common;
using namespace std::chrono_literals;

class DataPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_address = "sensors/temperature/zone1";
        test_protocol_id = 1;
    }

    std::string test_address;
    uint16_t test_protocol_id;
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(DataPointTest, DefaultConstruction) {
    DataPoint dp;

    EXPECT_EQ(dp.address(), "N/A");
    EXPECT_EQ(dp.protocol_id(), 0);
    EXPECT_EQ(dp.quality(), Quality::INITIAL);
    EXPECT_EQ(dp.sequence_number(), 0);
    EXPECT_TRUE(dp.value().empty());
}

TEST_F(DataPointTest, ConstructWithAddress) {
    DataPoint dp(test_address);

    EXPECT_EQ(dp.address(), test_address);
    EXPECT_EQ(dp.protocol_id(), 0);
    EXPECT_EQ(dp.quality(), Quality::INITIAL);
}

TEST_F(DataPointTest, ConstructWithAddressAndValue) {
    Value v;
    v.set(25.5);

    DataPoint dp(test_address, v, test_protocol_id);

    EXPECT_EQ(dp.address(), test_address);
    EXPECT_EQ(dp.protocol_id(), test_protocol_id);
    EXPECT_EQ(dp.quality(), Quality::GOOD);
    EXPECT_DOUBLE_EQ(dp.value().get<double>(), 25.5);
}

TEST_F(DataPointTest, CopyConstruction) {
    Value v;
    v.set(42.0);
    DataPoint original(test_address, v, test_protocol_id);
    original.set_quality(Quality::GOOD);

    DataPoint copy(original);

    EXPECT_EQ(copy.address(), original.address());
    EXPECT_EQ(copy.protocol_id(), original.protocol_id());
    EXPECT_EQ(copy.quality(), original.quality());
    EXPECT_EQ(copy.value().type(), original.value().type());
}

TEST_F(DataPointTest, MoveConstruction) {
    Value v;
    v.set(42.0);
    DataPoint original(test_address, v, test_protocol_id);

    std::string_view orig_address = original.address();

    DataPoint moved(std::move(original));

    EXPECT_EQ(moved.address(), orig_address);
}

// ============================================================================
// Address Tests
// ============================================================================

TEST_F(DataPointTest, SetAddressInline) {
    DataPoint dp;
    std::string short_addr = "short";  // Less than MAX_INLINE_ADDRESS

    dp.set_address(short_addr);

    EXPECT_EQ(dp.address(), short_addr);
}

TEST_F(DataPointTest, SetAddressExternal) {
    DataPoint dp;
    std::string long_addr(100, 'X');  // Larger than MAX_INLINE_ADDRESS

    dp.set_address(long_addr);

    EXPECT_EQ(dp.address(), long_addr);
}

TEST_F(DataPointTest, AddressAtBoundary) {
    DataPoint dp;
    std::string boundary_addr(DataPoint::MAX_INLINE_ADDRESS, 'A');

    dp.set_address(boundary_addr);

    EXPECT_EQ(dp.address(), boundary_addr);
}

// ============================================================================
// Value Tests
// ============================================================================

TEST_F(DataPointTest, SetValueTemplate) {
    DataPoint dp(test_address);

    dp.set_value(25.5);

    EXPECT_EQ(dp.value().type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(dp.value().get<double>(), 25.5);
    EXPECT_EQ(dp.quality(), Quality::GOOD);
}

TEST_F(DataPointTest, SetValueObject) {
    DataPoint dp(test_address);
    Value v;
    v.set(int32_t(100));

    dp.set_value(v);

    EXPECT_EQ(dp.value().get<int32_t>(), 100);
}

TEST_F(DataPointTest, SetValueUpdatesTimestamp) {
    DataPoint dp(test_address);
    Timestamp ts1 = dp.timestamp();

    std::this_thread::sleep_for(1ms);
    dp.set_value(42.0);

    EXPECT_GT(dp.timestamp().nanoseconds(), ts1.nanoseconds());
}

// ============================================================================
// Metadata Tests
// ============================================================================

TEST_F(DataPointTest, SetGetTimestamp) {
    DataPoint dp(test_address);
    Timestamp ts(std::chrono::nanoseconds(1234567890));

    dp.set_timestamp(ts);

    EXPECT_EQ(dp.timestamp().nanoseconds(), 1234567890);
}

TEST_F(DataPointTest, SetGetProtocolId) {
    DataPoint dp(test_address);

    dp.set_protocol_id(42);

    EXPECT_EQ(dp.protocol_id(), 42);
}

TEST_F(DataPointTest, SetGetQuality) {
    DataPoint dp(test_address);

    std::vector<Quality> qualities = {
        Quality::GOOD,
        Quality::UNCERTAIN,
        Quality::BAD,
        Quality::STALE,
        Quality::COMM_FAILURE,
        Quality::CONFIG_ERROR,
        Quality::NOT_CONNECTED,
        Quality::DEVICE_FAILURE,
        Quality::SENSOR_FAILURE,
        Quality::LAST_KNOWN,
        Quality::INITIAL,
        Quality::FORCED
    };

    for (auto q : qualities) {
        dp.set_quality(q);
        EXPECT_EQ(dp.quality(), q);
    }
}

TEST_F(DataPointTest, SetGetSequenceNumber) {
    DataPoint dp(test_address);

    dp.set_sequence_number(12345);

    EXPECT_EQ(dp.sequence_number(), 12345);
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST_F(DataPointTest, BackwardCompatGetAddress) {
    DataPoint dp(test_address);
    EXPECT_EQ(dp.get_address(), dp.address());
}

TEST_F(DataPointTest, BackwardCompatGetTimestamp) {
    DataPoint dp(test_address);
    EXPECT_EQ(dp.get_timestamp().nanoseconds(), dp.timestamp().nanoseconds());
}

TEST_F(DataPointTest, BackwardCompatGetProtocolId) {
    DataPoint dp(test_address);
    dp.set_protocol_id(5);
    EXPECT_EQ(dp.get_protocol_id(), dp.protocol_id());
}

TEST_F(DataPointTest, BackwardCompatGetQuality) {
    DataPoint dp(test_address);
    dp.set_quality(Quality::GOOD);
    EXPECT_EQ(dp.get_quality(), dp.quality());
}

TEST_F(DataPointTest, BackwardCompatGetValue) {
    DataPoint dp(test_address);
    dp.set_value(42.0);

    auto wrapper = dp.get_value();
    EXPECT_TRUE(wrapper.has_value());
    EXPECT_DOUBLE_EQ(wrapper.value().get<double>(), 42.0);
}

// ============================================================================
// Utility Method Tests
// ============================================================================

TEST_F(DataPointTest, IsValidGood) {
    DataPoint dp(test_address);
    dp.set_quality(Quality::GOOD);

    EXPECT_TRUE(dp.is_valid());
}

TEST_F(DataPointTest, IsValidUncertain) {
    DataPoint dp(test_address);
    dp.set_quality(Quality::UNCERTAIN);

    EXPECT_TRUE(dp.is_valid());
}

TEST_F(DataPointTest, IsValidBad) {
    DataPoint dp(test_address);
    dp.set_quality(Quality::BAD);

    EXPECT_FALSE(dp.is_valid());
}

TEST_F(DataPointTest, IsStale) {
    DataPoint dp(test_address);
    dp.set_timestamp(Timestamp(std::chrono::nanoseconds(0)));  // Epoch

    auto max_age = std::chrono::seconds(60);
    EXPECT_TRUE(dp.is_stale(Timestamp::now(), max_age));
}

TEST_F(DataPointTest, IsNotStale) {
    DataPoint dp(test_address);
    dp.set_timestamp(Timestamp::now());

    auto max_age = std::chrono::seconds(60);
    EXPECT_FALSE(dp.is_stale(Timestamp::now(), max_age));
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(DataPointTest, EqualitySameAddressAndProtocol) {
    Value v;
    v.set(42.0);

    DataPoint dp1(test_address, v, test_protocol_id);
    DataPoint dp2(test_address, v, test_protocol_id);

    EXPECT_TRUE(dp1 == dp2);
}

TEST_F(DataPointTest, EqualityDifferentAddress) {
    Value v;
    v.set(42.0);

    DataPoint dp1("address1", v, test_protocol_id);
    DataPoint dp2("address2", v, test_protocol_id);

    EXPECT_FALSE(dp1 == dp2);
}

TEST_F(DataPointTest, EqualityDifferentProtocol) {
    Value v;
    v.set(42.0);

    DataPoint dp1(test_address, v, 1);
    DataPoint dp2(test_address, v, 2);

    EXPECT_FALSE(dp1 == dp2);
}

// ============================================================================
// Hash Tests
// ============================================================================

TEST_F(DataPointTest, HashConsistency) {
    Value v;
    v.set(42.0);
    DataPoint dp(test_address, v, test_protocol_id);

    size_t hash1 = dp.hash();
    size_t hash2 = dp.hash();

    EXPECT_EQ(hash1, hash2);
}

TEST_F(DataPointTest, HashInUnorderedSet) {
    std::unordered_set<DataPoint> set;

    for (int i = 0; i < 100; ++i) {
        Value v;
        v.set(static_cast<double>(i));
        DataPoint dp(test_address + std::to_string(i), v, test_protocol_id);
        set.insert(dp);
    }

    EXPECT_EQ(set.size(), 100);
}

// ============================================================================
// Assignment Tests
// ============================================================================

TEST_F(DataPointTest, CopyAssignment) {
    Value v1, v2;
    v1.set(42.0);
    v2.set(100.0);

    DataPoint dp1(test_address, v1, 1);
    DataPoint dp2("other", v2, 2);

    dp2 = dp1;

    EXPECT_EQ(dp2.address(), dp1.address());
    EXPECT_EQ(dp2.protocol_id(), dp1.protocol_id());
}

TEST_F(DataPointTest, MoveAssignment) {
    Value v1, v2;
    v1.set(42.0);
    v2.set(100.0);

    DataPoint dp1(test_address, v1, 1);
    DataPoint dp2("other", v2, 2);

    dp2 = std::move(dp1);

    EXPECT_EQ(dp2.address(), test_address);
}

TEST_F(DataPointTest, SelfAssignment) {
    Value v;
    v.set(42.0);
    DataPoint dp(test_address, v, test_protocol_id);

    dp = dp;

    EXPECT_EQ(dp.address(), test_address);
}

// ============================================================================
// Alignment Tests
// ============================================================================

TEST_F(DataPointTest, CacheLineAlignment) {
    // DataPoint should be 64-byte aligned for cache efficiency
    EXPECT_EQ(alignof(DataPoint), 64);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(DataPointTest, ConstructionPerformance) {
    const size_t iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        DataPoint dp(test_address);
        dp.set_value(static_cast<double>(i));
        volatile auto addr = dp.address().data();
        (void)addr;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    // Should be less than 1 microsecond per construction
    EXPECT_LT(ns_per_op, 1000);

    std::cout << "DataPoint construction: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(DataPointTest, CopyPerformance) {
    const size_t iterations = 100000;
    Value v;
    v.set(42.0);
    DataPoint original(test_address, v, test_protocol_id);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        DataPoint copy(original);
        volatile auto addr = copy.address().data();
        (void)addr;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 2000);

    std::cout << "DataPoint copy: " << ns_per_op << " ns/op" << std::endl;
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(DataPointTest, ConcurrentConstruction) {
    const size_t num_threads = 4;
    const size_t ops_per_thread = 10000;

    std::vector<std::thread> threads;
    std::atomic<size_t> success_count{0};

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (size_t i = 0; i < ops_per_thread; ++i) {
                try {
                    std::string addr = test_address + "." + std::to_string(t) + "." + std::to_string(i);
                    DataPoint dp(addr);
                    dp.set_value(static_cast<double>(i));

                    if (dp.is_valid() || dp.quality() == Quality::GOOD) {
                        success_count.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    FAIL() << "Exception in thread";
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * ops_per_thread);
}

// ============================================================================
// Memory Tests
// ============================================================================

TEST_F(DataPointTest, ManyDataPoints) {
    const size_t count = 10000;
    std::vector<DataPoint> datapoints;
    datapoints.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        DataPoint dp(test_address + std::to_string(i));
        dp.set_value(static_cast<double>(i));
        datapoints.push_back(std::move(dp));
    }

    EXPECT_EQ(datapoints.size(), count);

    // Verify all are accessible
    for (size_t i = 0; i < count; ++i) {
        EXPECT_DOUBLE_EQ(datapoints[i].value().get<double>(), static_cast<double>(i));
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
