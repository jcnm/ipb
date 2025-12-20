/**
 * @file test_result_ext.cpp
 * @brief Comprehensive tests for result_ext.hpp
 *
 * Covers: and_then, or_else, map_error, flatten, inspect, unwrap_or_throw,
 *         has_error, has_error_category, first_success, combine, apply_all,
 *         retry, Pipeline
 */

#include <gtest/gtest.h>

#include <string>
#include <stdexcept>

#include <ipb/common/result_ext.hpp>

using namespace ipb::common;

//=============================================================================
// and_then Tests
//=============================================================================

class AndThenTest : public ::testing::Test {
protected:
    Result<int> success_result = ok(42);
    Result<int> error_result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "test error"));
};

TEST_F(AndThenTest, SuccessChaining) {
    auto doubled = and_then(success_result, [](int x) -> Result<int> {
        return ok(x * 2);
    });

    ASSERT_TRUE(doubled.is_success());
    EXPECT_EQ(doubled.value(), 84);
}

TEST_F(AndThenTest, ErrorPropagation) {
    auto result = and_then(error_result, [](int x) -> Result<int> {
        return ok(x * 2);
    });

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::INVALID_ARGUMENT);
}

TEST_F(AndThenTest, TypeTransformation) {
    auto to_string = and_then(success_result, [](int x) -> Result<std::string> {
        return ok(std::to_string(x));
    });

    ASSERT_TRUE(to_string.is_success());
    EXPECT_EQ(to_string.value(), "42");
}

TEST_F(AndThenTest, MultipleChaining) {
    auto result = and_then(success_result, [](int x) -> Result<int> {
        return ok(x + 1);
    });
    result = and_then(result, [](int x) -> Result<int> {
        return ok(x * 2);
    });
    result = and_then(result, [](int x) -> Result<int> {
        return ok(x - 10);
    });

    // (42 + 1) * 2 - 10 = 76
    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 76);
}

TEST_F(AndThenTest, ChainErrorInMiddle) {
    auto result = and_then(success_result, [](int x) -> Result<int> {
        return ok(x + 1);
    });
    result = and_then(result, [](int) -> Result<int> {
        return Result<int>(Error(ErrorCode::OPERATION_TIMEOUT, "timeout"));
    });
    result = and_then(result, [](int x) -> Result<int> {
        return ok(x * 100);  // Should not be called
    });

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::OPERATION_TIMEOUT);
}

