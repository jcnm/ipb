#pragma once

/**
 * @file result_ext.hpp
 * @brief Monadic extensions for Result<T>
 *
 * Provides functional programming style operations:
 * - and_then: Chain operations that return Result<U>
 * - or_else: Handle errors with fallback
 * - map_error: Transform error codes/messages
 * - flatten: Convert Result<Result<T>> to Result<T>
 * - inspect: Peek at value without consuming
 * - unwrap_or_throw: Get value or throw exception
 *
 * Example:
 * @code
 * auto result = read_config("config.yaml")
 *     .and_then([](Config cfg) { return validate(cfg); })
 *     .and_then([](Config cfg) { return apply(cfg); })
 *     .or_else([](const Error& e) { return load_default_config(); })
 *     .map([](Config cfg) { return cfg.name; });
 * @endcode
 */

#include "error.hpp"
#include <exception>
#include <functional>
#include <type_traits>

namespace ipb::common {

// ============================================================================
// TYPE TRAITS HELPERS
// ============================================================================

namespace detail {

// Check if F(T) returns Result<U>
template<typename F, typename T, typename = void>
struct returns_result : std::false_type {};

template<typename F, typename T>
struct returns_result<F, T, std::void_t<
    typename std::invoke_result_t<F, T>::value_type
>> : std::true_type {};

template<typename F, typename T>
inline constexpr bool returns_result_v = returns_result<F, T>::value;

// Extract inner type from Result<T>
template<typename R>
struct result_value_type { using type = void; };

template<typename T>
struct result_value_type<Result<T>> { using type = T; };

template<typename R>
using result_value_type_t = typename result_value_type<R>::type;

}  // namespace detail

// ============================================================================
// RESULT MONADIC EXTENSIONS
// ============================================================================

/**
 * @brief Extension methods for Result<T> via free functions
 */

/**
 * @brief Chain a function that returns Result<U> on success
 *
 * Also known as flatMap or bind in functional programming.
 * If this Result is an error, propagates the error.
 * If this Result is success, applies func to the value.
 *
 * @param result The input Result
 * @param func Function that takes T and returns Result<U>
 * @return Result<U>
 */
template<typename T, typename F>
auto and_then(const Result<T>& result, F&& func)
    -> std::invoke_result_t<F, const T&>
{
    using ReturnType = std::invoke_result_t<F, const T&>;
    if (result.is_success()) {
        return func(result.value());
    }
    return ReturnType(result.error());
}

template<typename T, typename F>
auto and_then(Result<T>&& result, F&& func)
    -> std::invoke_result_t<F, T&&>
{
    using ReturnType = std::invoke_result_t<F, T&&>;
    if (result.is_success()) {
        return func(std::move(result).value());
    }
    return ReturnType(result.error());
}

// Specialization for Result<void>
template<typename F>
auto and_then(const Result<void>& result, F&& func)
    -> std::invoke_result_t<F>
{
    using ReturnType = std::invoke_result_t<F>;
    if (result.is_success()) {
        return func();
    }
    return ReturnType(result.error());
}

/**
 * @brief Handle error with a fallback function
 *
 * If this Result is success, returns it unchanged.
 * If this Result is an error, applies func to the error.
 *
 * @param result The input Result
 * @param func Function that takes const Error& and returns Result<T>
 * @return Result<T>
 */
template<typename T, typename F>
auto or_else(const Result<T>& result, F&& func)
    -> Result<T>
{
    if (result.is_success()) {
        return result;
    }
    return func(result.error());
}

template<typename T, typename F>
auto or_else(Result<T>&& result, F&& func)
    -> Result<T>
{
    if (result.is_success()) {
        return std::move(result);
    }
    return func(result.error());
}

/**
 * @brief Transform the error (if present)
 *
 * If this Result is success, returns it unchanged.
 * If this Result is an error, transforms the error using func.
 *
 * @param result The input Result
 * @param func Function that takes const Error& and returns Error
 * @return Result<T>
 */
template<typename T, typename F>
auto map_error(const Result<T>& result, F&& func)
    -> Result<T>
{
    if (result.is_success()) {
        return result;
    }
    return Result<T>(func(result.error()));
}

template<typename T, typename F>
auto map_error(Result<T>&& result, F&& func)
    -> Result<T>
{
    if (result.is_success()) {
        return std::move(result);
    }
    return Result<T>(func(result.error()));
}

/**
 * @brief Flatten Result<Result<T>> to Result<T>
 */
template<typename T>
Result<T> flatten(const Result<Result<T>>& result) {
    if (result.is_error()) {
        return Result<T>(result.error());
    }
    return result.value();
}

template<typename T>
Result<T> flatten(Result<Result<T>>&& result) {
    if (result.is_error()) {
        return Result<T>(result.error());
    }
    return std::move(result).value();
}

/**
 * @brief Inspect the value without consuming it (for debugging)
 *
 * @param result The input Result
 * @param func Function that takes const T& (side effect only)
 * @return const Result<T>& (unchanged)
 */
template<typename T, typename F>
const Result<T>& inspect(const Result<T>& result, F&& func) {
    if (result.is_success()) {
        func(result.value());
    }
    return result;
}

/**
 * @brief Inspect the error without consuming it
 */
template<typename T, typename F>
const Result<T>& inspect_error(const Result<T>& result, F&& func) {
    if (result.is_error()) {
        func(result.error());
    }
    return result;
}

/**
 * @brief Get the value or throw an exception
 */
template<typename T>
T unwrap_or_throw(Result<T>&& result) {
    if (result.is_success()) {
        return std::move(result).value();
    }
    throw std::runtime_error(result.error().to_string());
}

template<typename T>
const T& unwrap_or_throw(const Result<T>& result) {
    if (result.is_success()) {
        return result.value();
    }
    throw std::runtime_error(result.error().to_string());
}

/**
 * @brief Check if result contains specific error code
 */
template<typename T>
bool has_error(const Result<T>& result, ErrorCode code) {
    return result.is_error() && result.code() == code;
}

/**
 * @brief Check if result contains error from specific category
 */
template<typename T>
bool has_error_category(const Result<T>& result, ErrorCategory category) {
    return result.is_error() && get_category(result.code()) == category;
}

// ============================================================================
// RESULT COMBINATORS
// ============================================================================

/**
 * @brief Combine two Results, returning first success or last error
 */
template<typename T>
Result<T> first_success(Result<T>&& a, Result<T>&& b) {
    if (a.is_success()) return std::move(a);
    if (b.is_success()) return std::move(b);
    return std::move(b);  // Return last error
}

/**
 * @brief Combine two Results into a tuple (if both succeed)
 */
template<typename T, typename U>
Result<std::pair<T, U>> combine(Result<T>&& a, Result<U>&& b) {
    if (a.is_error()) return Result<std::pair<T, U>>(a.error());
    if (b.is_error()) return Result<std::pair<T, U>>(b.error());
    return ok(std::make_pair(std::move(a).value(), std::move(b).value()));
}

/**
 * @brief Apply a function to multiple Results (fails on first error)
 */
template<typename F, typename... Results>
auto apply_all(F&& func, Results&&... results) {
    // Check if any result is error
    bool has_err = (... || results.is_error());
    if (has_err) {
        // Find first error and return it
        Error first_err;
        bool found = false;
        ((results.is_error() && !found ? (first_err = results.error(), found = true) : false), ...);
        using ReturnType = std::invoke_result_t<F, decltype(results.value())...>;
        return Result<ReturnType>(first_err);
    }
    return ok(func(std::forward<Results>(results).value()...));
}

// ============================================================================
// RETRY UTILITIES
// ============================================================================

/**
 * @brief Retry a function that returns Result<T>
 *
 * @param func Function to retry
 * @param max_attempts Maximum number of attempts
 * @param should_retry Predicate to check if error is retryable
 * @return Result<T>
 */
template<typename F, typename Pred = decltype([](const Error&) { return true; })>
auto retry(F&& func, size_t max_attempts, Pred&& should_retry = {})
    -> std::invoke_result_t<F>
{
    using ReturnType = std::invoke_result_t<F>;
    
    ReturnType result = func();
    size_t attempts = 1;
    
    while (result.is_error() && attempts < max_attempts && should_retry(result.error())) {
        result = func();
        ++attempts;
    }
    
    return result;
}

/**
 * @brief Retry only transient errors
 */
template<typename F>
auto retry_transient(F&& func, size_t max_attempts)
    -> std::invoke_result_t<F>
{
    return retry(std::forward<F>(func), max_attempts, [](const Error& e) {
        return e.is_transient();
    });
}

// ============================================================================
// PIPELINE BUILDER
// ============================================================================

/**
 * @brief Fluent pipeline builder for chaining Result operations
 *
 * Example:
 * @code
 * auto result = Pipeline(read_file("input.txt"))
 *     .and_then(parse_json)
 *     .map([](auto& json) { return json["name"]; })
 *     .or_else([](auto&) { return ok(std::string("default")); })
 *     .result();
 * @endcode
 */
template<typename T>
class Pipeline {
public:
    explicit Pipeline(Result<T> result) : result_(std::move(result)) {}
    
