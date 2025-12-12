/**
 * @file test_data_point.cpp
 * @brief Comprehensive unit tests for ipb::common data types (v1.5.0 API)
 *
 * Tests cover: Timestamp, Value, Quality, DataPoint, RawMessage
 */

#include <gtest/gtest.h>
#include <ipb/common/data_point.hpp>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <cstring>

using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// Timestamp Tests
// ============================================================================

class TimestampTest : public ::testing::Test {};

TEST_F(TimestampTest, DefaultConstruction) {
    Timestamp ts;
    EXPECT_EQ(ts.nanoseconds(), 0);
    EXPECT_EQ(ts.microseconds(), 0);
    EXPECT_EQ(ts.milliseconds(), 0);
    EXPECT_EQ(ts.seconds(), 0);
}

TEST_F(TimestampTest, ConstructFromDuration) {
    Timestamp ts(1000000ns);
    EXPECT_EQ(ts.nanoseconds(), 1000000);
    EXPECT_EQ(ts.microseconds(), 1000);
    EXPECT_EQ(ts.milliseconds(), 1);
}

TEST_F(TimestampTest, Now) {
    auto before = std::chrono::steady_clock::now();
    Timestamp ts = Timestamp::now();
    auto after = std::chrono::steady_clock::now();

    auto before_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(before.time_since_epoch()).count();
    auto after_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(after.time_since_epoch()).count();

    EXPECT_GE(ts.nanoseconds(), before_ns);
    EXPECT_LE(ts.nanoseconds(), after_ns);
}

TEST_F(TimestampTest, FromSystemTime) {
    Timestamp ts = Timestamp::from_system_time();
    EXPECT_GT(ts.nanoseconds(), 0);
}

TEST_F(TimestampTest, Comparison) {
    Timestamp ts1(1000ns);
    Timestamp ts2(2000ns);
    Timestamp ts3(1000ns);

    EXPECT_TRUE(ts1 == ts3);
    EXPECT_FALSE(ts1 == ts2);
    EXPECT_TRUE(ts1 < ts2);
    EXPECT_TRUE(ts1 <= ts2);
    EXPECT_TRUE(ts1 <= ts3);
    EXPECT_TRUE(ts2 > ts1);
    EXPECT_TRUE(ts2 >= ts1);
    EXPECT_TRUE(ts1 >= ts3);
}

TEST_F(TimestampTest, Arithmetic) {
    Timestamp ts1(1000ns);
    Timestamp ts2 = ts1 + 500ns;

    EXPECT_EQ(ts2.nanoseconds(), 1500);

    auto diff = ts2 - ts1;
    EXPECT_EQ(diff.count(), 500);
}

// ============================================================================
// Value Tests
// ============================================================================

class ValueTest : public ::testing::Test {};

TEST_F(ValueTest, DefaultConstruction) {
    Value v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.type(), Value::Type::EMPTY);
    EXPECT_EQ(v.size(), 0);
}

TEST_F(ValueTest, BoolValue) {
    Value v;
    v.set(true);
    EXPECT_EQ(v.type(), Value::Type::BOOL);
    EXPECT_EQ(v.get<bool>(), true);

    Value v2;
    v2.set(false);
    EXPECT_EQ(v2.get<bool>(), false);
}

TEST_F(ValueTest, IntegerValues) {
    {
        Value v;
        v.set(static_cast<int8_t>(-42));
        EXPECT_EQ(v.type(), Value::Type::INT8);
        EXPECT_EQ(v.get<int8_t>(), -42);
    }
    {
        Value v;
        v.set(static_cast<int16_t>(-1000));
        EXPECT_EQ(v.type(), Value::Type::INT16);
        EXPECT_EQ(v.get<int16_t>(), -1000);
    }
    {
        Value v;
        v.set(static_cast<int32_t>(-100000));
        EXPECT_EQ(v.type(), Value::Type::INT32);
        EXPECT_EQ(v.get<int32_t>(), -100000);
    }
    {
        Value v;
        v.set(static_cast<int64_t>(-10000000000LL));
        EXPECT_EQ(v.type(), Value::Type::INT64);
        EXPECT_EQ(v.get<int64_t>(), -10000000000LL);
    }
}

