#pragma once

/**
 * @file debug.hpp
 * @brief Modern debug and logging system for IPB
 *
 * Features:
 * - Hierarchical log levels
 * - Category-based filtering
 * - Trace/correlation IDs for request tracking
 * - Automatic source location capture
 * - Scope-based timing (spans)
 * - Thread-safe logging
 * - Zero-overhead when disabled
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "error.hpp"
#include "platform.hpp"

namespace ipb::common::debug {

// ============================================================================
// LOG LEVELS
// ============================================================================

/**
 * @brief Log severity levels
 */
enum class LogLevel : uint8_t {
    TRACE = 0,  // Finest granularity, very verbose
    DEBUG = 1,  // Debugging information
    INFO  = 2,  // Informational messages
    WARN  = 3,  // Warning conditions
    ERROR = 4,  // Error conditions
    FATAL = 5,  // Fatal errors, system about to crash
    OFF   = 6   // Logging disabled
};

/**
 * @brief Get log level name
 */
constexpr std::string_view level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        case LogLevel::OFF:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Get short level name (1 char)
 */
constexpr char level_char(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE:
            return 'T';
        case LogLevel::DEBUG:
            return 'D';
        case LogLevel::INFO:
            return 'I';
        case LogLevel::WARN:
            return 'W';
        case LogLevel::ERROR:
            return 'E';
        case LogLevel::FATAL:
            return 'F';
        default:
            return '?';
    }
}

/**
 * @brief Parse log level from string
 */
IPB_API LogLevel parse_log_level(std::string_view name) noexcept;

// ============================================================================
// LOG CATEGORIES
// ============================================================================

/**
 * @brief Predefined log categories for filtering
 */
namespace category {
constexpr std::string_view GENERAL   = "general";
constexpr std::string_view ROUTER    = "router";
constexpr std::string_view SCHEDULER = "scheduler";
constexpr std::string_view MESSAGING = "messaging";
constexpr std::string_view PROTOCOL  = "protocol";
constexpr std::string_view TRANSPORT = "transport";
constexpr std::string_view CONFIG    = "config";
constexpr std::string_view SECURITY  = "security";
constexpr std::string_view METRICS   = "metrics";
constexpr std::string_view LIFECYCLE = "lifecycle";
}  // namespace category

// ============================================================================
// TRACE/CORRELATION IDS
// ============================================================================

/**
 * @brief Unique identifier for tracing requests across components
 *
 * Format: 16 hex chars (64 bits) e.g., "a1b2c3d4e5f60718"
 */
class TraceId {
public:
    constexpr TraceId() noexcept : id_(0) {}
    explicit constexpr TraceId(uint64_t id) noexcept : id_(id) {}

    static TraceId generate() noexcept;
    static TraceId from_string(std::string_view str) noexcept;

    std::string to_string() const;
    constexpr uint64_t value() const noexcept { return id_; }
    constexpr bool is_valid() const noexcept { return id_ != 0; }

    constexpr bool operator==(const TraceId& other) const noexcept { return id_ == other.id_; }
    constexpr bool operator!=(const TraceId& other) const noexcept { return id_ != other.id_; }

private:
    uint64_t id_;
};

/**
 * @brief Span ID for tracking sub-operations within a trace
 */
class SpanId {
public:
    constexpr SpanId() noexcept : id_(0) {}
    explicit constexpr SpanId(uint64_t id) noexcept : id_(id) {}

    static SpanId generate() noexcept;
    static SpanId from_string(std::string_view str) noexcept;

    std::string to_string() const;
    constexpr uint64_t value() const noexcept { return id_; }
    constexpr bool is_valid() const noexcept { return id_ != 0; }

private:
    uint64_t id_;
};

// ============================================================================
// LOG RECORD
// ============================================================================

/**
 * @brief A single log entry with all context
 */
struct LogRecord {
    LogLevel level = LogLevel::INFO;
    std::string_view category;
    std::string message;
    SourceLocation location;

    // Timing
    std::chrono::system_clock::time_point timestamp;
    std::chrono::steady_clock::time_point monotonic_time;

    // Tracing
    TraceId trace_id;
    SpanId span_id;
    SpanId parent_span_id;

    // Thread info
    uint64_t thread_id = 0;
    std::string_view thread_name;

    // Additional context (key-value pairs)
    std::vector<std::pair<std::string, std::string>> context;
};

// ============================================================================
// LOG SINK INTERFACE
// ============================================================================

