/**
 * @file test_error.cpp
 * @brief Unit tests for IPB Error handling system
 *
 * Tests coverage for:
 * - ErrorCode: Error codes, categories, helper functions
 * - SourceLocation: Source tracking
 * - Error: Error class with context and cause chains
 * - Result<T>: Result type for both void and value types
 */

#include <gtest/gtest.h>
#include <ipb/common/error.hpp>
#include <string>
#include <thread>

using namespace ipb::common;

// ============================================================================
// ErrorCode Tests
// ============================================================================

class ErrorCodeTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

TEST_F(ErrorCodeTest, SuccessCode) {
    EXPECT_TRUE(is_success(ErrorCode::SUCCESS));
    EXPECT_FALSE(is_success(ErrorCode::UNKNOWN_ERROR));
}

TEST_F(ErrorCodeTest, CategoryExtraction) {
    // General category (0x00xx)
    EXPECT_EQ(get_category(ErrorCode::SUCCESS), ErrorCategory::GENERAL);
    EXPECT_EQ(get_category(ErrorCode::UNKNOWN_ERROR), ErrorCategory::GENERAL);
    EXPECT_EQ(get_category(ErrorCode::INVALID_ARGUMENT), ErrorCategory::GENERAL);

    // I/O category (0x01xx)
    EXPECT_EQ(get_category(ErrorCode::CONNECTION_FAILED), ErrorCategory::IO);
    EXPECT_EQ(get_category(ErrorCode::CONNECTION_TIMEOUT), ErrorCategory::IO);

    // Protocol category (0x02xx)
    EXPECT_EQ(get_category(ErrorCode::PROTOCOL_ERROR), ErrorCategory::PROTOCOL);
    EXPECT_EQ(get_category(ErrorCode::INVALID_MESSAGE), ErrorCategory::PROTOCOL);

    // Resource category (0x03xx)
    EXPECT_EQ(get_category(ErrorCode::OUT_OF_MEMORY), ErrorCategory::RESOURCE);
    EXPECT_EQ(get_category(ErrorCode::QUEUE_FULL), ErrorCategory::RESOURCE);

    // Config category (0x04xx)
    EXPECT_EQ(get_category(ErrorCode::CONFIG_INVALID), ErrorCategory::CONFIG);

    // Security category (0x05xx)
    EXPECT_EQ(get_category(ErrorCode::PERMISSION_DENIED), ErrorCategory::SECURITY);

    // Routing category (0x06xx)
    EXPECT_EQ(get_category(ErrorCode::ROUTE_NOT_FOUND), ErrorCategory::ROUTING);

    // Scheduling category (0x07xx)
    EXPECT_EQ(get_category(ErrorCode::DEADLINE_MISSED), ErrorCategory::SCHEDULING);

    // Serialization category (0x08xx)
    EXPECT_EQ(get_category(ErrorCode::SERIALIZE_FAILED), ErrorCategory::SERIALIZATION);

    // Validation category (0x09xx)
    EXPECT_EQ(get_category(ErrorCode::VALIDATION_FAILED), ErrorCategory::VALIDATION);

    // Platform category (0x0Axx)
    EXPECT_EQ(get_category(ErrorCode::PLATFORM_ERROR), ErrorCategory::PLATFORM);
}

TEST_F(ErrorCodeTest, TransientErrors) {
    EXPECT_TRUE(is_transient(ErrorCode::CONNECTION_TIMEOUT));
    EXPECT_TRUE(is_transient(ErrorCode::WOULD_BLOCK));
    EXPECT_TRUE(is_transient(ErrorCode::IN_PROGRESS));
    EXPECT_TRUE(is_transient(ErrorCode::RESOURCE_BUSY));
    EXPECT_TRUE(is_transient(ErrorCode::QUEUE_FULL));
    EXPECT_TRUE(is_transient(ErrorCode::SCHEDULER_OVERLOADED));
    EXPECT_TRUE(is_transient(ErrorCode::SINK_OVERLOADED));

    // Non-transient errors
    EXPECT_FALSE(is_transient(ErrorCode::SUCCESS));
    EXPECT_FALSE(is_transient(ErrorCode::INVALID_ARGUMENT));
    EXPECT_FALSE(is_transient(ErrorCode::OUT_OF_MEMORY));
}