TEST_F(AndThenTest, MoveSemantics) {
    Result<int> movable = ok(42);
    auto result = and_then(std::move(movable), [](int x) -> Result<int> {
        return ok(x * 2);
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 84);
}

TEST_F(AndThenTest, VoidSpecialization) {
    Result<void> void_result = ok();
    auto result = and_then(void_result, []() -> Result<int> {
        return ok(42);
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

//=============================================================================
// or_else Tests
//=============================================================================

class OrElseTest : public ::testing::Test {
protected:
    Result<int> success_result = ok(42);
    Result<int> error_result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "test error"));
};

TEST_F(OrElseTest, SuccessPassthrough) {
    auto result = or_else(success_result, [](const Error&) -> Result<int> {
        return ok(0);  // Should not be called
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(OrElseTest, ErrorRecovery) {
    auto result = or_else(error_result, [](const Error&) -> Result<int> {
        return ok(0);  // Fallback value
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 0);
}

TEST_F(OrElseTest, ErrorToError) {
    auto result = or_else(error_result, [](const Error&) -> Result<int> {
        return Result<int>(Error(ErrorCode::NOT_FOUND, "fallback failed"));
    });

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
}

TEST_F(OrElseTest, MoveSemantics) {
    Result<int> movable = Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "error"));
    auto result = or_else(std::move(movable), [](const Error&) -> Result<int> {
        return ok(99);
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 99);
}

//=============================================================================
// map_error Tests
//=============================================================================

class MapErrorTest : public ::testing::Test {
protected:
    Result<int> success_result = ok(42);
    Result<int> error_result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "original"));
};

TEST_F(MapErrorTest, SuccessPassthrough) {
    auto result = map_error(success_result, []([[maybe_unused]] const Error& e) {
        return Error(ErrorCode::UNKNOWN_ERROR, "should not be called");
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(MapErrorTest, TransformError) {
    auto result = map_error(error_result, [](const Error& e) {
        return Error(ErrorCode::NOT_FOUND, "transformed: " + e.message());
    });

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
    EXPECT_TRUE(result.error().message().find("transformed") != std::string::npos);
}

//=============================================================================
// flatten Tests
//=============================================================================

TEST(FlattenTest, SuccessFlattening) {
    Result<Result<int>> nested = ok(ok(42));
    auto flattened = flatten(nested);

    ASSERT_TRUE(flattened.is_success());
    EXPECT_EQ(flattened.value(), 42);
}

TEST(FlattenTest, OuterError) {
    Result<Result<int>> nested = Result<Result<int>>(
        Error(ErrorCode::INVALID_ARGUMENT, "outer error")
    );
    auto flattened = flatten(nested);

    ASSERT_TRUE(flattened.is_error());
    EXPECT_EQ(flattened.code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(FlattenTest, InnerError) {
    Result<Result<int>> nested = ok(
        Result<int>(Error(ErrorCode::NOT_FOUND, "inner error"))
    );
    auto flattened = flatten(nested);

    ASSERT_TRUE(flattened.is_error());
    EXPECT_EQ(flattened.code(), ErrorCode::NOT_FOUND);
}

//=============================================================================
// inspect Tests
//=============================================================================

TEST(InspectTest, InspectSuccess) {
    Result<int> result = ok(42);
    int inspected_value = 0;

    const auto& same_result = inspect(result, [&inspected_value](int val) {
        inspected_value = val;
    });

    EXPECT_EQ(inspected_value, 42);
    EXPECT_EQ(&same_result, &result);  // Should return same reference
}

TEST(InspectTest, InspectError) {
    Result<int> result = Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "error"));
    int inspected_value = 0;

    inspect(result, [&inspected_value](int val) {
        inspected_value = val;  // Should not be called
    });

    EXPECT_EQ(inspected_value, 0);  // Not modified
}

TEST(InspectErrorTest, InspectOnError) {
    Result<int> result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "test"));
    ErrorCode inspected_code = ErrorCode::SUCCESS;

    inspect_error(result, [&inspected_code](const Error& e) {
        inspected_code = e.code();
    });

    EXPECT_EQ(inspected_code, ErrorCode::INVALID_ARGUMENT);
}

TEST(InspectErrorTest, InspectOnSuccess) {
    Result<int> result = ok(42);
    ErrorCode inspected_code = ErrorCode::SUCCESS;

    inspect_error(result, [&inspected_code](const Error& e) {
        inspected_code = e.code();  // Should not be called
    });

    EXPECT_EQ(inspected_code, ErrorCode::SUCCESS);  // Not modified
}

//=============================================================================
// unwrap_or_throw Tests
//=============================================================================

TEST(UnwrapOrThrowTest, Success) {
    Result<int> result = ok(42);
    EXPECT_EQ(unwrap_or_throw(result), 42);
}

TEST(UnwrapOrThrowTest, SuccessMove) {
    Result<int> result = ok(42);
    EXPECT_EQ(unwrap_or_throw(std::move(result)), 42);
}

TEST(UnwrapOrThrowTest, ErrorThrows) {
    Result<int> result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "test error"));

    EXPECT_THROW(unwrap_or_throw(result), std::runtime_error);
}

TEST(UnwrapOrThrowTest, ErrorMessage) {
    Result<int> result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "specific message"));

    try {
        unwrap_or_throw(result);
        FAIL() << "Expected exception";
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("specific message") != std::string::npos);
    }
}

//=============================================================================
// has_error Tests
//=============================================================================

TEST(HasErrorTest, MatchingError) {
    Result<int> result = Result<int>(Error(ErrorCode::NOT_FOUND, "test"));
    EXPECT_TRUE(has_error(result, ErrorCode::NOT_FOUND));
}

TEST(HasErrorTest, NonMatchingError) {
    Result<int> result = Result<int>(Error(ErrorCode::NOT_FOUND, "test"));
    EXPECT_FALSE(has_error(result, ErrorCode::INVALID_ARGUMENT));
}

TEST(HasErrorTest, Success) {
    Result<int> result = ok(42);
    EXPECT_FALSE(has_error(result, ErrorCode::NOT_FOUND));
}

//=============================================================================
// has_error_category Tests
//=============================================================================