/**
 * @brief Interface for log output destinations
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;

    /**
     * @brief Write a log record
     */
    virtual void write(const LogRecord& record) = 0;

    /**
     * @brief Flush pending writes
     */
    virtual void flush() = 0;

    /**
     * @brief Check if sink is ready to accept logs
     */
    virtual bool is_ready() const noexcept = 0;
};

// ============================================================================
// BUILT-IN LOG SINKS
// ============================================================================

/**
 * @brief Console log sink with optional color support
 */
class ConsoleSink : public ILogSink {
public:
    struct Config {
        bool use_colors        = true;
        bool use_stderr        = false;  // Use stderr for errors
        bool include_timestamp = true;
        bool include_thread_id = true;
        bool include_location  = true;
        bool include_trace_id  = false;
    };

    ConsoleSink();  // Uses default config
    explicit ConsoleSink(const Config& config);
    ~ConsoleSink() override;

    void write(const LogRecord& record) override;
    void flush() override;
    bool is_ready() const noexcept override { return true; }

private:
    Config config_;
    std::mutex mutex_;
};

/**
 * @brief File log sink with rotation support
 */
class FileSink : public ILogSink {
public:
    struct Config {
        std::string file_path;
        size_t max_file_size = 10 * 1024 * 1024;  // 10MB
        uint32_t max_files   = 5;                 // Keep last 5 files
        bool async_write     = false;
    };

    explicit FileSink(Config config);
    ~FileSink() override;

    void write(const LogRecord& record) override;
    void flush() override;
    bool is_ready() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Callback-based sink for custom handling
 */
class CallbackSink : public ILogSink {
public:
    using Callback = std::function<void(const LogRecord&)>;

    explicit CallbackSink(Callback cb) : callback_(std::move(cb)) {}

    void write(const LogRecord& record) override {
        if (callback_)
            callback_(record);
    }

    void flush() override {}
    bool is_ready() const noexcept override { return callback_ != nullptr; }

private:
    Callback callback_;
};

// ============================================================================
// LOG FILTER
// ============================================================================

/**
 * @brief Log filtering configuration
 */
class LogFilter {
public:
    LogFilter() = default;

    /**
     * @brief Set global minimum log level
     */
    void set_level(LogLevel level) noexcept { global_level_ = level; }

    /**
     * @brief Set level for specific category
     */
    void set_category_level(std::string_view category, LogLevel level);

    /**
     * @brief Check if a log should be emitted
     */
    bool should_log(LogLevel level, std::string_view category) const noexcept;

    /**
     * @brief Reset all filters to defaults
     */
    void reset() noexcept;

private:
    std::atomic<LogLevel> global_level_{LogLevel::INFO};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, LogLevel> category_levels_;
};

// ============================================================================
// LOGGER
// ============================================================================

/**
 * @brief Thread-safe logger with multiple sinks
 */
class Logger {
public:
    /**
     * @brief Get the global logger instance
     */
    static Logger& instance() noexcept;

    /**
     * @brief Add a log sink
     */
    void add_sink(std::shared_ptr<ILogSink> sink);

    /**
     * @brief Remove all sinks
     */
    void clear_sinks();

    /**
     * @brief Get the filter configuration
     */
    LogFilter& filter() noexcept { return filter_; }
    const LogFilter& filter() const noexcept { return filter_; }

    /**
     * @brief Set global log level
     */
    void set_level(LogLevel level) noexcept { filter_.set_level(level); }

    /**
     * @brief Check if logging is enabled for level/category
     */
    bool is_enabled(LogLevel level, std::string_view category = {}) const noexcept {
        return filter_.should_log(level, category);
    }

    /**
     * @brief Log a message
     */
    void log(LogLevel level, std::string_view category, std::string message,
             SourceLocation loc = IPB_CURRENT_LOCATION);

    /**
     * @brief Log a message with trace context
     */
    void log(LogLevel level, std::string_view category, std::string message, TraceId trace_id,
             SpanId span_id, SourceLocation loc = IPB_CURRENT_LOCATION);

    /**
     * @brief Flush all sinks
     */
    void flush();

    /**
     * @brief Set thread name for current thread
     */
    static void set_thread_name(std::string_view name);
    static std::string_view get_thread_name() noexcept;

private:
    Logger();
    ~Logger();

    void dispatch(LogRecord record);