TEST_F(ErrorCodeTest, FatalErrors) {
    EXPECT_TRUE(is_fatal(ErrorCode::OUT_OF_MEMORY));
    EXPECT_TRUE(is_fatal(ErrorCode::INVARIANT_VIOLATED));
    EXPECT_TRUE(is_fatal(ErrorCode::ASSERTION_FAILED));
    EXPECT_TRUE(is_fatal(ErrorCode::CORRUPT_DATA));

    // Non-fatal errors
    EXPECT_FALSE(is_fatal(ErrorCode::SUCCESS));
    EXPECT_FALSE(is_fatal(ErrorCode::CONNECTION_TIMEOUT));
    EXPECT_FALSE(is_fatal(ErrorCode::INVALID_ARGUMENT));
}

TEST_F(ErrorCodeTest, CategoryNames) {
    EXPECT_EQ(category_name(ErrorCategory::GENERAL), "General");
    EXPECT_EQ(category_name(ErrorCategory::IO), "I/O");
    EXPECT_EQ(category_name(ErrorCategory::PROTOCOL), "Protocol");
    EXPECT_EQ(category_name(ErrorCategory::RESOURCE), "Resource");
    EXPECT_EQ(category_name(ErrorCategory::CONFIG), "Configuration");
    EXPECT_EQ(category_name(ErrorCategory::SECURITY), "Security");
    EXPECT_EQ(category_name(ErrorCategory::ROUTING), "Routing");
    EXPECT_EQ(category_name(ErrorCategory::SCHEDULING), "Scheduling");
    EXPECT_EQ(category_name(ErrorCategory::SERIALIZATION), "Serialization");
    EXPECT_EQ(category_name(ErrorCategory::VALIDATION), "Validation");
    EXPECT_EQ(category_name(ErrorCategory::PLATFORM), "Platform");
}

TEST_F(ErrorCodeTest, ErrorNames) {
    EXPECT_EQ(error_name(ErrorCode::SUCCESS), "SUCCESS");
    EXPECT_EQ(error_name(ErrorCode::UNKNOWN_ERROR), "UNKNOWN_ERROR");
    EXPECT_EQ(error_name(ErrorCode::CONNECTION_FAILED), "CONNECTION_FAILED");
    EXPECT_EQ(error_name(ErrorCode::OUT_OF_MEMORY), "OUT_OF_MEMORY");
    EXPECT_EQ(error_name(ErrorCode::CONFIG_INVALID), "CONFIG_INVALID");
    EXPECT_EQ(error_name(ErrorCode::PERMISSION_DENIED), "PERMISSION_DENIED");
}

// ============================================================================
// SourceLocation Tests
// ============================================================================

class SourceLocationTest : public ::testing::Test {};

TEST_F(SourceLocationTest, DefaultConstruction) {
    SourceLocation loc;
    EXPECT_FALSE(loc.is_valid());
    EXPECT_EQ(loc.line, 0u);
}

TEST_F(SourceLocationTest, ManualConstruction) {
    SourceLocation loc("test.cpp", "test_func", 42, 10);
    EXPECT_TRUE(loc.is_valid());
    EXPECT_STREQ(loc.file, "test.cpp");
    EXPECT_STREQ(loc.function, "test_func");
    EXPECT_EQ(loc.line, 42u);
    EXPECT_EQ(loc.column, 10u);
}

TEST_F(SourceLocationTest, CurrentLocation) {
    auto loc = SourceLocation::current();
    // Should capture current location (if source_location is available)
    // Either way, we should be able to call it
    (void)loc;
}

// ============================================================================
// Error Tests
// ============================================================================

class ErrorTest : public ::testing::Test {};

TEST_F(ErrorTest, DefaultConstruction) {
    Error err;
    EXPECT_TRUE(err.is_success());
    EXPECT_FALSE(err.is_error());
    EXPECT_EQ(err.code(), ErrorCode::SUCCESS);
    EXPECT_TRUE(err.message().empty());
}