TEST(HasErrorCategoryTest, MatchingCategory) {
    // VALIDATION_FAILED is 0x0900 which is in VALIDATION category (0x09xx)
    Result<int> result = Result<int>(Error(ErrorCode::VALIDATION_FAILED, "test"));
    EXPECT_TRUE(has_error_category(result, ErrorCategory::VALIDATION));
}

TEST(HasErrorCategoryTest, NonMatchingCategory) {
    // INVALID_ARGUMENT is 0x0003 which is in GENERAL category (0x00xx)
    Result<int> result = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "test"));
    EXPECT_FALSE(has_error_category(result, ErrorCategory::IO));
}

TEST(HasErrorCategoryTest, Success) {
    Result<int> result = ok(42);
    EXPECT_FALSE(has_error_category(result, ErrorCategory::VALIDATION));
}

//=============================================================================
// first_success Tests
//=============================================================================

TEST(FirstSuccessTest, FirstIsSuccess) {
    Result<int> a = ok(1);
    Result<int> b = ok(2);

    auto result = first_success(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 1);
}

TEST(FirstSuccessTest, SecondIsSuccess) {
    Result<int> a = Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "a failed"));
    Result<int> b = ok(2);

    auto result = first_success(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 2);
}

TEST(FirstSuccessTest, BothFail) {
    Result<int> a = Result<int>(Error(ErrorCode::NOT_FOUND, "a failed"));
    Result<int> b = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "b failed"));

    auto result = first_success(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_error());
    // Returns last error
    EXPECT_EQ(result.code(), ErrorCode::INVALID_ARGUMENT);
}

//=============================================================================
// combine Tests
//=============================================================================

TEST(CombineTest, BothSuccess) {
    Result<int> a = ok(1);
    Result<std::string> b = ok(std::string("hello"));

    auto result = combine(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value().first, 1);
    EXPECT_EQ(result.value().second, "hello");
}

TEST(CombineTest, FirstFails) {
    Result<int> a = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "a failed"));
    Result<std::string> b = ok(std::string("hello"));

    auto result = combine(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(CombineTest, SecondFails) {
    Result<int> a = ok(1);
    Result<std::string> b = Result<std::string>(Error(ErrorCode::NOT_FOUND, "b failed"));

    auto result = combine(std::move(a), std::move(b));

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
}

//=============================================================================
// apply_all Tests
//=============================================================================

TEST(ApplyAllTest, AllSuccess) {
    Result<int> a = ok(1);
    Result<int> b = ok(2);
    Result<int> c = ok(3);

    auto result = apply_all([](int x, int y, int z) { return x + y + z; },
                            a, b, c);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 6);
}

TEST(ApplyAllTest, OneFails) {
    Result<int> a = ok(1);
    Result<int> b = Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "b failed"));
    Result<int> c = ok(3);

    auto result = apply_all([](int x, int y, int z) { return x + y + z; },
                            a, b, c);

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::INVALID_ARGUMENT);
}

//=============================================================================
// retry Tests
//=============================================================================

TEST(RetryTest, ImmediateSuccess) {
    int call_count = 0;

    auto result = retry([&call_count]() -> Result<int> {
        call_count++;
        return ok(42);
    }, 3);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(call_count, 1);
}

TEST(RetryTest, SuccessAfterRetries) {
    int call_count = 0;

    auto result = retry([&call_count]() -> Result<int> {
        call_count++;
        if (call_count < 3) {
            return Result<int>(Error(ErrorCode::OPERATION_TIMEOUT, "retry"));
        }
        return ok(42);
    }, 5);

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(call_count, 3);
}

TEST(RetryTest, AllRetriesFail) {
    int call_count = 0;

    auto result = retry([&call_count]() -> Result<int> {
        call_count++;
        return Result<int>(Error(ErrorCode::OPERATION_TIMEOUT, "always fails"));
    }, 3);

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(call_count, 3);
}

TEST(RetryTest, CustomPredicate) {
    int call_count = 0;

    auto result = retry([&call_count]() -> Result<int> {
        call_count++;
        return Result<int>(Error(ErrorCode::PERMISSION_DENIED, "not retryable"));
    }, 5, [](const Error& e) {
        // Only retry timeouts
        return e.code() == ErrorCode::OPERATION_TIMEOUT;
    });

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(call_count, 1);  // Did not retry non-timeout error
}

