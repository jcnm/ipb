#pragma once

/**
 * @file error.hpp
 * @brief Comprehensive error handling system for IPB
 *
 * This header provides:
 * - Hierarchical error codes organized by category
 * - Rich error context with source location
 * - Error propagation without masking
 * - Compile-time and runtime error helpers
 */

#include "platform.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <utility>
#include <array>
#include <memory>
#include <vector>

#if defined(IPB_HAS_SOURCE_LOCATION)
    #include <source_location>
#endif

namespace ipb::common {

// ============================================================================
// ERROR CATEGORY SYSTEM
// ============================================================================

/**
 * @brief Error categories for hierarchical classification
 *
 * Categories are grouped by functional area:
 * - 0x00xx: General/Common errors
 * - 0x01xx: I/O and Connection errors
 * - 0x02xx: Protocol errors
 * - 0x03xx: Resource errors
 * - 0x04xx: Configuration errors
 * - 0x05xx: Security errors
 * - 0x06xx: Routing errors
 * - 0x07xx: Scheduling errors
 * - 0x08xx: Serialization errors
 * - 0x09xx: Validation errors
 * - 0x0Axx: Platform-specific errors
 */
enum class ErrorCategory : uint8_t {
    GENERAL     = 0x00,
    IO          = 0x01,
    PROTOCOL    = 0x02,
    RESOURCE    = 0x03,
    CONFIG      = 0x04,
    SECURITY    = 0x05,
    ROUTING     = 0x06,
    SCHEDULING  = 0x07,
    SERIALIZATION = 0x08,
    VALIDATION  = 0x09,
    PLATFORM    = 0x0A,
};

/**
 * @brief Get category name as string
 */
constexpr std::string_view category_name(ErrorCategory cat) noexcept {
    switch (cat) {
        case ErrorCategory::GENERAL:       return "General";
        case ErrorCategory::IO:            return "I/O";
        case ErrorCategory::PROTOCOL:      return "Protocol";
        case ErrorCategory::RESOURCE:      return "Resource";
        case ErrorCategory::CONFIG:        return "Configuration";
        case ErrorCategory::SECURITY:      return "Security";
        case ErrorCategory::ROUTING:       return "Routing";
        case ErrorCategory::SCHEDULING:    return "Scheduling";
        case ErrorCategory::SERIALIZATION: return "Serialization";
        case ErrorCategory::VALIDATION:    return "Validation";
        case ErrorCategory::PLATFORM:      return "Platform";
        default:                           return "Unknown";
    }
}

// ============================================================================
// ERROR CODE DEFINITIONS
// ============================================================================

/**
 * @brief Comprehensive error codes
 *
 * Format: 0xCCEE where CC = category, EE = specific error
 */
enum class ErrorCode : uint32_t {
    // ========== General (0x00xx) ==========
    SUCCESS             = 0x0000,
    UNKNOWN_ERROR       = 0x0001,
    NOT_IMPLEMENTED     = 0x0002,
    INVALID_ARGUMENT    = 0x0003,
    INVALID_STATE       = 0x0004,
    OPERATION_CANCELLED = 0x0005,
    OPERATION_TIMEOUT   = 0x0006,
    ALREADY_EXISTS      = 0x0007,
    NOT_FOUND           = 0x0008,
    PRECONDITION_FAILED = 0x0009,
    POSTCONDITION_FAILED = 0x000A,
    INVARIANT_VIOLATED  = 0x000B,
    ASSERTION_FAILED    = 0x000C,

    // ========== I/O and Connection (0x01xx) ==========
    CONNECTION_FAILED   = 0x0100,
    CONNECTION_REFUSED  = 0x0101,
    CONNECTION_RESET    = 0x0102,
    CONNECTION_TIMEOUT  = 0x0103,
    CONNECTION_CLOSED   = 0x0104,
    HOST_UNREACHABLE    = 0x0105,
    NETWORK_UNREACHABLE = 0x0106,
    DNS_RESOLUTION_FAILED = 0x0107,
    SOCKET_ERROR        = 0x0108,
    READ_ERROR          = 0x0109,
    WRITE_ERROR         = 0x010A,
    EOF_REACHED         = 0x010B,
    BROKEN_PIPE         = 0x010C,
    WOULD_BLOCK         = 0x010D,
    IN_PROGRESS         = 0x010E,
    ALREADY_CONNECTED   = 0x010F,
    NOT_CONNECTED       = 0x0110,
    IO_FILE_NOT_FOUND   = 0x0111,
    IO_SOCKET_ERROR     = 0x0112,
    RESOURCE_MEMORY_ALLOCATION_FAILED = 0x0113,