TEST_F(ErrorTest, ConstructWithCode) {
    Error err(ErrorCode::INVALID_ARGUMENT);
    EXPECT_FALSE(err.is_success());
    EXPECT_TRUE(err.is_error());
    EXPECT_EQ(err.code(), ErrorCode::INVALID_ARGUMENT);
    EXPECT_EQ(err.category(), ErrorCategory::GENERAL);
}

TEST_F(ErrorTest, ConstructWithMessage) {
    Error err(ErrorCode::CONNECTION_FAILED, "Unable to connect to server");
    EXPECT_EQ(err.code(), ErrorCode::CONNECTION_FAILED);
    EXPECT_EQ(err.message(), "Unable to connect to server");
}

TEST_F(ErrorTest, ConstructWithLocation) {
    SourceLocation loc("test.cpp", "test_func", 100);
    Error err(ErrorCode::NOT_FOUND, std::string("Resource not found"), loc);

    EXPECT_EQ(err.code(), ErrorCode::NOT_FOUND);
    EXPECT_EQ(err.message(), "Resource not found");
    EXPECT_TRUE(err.location().is_valid());
    EXPECT_STREQ(err.location().file, "test.cpp");
}

TEST_F(ErrorTest, BoolConversion) {
    Error success;
    Error failure(ErrorCode::UNKNOWN_ERROR);

    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_FALSE(static_cast<bool>(failure));
}

TEST_F(ErrorTest, TransientAndFatalChecks) {
    Error transient(ErrorCode::CONNECTION_TIMEOUT);
    Error fatal(ErrorCode::OUT_OF_MEMORY);
    Error normal(ErrorCode::NOT_FOUND);

    EXPECT_TRUE(transient.is_transient());
    EXPECT_FALSE(transient.is_fatal());

    EXPECT_FALSE(fatal.is_transient());
    EXPECT_TRUE(fatal.is_fatal());

    EXPECT_FALSE(normal.is_transient());
    EXPECT_FALSE(normal.is_fatal());
}

TEST_F(ErrorTest, WithContext) {
    Error err(ErrorCode::CONFIG_INVALID, "Invalid configuration");
    err.with_context("file", "config.yaml")
       .with_context("line", "42");

    std::string str = err.to_string();
    EXPECT_NE(str.find("file: config.yaml"), std::string::npos);
    EXPECT_NE(str.find("line: 42"), std::string::npos);
}

TEST_F(ErrorTest, WithCause) {
    Error root_cause(ErrorCode::DNS_RESOLUTION_FAILED, "DNS lookup failed");
    Error err(ErrorCode::CONNECTION_FAILED, "Could not connect");
    err.with_cause(root_cause);

    EXPECT_NE(err.cause(), nullptr);
    EXPECT_EQ(err.cause()->code(), ErrorCode::DNS_RESOLUTION_FAILED);

    std::string str = err.to_string();
    EXPECT_NE(str.find("Caused by"), std::string::npos);
}

TEST_F(ErrorTest, ToString) {
    Error err(ErrorCode::PROTOCOL_ERROR, "Invalid frame");
    std::string str = err.to_string();

    EXPECT_NE(str.find("[Protocol]"), std::string::npos);
    EXPECT_NE(str.find("PROTOCOL_ERROR"), std::string::npos);
    EXPECT_NE(str.find("0x0200"), std::string::npos);
    EXPECT_NE(str.find("Invalid frame"), std::string::npos);
}

TEST_F(ErrorTest, CopyConstruction) {
    Error original(ErrorCode::QUEUE_FULL, "Queue is full");
    original.with_context("queue", "main");
    original.with_cause(Error(ErrorCode::RESOURCE_BUSY));

    Error copy(original);

    EXPECT_EQ(copy.code(), ErrorCode::QUEUE_FULL);
    EXPECT_EQ(copy.message(), "Queue is full");
    EXPECT_NE(copy.cause(), nullptr);
    EXPECT_EQ(copy.cause()->code(), ErrorCode::RESOURCE_BUSY);
}

TEST_F(ErrorTest, MoveConstruction) {
    Error original(ErrorCode::OPERATION_TIMEOUT, "Timed out");
    Error moved(std::move(original));

    EXPECT_EQ(moved.code(), ErrorCode::OPERATION_TIMEOUT);
}

