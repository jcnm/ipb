#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ipb/common/data_point.hpp"
#include <chrono>
#include <thread>

using namespace ipb::common;
using namespace std::chrono_literals;

class DataPointTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup common test data
        test_timestamp = std::chrono::system_clock::now();
        test_address = "test.device.register.001";
        test_protocol_id = 1;
    }

    Timestamp test_timestamp;
    std::string test_address;
    uint16_t test_protocol_id;
};

TEST_F(DataPointTest, DefaultConstruction) {
    DataPoint dp;
    
    EXPECT_EQ(dp.get_address(), "");
    EXPECT_EQ(dp.get_protocol_id(), 0);
    EXPECT_EQ(dp.get_quality(), Quality::UNKNOWN);
    EXPECT_FALSE(dp.get_value().has_value());
}

TEST_F(DataPointTest, ParameterizedConstruction) {
    Value test_value = 42.5;
    
    DataPoint dp(test_address, test_protocol_id, test_value, test_timestamp, Quality::GOOD);
    
    EXPECT_EQ(dp.get_address(), test_address);
    EXPECT_EQ(dp.get_protocol_id(), test_protocol_id);
    EXPECT_EQ(dp.get_quality(), Quality::GOOD);
    EXPECT_TRUE(dp.get_value().has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(dp.get_value().value()), 42.5);
    EXPECT_EQ(dp.get_timestamp(), test_timestamp);
}

TEST_F(DataPointTest, ValueTypes) {
    // Test different value types
    {
        DataPoint dp_bool(test_address, test_protocol_id, Value{true}, test_timestamp);
        EXPECT_TRUE(std::holds_alternative<bool>(dp_bool.get_value().value()));
        EXPECT_TRUE(std::get<bool>(dp_bool.get_value().value()));
    }
    
    {
        DataPoint dp_int(test_address, test_protocol_id, Value{123}, test_timestamp);
        EXPECT_TRUE(std::holds_alternative<int64_t>(dp_int.get_value().value()));
        EXPECT_EQ(std::get<int64_t>(dp_int.get_value().value()), 123);
    }
    
    {
        DataPoint dp_double(test_address, test_protocol_id, Value{3.14159}, test_timestamp);
        EXPECT_TRUE(std::holds_alternative<double>(dp_double.get_value().value()));
        EXPECT_DOUBLE_EQ(std::get<double>(dp_double.get_value().value()), 3.14159);
    }
    
    {
        std::string test_str = "test_string";
        DataPoint dp_string(test_address, test_protocol_id, Value{test_str}, test_timestamp);
        EXPECT_TRUE(std::holds_alternative<std::string>(dp_string.get_value().value()));
        EXPECT_EQ(std::get<std::string>(dp_string.get_value().value()), test_str);
    }
    
    {
        std::vector<uint8_t> test_blob = {0x01, 0x02, 0x03, 0x04};
        DataPoint dp_blob(test_address, test_protocol_id, Value{test_blob}, test_timestamp);
        EXPECT_TRUE(std::holds_alternative<std::vector<uint8_t>>(dp_blob.get_value().value()));
        EXPECT_EQ(std::get<std::vector<uint8_t>>(dp_blob.get_value().value()), test_blob);
    }
}

TEST_F(DataPointTest, QualityLevels) {
    std::vector<Quality> qualities = {
        Quality::GOOD,
        Quality::UNCERTAIN,
        Quality::BAD,
        Quality::UNKNOWN
    };
    
    for (auto quality : qualities) {
        DataPoint dp(test_address, test_protocol_id, Value{100}, test_timestamp, quality);
        EXPECT_EQ(dp.get_quality(), quality);
    }
}

TEST_F(DataPointTest, CopyConstructor) {
    Value test_value = 42.5;
    DataPoint original(test_address, test_protocol_id, test_value, test_timestamp, Quality::GOOD);
    
    DataPoint copy(original);
    
    EXPECT_EQ(copy.get_address(), original.get_address());
    EXPECT_EQ(copy.get_protocol_id(), original.get_protocol_id());
    EXPECT_EQ(copy.get_quality(), original.get_quality());
    EXPECT_EQ(copy.get_timestamp(), original.get_timestamp());
    EXPECT_EQ(copy.get_value().has_value(), original.get_value().has_value());
    
    if (copy.get_value().has_value() && original.get_value().has_value()) {
        EXPECT_DOUBLE_EQ(
            std::get<double>(copy.get_value().value()),
            std::get<double>(original.get_value().value())
        );
    }
}

TEST_F(DataPointTest, MoveConstructor) {
    Value test_value = 42.5;
    DataPoint original(test_address, test_protocol_id, test_value, test_timestamp, Quality::GOOD);
    
    std::string original_address = original.get_address();
    uint16_t original_protocol_id = original.get_protocol_id();
    Quality original_quality = original.get_quality();
    Timestamp original_timestamp = original.get_timestamp();
    
    DataPoint moved(std::move(original));
    
    EXPECT_EQ(moved.get_address(), original_address);
    EXPECT_EQ(moved.get_protocol_id(), original_protocol_id);
    EXPECT_EQ(moved.get_quality(), original_quality);
    EXPECT_EQ(moved.get_timestamp(), original_timestamp);
    EXPECT_TRUE(moved.get_value().has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(moved.get_value().value()), 42.5);
}

