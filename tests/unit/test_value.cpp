/**
 * @file test_value.cpp
 * @brief Comprehensive unit tests for ipb::common::Value
 */

#include <gtest/gtest.h>
#include <ipb/common/data_point.hpp>
#include <cstring>
#include <vector>
#include <string>

using namespace ipb::common;

class ValueTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(ValueTest, DefaultConstruction) {
    Value v;
    EXPECT_EQ(v.type(), Value::Type::EMPTY);
    EXPECT_EQ(v.size(), 0);
    EXPECT_TRUE(v.empty());
}

TEST_F(ValueTest, CopyConstruction) {
    Value v1;
    v1.set(42);

    Value v2(v1);
    EXPECT_EQ(v2.type(), Value::Type::INT32);
    EXPECT_EQ(v2.get<int32_t>(), 42);
}

TEST_F(ValueTest, MoveConstruction) {
    Value v1;
    v1.set(42);

    Value v2(std::move(v1));
    EXPECT_EQ(v2.type(), Value::Type::INT32);
    EXPECT_EQ(v2.get<int32_t>(), 42);
}

// ============================================================================
// Type Tests - Boolean
// ============================================================================

TEST_F(ValueTest, SetGetBoolTrue) {
    Value v;
    v.set(true);

    EXPECT_EQ(v.type(), Value::Type::BOOL);
    EXPECT_EQ(v.get<bool>(), true);
    EXPECT_FALSE(v.empty());
}

TEST_F(ValueTest, SetGetBoolFalse) {
    Value v;
    v.set(false);

    EXPECT_EQ(v.type(), Value::Type::BOOL);
    EXPECT_EQ(v.get<bool>(), false);
}

// ============================================================================
// Type Tests - Integers
// ============================================================================

TEST_F(ValueTest, SetGetInt8) {
    Value v;
    int8_t val = -42;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::INT8);
    EXPECT_EQ(v.get<int8_t>(), -42);
}

TEST_F(ValueTest, SetGetInt16) {
    Value v;
    int16_t val = -12345;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::INT16);
    EXPECT_EQ(v.get<int16_t>(), -12345);
}

TEST_F(ValueTest, SetGetInt32) {
    Value v;
    int32_t val = -123456789;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::INT32);
    EXPECT_EQ(v.get<int32_t>(), -123456789);
}

TEST_F(ValueTest, SetGetInt64) {
    Value v;
    int64_t val = -9223372036854775807LL;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::INT64);
    EXPECT_EQ(v.get<int64_t>(), -9223372036854775807LL);
}

TEST_F(ValueTest, SetGetUInt8) {
    Value v;
    uint8_t val = 255;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::UINT8);
    EXPECT_EQ(v.get<uint8_t>(), 255);
}

TEST_F(ValueTest, SetGetUInt16) {
    Value v;
    uint16_t val = 65535;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::UINT16);
    EXPECT_EQ(v.get<uint16_t>(), 65535);
}

TEST_F(ValueTest, SetGetUInt32) {
    Value v;
    uint32_t val = 4294967295U;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::UINT32);
    EXPECT_EQ(v.get<uint32_t>(), 4294967295U);
}

TEST_F(ValueTest, SetGetUInt64) {
    Value v;
    uint64_t val = 18446744073709551615ULL;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::UINT64);
    EXPECT_EQ(v.get<uint64_t>(), 18446744073709551615ULL);
}

// ============================================================================
// Type Tests - Floating Point
// ============================================================================

TEST_F(ValueTest, SetGetFloat32) {
    Value v;
    float val = 3.14159f;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::FLOAT32);
    EXPECT_FLOAT_EQ(v.get<float>(), 3.14159f);
}

TEST_F(ValueTest, SetGetFloat64) {
    Value v;
    double val = 3.141592653589793;
    v.set(val);

    EXPECT_EQ(v.type(), Value::Type::FLOAT64);
    EXPECT_DOUBLE_EQ(v.get<double>(), 3.141592653589793);
}