TEST_F(ErrorTest, CopyAssignment) {
    Error original(ErrorCode::ACCESS_DENIED, "Access denied");
    Error copy;
    copy = original;

    EXPECT_EQ(copy.code(), ErrorCode::ACCESS_DENIED);
    EXPECT_EQ(copy.message(), "Access denied");
}

TEST_F(ErrorTest, MoveAssignment) {
    Error original(ErrorCode::FILE_NOT_FOUND);
    Error moved;
    moved = std::move(original);

    EXPECT_EQ(moved.code(), ErrorCode::FILE_NOT_FOUND);
}

// ============================================================================
// Result<void> Tests
// ============================================================================

class ResultVoidTest : public ::testing::Test {};

TEST_F(ResultVoidTest, DefaultConstruction) {
    Result<void> result;
    EXPECT_TRUE(result.is_success());
    EXPECT_FALSE(result.is_error());
    EXPECT_TRUE(static_cast<bool>(result));
}

TEST_F(ResultVoidTest, ConstructWithErrorCode) {
    Result<void> result(ErrorCode::INVALID_STATE);
    EXPECT_FALSE(result.is_success());
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::INVALID_STATE);
}

TEST_F(ResultVoidTest, ConstructWithMessage) {
    Result<void> result(ErrorCode::VALIDATION_FAILED, "Value out of range");
    EXPECT_EQ(result.code(), ErrorCode::VALIDATION_FAILED);
    EXPECT_EQ(result.message(), "Value out of range");
}

TEST_F(ResultVoidTest, ConstructWithError) {
    Error err(ErrorCode::ENCRYPTION_FAILED, "Key mismatch");
    Result<void> result(err);

    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::ENCRYPTION_FAILED);
}

TEST_F(ResultVoidTest, WithCause) {
    Result<void> result(ErrorCode::TASK_FAILED, "Task execution failed");
    result.with_cause(Error(ErrorCode::DEADLINE_MISSED));

    EXPECT_NE(result.error().cause(), nullptr);
}

TEST_F(ResultVoidTest, OkHelper) {
    auto result = ok();
    EXPECT_TRUE(result.is_success());
}

TEST_F(ResultVoidTest, ErrHelper) {
    auto result = err(ErrorCode::NOT_IMPLEMENTED, "Feature not yet available");
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_IMPLEMENTED);
}

// ============================================================================
// Result<T> Tests
// ============================================================================

class ResultValueTest : public ::testing::Test {};

TEST_F(ResultValueTest, ConstructWithValue) {
    Result<int> result(42);
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(ResultValueTest, ConstructWithErrorCode) {
    Result<int> result(ErrorCode::VALUE_OUT_OF_RANGE);
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::VALUE_OUT_OF_RANGE);
}

TEST_F(ResultValueTest, ConstructWithMessage) {
    Result<std::string> result(ErrorCode::FORMAT_INVALID, "Expected JSON");
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::FORMAT_INVALID);
    EXPECT_EQ(result.message(), "Expected JSON");
}

TEST_F(ResultValueTest, ValueOr) {
    Result<int> success(100);
    Result<int> failure(ErrorCode::NOT_FOUND);

    EXPECT_EQ(success.value_or(0), 100);
    EXPECT_EQ(failure.value_or(0), 0);
}

TEST_F(ResultValueTest, MapSuccess) {
    Result<int> result(10);
    auto mapped = result.map([](int x) { return x * 2; });

    EXPECT_TRUE(mapped.is_success());
    EXPECT_EQ(mapped.value(), 20);
}

TEST_F(ResultValueTest, MapError) {
    Result<int> result(ErrorCode::INVALID_ARGUMENT);
    auto mapped = result.map([](int x) { return x * 2; });

    EXPECT_TRUE(mapped.is_error());
    EXPECT_EQ(mapped.code(), ErrorCode::INVALID_ARGUMENT);
}

TEST_F(ResultValueTest, CopyConstruction) {
    Result<std::string> original("hello");
    Result<std::string> copy(original);

    EXPECT_TRUE(copy.is_success());
    EXPECT_EQ(copy.value(), "hello");
}