    // ========== Protocol (0x02xx) ==========
    PROTOCOL_ERROR      = 0x0200,
    INVALID_MESSAGE     = 0x0201,
    INVALID_HEADER      = 0x0202,
    INVALID_PAYLOAD     = 0x0203,
    INVALID_CHECKSUM    = 0x0204,
    UNSUPPORTED_VERSION = 0x0205,
    UNSUPPORTED_FEATURE = 0x0206,
    HANDSHAKE_FAILED    = 0x0207,
    AUTHENTICATION_FAILED = 0x0208,
    AUTHORIZATION_FAILED = 0x0209,
    MESSAGE_TOO_LARGE   = 0x020A,
    SEQUENCE_ERROR      = 0x020B,
    MALFORMED_DATA      = 0x020C,

    // ========== Resource (0x03xx) ==========
    OUT_OF_MEMORY       = 0x0300,
    BUFFER_OVERFLOW     = 0x0301,
    BUFFER_UNDERFLOW    = 0x0302,
    QUEUE_FULL          = 0x0303,
    QUEUE_EMPTY         = 0x0304,
    RESOURCE_EXHAUSTED  = 0x0305,
    RESOURCE_BUSY       = 0x0306,
    RESOURCE_UNAVAILABLE = 0x0307,
    TOO_MANY_HANDLES    = 0x0308,
    HANDLE_INVALID      = 0x0309,
    POOL_EXHAUSTED      = 0x030A,
    LIMIT_EXCEEDED      = 0x030B,
    CAPACITY_EXCEEDED   = 0x030C,

    // ========== Configuration (0x04xx) ==========
    CONFIG_INVALID      = 0x0400,
    CONFIG_MISSING      = 0x0401,
    CONFIG_PARSE_ERROR  = 0x0402,
    CONFIG_VALUE_OUT_OF_RANGE = 0x0403,
    CONFIG_TYPE_MISMATCH = 0x0404,
    CONFIG_REQUIRED_MISSING = 0x0405,
    CONFIG_FILE_NOT_FOUND = 0x0406,
    CONFIG_PERMISSION_DENIED = 0x0407,
    CONFIG_INVALID_VALUE = 0x0408,

    // ========== Security (0x05xx) ==========
    PERMISSION_DENIED   = 0x0500,
    ACCESS_DENIED       = 0x0501,
    CERTIFICATE_ERROR   = 0x0502,
    CERTIFICATE_EXPIRED = 0x0503,
    CERTIFICATE_REVOKED = 0x0504,
    CERTIFICATE_UNTRUSTED = 0x0505,
    ENCRYPTION_FAILED   = 0x0506,
    DECRYPTION_FAILED   = 0x0507,
    SIGNATURE_INVALID   = 0x0508,
    KEY_INVALID         = 0x0509,
    TOKEN_EXPIRED       = 0x050A,
    TOKEN_INVALID       = 0x050B,
    SECURITY_SSL_INIT_FAILED = 0x050C,
    SECURITY_CERTIFICATE_INVALID = 0x050D,
    SECURITY_KEY_INVALID = 0x050E,
    SECURITY_HANDSHAKE_FAILED = 0x050F,
    SECURITY_CRYPTO_ERROR = 0x0510,

    // ========== Routing (0x06xx) ==========
    ROUTE_NOT_FOUND     = 0x0600,
    RULE_NOT_FOUND      = 0x0601,
    RULE_INVALID        = 0x0602,
    RULE_CONFLICT       = 0x0603,
    SINK_NOT_FOUND      = 0x0604,
    SINK_UNAVAILABLE    = 0x0605,
    SINK_OVERLOADED     = 0x0606,
    ALL_SINKS_FAILED    = 0x0607,
    ROUTING_LOOP        = 0x0608,
    NO_MATCHING_RULE    = 0x0609,
    PATTERN_INVALID     = 0x060A,
    DEAD_LETTER_FULL    = 0x060B,

    // ========== Scheduling (0x07xx) ==========
    DEADLINE_MISSED     = 0x0700,
    TASK_CANCELLED      = 0x0701,
    TASK_FAILED         = 0x0702,
    SCHEDULER_STOPPED   = 0x0703,
    SCHEDULER_OVERLOADED = 0x0704,
    PRIORITY_INVALID    = 0x0705,
    TIMING_CONSTRAINT_VIOLATED = 0x0706,

