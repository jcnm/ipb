/**
 * @file test_error.cpp
 * @brief Comprehensive unit tests for ipb::common error handling
 */

#include <gtest/gtest.h>
#include <ipb/common/error.hpp>
#include <string>

using namespace ipb::common;

class ErrorCodeTest : public ::testing::Test {};

// ============================================================================
// ErrorCode Tests
// ============================================================================

TEST_F(ErrorCodeTest, SuccessIsZero) {
    EXPECT_EQ(static_cast<uint32_t>(ErrorCode::SUCCESS), 0);
}

TEST_F(ErrorCodeTest, IsSuccessTrue) {
    EXPECT_TRUE(is_success(ErrorCode::SUCCESS));
}

TEST_F(ErrorCodeTest, IsSuccessFalseForErrors) {
    EXPECT_FALSE(is_success(ErrorCode::UNKNOWN_ERROR));
    EXPECT_FALSE(is_success(ErrorCode::CONNECTION_FAILED));
    EXPECT_FALSE(is_success(ErrorCode::OUT_OF_MEMORY));
}

TEST_F(ErrorCodeTest, GetCategoryGeneral) {
    EXPECT_EQ(get_category(ErrorCode::SUCCESS), ErrorCategory::GENERAL);
    EXPECT_EQ(get_category(ErrorCode::UNKNOWN_ERROR), ErrorCategory::GENERAL);
    EXPECT_EQ(get_category(ErrorCode::NOT_IMPLEMENTED), ErrorCategory::GENERAL);
}

TEST_F(ErrorCodeTest, GetCategoryIO) {
    EXPECT_EQ(get_category(ErrorCode::CONNECTION_FAILED), ErrorCategory::IO);
    EXPECT_EQ(get_category(ErrorCode::CONNECTION_TIMEOUT), ErrorCategory::IO);
    EXPECT_EQ(get_category(ErrorCode::READ_ERROR), ErrorCategory::IO);
}

TEST_F(ErrorCodeTest, GetCategoryProtocol) {
    EXPECT_EQ(get_category(ErrorCode::PROTOCOL_ERROR), ErrorCategory::PROTOCOL);
    EXPECT_EQ(get_category(ErrorCode::INVALID_MESSAGE), ErrorCategory::PROTOCOL);
}

TEST_F(ErrorCodeTest, GetCategoryResource) {
    EXPECT_EQ(get_category(ErrorCode::OUT_OF_MEMORY), ErrorCategory::RESOURCE);
    EXPECT_EQ(get_category(ErrorCode::QUEUE_FULL), ErrorCategory::RESOURCE);
}

TEST_F(ErrorCodeTest, GetCategoryConfig) {
    EXPECT_EQ(get_category(ErrorCode::CONFIG_INVALID), ErrorCategory::CONFIG);
    EXPECT_EQ(get_category(ErrorCode::CONFIG_PARSE_ERROR), ErrorCategory::CONFIG);
}

TEST_F(ErrorCodeTest, GetCategorySecurity) {
    EXPECT_EQ(get_category(ErrorCode::PERMISSION_DENIED), ErrorCategory::SECURITY);
    EXPECT_EQ(get_category(ErrorCode::CERTIFICATE_ERROR), ErrorCategory::SECURITY);
}

TEST_F(ErrorCodeTest, GetCategoryRouting) {
    EXPECT_EQ(get_category(ErrorCode::ROUTE_NOT_FOUND), ErrorCategory::ROUTING);
    EXPECT_EQ(get_category(ErrorCode::SINK_NOT_FOUND), ErrorCategory::ROUTING);
}

TEST_F(ErrorCodeTest, GetCategoryScheduling) {
    EXPECT_EQ(get_category(ErrorCode::DEADLINE_MISSED), ErrorCategory::SCHEDULING);
    EXPECT_EQ(get_category(ErrorCode::TASK_CANCELLED), ErrorCategory::SCHEDULING);
}

TEST_F(ErrorCodeTest, IsTransientTrue) {
    EXPECT_TRUE(is_transient(ErrorCode::CONNECTION_TIMEOUT));
    EXPECT_TRUE(is_transient(ErrorCode::WOULD_BLOCK));
    EXPECT_TRUE(is_transient(ErrorCode::IN_PROGRESS));
    EXPECT_TRUE(is_transient(ErrorCode::RESOURCE_BUSY));
    EXPECT_TRUE(is_transient(ErrorCode::QUEUE_FULL));
}

TEST_F(ErrorCodeTest, IsTransientFalse) {
    EXPECT_FALSE(is_transient(ErrorCode::SUCCESS));
    EXPECT_FALSE(is_transient(ErrorCode::CONFIG_INVALID));
    EXPECT_FALSE(is_transient(ErrorCode::OUT_OF_MEMORY));
}