TEST_F(ValueTest, UnsignedIntegerValues) {
    {
        Value v;
        v.set(static_cast<uint8_t>(200));
        EXPECT_EQ(v.type(), Value::Type::UINT8);
        EXPECT_EQ(v.get<uint8_t>(), 200);
    }
    {
        Value v;
        v.set(static_cast<uint16_t>(50000));
        EXPECT_EQ(v.type(), Value::Type::UINT16);
        EXPECT_EQ(v.get<uint16_t>(), 50000);
    }
    {
        Value v;
        v.set(static_cast<uint32_t>(3000000000U));
        EXPECT_EQ(v.type(), Value::Type::UINT32);
        EXPECT_EQ(v.get<uint32_t>(), 3000000000U);
    }
    {
        Value v;
        v.set(static_cast<uint64_t>(10000000000000ULL));
        EXPECT_EQ(v.type(), Value::Type::UINT64);
        EXPECT_EQ(v.get<uint64_t>(), 10000000000000ULL);
    }
}

TEST_F(ValueTest, FloatValues) {
    {
        Value v;
        v.set(3.14f);
        EXPECT_EQ(v.type(), Value::Type::FLOAT32);
        EXPECT_FLOAT_EQ(v.get<float>(), 3.14f);
    }
    {
        Value v;
        v.set(3.14159265359);
        EXPECT_EQ(v.type(), Value::Type::FLOAT64);
        EXPECT_DOUBLE_EQ(v.get<double>(), 3.14159265359);
    }
}

TEST_F(ValueTest, StringValue) {
    Value v;
    v.set_string_view("Hello, World!");

    EXPECT_EQ(v.type(), Value::Type::STRING);
    EXPECT_EQ(v.as_string_view(), "Hello, World!");
}

TEST_F(ValueTest, LongStringValue) {
    // Test string longer than inline storage
    std::string long_str(100, 'x');
    Value v;
    v.set_string_view(long_str);

    EXPECT_EQ(v.type(), Value::Type::STRING);
    EXPECT_EQ(v.as_string_view(), long_str);
}

TEST_F(ValueTest, BinaryValue) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    Value v;
    v.set_binary(data);

    EXPECT_EQ(v.type(), Value::Type::BINARY);
    auto binary = v.as_binary();
    EXPECT_EQ(binary.size(), 5);
    EXPECT_EQ(binary[0], 0x01);
    EXPECT_EQ(binary[4], 0x05);
}

TEST_F(ValueTest, LargeBinaryValue) {
    std::vector<uint8_t> data(100, 0xAB);
    Value v;
    v.set_binary(data);

    EXPECT_EQ(v.type(), Value::Type::BINARY);
    auto binary = v.as_binary();
    EXPECT_EQ(binary.size(), 100);
}