    // ========== Serialization (0x08xx) ==========
    SERIALIZE_FAILED    = 0x0800,
    DESERIALIZE_FAILED  = 0x0801,
    FORMAT_UNSUPPORTED  = 0x0802,
    ENCODING_ERROR      = 0x0803,
    DECODING_ERROR      = 0x0804,
    TRUNCATED_DATA      = 0x0805,
    CORRUPT_DATA        = 0x0806,

    // ========== Validation (0x09xx) ==========
    VALIDATION_FAILED   = 0x0900,
    VALUE_OUT_OF_RANGE  = 0x0901,
    TYPE_MISMATCH       = 0x0902,
    NULL_POINTER        = 0x0903,
    EMPTY_VALUE         = 0x0904,
    SIZE_MISMATCH       = 0x0905,
    FORMAT_INVALID      = 0x0906,
    CONSTRAINT_VIOLATED = 0x0907,

    // ========== Platform (0x0Axx) ==========
    PLATFORM_ERROR      = 0x0A00,
    FEATURE_UNAVAILABLE = 0x0A01,
    SYSCALL_FAILED      = 0x0A02,
    SIGNAL_ERROR        = 0x0A03,
    THREAD_ERROR        = 0x0A04,
    FILE_NOT_FOUND      = 0x0A05,
    FILE_ACCESS_DENIED  = 0x0A06,
    DEVICE_NOT_FOUND    = 0x0A07,
    DEVICE_ERROR        = 0x0A08,
    OS_ERROR            = 0x0A09,
};

/**
 * @brief Extract category from error code
 */
constexpr ErrorCategory get_category(ErrorCode code) noexcept {
    return static_cast<ErrorCategory>((static_cast<uint32_t>(code) >> 8) & 0xFF);
}

/**
 * @brief Check if error code is success
 */
constexpr bool is_success(ErrorCode code) noexcept {
    return code == ErrorCode::SUCCESS;
}

/**
 * @brief Check if error code indicates a transient error (can retry)
 */
constexpr bool is_transient(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::CONNECTION_TIMEOUT:
        case ErrorCode::WOULD_BLOCK:
        case ErrorCode::IN_PROGRESS:
        case ErrorCode::RESOURCE_BUSY:
        case ErrorCode::QUEUE_FULL:
        case ErrorCode::SCHEDULER_OVERLOADED:
        case ErrorCode::SINK_OVERLOADED:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Check if error is fatal (unrecoverable)
 */
constexpr bool is_fatal(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::OUT_OF_MEMORY:
        case ErrorCode::INVARIANT_VIOLATED:
        case ErrorCode::ASSERTION_FAILED:
        case ErrorCode::CORRUPT_DATA:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Get human-readable error name
 */
constexpr std::string_view error_name(ErrorCode code) noexcept {
    switch (code) {
        // General
        case ErrorCode::SUCCESS:              return "SUCCESS";
        case ErrorCode::UNKNOWN_ERROR:        return "UNKNOWN_ERROR";
        case ErrorCode::NOT_IMPLEMENTED:      return "NOT_IMPLEMENTED";
        case ErrorCode::INVALID_ARGUMENT:     return "INVALID_ARGUMENT";
        case ErrorCode::INVALID_STATE:        return "INVALID_STATE";
        case ErrorCode::OPERATION_CANCELLED:  return "OPERATION_CANCELLED";
        case ErrorCode::OPERATION_TIMEOUT:    return "OPERATION_TIMEOUT";
        case ErrorCode::ALREADY_EXISTS:       return "ALREADY_EXISTS";
        case ErrorCode::NOT_FOUND:            return "NOT_FOUND";
        case ErrorCode::PRECONDITION_FAILED:  return "PRECONDITION_FAILED";
        case ErrorCode::POSTCONDITION_FAILED: return "POSTCONDITION_FAILED";
        case ErrorCode::INVARIANT_VIOLATED:   return "INVARIANT_VIOLATED";
        case ErrorCode::ASSERTION_FAILED:     return "ASSERTION_FAILED";

        // I/O
        case ErrorCode::CONNECTION_FAILED:    return "CONNECTION_FAILED";
        case ErrorCode::CONNECTION_REFUSED:   return "CONNECTION_REFUSED";
        case ErrorCode::CONNECTION_RESET:     return "CONNECTION_RESET";
        case ErrorCode::CONNECTION_TIMEOUT:   return "CONNECTION_TIMEOUT";
        case ErrorCode::CONNECTION_CLOSED:    return "CONNECTION_CLOSED";
        case ErrorCode::HOST_UNREACHABLE:     return "HOST_UNREACHABLE";
        case ErrorCode::NETWORK_UNREACHABLE:  return "NETWORK_UNREACHABLE";
        case ErrorCode::DNS_RESOLUTION_FAILED: return "DNS_RESOLUTION_FAILED";
        case ErrorCode::SOCKET_ERROR:         return "SOCKET_ERROR";
        case ErrorCode::READ_ERROR:           return "READ_ERROR";
        case ErrorCode::WRITE_ERROR:          return "WRITE_ERROR";
        case ErrorCode::EOF_REACHED:          return "EOF_REACHED";
        case ErrorCode::BROKEN_PIPE:          return "BROKEN_PIPE";
        case ErrorCode::WOULD_BLOCK:          return "WOULD_BLOCK";
        case ErrorCode::IN_PROGRESS:          return "IN_PROGRESS";
        case ErrorCode::ALREADY_CONNECTED:    return "ALREADY_CONNECTED";
        case ErrorCode::NOT_CONNECTED:        return "NOT_CONNECTED";
        case ErrorCode::IO_FILE_NOT_FOUND:    return "IO_FILE_NOT_FOUND";
        case ErrorCode::IO_SOCKET_ERROR:      return "IO_SOCKET_ERROR";
        case ErrorCode::RESOURCE_MEMORY_ALLOCATION_FAILED: return "RESOURCE_MEMORY_ALLOCATION_FAILED";

        // Protocol
        case ErrorCode::PROTOCOL_ERROR:       return "PROTOCOL_ERROR";
        case ErrorCode::INVALID_MESSAGE:      return "INVALID_MESSAGE";
        case ErrorCode::INVALID_HEADER:       return "INVALID_HEADER";
        case ErrorCode::INVALID_PAYLOAD:      return "INVALID_PAYLOAD";
        case ErrorCode::INVALID_CHECKSUM:     return "INVALID_CHECKSUM";
        case ErrorCode::UNSUPPORTED_VERSION:  return "UNSUPPORTED_VERSION";
        case ErrorCode::UNSUPPORTED_FEATURE:  return "UNSUPPORTED_FEATURE";
        case ErrorCode::HANDSHAKE_FAILED:     return "HANDSHAKE_FAILED";
        case ErrorCode::AUTHENTICATION_FAILED: return "AUTHENTICATION_FAILED";
        case ErrorCode::AUTHORIZATION_FAILED: return "AUTHORIZATION_FAILED";
        case ErrorCode::MESSAGE_TOO_LARGE:    return "MESSAGE_TOO_LARGE";
        case ErrorCode::SEQUENCE_ERROR:       return "SEQUENCE_ERROR";
        case ErrorCode::MALFORMED_DATA:       return "MALFORMED_DATA";

        // Resource
        case ErrorCode::OUT_OF_MEMORY:        return "OUT_OF_MEMORY";
        case ErrorCode::BUFFER_OVERFLOW:      return "BUFFER_OVERFLOW";
        case ErrorCode::BUFFER_UNDERFLOW:     return "BUFFER_UNDERFLOW";
        case ErrorCode::QUEUE_FULL:           return "QUEUE_FULL";
        case ErrorCode::QUEUE_EMPTY:          return "QUEUE_EMPTY";
        case ErrorCode::RESOURCE_EXHAUSTED:   return "RESOURCE_EXHAUSTED";
        case ErrorCode::RESOURCE_BUSY:        return "RESOURCE_BUSY";
        case ErrorCode::RESOURCE_UNAVAILABLE: return "RESOURCE_UNAVAILABLE";
        case ErrorCode::TOO_MANY_HANDLES:     return "TOO_MANY_HANDLES";
        case ErrorCode::HANDLE_INVALID:       return "HANDLE_INVALID";
        case ErrorCode::POOL_EXHAUSTED:       return "POOL_EXHAUSTED";
        case ErrorCode::LIMIT_EXCEEDED:       return "LIMIT_EXCEEDED";
        case ErrorCode::CAPACITY_EXCEEDED:    return "CAPACITY_EXCEEDED";

        // Configuration
        case ErrorCode::CONFIG_INVALID:       return "CONFIG_INVALID";
        case ErrorCode::CONFIG_MISSING:       return "CONFIG_MISSING";
        case ErrorCode::CONFIG_PARSE_ERROR:   return "CONFIG_PARSE_ERROR";
        case ErrorCode::CONFIG_VALUE_OUT_OF_RANGE: return "CONFIG_VALUE_OUT_OF_RANGE";
        case ErrorCode::CONFIG_TYPE_MISMATCH: return "CONFIG_TYPE_MISMATCH";
        case ErrorCode::CONFIG_REQUIRED_MISSING: return "CONFIG_REQUIRED_MISSING";
        case ErrorCode::CONFIG_FILE_NOT_FOUND: return "CONFIG_FILE_NOT_FOUND";
        case ErrorCode::CONFIG_PERMISSION_DENIED: return "CONFIG_PERMISSION_DENIED";
        case ErrorCode::CONFIG_INVALID_VALUE: return "CONFIG_INVALID_VALUE";

        // Security
        case ErrorCode::PERMISSION_DENIED:    return "PERMISSION_DENIED";
        case ErrorCode::ACCESS_DENIED:        return "ACCESS_DENIED";
        case ErrorCode::CERTIFICATE_ERROR:    return "CERTIFICATE_ERROR";
        case ErrorCode::CERTIFICATE_EXPIRED:  return "CERTIFICATE_EXPIRED";
        case ErrorCode::CERTIFICATE_REVOKED:  return "CERTIFICATE_REVOKED";
        case ErrorCode::CERTIFICATE_UNTRUSTED: return "CERTIFICATE_UNTRUSTED";
        case ErrorCode::ENCRYPTION_FAILED:    return "ENCRYPTION_FAILED";
        case ErrorCode::DECRYPTION_FAILED:    return "DECRYPTION_FAILED";
        case ErrorCode::SIGNATURE_INVALID:    return "SIGNATURE_INVALID";
        case ErrorCode::KEY_INVALID:          return "KEY_INVALID";
        case ErrorCode::TOKEN_EXPIRED:        return "TOKEN_EXPIRED";
        case ErrorCode::TOKEN_INVALID:        return "TOKEN_INVALID";
        case ErrorCode::SECURITY_SSL_INIT_FAILED: return "SECURITY_SSL_INIT_FAILED";
        case ErrorCode::SECURITY_CERTIFICATE_INVALID: return "SECURITY_CERTIFICATE_INVALID";
        case ErrorCode::SECURITY_KEY_INVALID: return "SECURITY_KEY_INVALID";
        case ErrorCode::SECURITY_HANDSHAKE_FAILED: return "SECURITY_HANDSHAKE_FAILED";
        case ErrorCode::SECURITY_CRYPTO_ERROR: return "SECURITY_CRYPTO_ERROR";

        // Routing
        case ErrorCode::ROUTE_NOT_FOUND:      return "ROUTE_NOT_FOUND";
        case ErrorCode::RULE_NOT_FOUND:       return "RULE_NOT_FOUND";
        case ErrorCode::RULE_INVALID:         return "RULE_INVALID";
        case ErrorCode::RULE_CONFLICT:        return "RULE_CONFLICT";
        case ErrorCode::SINK_NOT_FOUND:       return "SINK_NOT_FOUND";
        case ErrorCode::SINK_UNAVAILABLE:     return "SINK_UNAVAILABLE";
        case ErrorCode::SINK_OVERLOADED:      return "SINK_OVERLOADED";
        case ErrorCode::ALL_SINKS_FAILED:     return "ALL_SINKS_FAILED";
        case ErrorCode::ROUTING_LOOP:         return "ROUTING_LOOP";
        case ErrorCode::NO_MATCHING_RULE:     return "NO_MATCHING_RULE";
        case ErrorCode::PATTERN_INVALID:      return "PATTERN_INVALID";
        case ErrorCode::DEAD_LETTER_FULL:     return "DEAD_LETTER_FULL";

        // Scheduling
        case ErrorCode::DEADLINE_MISSED:      return "DEADLINE_MISSED";
        case ErrorCode::TASK_CANCELLED:       return "TASK_CANCELLED";
        case ErrorCode::TASK_FAILED:          return "TASK_FAILED";
        case ErrorCode::SCHEDULER_STOPPED:    return "SCHEDULER_STOPPED";
        case ErrorCode::SCHEDULER_OVERLOADED: return "SCHEDULER_OVERLOADED";
        case ErrorCode::PRIORITY_INVALID:     return "PRIORITY_INVALID";
        case ErrorCode::TIMING_CONSTRAINT_VIOLATED: return "TIMING_CONSTRAINT_VIOLATED";

        // Serialization
        case ErrorCode::SERIALIZE_FAILED:     return "SERIALIZE_FAILED";
        case ErrorCode::DESERIALIZE_FAILED:   return "DESERIALIZE_FAILED";
        case ErrorCode::FORMAT_UNSUPPORTED:   return "FORMAT_UNSUPPORTED";
        case ErrorCode::ENCODING_ERROR:       return "ENCODING_ERROR";
        case ErrorCode::DECODING_ERROR:       return "DECODING_ERROR";
        case ErrorCode::TRUNCATED_DATA:       return "TRUNCATED_DATA";
        case ErrorCode::CORRUPT_DATA:         return "CORRUPT_DATA";

        // Validation
        case ErrorCode::VALIDATION_FAILED:    return "VALIDATION_FAILED";
        case ErrorCode::VALUE_OUT_OF_RANGE:   return "VALUE_OUT_OF_RANGE";
        case ErrorCode::TYPE_MISMATCH:        return "TYPE_MISMATCH";
        case ErrorCode::NULL_POINTER:         return "NULL_POINTER";
        case ErrorCode::EMPTY_VALUE:          return "EMPTY_VALUE";
        case ErrorCode::SIZE_MISMATCH:        return "SIZE_MISMATCH";
        case ErrorCode::FORMAT_INVALID:       return "FORMAT_INVALID";
        case ErrorCode::CONSTRAINT_VIOLATED:  return "CONSTRAINT_VIOLATED";

        // Platform
        case ErrorCode::PLATFORM_ERROR:       return "PLATFORM_ERROR";
        case ErrorCode::FEATURE_UNAVAILABLE:  return "FEATURE_UNAVAILABLE";
        case ErrorCode::SYSCALL_FAILED:       return "SYSCALL_FAILED";
        case ErrorCode::SIGNAL_ERROR:         return "SIGNAL_ERROR";
        case ErrorCode::THREAD_ERROR:         return "THREAD_ERROR";
        case ErrorCode::FILE_NOT_FOUND:       return "FILE_NOT_FOUND";
        case ErrorCode::FILE_ACCESS_DENIED:   return "FILE_ACCESS_DENIED";
        case ErrorCode::DEVICE_NOT_FOUND:     return "DEVICE_NOT_FOUND";
        case ErrorCode::DEVICE_ERROR:         return "DEVICE_ERROR";
        case ErrorCode::OS_ERROR:             return "OS_ERROR";

        default:                              return "UNKNOWN";
    }
}

// ============================================================================
// SOURCE LOCATION
// ============================================================================

/**
 * @brief Source location information for error tracking
 */
struct SourceLocation {
    const char* file = "";
    const char* function = "";
    uint32_t line = 0;
    uint32_t column = 0;

    constexpr SourceLocation() noexcept = default;

    constexpr SourceLocation(const char* file_, const char* func_,
                            uint32_t line_, uint32_t col_ = 0) noexcept
        : file(file_), function(func_), line(line_), column(col_) {}

#if defined(IPB_HAS_SOURCE_LOCATION)
    constexpr SourceLocation(const std::source_location& loc) noexcept
        : file(loc.file_name())
        , function(loc.function_name())
        , line(loc.line())
        , column(loc.column()) {}

    static constexpr SourceLocation current(
        const std::source_location& loc = std::source_location::current()) noexcept {
        return SourceLocation(loc);
    }
#else
    static constexpr SourceLocation current() noexcept {
        return SourceLocation();
    }
#endif

    constexpr bool is_valid() const noexcept {
        return line > 0 && file[0] != '\0';
    }
};

// Helper macro for source location when std::source_location is not available
#if defined(IPB_HAS_SOURCE_LOCATION)
    #define IPB_CURRENT_LOCATION ::ipb::common::SourceLocation::current()
#else
    #define IPB_CURRENT_LOCATION ::ipb::common::SourceLocation(__FILE__, __func__, __LINE__)
#endif

// ============================================================================
// ERROR CONTEXT
// ============================================================================

/**
 * @brief Rich error information with context
 */
class Error {
public:
    constexpr Error() noexcept = default;

    constexpr Error(ErrorCode code) noexcept
        : code_(code) {}

    constexpr Error(ErrorCode code, std::string_view message) noexcept
        : code_(code), message_(message) {}

    Error(ErrorCode code, std::string_view message, SourceLocation loc) noexcept
        : code_(code), message_(message), location_(loc) {}

    Error(ErrorCode code, std::string message, SourceLocation loc) noexcept
        : code_(code), message_(std::move(message)), location_(loc) {}

    // Copy constructor (deep copy cause chain)
    Error(const Error& other)
        : code_(other.code_)
        , message_(other.message_)
        , location_(other.location_)
        , context_(other.context_) {
        if (other.cause_.has_value() && *other.cause_) {
            cause_ = std::make_optional(std::make_unique<Error>(**other.cause_));
        }
    }

    // Move constructor
    Error(Error&& other) noexcept = default;

    // Copy assignment (deep copy cause chain)
    Error& operator=(const Error& other) {
        if (this != &other) {
            code_ = other.code_;
            message_ = other.message_;
            location_ = other.location_;
            context_ = other.context_;
            if (other.cause_.has_value() && *other.cause_) {
                cause_ = std::make_optional(std::make_unique<Error>(**other.cause_));
            } else {
                cause_.reset();
            }
        }
        return *this;
    }

    // Move assignment
    Error& operator=(Error&& other) noexcept = default;

    // Accessors
    constexpr ErrorCode code() const noexcept { return code_; }
    constexpr ErrorCategory category() const noexcept { return get_category(code_); }
    const std::string& message() const noexcept { return message_; }
    constexpr const SourceLocation& location() const noexcept { return location_; }

    // Status checks
    constexpr bool is_success() const noexcept { return ipb::common::is_success(code_); }
    constexpr bool is_error() const noexcept { return !is_success(); }
    constexpr bool is_transient() const noexcept { return ipb::common::is_transient(code_); }
    constexpr bool is_fatal() const noexcept { return ipb::common::is_fatal(code_); }

    constexpr explicit operator bool() const noexcept { return is_success(); }

    // Get formatted error string
    std::string to_string() const;

    // Chain errors (for error wrapping)
    Error& with_cause(Error cause) {
        cause_ = std::make_optional(std::make_unique<Error>(std::move(cause)));
        return *this;
    }

    const Error* cause() const noexcept {
        return cause_.has_value() ? cause_->get() : nullptr;
    }

    // Context addition
    Error& with_context(std::string_view key, std::string_view value);

private:
    ErrorCode code_ = ErrorCode::SUCCESS;
    std::string message_;
    SourceLocation location_;
    std::optional<std::unique_ptr<Error>> cause_;
    std::vector<std::pair<std::string, std::string>> context_;
};

// ============================================================================
// RESULT TYPE
// ============================================================================

/**
 * @brief Modern Result type with rich error information
 *
 * This replaces the simpler Result<T> with better error handling.
 */
template<typename T = void>
class Result;

// Specialization for void
template<>
class Result<void> {
public:
    // Success
    constexpr Result() noexcept = default;

    // Error from code
    constexpr Result(ErrorCode code) noexcept : error_(code) {}

    // Error with message
    Result(ErrorCode code, std::string_view message,
           SourceLocation loc = IPB_CURRENT_LOCATION) noexcept
        : error_(code, message, loc) {}

    // Error from Error object
    Result(Error error) noexcept : error_(std::move(error)) {}

    // Status
    constexpr bool is_success() const noexcept { return error_.is_success(); }
    constexpr bool is_error() const noexcept { return error_.is_error(); }
    constexpr explicit operator bool() const noexcept { return is_success(); }

    // Error access
    constexpr ErrorCode code() const noexcept { return error_.code(); }
    const Error& error() const noexcept { return error_; }
    const std::string& message() const noexcept { return error_.message(); }

    // For backward compatibility
    constexpr ErrorCode error_code() const noexcept { return error_.code(); }
    const std::string& error_message() const noexcept { return error_.message(); }

    // Chain errors
    Result& with_cause(Error cause) {
        error_.with_cause(std::move(cause));
        return *this;
    }

private:
    Error error_;
};

// Specialization for non-void types
template<typename T>
class Result {
public:
    // Success with value
    Result(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(true) {
        new (&storage_) T(std::move(value));
    }

    // Error from code
    Result(ErrorCode code) noexcept : error_(code), has_value_(false) {}

    // Error with message
    Result(ErrorCode code, std::string_view message,
           SourceLocation loc = IPB_CURRENT_LOCATION) noexcept
        : error_(code, message, loc), has_value_(false) {}

    // Error from Error object
    Result(Error error) noexcept : error_(std::move(error)), has_value_(false) {}

    // Copy constructor
    Result(const Result& other) : error_(other.error_), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(other.value_ref());
        }
    }

    // Move constructor
    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : error_(std::move(other.error_)), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(std::move(other.value_ref()));
        }
    }