TEST_F(ValueTest, FloatSpecialValues) {
    Value v;

    // Positive infinity
    v.set(std::numeric_limits<float>::infinity());
    EXPECT_TRUE(std::isinf(v.get<float>()));

    // Negative infinity
    v.set(-std::numeric_limits<double>::infinity());
    EXPECT_TRUE(std::isinf(v.get<double>()));

    // NaN
    v.set(std::numeric_limits<float>::quiet_NaN());
    EXPECT_TRUE(std::isnan(v.get<float>()));
}

// ============================================================================
// Type Tests - String
// ============================================================================

TEST_F(ValueTest, SetStringViewInline) {
    Value v;
    std::string_view sv = "Hello, World!";  // Small enough for inline storage
    v.set_string_view(sv);

    EXPECT_EQ(v.type(), Value::Type::STRING);
    EXPECT_EQ(v.as_string_view(), "Hello, World!");
    EXPECT_EQ(v.size(), 13);
}

TEST_F(ValueTest, SetStringViewExternal) {
    Value v;
    std::string long_str(100, 'X');  // Large string requiring external storage
    v.set_string_view(long_str);

    EXPECT_EQ(v.type(), Value::Type::STRING);
    EXPECT_EQ(v.as_string_view(), long_str);
    EXPECT_EQ(v.size(), 100);
}

TEST_F(ValueTest, SetStringViewEmpty) {
    Value v;
    v.set_string_view("");

    EXPECT_EQ(v.type(), Value::Type::STRING);
    EXPECT_EQ(v.as_string_view(), "");
    EXPECT_EQ(v.size(), 0);
}

TEST_F(ValueTest, AsStringViewWrongType) {
    Value v;
    v.set(42);

    EXPECT_TRUE(v.as_string_view().empty());
}

// ============================================================================
// Type Tests - Binary
// ============================================================================

TEST_F(ValueTest, SetBinaryInline) {
    Value v;
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    v.set_binary(data);

    EXPECT_EQ(v.type(), Value::Type::BINARY);
    auto result = v.as_binary();
    EXPECT_EQ(result.size(), 5);
    EXPECT_EQ(result[0], 0x01);
    EXPECT_EQ(result[4], 0x05);
}

TEST_F(ValueTest, SetBinaryExternal) {
    Value v;
    std::vector<uint8_t> data(100, 0xFF);
    v.set_binary(data);

    EXPECT_EQ(v.type(), Value::Type::BINARY);
    auto result = v.as_binary();
    EXPECT_EQ(result.size(), 100);
    EXPECT_EQ(result[0], 0xFF);
}

TEST_F(ValueTest, SetBinaryEmpty) {
    Value v;
    std::vector<uint8_t> data;
    v.set_binary(data);

    EXPECT_EQ(v.type(), Value::Type::BINARY);
    EXPECT_EQ(v.as_binary().size(), 0);
}

TEST_F(ValueTest, AsBinaryWrongType) {
    Value v;
    v.set(42);

    EXPECT_TRUE(v.as_binary().empty());
}

// ============================================================================
// Assignment Tests
// ============================================================================

TEST_F(ValueTest, CopyAssignment) {
    Value v1;
    v1.set(42);

    Value v2;
    v2.set(100);

    v2 = v1;

    EXPECT_EQ(v2.get<int32_t>(), 42);
}

TEST_F(ValueTest, MoveAssignment) {
    Value v1;
    v1.set(42);

    Value v2;
    v2.set(100);

    v2 = std::move(v1);

    EXPECT_EQ(v2.get<int32_t>(), 42);
}