    LogFilter filter_;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    mutable std::mutex sinks_mutex_;
};

// ============================================================================
// SCOPED LOGGING CONTEXT
// ============================================================================

/**
 * @brief RAII scope that sets trace context for current thread
 */
class TraceScope {
public:
    explicit TraceScope(TraceId trace_id);
    TraceScope(TraceId trace_id, SpanId span_id);
    ~TraceScope();

    TraceScope(const TraceScope&)            = delete;
    TraceScope& operator=(const TraceScope&) = delete;

    TraceId trace_id() const noexcept { return trace_id_; }
    SpanId span_id() const noexcept { return span_id_; }

    /**
     * @brief Get current thread's trace ID
     */
    static TraceId current_trace_id() noexcept;

    /**
     * @brief Get current thread's span ID
     */
    static SpanId current_span_id() noexcept;

private:
    TraceId trace_id_;
    SpanId span_id_;
    TraceId previous_trace_id_;
    SpanId previous_span_id_;
};

// ============================================================================
// SPAN (TIMING SCOPE)
// ============================================================================

/**
 * @brief RAII scope for timing operations and logging duration
 */
class Span {
public:
    /**
     * @brief Create a span with automatic timing
     */
    Span(std::string_view name, std::string_view category = category::GENERAL,
         SourceLocation loc = IPB_CURRENT_LOCATION);

    /**
     * @brief Create a child span
     */
    Span(std::string_view name, const Span& parent, SourceLocation loc = IPB_CURRENT_LOCATION);

    ~Span();

    Span(const Span&)            = delete;
    Span& operator=(const Span&) = delete;

    /**
     * @brief Add key-value context to span
     */
    Span& add_context(std::string_view key, std::string_view value);
    Span& add_context(std::string_view key, int64_t value);
    Span& add_context(std::string_view key, double value);

    /**
     * @brief Mark span as failed
     */
    void set_error(ErrorCode code, std::string_view message = {});

    /**
     * @brief Get elapsed time so far
     */
    std::chrono::nanoseconds elapsed() const noexcept;

    SpanId id() const noexcept { return span_id_; }
    TraceId trace_id() const noexcept { return trace_id_; }

private:
    std::string name_;
    std::string_view category_;
    SourceLocation location_;

    TraceId trace_id_;
    SpanId span_id_;
    SpanId parent_span_id_;

    std::chrono::steady_clock::time_point start_time_;
    std::vector<std::pair<std::string, std::string>> context_;

    bool has_error_       = false;
    ErrorCode error_code_ = ErrorCode::SUCCESS;
    std::string error_message_;
};

// ============================================================================
// LOGGING MACROS
// ============================================================================

// Check if logging is enabled (for avoiding string construction)
#define IPB_LOG_ENABLED(level) \
    ::ipb::common::debug::Logger::instance().is_enabled(::ipb::common::debug::LogLevel::level)

#define IPB_LOG_ENABLED_CAT(level, cat) \
    ::ipb::common::debug::Logger::instance().is_enabled(::ipb::common::debug::LogLevel::level, cat)

// Core logging macro
#define IPB_LOG_IMPL(level, category, ...)                                                   \
    do {                                                                                     \
        auto& _ipb_logger = ::ipb::common::debug::Logger::instance();                        \
        if (_ipb_logger.is_enabled(::ipb::common::debug::LogLevel::level, category)) {       \
            std::ostringstream _ipb_oss;                                                     \
            _ipb_oss << __VA_ARGS__;                                                         \
            _ipb_logger.log(::ipb::common::debug::LogLevel::level, category, _ipb_oss.str(), \
                            IPB_CURRENT_LOCATION);                                           \
        }                                                                                    \
    } while (0)

// Category-specific macros
#define IPB_LOG_TRACE(cat, ...) IPB_LOG_IMPL(TRACE, cat, __VA_ARGS__)
#define IPB_LOG_DEBUG(cat, ...) IPB_LOG_IMPL(DEBUG, cat, __VA_ARGS__)
#define IPB_LOG_INFO(cat, ...)  IPB_LOG_IMPL(INFO, cat, __VA_ARGS__)
#define IPB_LOG_WARN(cat, ...)  IPB_LOG_IMPL(WARN, cat, __VA_ARGS__)
#define IPB_LOG_ERROR(cat, ...) IPB_LOG_IMPL(ERROR, cat, __VA_ARGS__)
#define IPB_LOG_FATAL(cat, ...) IPB_LOG_IMPL(FATAL, cat, __VA_ARGS__)

// Simplified macros (use GENERAL category)
#define IPB_TRACE(...) IPB_LOG_TRACE(::ipb::common::debug::category::GENERAL, __VA_ARGS__)
#define IPB_DEBUG(...) IPB_LOG_DEBUG(::ipb::common::debug::category::GENERAL, __VA_ARGS__)
#define IPB_INFO(...)  IPB_LOG_INFO(::ipb::common::debug::category::GENERAL, __VA_ARGS__)
#define IPB_WARN(...)  IPB_LOG_WARN(::ipb::common::debug::category::GENERAL, __VA_ARGS__)
#define IPB_ERROR(...) IPB_LOG_ERROR(::ipb::common::debug::category::GENERAL, __VA_ARGS__)
#define IPB_FATAL(...) IPB_LOG_FATAL(::ipb::common::debug::category::GENERAL, __VA_ARGS__)

// Span creation macro
#define IPB_SPAN(name)                                                                             \
    ::ipb::common::debug::Span _ipb_span_##__LINE__(name, ::ipb::common::debug::category::GENERAL, \
                                                    IPB_CURRENT_LOCATION)