    // Copy assignment
    Result& operator=(const Result& other) {
        if (this != &other) {
            destroy_value();
            error_ = other.error_;
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_) T(other.value_ref());
            }
        }
        return *this;
    }

    // Move assignment
    Result& operator=(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            destroy_value();
            error_ = std::move(other.error_);
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_) T(std::move(other.value_ref()));
            }
        }
        return *this;
    }

    // Destructor
    ~Result() {
        destroy_value();
    }

    // Status
    bool is_success() const noexcept { return has_value_; }
    bool is_error() const noexcept { return !has_value_; }
    explicit operator bool() const noexcept { return is_success(); }

    // Value access (only call if is_success())
    T& value() & noexcept { return value_ref(); }
    const T& value() const& noexcept { return value_ref(); }
    T&& value() && noexcept { return std::move(value_ref()); }

    // Value access with default
    T value_or(T default_value) const& noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return has_value_ ? value_ref() : std::move(default_value);
    }

    T value_or(T default_value) && noexcept(std::is_nothrow_move_constructible_v<T>) {
        return has_value_ ? std::move(value_ref()) : std::move(default_value);
    }

    // Error access
    ErrorCode code() const noexcept { return has_value_ ? ErrorCode::SUCCESS : error_.code(); }
    const Error& error() const noexcept { return error_; }
    const std::string& message() const noexcept { return error_.message(); }

    // For backward compatibility
    ErrorCode error_code() const noexcept { return code(); }
    const std::string& error_message() const noexcept { return message(); }

    // Chain errors
    Result& with_cause(Error cause) {
        error_.with_cause(std::move(cause));
        return *this;
    }

    // Transform the value (if success)
    template<typename F>
    auto map(F&& func) const& -> Result<decltype(func(std::declval<const T&>()))> {
        using ReturnType = Result<decltype(func(std::declval<const T&>()))>;
        if (has_value_) {
            return ReturnType(func(value_ref()));
        }
        return ReturnType(error_);
    }

    template<typename F>
    auto map(F&& func) && -> Result<decltype(func(std::declval<T&&>()))> {
        using ReturnType = Result<decltype(func(std::declval<T&&>()))>;
        if (has_value_) {
            return ReturnType(func(std::move(value_ref())));
        }
        return ReturnType(error_);
    }