TEST_F(ResultValueTest, MoveConstruction) {
    Result<std::string> original("world");
    Result<std::string> moved(std::move(original));

    EXPECT_TRUE(moved.is_success());
    EXPECT_EQ(moved.value(), "world");
}

TEST_F(ResultValueTest, CopyAssignment) {
    Result<int> original(123);
    Result<int> copy(ErrorCode::UNKNOWN_ERROR);
    copy = original;

    EXPECT_TRUE(copy.is_success());
    EXPECT_EQ(copy.value(), 123);
}

TEST_F(ResultValueTest, MoveAssignment) {
    Result<std::vector<int>> original(std::vector<int>{1, 2, 3});
    Result<std::vector<int>> moved(ErrorCode::UNKNOWN_ERROR);
    moved = std::move(original);

    EXPECT_TRUE(moved.is_success());
    EXPECT_EQ(moved.value().size(), 3u);
}

TEST_F(ResultValueTest, OkValueHelper) {
    auto result = ok<int>(42);
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(ResultValueTest, ErrValueHelper) {
    auto result = err<int>(ErrorCode::EMPTY_VALUE, "No value provided");
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::EMPTY_VALUE);
}

TEST_F(ResultValueTest, ComplexTypes) {
    struct Complex {
        std::string name;
        int value;
        bool operator==(const Complex& other) const {
            return name == other.name && value == other.value;
        }
    };

    Complex c{"test", 42};
    Result<Complex> result(c);

    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value().name, "test");
    EXPECT_EQ(result.value().value, 42);
}

// ============================================================================
// Error Chain Tests
// ============================================================================

class ErrorChainTest : public ::testing::Test {};

TEST_F(ErrorChainTest, MultiLevelCauseChain) {
    Error level3(ErrorCode::DNS_RESOLUTION_FAILED, "DNS failure");
    Error level2(ErrorCode::CONNECTION_TIMEOUT, "Connection timed out");
    level2.with_cause(level3);
    Error level1(ErrorCode::HANDSHAKE_FAILED, "Handshake failed");
    level1.with_cause(level2);

    // Verify chain
    EXPECT_EQ(level1.code(), ErrorCode::HANDSHAKE_FAILED);
    EXPECT_NE(level1.cause(), nullptr);
    EXPECT_EQ(level1.cause()->code(), ErrorCode::CONNECTION_TIMEOUT);
    EXPECT_NE(level1.cause()->cause(), nullptr);
    EXPECT_EQ(level1.cause()->cause()->code(), ErrorCode::DNS_RESOLUTION_FAILED);
    EXPECT_EQ(level1.cause()->cause()->cause(), nullptr);
}

TEST_F(ErrorChainTest, ToStringWithChain) {
    Error root(ErrorCode::SOCKET_ERROR, "Socket creation failed");
    Error err(ErrorCode::CONNECTION_FAILED, "Could not connect");
    err.with_cause(root);

    std::string str = err.to_string();
    EXPECT_NE(str.find("CONNECTION_FAILED"), std::string::npos);
    EXPECT_NE(str.find("Caused by"), std::string::npos);
    EXPECT_NE(str.find("SOCKET_ERROR"), std::string::npos);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

class ErrorThreadSafetyTest : public ::testing::Test {};

TEST_F(ErrorThreadSafetyTest, ConcurrentErrorCreation) {
    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 1000;

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&success_count]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                Error err(ErrorCode::UNKNOWN_ERROR, "Test error");
                err.with_context("iteration", std::to_string(i));

                if (err.is_error()) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * ITERATIONS);
}

TEST_F(ErrorThreadSafetyTest, ConcurrentResultCreation) {
    constexpr int NUM_THREADS = 4;
    constexpr int ITERATIONS = 1000;

    std::vector<std::thread> threads;
    std::atomic<int> value_sum{0};

    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&value_sum, t]() {
            for (int i = 0; i < ITERATIONS; ++i) {
                Result<int> result(t * ITERATIONS + i);
                if (result.is_success()) {
                    value_sum += 1;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(value_sum.load(), NUM_THREADS * ITERATIONS);
}