TEST_F(ValueTest, SelfAssignment) {
    Value v;
    v.set(42);

    v = v;  // Self-assignment

    EXPECT_EQ(v.get<int32_t>(), 42);
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_F(ValueTest, EqualityEmpty) {
    Value v1;
    Value v2;

    EXPECT_TRUE(v1 == v2);
}

TEST_F(ValueTest, EqualityBool) {
    Value v1, v2, v3;
    v1.set(true);
    v2.set(true);
    v3.set(false);

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(ValueTest, EqualityInt32) {
    Value v1, v2, v3;
    v1.set(int32_t(42));
    v2.set(int32_t(42));
    v3.set(int32_t(100));

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(ValueTest, EqualityFloat) {
    Value v1, v2;
    v1.set(3.14159);
    v2.set(3.14159);

    EXPECT_TRUE(v1 == v2);
}

TEST_F(ValueTest, EqualityString) {
    Value v1, v2, v3;
    v1.set_string_view("hello");
    v2.set_string_view("hello");
    v3.set_string_view("world");

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(ValueTest, EqualityBinary) {
    Value v1, v2, v3;
    std::vector<uint8_t> data1 = {0x01, 0x02, 0x03};
    std::vector<uint8_t> data2 = {0x01, 0x02, 0x03};
    std::vector<uint8_t> data3 = {0x01, 0x02, 0x04};

    v1.set_binary(data1);
    v2.set_binary(data2);
    v3.set_binary(data3);

    EXPECT_TRUE(v1 == v2);
    EXPECT_FALSE(v1 == v3);
}

TEST_F(ValueTest, InequalityDifferentTypes) {
    Value v1, v2;
    v1.set(int32_t(42));
    v2.set(42.0);

    EXPECT_FALSE(v1 == v2);  // Different types
}

TEST_F(ValueTest, InequalityOperator) {
    Value v1, v2;
    v1.set(42);
    v2.set(100);

    EXPECT_TRUE(v1 != v2);
}

// ============================================================================
// Type Transition Tests
// ============================================================================

TEST_F(ValueTest, TypeTransition) {
    Value v;

    v.set(int32_t(42));
    EXPECT_EQ(v.type(), Value::Type::INT32);

    v.set(3.14);
    EXPECT_EQ(v.type(), Value::Type::FLOAT64);

    v.set_string_view("hello");
    EXPECT_EQ(v.type(), Value::Type::STRING);

    v.set(true);
    EXPECT_EQ(v.type(), Value::Type::BOOL);
}

// ============================================================================
// Inline Storage Tests
// ============================================================================

TEST_F(ValueTest, InlineStorageThreshold) {
    Value v;

    // Should use inline storage
    std::string small_str(Value::INLINE_SIZE, 'X');
    v.set_string_view(small_str);
    EXPECT_EQ(v.size(), Value::INLINE_SIZE);
    EXPECT_EQ(v.as_string_view(), small_str);

    // Should use external storage
    std::string large_str(Value::INLINE_SIZE + 1, 'Y');
    v.set_string_view(large_str);
    EXPECT_EQ(v.size(), Value::INLINE_SIZE + 1);
    EXPECT_EQ(v.as_string_view(), large_str);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(ValueTest, SetGetPerformance) {
    const size_t iterations = 1000000;
    Value v;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        v.set(static_cast<int32_t>(i));
        volatile auto val = v.get<int32_t>();
        (void)val;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 100);  // Less than 100ns per set+get

    std::cout << "Value set+get performance: " << ns_per_op << " ns/op" << std::endl;
}

TEST_F(ValueTest, CopyPerformance) {
    const size_t iterations = 1000000;
    Value v1;
    v1.set(42);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        Value v2(v1);
        volatile auto val = v2.get<int32_t>();
        (void)val;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 500);

    std::cout << "Value copy performance: " << ns_per_op << " ns/op" << std::endl;
}

// ============================================================================
// Serialization Tests
// ============================================================================

TEST_F(ValueTest, SerializedSizeEmpty) {
    Value v;
    EXPECT_GT(v.serialized_size(), 0);
}

TEST_F(ValueTest, SerializedSizeNumeric) {
    Value v;
    v.set(int32_t(42));

    size_t expected = sizeof(Value::Type) + sizeof(size_t) + sizeof(int32_t);
    EXPECT_GE(v.serialized_size(), sizeof(Value::Type));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