TEST_F(ErrorCodeTest, IsFatalTrue) {
    EXPECT_TRUE(is_fatal(ErrorCode::OUT_OF_MEMORY));
    EXPECT_TRUE(is_fatal(ErrorCode::INVARIANT_VIOLATED));
    EXPECT_TRUE(is_fatal(ErrorCode::ASSERTION_FAILED));
    EXPECT_TRUE(is_fatal(ErrorCode::CORRUPT_DATA));
}

TEST_F(ErrorCodeTest, IsFatalFalse) {
    EXPECT_FALSE(is_fatal(ErrorCode::SUCCESS));
    EXPECT_FALSE(is_fatal(ErrorCode::CONNECTION_TIMEOUT));
    EXPECT_FALSE(is_fatal(ErrorCode::CONFIG_INVALID));
}

TEST_F(ErrorCodeTest, ErrorNameNotEmpty) {
    EXPECT_FALSE(error_name(ErrorCode::SUCCESS).empty());
    EXPECT_FALSE(error_name(ErrorCode::UNKNOWN_ERROR).empty());
    EXPECT_FALSE(error_name(ErrorCode::CONNECTION_FAILED).empty());
}

TEST_F(ErrorCodeTest, CategoryNameNotEmpty) {
    EXPECT_FALSE(category_name(ErrorCategory::GENERAL).empty());
    EXPECT_FALSE(category_name(ErrorCategory::IO).empty());
    EXPECT_FALSE(category_name(ErrorCategory::PROTOCOL).empty());
}

// ============================================================================
// SourceLocation Tests
// ============================================================================

class SourceLocationTest : public ::testing::Test {};

TEST_F(SourceLocationTest, DefaultConstruction) {
    SourceLocation loc;
    EXPECT_STREQ(loc.file, "");
    EXPECT_STREQ(loc.function, "");
    EXPECT_EQ(loc.line, 0);
}

TEST_F(SourceLocationTest, ExplicitConstruction) {
    SourceLocation loc("test.cpp", "test_func", 42, 10);
    EXPECT_STREQ(loc.file, "test.cpp");
    EXPECT_STREQ(loc.function, "test_func");
    EXPECT_EQ(loc.line, 42);
    EXPECT_EQ(loc.column, 10);
}

TEST_F(SourceLocationTest, IsValidTrue) {
    SourceLocation loc("test.cpp", "test_func", 42);
    EXPECT_TRUE(loc.is_valid());
}

TEST_F(SourceLocationTest, IsValidFalseEmptyFile) {
    SourceLocation loc("", "test_func", 42);
    EXPECT_FALSE(loc.is_valid());
}

TEST_F(SourceLocationTest, IsValidFalseZeroLine) {
    SourceLocation loc("test.cpp", "test_func", 0);
    EXPECT_FALSE(loc.is_valid());
}

// ============================================================================
// Error Class Tests
// ============================================================================

class ErrorTest : public ::testing::Test {};

TEST_F(ErrorTest, DefaultConstruction) {
    Error err;
    EXPECT_EQ(err.code(), ErrorCode::SUCCESS);
    EXPECT_TRUE(err.is_success());
    EXPECT_FALSE(err.is_error());
}

TEST_F(ErrorTest, ConstructWithCode) {
    Error err(ErrorCode::CONNECTION_FAILED);
    EXPECT_EQ(err.code(), ErrorCode::CONNECTION_FAILED);
    EXPECT_FALSE(err.is_success());
    EXPECT_TRUE(err.is_error());
}

TEST_F(ErrorTest, ConstructWithCodeAndMessage) {
    Error err(ErrorCode::CONFIG_INVALID, "Invalid configuration file");
    EXPECT_EQ(err.code(), ErrorCode::CONFIG_INVALID);
    EXPECT_EQ(err.message(), "Invalid configuration file");
}

TEST_F(ErrorTest, ConstructWithLocation) {
    SourceLocation loc("test.cpp", "test_func", 42);
    Error err(ErrorCode::TIMEOUT, "Operation timed out", loc);

    EXPECT_EQ(err.code(), ErrorCode::OPERATION_TIMEOUT);
    EXPECT_EQ(err.location().line, 42);
}

TEST_F(ErrorTest, Category) {
    Error err(ErrorCode::CONNECTION_FAILED);
    EXPECT_EQ(err.category(), ErrorCategory::IO);
}

TEST_F(ErrorTest, IsTransient) {
    Error transient_err(ErrorCode::CONNECTION_TIMEOUT);
    Error permanent_err(ErrorCode::CONFIG_INVALID);

    EXPECT_TRUE(transient_err.is_transient());
    EXPECT_FALSE(permanent_err.is_transient());
}

TEST_F(ErrorTest, IsFatal) {
    Error fatal_err(ErrorCode::OUT_OF_MEMORY);
    Error normal_err(ErrorCode::CONNECTION_FAILED);

    EXPECT_TRUE(fatal_err.is_fatal());
    EXPECT_FALSE(normal_err.is_fatal());
}

TEST_F(ErrorTest, BoolConversion) {
    Error success;
    Error failure(ErrorCode::UNKNOWN_ERROR);

    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_FALSE(static_cast<bool>(failure));
}