TEST_F(DataPointTest, AssignmentOperator) {
    Value test_value1 = 42.5;
    Value test_value2 = 100;
    
    DataPoint dp1(test_address, test_protocol_id, test_value1, test_timestamp, Quality::GOOD);
    DataPoint dp2("other.address", 2, test_value2, test_timestamp + 1s, Quality::BAD);
    
    dp2 = dp1;
    
    EXPECT_EQ(dp2.get_address(), dp1.get_address());
    EXPECT_EQ(dp2.get_protocol_id(), dp1.get_protocol_id());
    EXPECT_EQ(dp2.get_quality(), dp1.get_quality());
    EXPECT_EQ(dp2.get_timestamp(), dp1.get_timestamp());
    EXPECT_EQ(dp2.get_value().has_value(), dp1.get_value().has_value());
}

TEST_F(DataPointTest, Setters) {
    DataPoint dp;
    
    dp.set_address(test_address);
    EXPECT_EQ(dp.get_address(), test_address);
    
    dp.set_protocol_id(test_protocol_id);
    EXPECT_EQ(dp.get_protocol_id(), test_protocol_id);
    
    Value test_value = 123.456;
    dp.set_value(test_value);
    EXPECT_TRUE(dp.get_value().has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(dp.get_value().value()), 123.456);
    
    dp.set_timestamp(test_timestamp);
    EXPECT_EQ(dp.get_timestamp(), test_timestamp);
    
    dp.set_quality(Quality::UNCERTAIN);
    EXPECT_EQ(dp.get_quality(), Quality::UNCERTAIN);
}

TEST_F(DataPointTest, Validation) {
    // Valid data point
    {
        DataPoint dp(test_address, test_protocol_id, Value{42}, test_timestamp, Quality::GOOD);
        EXPECT_TRUE(dp.is_valid());
    }
    
    // Invalid: empty address
    {
        DataPoint dp("", test_protocol_id, Value{42}, test_timestamp, Quality::GOOD);
        EXPECT_FALSE(dp.is_valid());
    }
    
    // Invalid: no value
    {
        DataPoint dp(test_address, test_protocol_id, std::nullopt, test_timestamp, Quality::GOOD);
        EXPECT_FALSE(dp.is_valid());
    }
    
    // Invalid: future timestamp (more than 1 second in the future)
    {
        auto future_time = std::chrono::system_clock::now() + 2s;
        DataPoint dp(test_address, test_protocol_id, Value{42}, future_time, Quality::GOOD);
        EXPECT_FALSE(dp.is_valid());
    }
}

TEST_F(DataPointTest, Serialization) {
    Value test_value = 42.5;
    DataPoint original(test_address, test_protocol_id, test_value, test_timestamp, Quality::GOOD);
    
    // Test JSON serialization
    std::string json = original.to_json();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find(test_address), std::string::npos);
    EXPECT_NE(json.find("42.5"), std::string::npos);
    
    // Test binary serialization
    auto binary = original.to_binary();
    EXPECT_FALSE(binary.empty());
    
    // Test deserialization
    auto deserialized = DataPoint::from_binary(binary);
    EXPECT_TRUE(deserialized.is_success());
    
    auto& dp = deserialized.value();
    EXPECT_EQ(dp.get_address(), original.get_address());
    EXPECT_EQ(dp.get_protocol_id(), original.get_protocol_id());
    EXPECT_EQ(dp.get_quality(), original.get_quality());
    EXPECT_EQ(dp.get_timestamp(), original.get_timestamp());
}

TEST_F(DataPointTest, Performance) {
    const size_t num_iterations = 100000;
    
    // Test construction performance
    auto start = std::chrono::high_resolution_clock::now();
    
    for (size_t i = 0; i < num_iterations; ++i) {
        DataPoint dp(test_address, test_protocol_id, Value{static_cast<double>(i)}, 
                    test_timestamp, Quality::GOOD);
        // Prevent optimization
        volatile auto addr = dp.get_address().c_str();
        (void)addr;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    // Should be able to create at least 100k DataPoints per second
    EXPECT_LT(duration.count() / num_iterations, 10000); // Less than 10μs per construction
    
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
                    DataPoint dp(addr, test_protocol_id, Value{static_cast<double>(i)}, 
                                test_timestamp, Quality::GOOD);
                    
                    if (dp.is_valid()) {
                        success_count.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (...) {
                    // Should not throw
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
    // Test that DataPoint doesn't use excessive memory
    const size_t num_datapoints = 1000;
    std::vector<DataPoint> datapoints;
    datapoints.reserve(num_datapoints);
    
    for (size_t i = 0; i < num_datapoints; ++i) {
        datapoints.emplace_back(
            test_address + std::to_string(i),
            test_protocol_id,
            Value{static_cast<double>(i)},
            test_timestamp,
            Quality::GOOD
        );
    }
    
    // Basic check that we can create many DataPoints without issues
    EXPECT_EQ(datapoints.size(), num_datapoints);
    
    // Verify all are valid
    for (const auto& dp : datapoints) {
        EXPECT_TRUE(dp.is_valid());
    }
}