//=============================================================================
// Pipeline Tests
//=============================================================================

TEST(PipelineTest, SimpleChain) {
    auto result = Pipeline(ok(10))
        .map([](int x) { return x * 2; })
        .map([](int x) { return x + 1; })
        .result();

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 21);  // (10 * 2) + 1
}

TEST(PipelineTest, AndThenChain) {
    auto result = Pipeline(ok(10))
        .and_then([](int x) -> Result<int> { return ok(x * 2); })
        .and_then([](int x) -> Result<int> { return ok(x + 1); })
        .result();

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 21);
}

TEST(PipelineTest, ErrorRecovery) {
    auto result = Pipeline(Result<int>(Error(ErrorCode::NOT_FOUND, "error")))
        .or_else([](const Error&) -> Result<int> { return ok(0); })
        .map([](int x) { return x + 100; })
        .result();

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 100);
}

TEST(PipelineTest, MapError) {
    auto result = Pipeline(Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "original")))
        .map_error([]([[maybe_unused]] const Error& e) {
            return Error(ErrorCode::NOT_FOUND, "transformed");
        })
        .result();

    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(result.code(), ErrorCode::NOT_FOUND);
}

TEST(PipelineTest, Unwrap) {
    auto value = Pipeline(ok(42)).unwrap();
    EXPECT_EQ(value, 42);
}

TEST(PipelineTest, UnwrapThrows) {
    EXPECT_THROW(
        Pipeline(Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "error"))).unwrap(),
        std::runtime_error
    );
}

TEST(PipelineTest, ValueOr) {
    auto value1 = Pipeline(ok(42)).value_or(0);
    EXPECT_EQ(value1, 42);

    auto value2 = Pipeline(Result<int>(Error(ErrorCode::UNKNOWN_ERROR, "error")))
        .value_or(0);
    EXPECT_EQ(value2, 0);
}

TEST(PipelineTest, TypeTransformation) {
    auto result = Pipeline(ok(42))
        .map([](int x) { return std::to_string(x); })
        .map([](const std::string& s) { return s + "!"; })
        .result();

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), "42!");
}

TEST(PipelineTest, MakePipeline) {
    auto result = make_pipeline(ok(10))
        .map([](int x) { return x * 2; })
        .result();

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 20);
}

//=============================================================================
// Complex Scenario Tests
//=============================================================================

TEST(ComplexScenarioTest, ParseValidateApply) {
    // Simulate a config parsing pipeline
    auto parse = [](const std::string& s) -> Result<int> {
        try {
            return ok(std::stoi(s));
        } catch (...) {
            return Result<int>(Error(ErrorCode::INVALID_ARGUMENT, "parse failed"));
        }
    };

    auto validate = [](int x) -> Result<int> {
        if (x < 0 || x > 100) {
            return Result<int>(Error(ErrorCode::VALIDATION_FAILED, "out of range"));
        }
        return ok(x);
    };

    // Success case - test validation chain
    auto result1 = and_then(parse("42"), validate);
    ASSERT_TRUE(result1.is_success());
    EXPECT_EQ(result1.value(), 42);

    // Parse failure
    auto result2 = and_then(parse("abc"), validate);
    ASSERT_TRUE(result2.is_error());
    EXPECT_EQ(result2.code(), ErrorCode::INVALID_ARGUMENT);

    // Validation failure
    auto result3 = and_then(parse("200"), validate);
    ASSERT_TRUE(result3.is_error());
    EXPECT_EQ(result3.code(), ErrorCode::VALIDATION_FAILED);
}

TEST(ComplexScenarioTest, FallbackChain) {
    auto try_primary = []() -> Result<int> {
        return Result<int>(Error(ErrorCode::CONNECTION_FAILED, "primary down"));
    };

    auto try_secondary = []() -> Result<int> {
        return Result<int>(Error(ErrorCode::CONNECTION_FAILED, "secondary down"));
    };

    auto try_tertiary = []() -> Result<int> {
        return ok(42);
    };

    auto result = or_else(try_primary(), [&](const Error&) {
        return or_else(try_secondary(), [&](const Error&) {
            return try_tertiary();
        });
    });

    ASSERT_TRUE(result.is_success());
    EXPECT_EQ(result.value(), 42);
}