#define IPB_SPAN_CAT(name, cat) \
    ::ipb::common::debug::Span _ipb_span_##__LINE__(name, cat, IPB_CURRENT_LOCATION)

// ============================================================================
// ASSERTIONS
// ============================================================================

/**
 * @brief Assertion handler callback
 */
using AssertHandler = void (*)(const char* expr, const char* msg, const SourceLocation& loc);

/**
 * @brief Set custom assertion handler
 */
IPB_API void set_assert_handler(AssertHandler handler);

/**
 * @brief Get current assertion handler
 */
IPB_API AssertHandler get_assert_handler() noexcept;

/**
 * @brief Default assertion handler (logs and aborts in debug, logs only in release)
 */
IPB_API void default_assert_handler(const char* expr, const char* msg, const SourceLocation& loc);

// Assertion implementation
IPB_API void assert_fail(const char* expr, const char* msg, const SourceLocation& loc);

// Main assertion macro - always checked
#define IPB_ASSERT(expr)                                                             \
    do {                                                                             \
        if (IPB_UNLIKELY(!(expr))) {                                                 \
            ::ipb::common::debug::assert_fail(#expr, nullptr, IPB_CURRENT_LOCATION); \
        }                                                                            \
    } while (0)

#define IPB_ASSERT_MSG(expr, msg)                                                \
    do {                                                                         \
        if (IPB_UNLIKELY(!(expr))) {                                             \
            ::ipb::common::debug::assert_fail(#expr, msg, IPB_CURRENT_LOCATION); \
        }                                                                        \
    } while (0)

// Debug-only assertions (compiled out in release)
#ifdef IPB_BUILD_DEBUG
#define IPB_DEBUG_ASSERT(expr)          IPB_ASSERT(expr)
#define IPB_DEBUG_ASSERT_MSG(expr, msg) IPB_ASSERT_MSG(expr, msg)
#else
#define IPB_DEBUG_ASSERT(expr)          ((void)0)
#define IPB_DEBUG_ASSERT_MSG(expr, msg) ((void)0)
#endif

// Precondition/Postcondition macros
#define IPB_PRECONDITION(expr)                                                                     \
    do {                                                                                           \
        if (IPB_UNLIKELY(!(expr))) {                                                               \
            ::ipb::common::debug::assert_fail(#expr, "Precondition failed", IPB_CURRENT_LOCATION); \
        }                                                                                          \
    } while (0)

#define IPB_POSTCONDITION(expr)                                              \
    do {                                                                     \
        if (IPB_UNLIKELY(!(expr))) {                                         \
            ::ipb::common::debug::assert_fail(#expr, "Postcondition failed", \
                                              IPB_CURRENT_LOCATION);         \
        }                                                                    \
    } while (0)

#define IPB_INVARIANT(expr)                                                                       \
    do {                                                                                          \
        if (IPB_UNLIKELY(!(expr))) {                                                              \
            ::ipb::common::debug::assert_fail(#expr, "Invariant violated", IPB_CURRENT_LOCATION); \
        }                                                                                         \
    } while (0)

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * @brief Initialize the logging system with defaults
 */
IPB_API void init_logging(LogLevel level = LogLevel::INFO);

/**
 * @brief Shutdown logging system cleanly
 */
IPB_API void shutdown_logging();

}  // namespace ipb::common::debug