TEST_F(ErrorTest, CopyConstruction) {
    Error original(ErrorCode::CONFIG_INVALID, "test message");
    Error copy(original);

    EXPECT_EQ(copy.code(), original.code());
    EXPECT_EQ(copy.message(), original.message());
}

TEST_F(ErrorTest, MoveConstruction) {
    Error original(ErrorCode::CONFIG_INVALID, "test message");
    Error moved(std::move(original));

    EXPECT_EQ(moved.code(), ErrorCode::CONFIG_INVALID);
    EXPECT_EQ(moved.message(), "test message");
}

TEST_F(ErrorTest, WithCause) {
    Error cause(ErrorCode::CONNECTION_TIMEOUT, "Connection timed out");
    Error err(ErrorCode::OPERATION_TIMEOUT, "Operation failed");

    err.with_cause(cause);

    EXPECT_NE(err.cause(), nullptr);
    EXPECT_EQ(err.cause()->code(), ErrorCode::CONNECTION_TIMEOUT);
}

TEST_F(ErrorTest, ToString) {
    Error err(ErrorCode::CONFIG_INVALID, "Invalid config");
    std::string str = err.to_string();

    EXPECT_FALSE(str.empty());
}

// ============================================================================
// Result<void> Tests
// ============================================================================

class ResultVoidTest : public ::testing::Test {};

TEST_F(ResultVoidTest, DefaultSuccess) {
    Result<void> result;
    EXPECT_TRUE(result.is_success());
    EXPECT_FALSE(result.is_error());
    EXPECT_TRUE(static_cast<bool>(result));
}

TEST_F(ResultVoidTest, ConstructWithErrorCode) {
    Result<void> result(ErrorCode::CONNECTION_FAILED);
    EXPECT_FALSE(result.is_success());
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::CONNECTION_FAILED);
}

TEST_F(ResultVoidTest, ConstructWithMessage) {
    Result<void> result(ErrorCode::CONFIG_INVALID, "Bad config");
    EXPECT_EQ(result.message(), "Bad config");
}

TEST_F(ResultVoidTest, ConstructFromError) {
    Error err(ErrorCode::TIMEOUT, "Timed out");
    Result<void> result(err);

    EXPECT_TRUE(result.is_error());
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
    Result<int> result(ErrorCode::NOT_FOUND);
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
}

TEST_F(ResultValueTest, ValueAccess) {
    Result<std::string> result("hello");
    EXPECT_EQ(result.value(), "hello");
}

TEST_F(ResultValueTest, ValueOr) {
    Result<int> success(42);
    Result<int> failure(ErrorCode::NOT_FOUND);

    EXPECT_EQ(success.value_or(-1), 42);
    EXPECT_EQ(failure.value_or(-1), -1);
}

TEST_F(ResultValueTest, Map) {
    Result<int> result(10);
    auto mapped = result.map([](int v) { return v * 2; });

    EXPECT_TRUE(mapped.is_success());
    EXPECT_EQ(mapped.value(), 20);
}

TEST_F(ResultValueTest, MapError) {
    Result<int> result(ErrorCode::NOT_FOUND);
    auto mapped = result.map([](int v) { return v * 2; });

    EXPECT_TRUE(mapped.is_error());
    EXPECT_EQ(mapped.code(), ErrorCode::NOT_FOUND);
}

TEST_F(ResultValueTest, CopyConstruction) {
    Result<int> original(42);
    Result<int> copy(original);

    EXPECT_EQ(copy.value(), 42);
}

TEST_F(ResultValueTest, MoveConstruction) {
    Result<std::string> original("hello");
    Result<std::string> moved(std::move(original));

    EXPECT_EQ(moved.value(), "hello");
}

TEST_F(ResultValueTest, Assignment) {
    Result<int> result(ErrorCode::NOT_FOUND);
    result = Result<int>(42);

    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

// ============================================================================
// Helper Function Tests
// ============================================================================

class HelperFunctionTest : public ::testing::Test {};

TEST_F(HelperFunctionTest, OkValue) {
    auto result = ok(42);
    EXPECT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(HelperFunctionTest, OkVoid) {
    auto result = ok();
    EXPECT_TRUE(result.is_success());
}

TEST_F(HelperFunctionTest, ErrWithCode) {
    auto result = err<int>(ErrorCode::NOT_FOUND);
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
}

TEST_F(HelperFunctionTest, ErrWithMessage) {
    auto result = err<int>(ErrorCode::CONFIG_INVALID, "Bad config");
    EXPECT_EQ(result.message(), "Bad config");
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(ResultValueTest, ConstructionPerformance) {
    const size_t iterations = 1000000;

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        volatile Result<int> result(42);
        (void)result;
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    auto ns_per_op = duration.count() / iterations;

    EXPECT_LT(ns_per_op, 100);

    std::cout << "Result<int> construction: " << ns_per_op << " ns/op" << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