TEST_F(ValueTest, CopyConstruction) {
    Value v1;
    v1.set(42.5);

    Value v2(v1);

    EXPECT_EQ(v2.type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(v2.get<double>(), 42.5);
}

TEST_F(ValueTest, MoveConstruction) {
    Value v1;
    v1.set(42.5);

    Value v2(std::move(v1));

    EXPECT_EQ(v2.type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(v2.get<double>(), 42.5);
}

TEST_F(ValueTest, CopyAssignment) {
    Value v1, v2;
    v1.set(42.5);
    v2.set(static_cast<int32_t>(100));

    v2 = v1;

    EXPECT_EQ(v2.type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(v2.get<double>(), 42.5);
}

TEST_F(ValueTest, MoveAssignment) {
    Value v1, v2;
    v1.set(42.5);
    v2.set(static_cast<int32_t>(100));

    v2 = std::move(v1);

    EXPECT_EQ(v2.type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(v2.get<double>(), 42.5);
}

TEST_F(ValueTest, Equality) {
    Value v1, v2, v3;
    v1.set(42.5);
    v2.set(42.5);
    v3.set(100.0);

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
    EXPECT_TRUE(v1 != v3);
}

TEST_F(ValueTest, EmptyEquality) {
    Value v1, v2;
    EXPECT_TRUE(v1 == v2);
}

TEST_F(ValueTest, TypeMismatchEquality) {
    Value v1, v2;
    v1.set(static_cast<int32_t>(42));
    v2.set(42.0);

    EXPECT_FALSE(v1 == v2);  // Different types
}

// ============================================================================
// Quality Tests
// ============================================================================

class QualityTest : public ::testing::Test {};

TEST_F(QualityTest, QualityValues) {
    EXPECT_EQ(static_cast<uint8_t>(Quality::GOOD), 0);
    EXPECT_EQ(static_cast<uint8_t>(Quality::UNCERTAIN), 1);
    EXPECT_EQ(static_cast<uint8_t>(Quality::BAD), 2);
    EXPECT_EQ(static_cast<uint8_t>(Quality::STALE), 3);
    EXPECT_EQ(static_cast<uint8_t>(Quality::COMM_FAILURE), 4);
    EXPECT_EQ(static_cast<uint8_t>(Quality::CONFIG_ERROR), 5);
    EXPECT_EQ(static_cast<uint8_t>(Quality::NOT_CONNECTED), 6);
    EXPECT_EQ(static_cast<uint8_t>(Quality::DEVICE_FAILURE), 7);
    EXPECT_EQ(static_cast<uint8_t>(Quality::SENSOR_FAILURE), 8);
    EXPECT_EQ(static_cast<uint8_t>(Quality::LAST_KNOWN), 9);
    EXPECT_EQ(static_cast<uint8_t>(Quality::INITIAL), 10);
    EXPECT_EQ(static_cast<uint8_t>(Quality::FORCED), 11);
}

// ============================================================================
// DataPoint Tests
// ============================================================================

class DataPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_address = "test.device.register.001";
        test_protocol_id = 1;
    }

    std::string test_address;
    uint16_t test_protocol_id;
};

TEST_F(DataPointTest, DefaultConstruction) {
    DataPoint dp;

    EXPECT_EQ(dp.address(), "N/A");
    EXPECT_EQ(dp.protocol_id(), 0);
    EXPECT_EQ(dp.quality(), Quality::INITIAL);
    EXPECT_TRUE(dp.value().empty());
    EXPECT_EQ(dp.sequence_number(), 0);
}

TEST_F(DataPointTest, ConstructWithAddress) {
    DataPoint dp(test_address);

    EXPECT_EQ(dp.address(), test_address);
    EXPECT_EQ(dp.protocol_id(), 0);
    EXPECT_EQ(dp.quality(), Quality::INITIAL);
}

TEST_F(DataPointTest, ConstructWithAddressAndValue) {
    Value v;
    v.set(42.5);

    DataPoint dp(test_address, v, test_protocol_id);

    EXPECT_EQ(dp.address(), test_address);
    EXPECT_EQ(dp.protocol_id(), test_protocol_id);
    EXPECT_EQ(dp.quality(), Quality::GOOD);
    EXPECT_DOUBLE_EQ(dp.value().get<double>(), 42.5);
}

TEST_F(DataPointTest, ValueTypes) {
    // Bool
    {
        Value v;
        v.set(true);
        DataPoint dp(test_address, v, test_protocol_id);
        EXPECT_EQ(dp.value().type(), Value::Type::BOOL);
        EXPECT_TRUE(dp.value().get<bool>());
    }

    // Integer
    {
        Value v;
        v.set(static_cast<int64_t>(123));
        DataPoint dp(test_address, v, test_protocol_id);
        EXPECT_EQ(dp.value().type(), Value::Type::INT64);
        EXPECT_EQ(dp.value().get<int64_t>(), 123);
    }

    // Double
    {
        Value v;
        v.set(3.14159);
        DataPoint dp(test_address, v, test_protocol_id);
        EXPECT_EQ(dp.value().type(), Value::Type::FLOAT64);
        EXPECT_DOUBLE_EQ(dp.value().get<double>(), 3.14159);
    }

    // String
    {
        Value v;
        v.set_string_view("test_string");
        DataPoint dp(test_address, v, test_protocol_id);
        EXPECT_EQ(dp.value().type(), Value::Type::STRING);
        EXPECT_EQ(dp.value().as_string_view(), "test_string");
    }

    // Binary
    {
        std::vector<uint8_t> blob = {0x01, 0x02, 0x03, 0x04};
        Value v;
        v.set_binary(blob);
        DataPoint dp(test_address, v, test_protocol_id);
        EXPECT_EQ(dp.value().type(), Value::Type::BINARY);
        EXPECT_EQ(dp.value().as_binary().size(), 4);
    }
}

TEST_F(DataPointTest, QualityLevels) {
    std::vector<Quality> qualities = {
        Quality::GOOD,
        Quality::UNCERTAIN,
        Quality::BAD,
        Quality::STALE,
        Quality::COMM_FAILURE,
        Quality::INITIAL
    };

    for (auto quality : qualities) {
        Value v;
        v.set(static_cast<int32_t>(100));
        DataPoint dp(test_address, v, test_protocol_id);
        dp.set_quality(quality);
        EXPECT_EQ(dp.quality(), quality);
    }
}

TEST_F(DataPointTest, CopyConstructor) {
    Value v;
    v.set(42.5);
    DataPoint original(test_address, v, test_protocol_id);
    original.set_quality(Quality::GOOD);

    DataPoint copy(original);

    EXPECT_EQ(copy.address(), original.address());
    EXPECT_EQ(copy.protocol_id(), original.protocol_id());
    EXPECT_EQ(copy.quality(), original.quality());
    EXPECT_DOUBLE_EQ(copy.value().get<double>(), original.value().get<double>());
}

TEST_F(DataPointTest, MoveConstructor) {
    Value v;
    v.set(42.5);
    DataPoint original(test_address, v, test_protocol_id);
    original.set_quality(Quality::GOOD);

    std::string original_address(original.address());
    uint16_t original_protocol_id = original.protocol_id();
    Quality original_quality = original.quality();

    DataPoint moved(std::move(original));

    EXPECT_EQ(moved.address(), original_address);
    EXPECT_EQ(moved.protocol_id(), original_protocol_id);
    EXPECT_EQ(moved.quality(), original_quality);
    EXPECT_DOUBLE_EQ(moved.value().get<double>(), 42.5);
}

TEST_F(DataPointTest, AssignmentOperator) {
    Value v1, v2;
    v1.set(42.5);
    v2.set(static_cast<int32_t>(100));

    DataPoint dp1(test_address, v1, test_protocol_id);
    dp1.set_quality(Quality::GOOD);

    DataPoint dp2("other.address", v2, 2);
    dp2.set_quality(Quality::BAD);

    dp2 = dp1;

    EXPECT_EQ(dp2.address(), dp1.address());
    EXPECT_EQ(dp2.protocol_id(), dp1.protocol_id());
    EXPECT_EQ(dp2.quality(), dp1.quality());
}

TEST_F(DataPointTest, Setters) {
    DataPoint dp;

    dp.set_address(test_address);
    EXPECT_EQ(dp.address(), test_address);

    dp.set_protocol_id(test_protocol_id);
    EXPECT_EQ(dp.protocol_id(), test_protocol_id);

    dp.set_value(123.456);
    EXPECT_DOUBLE_EQ(dp.value().get<double>(), 123.456);

    auto ts = Timestamp::now();
    dp.set_timestamp(ts);
    EXPECT_EQ(dp.timestamp(), ts);

    dp.set_quality(Quality::UNCERTAIN);
    EXPECT_EQ(dp.quality(), Quality::UNCERTAIN);

    dp.set_sequence_number(42);
    EXPECT_EQ(dp.sequence_number(), 42);
}

TEST_F(DataPointTest, Validation) {
    // Valid with GOOD quality
    {
        Value v;
        v.set(static_cast<int32_t>(42));
        DataPoint dp(test_address, v, test_protocol_id);
        dp.set_quality(Quality::GOOD);
        EXPECT_TRUE(dp.is_valid());
    }

    // Valid with UNCERTAIN quality
    {
        Value v;
        v.set(static_cast<int32_t>(42));
        DataPoint dp(test_address, v, test_protocol_id);
        dp.set_quality(Quality::UNCERTAIN);
        EXPECT_TRUE(dp.is_valid());
    }

    // Invalid with BAD quality
    {
        Value v;
        v.set(static_cast<int32_t>(42));
        DataPoint dp(test_address, v, test_protocol_id);
        dp.set_quality(Quality::BAD);
        EXPECT_FALSE(dp.is_valid());
    }

    // Invalid with COMM_FAILURE quality
    {
        Value v;
        v.set(static_cast<int32_t>(42));
        DataPoint dp(test_address, v, test_protocol_id);
        dp.set_quality(Quality::COMM_FAILURE);
        EXPECT_FALSE(dp.is_valid());
    }
}

TEST_F(DataPointTest, StaleCheck) {
    Value v;
    v.set(static_cast<int32_t>(42));
    DataPoint dp(test_address, v, test_protocol_id);

    auto now = Timestamp::now();

    // Not stale - just created
    EXPECT_FALSE(dp.is_stale(now, 1s));

    // Wait a bit
    std::this_thread::sleep_for(10ms);
    auto later = Timestamp::now();

    // Should be stale with 1ms max age
    EXPECT_TRUE(dp.is_stale(later, 1ms));

    // Should not be stale with 1s max age
    EXPECT_FALSE(dp.is_stale(later, 1s));
}

TEST_F(DataPointTest, LongAddress) {
    // Test address longer than MAX_INLINE_ADDRESS (32 bytes)
    std::string long_address(100, 'x');

    Value v;
    v.set(static_cast<int32_t>(42));
    DataPoint dp(long_address, v, test_protocol_id);

    EXPECT_EQ(dp.address(), long_address);
}

TEST_F(DataPointTest, Equality) {
    Value v;
    v.set(static_cast<int32_t>(42));

    DataPoint dp1(test_address, v, test_protocol_id);
    DataPoint dp2(test_address, v, test_protocol_id);
    DataPoint dp3("different.address", v, test_protocol_id);
    DataPoint dp4(test_address, v, 99);

    EXPECT_TRUE(dp1 == dp2);
    EXPECT_FALSE(dp1 == dp3);  // Different address
    EXPECT_FALSE(dp1 == dp4);  // Different protocol_id
}

TEST_F(DataPointTest, BackwardCompatibleAccessors) {
    Value v;
    v.set(42.5);
    DataPoint dp(test_address, v, test_protocol_id);
    dp.set_quality(Quality::GOOD);

    // Test backward-compatible accessors
    EXPECT_EQ(dp.get_address(), test_address);
    EXPECT_EQ(dp.get_protocol_id(), test_protocol_id);
    EXPECT_EQ(dp.get_quality(), Quality::GOOD);

    // get_value() returns OptionalValueWrapper
    auto val_wrapper = dp.get_value();
    EXPECT_TRUE(val_wrapper.has_value());
    EXPECT_DOUBLE_EQ(val_wrapper.value().get<double>(), 42.5);
}

TEST_F(DataPointTest, Performance) {
    const size_t num_iterations = 100000;

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < num_iterations; ++i) {
        Value v;
        v.set(static_cast<double>(i));
        DataPoint dp(test_address, v, test_protocol_id);
        // Prevent optimization
        volatile auto addr = dp.address().data();
        (void)addr;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    // Should be able to create at least 100k DataPoints per second
    EXPECT_LT(duration.count() / num_iterations, 10000);  // Less than 10μs per construction

    std::cout << "DataPoint construction: "
              << (duration.count() / num_iterations) << " ns/op" << std::endl;
}

TEST_F(DataPointTest, ThreadSafety) {
    const size_t num_threads = 4;
    const size_t operations_per_thread = 10000;

    std::vector<std::thread> threads;
    std::atomic<size_t> success_count{0};

    for (size_t t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (size_t i = 0; i < operations_per_thread; ++i) {
                try {
                    std::string addr = test_address + "." + std::to_string(t) + "." + std::to_string(i);
                    Value v;
                    v.set(static_cast<double>(i));
                    DataPoint dp(addr, v, test_protocol_id);
                    dp.set_quality(Quality::GOOD);

                    if (dp.is_valid()) {
                        success_count.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    FAIL() << "DataPoint construction threw exception";
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), num_threads * operations_per_thread);
}

TEST_F(DataPointTest, MemoryUsage) {
    const size_t num_datapoints = 1000;
    std::vector<DataPoint> datapoints;
    datapoints.reserve(num_datapoints);

    for (size_t i = 0; i < num_datapoints; ++i) {
        Value v;
        v.set(static_cast<double>(i));
        datapoints.emplace_back(
            test_address + std::to_string(i),
            v,
            test_protocol_id
        );
        datapoints.back().set_quality(Quality::GOOD);
    }

    // Basic check that we can create many DataPoints without issues
    EXPECT_EQ(datapoints.size(), num_datapoints);

    // Verify all are valid
    for (const auto& dp : datapoints) {
        EXPECT_TRUE(dp.is_valid());
    }
}

// ============================================================================
// RawMessage Tests
// ============================================================================

class RawMessageTest : public ::testing::Test {};

TEST_F(RawMessageTest, DefaultConstruction) {
    RawMessage msg;
    EXPECT_TRUE(msg.empty());
    EXPECT_EQ(msg.size(), 0);
    EXPECT_EQ(msg.protocol_id(), 0);
}

TEST_F(RawMessageTest, ConstructWithSpan) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    RawMessage msg{std::span<const uint8_t>(data)};

    EXPECT_FALSE(msg.empty());
    EXPECT_EQ(msg.size(), 4);
    EXPECT_FALSE(msg.owns_data());
}

TEST_F(RawMessageTest, ConstructWithOwnedData) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    RawMessage msg(std::move(data));

    EXPECT_FALSE(msg.empty());
    EXPECT_EQ(msg.size(), 4);
    EXPECT_TRUE(msg.owns_data());
}

TEST_F(RawMessageTest, MoveConstruction) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    RawMessage msg1(std::move(data));
    msg1.set_protocol_id(42);

    RawMessage msg2(std::move(msg1));

    EXPECT_EQ(msg2.size(), 4);
    EXPECT_EQ(msg2.protocol_id(), 42);
    EXPECT_TRUE(msg2.owns_data());
}

TEST_F(RawMessageTest, Metadata) {
    std::vector<uint8_t> data = {0x01};
    RawMessage msg(std::move(data));

    msg.set_protocol_id(123);
    EXPECT_EQ(msg.protocol_id(), 123);

    auto ts = Timestamp::now();
    msg.set_timestamp(ts);
    EXPECT_EQ(msg.timestamp(), ts);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