    // Chain operation returning Result<U>
    template<typename F>
    auto and_then(F&& func) && {
        using U = detail::result_value_type_t<std::invoke_result_t<F, T>>;
        auto new_result = ::ipb::common::and_then(std::move(result_), std::forward<F>(func));
        return Pipeline<U>(std::move(new_result));
    }
    
    // Transform value
    template<typename F>
    auto map(F&& func) && {
        using U = std::invoke_result_t<F, T>;
        auto new_result = result_.map(std::forward<F>(func));
        return Pipeline<U>(std::move(new_result));
    }
    
    // Handle error with fallback
    template<typename F>
    Pipeline<T> or_else(F&& func) && {
        auto new_result = ::ipb::common::or_else(std::move(result_), std::forward<F>(func));
        return Pipeline<T>(std::move(new_result));
    }
    
    // Transform error
    template<typename F>
    Pipeline<T> map_error(F&& func) && {
        auto new_result = ::ipb::common::map_error(std::move(result_), std::forward<F>(func));
        return Pipeline<T>(std::move(new_result));
    }
    
    // Inspect without consuming
    template<typename F>
    Pipeline<T>& inspect(F&& func) & {
        ::ipb::common::inspect(result_, std::forward<F>(func));
        return *this;
    }
    
    // Get final result
    Result<T> result() && { return std::move(result_); }
    
    // Unwrap or throw
    T unwrap() && { return unwrap_or_throw(std::move(result_)); }
    
    // Get value or default
    T value_or(T default_val) && { return std::move(result_).value_or(std::move(default_val)); }

private:
    Result<T> result_;
};

// Helper to create pipeline
template<typename T>
Pipeline<T> make_pipeline(Result<T> result) {
    return Pipeline<T>(std::move(result));
}

}  // namespace ipb::common