private:
    // Aligned storage for T
    alignas(T) unsigned char storage_[sizeof(T)];
    Error error_;
    bool has_value_;

    // Helper to access value
    T& value_ref() noexcept {
        return *reinterpret_cast<T*>(&storage_);
    }

    const T& value_ref() const noexcept {
        return *reinterpret_cast<const T*>(&storage_);
    }

    // Helper to destroy value if present
    void destroy_value() noexcept {
        if (has_value_) {
            value_ref().~T();
            has_value_ = false;
        }
    }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Create a success Result
 */
template<typename T>
Result<T> ok(T value) {
    return Result<T>(std::move(value));
}

inline Result<void> ok() {
    return Result<void>();
}

/**
 * @brief Create an error Result
 */
template<typename T = void>
Result<T> err(ErrorCode code,
              std::string_view message = {},
              SourceLocation loc = IPB_CURRENT_LOCATION) {
    return Result<T>(code, message, loc);
}

/**
 * @brief Create an error Result from Error object
 */
template<typename T = void>
Result<T> err(Error error) {
    return Result<T>(std::move(error));
}

// ============================================================================
// ERROR PROPAGATION MACROS
// ============================================================================

/**
 * @brief Return early if result is error
 *
 * Usage: IPB_TRY(some_function_returning_result());
 */
#define IPB_TRY(expr)                                               \
    do {                                                            \
        auto _ipb_result = (expr);                                  \
        if (IPB_UNLIKELY(_ipb_result.is_error())) {                \
            return _ipb_result;                                     \
        }                                                           \
    } while (0)

/**
 * @brief Return early if result is error, with custom error message
 */
#define IPB_TRY_MSG(expr, msg)                                      \
    do {                                                            \
        auto _ipb_result = (expr);                                  \
        if (IPB_UNLIKELY(_ipb_result.is_error())) {                \
            return ::ipb::common::err(_ipb_result.code(), msg,      \
                                      IPB_CURRENT_LOCATION)         \
                   .with_cause(_ipb_result.error());               \
        }                                                           \
    } while (0)

/**
 * @brief Assign value or return error
 *
 * Usage: IPB_TRY_ASSIGN(var, some_function_returning_result());
 */
#define IPB_TRY_ASSIGN(var, expr)                                   \
    auto _ipb_try_##var = (expr);                                   \
    if (IPB_UNLIKELY(_ipb_try_##var.is_error())) {                 \
        return _ipb_try_##var;                                      \
    }                                                               \
    var = std::move(_ipb_try_##var).value()

} // namespace ipb::common
