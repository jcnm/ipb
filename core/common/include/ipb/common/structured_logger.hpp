#pragma once

/**
 * @file structured_logger.hpp
 * @brief Structured logging with JSON output and correlation IDs
 *
 * Extends the existing debug.hpp logging system with:
 * - JSON formatted output for log aggregation
 * - Fluent API for adding structured fields
 * - Async logging support via lock-free queue
 * - Correlation ID propagation
 * - OpenTelemetry-compatible trace context
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include "debug.hpp"
#include "lockfree_queue.hpp"
#include "platform.hpp"

namespace ipb::common::logging {

using debug::LogLevel;
using debug::SpanId;
using debug::TraceId;
using ipb::common::SourceLocation;  // SourceLocation is in common::, not debug::

// ============================================================================
// FIELD VALUE TYPES
// ============================================================================

/**
 * @brief Supported field value types for structured logging
 */
using FieldValue = std::variant<
    std::nullptr_t,
    bool,
    int64_t,
    uint64_t,
    double,
    std::string,
    std::vector<std::string>
>;

/**
 * @brief A single field in a structured log entry
 */
struct Field {
    std::string key;
    FieldValue value;
    
    // Convenience constructors
    Field(std::string k, std::nullptr_t) : key(std::move(k)), value(nullptr) {}
    Field(std::string k, bool v) : key(std::move(k)), value(v) {}
    Field(std::string k, int v) : key(std::move(k)), value(static_cast<int64_t>(v)) {}
    Field(std::string k, int64_t v) : key(std::move(k)), value(v) {}
    Field(std::string k, uint64_t v) : key(std::move(k)), value(v) {}
    Field(std::string k, double v) : key(std::move(k)), value(v) {}
    Field(std::string k, const char* v) : key(std::move(k)), value(std::string(v)) {}
    Field(std::string k, std::string v) : key(std::move(k)), value(std::move(v)) {}
    Field(std::string k, std::string_view v) : key(std::move(k)), value(std::string(v)) {}
    Field(std::string k, std::vector<std::string> v) : key(std::move(k)), value(std::move(v)) {}
};

// ============================================================================
// STRUCTURED LOG ENTRY
// ============================================================================

/**
 * @brief A structured log entry with fields
 */
class LogEntry {
public:
    LogEntry(LogLevel level, std::string_view component)
        : level_(level)
        , component_(component)
        , timestamp_(std::chrono::system_clock::now())
        , thread_id_(std::hash<std::thread::id>{}(std::this_thread::get_id()))
        , trace_id_(debug::TraceScope::current_trace_id())
        , span_id_(debug::TraceScope::current_span_id())
    {}

    // Fluent API for adding fields
    LogEntry& msg(std::string message) {
        message_ = std::move(message);
        return *this;
    }
    
    LogEntry& field(std::string key, std::nullptr_t) {
        fields_.emplace_back(std::move(key), nullptr);
        return *this;
    }
    
    LogEntry& field(std::string key, bool value) {
        fields_.emplace_back(std::move(key), value);
        return *this;
    }
    
    LogEntry& field(std::string key, int value) {
        fields_.emplace_back(std::move(key), static_cast<int64_t>(value));
        return *this;
    }
    
    LogEntry& field(std::string key, int64_t value) {
        fields_.emplace_back(std::move(key), value);
        return *this;
    }
    
    LogEntry& field(std::string key, uint64_t value) {
        fields_.emplace_back(std::move(key), value);
        return *this;
    }
    
    LogEntry& field(std::string key, double value) {
        fields_.emplace_back(std::move(key), value);
        return *this;
    }
    
    LogEntry& field(std::string key, const char* value) {
        fields_.emplace_back(std::move(key), std::string(value));
        return *this;
    }
    
    LogEntry& field(std::string key, std::string value) {
        fields_.emplace_back(std::move(key), std::move(value));
        return *this;
    }
    
    LogEntry& field(std::string key, std::string_view value) {
        fields_.emplace_back(std::move(key), std::string(value));
        return *this;
    }
    
    // Error information
    LogEntry& error(ErrorCode code) {
        error_code_ = code;
        return *this;
    }
    
    LogEntry& error(ErrorCode code, std::string message) {
        error_code_ = code;
        error_message_ = std::move(message);
        return *this;
    }
    
    // Duration tracking
    LogEntry& duration(std::chrono::nanoseconds dur) {
        duration_ = dur;
        return *this;
    }
    
    template<typename Rep, typename Period>
    LogEntry& duration(std::chrono::duration<Rep, Period> dur) {
        duration_ = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
        return *this;
    }
    
    // Source location
    LogEntry& location(SourceLocation loc) {
        location_ = loc;
        return *this;
    }
    
    // Custom trace context
    LogEntry& trace(TraceId tid) {
        trace_id_ = tid;
        return *this;
    }
    
    LogEntry& span(SpanId sid) {
        span_id_ = sid;
        return *this;
    }
    
    // Emit the log entry
    void emit();
    
    // Convert to JSON string
    std::string to_json() const;
    
    // Convert to human-readable string
    std::string to_string() const;
    
    // Accessors
    LogLevel level() const { return level_; }
    std::string_view component() const { return component_; }
    const std::string& message() const { return message_; }
    const std::vector<Field>& fields() const { return fields_; }
    std::chrono::system_clock::time_point timestamp() const { return timestamp_; }
    uint64_t thread_id() const { return thread_id_; }
    TraceId trace_id() const { return trace_id_; }
    SpanId span_id() const { return span_id_; }
    std::optional<ErrorCode> error_code() const { return error_code_; }
    const std::optional<std::string>& error_message() const { return error_message_; }
    std::optional<std::chrono::nanoseconds> duration() const { return duration_; }
    const std::optional<SourceLocation>& source_location() const { return location_; }

private:
    LogLevel level_;
    std::string component_;
    std::string message_;
    std::vector<Field> fields_;
    
    std::chrono::system_clock::time_point timestamp_;
    uint64_t thread_id_;
    
    TraceId trace_id_;
    SpanId span_id_;
    
    std::optional<ErrorCode> error_code_;
    std::optional<std::string> error_message_;
    std::optional<std::chrono::nanoseconds> duration_;
    std::optional<SourceLocation> location_;
};

// ============================================================================
// STRUCTURED LOGGER
// ============================================================================

/**
 * @brief Output format for structured logging
 */
enum class OutputFormat {
    JSON,           // JSON format for log aggregation
    JSON_PRETTY,    // Pretty-printed JSON
    LOGFMT,         // logfmt key=value format
    TEXT            // Human-readable text
};

/**
 * @brief Configuration for structured logger
 */
struct StructuredLoggerConfig {
    OutputFormat format = OutputFormat::JSON;
    LogLevel min_level = LogLevel::INFO;
    bool async_logging = false;
    size_t async_queue_size = 10000;
    bool include_timestamp = true;
    bool include_thread_id = true;
    bool include_trace_id = true;
    bool include_source_location = false;
    std::string service_name = "ipb";
    std::string service_version = "1.0.0";
    std::string environment = "production";
};

/**
 * @brief Output sink interface for structured logs
 */
class IStructuredSink {
public:
    virtual ~IStructuredSink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() = 0;
};

/**
 * @brief Console sink for structured logging
 */
class StructuredConsoleSink : public IStructuredSink {
public:
    explicit StructuredConsoleSink(OutputFormat format = OutputFormat::TEXT);
    void write(const LogEntry& entry) override;
    void flush() override;
    
private:
    OutputFormat format_;
    std::mutex mutex_;
};

/**
 * @brief File sink for structured logging with rotation
 */
class StructuredFileSink : public IStructuredSink {
public:
    struct Config {
        std::string path;
        OutputFormat format = OutputFormat::JSON;
        size_t max_size = 100 * 1024 * 1024;  // 100MB
        uint32_t max_files = 10;
        bool compress_rotated = false;
    };
    
    explicit StructuredFileSink(Config config);
    ~StructuredFileSink();
    
    void write(const LogEntry& entry) override;
    void flush() override;
    
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Structured logger with fluent API
 */
class StructuredLogger {
public:
    /**
     * @brief Get the global structured logger instance
     */
    static StructuredLogger& instance();
    
    /**
     * @brief Configure the logger
     */
    void configure(const StructuredLoggerConfig& config);
    
    /**
     * @brief Add a sink
     */
    void add_sink(std::shared_ptr<IStructuredSink> sink);
    
    /**
     * @brief Clear all sinks
     */
    void clear_sinks();
    
    /**
     * @brief Set minimum log level
     */
    void set_level(LogLevel level) { config_.min_level = level; }
    
    /**
     * @brief Check if level is enabled
     */
    bool is_enabled(LogLevel level) const {
        return static_cast<uint8_t>(level) >= static_cast<uint8_t>(config_.min_level);
    }
    
    /**
     * @brief Create a log entry builder
     */
    LogEntry log(LogLevel level, std::string_view component) {
        return LogEntry(level, component);
    }
    
    // Convenience methods for each level
    LogEntry trace(std::string_view component) { return log(LogLevel::TRACE, component); }
    LogEntry debug(std::string_view component) { return log(LogLevel::DEBUG, component); }
    LogEntry info(std::string_view component) { return log(LogLevel::INFO, component); }
    LogEntry warn(std::string_view component) { return log(LogLevel::WARN, component); }
    LogEntry error(std::string_view component) { return log(LogLevel::ERROR, component); }
    LogEntry fatal(std::string_view component) { return log(LogLevel::FATAL, component); }
    
    /**
     * @brief Emit a log entry to all sinks
     */
    void emit(const LogEntry& entry);
    
    /**
     * @brief Flush all sinks
     */
    void flush();
    
    /**
     * @brief Shutdown async logging thread
     */
    void shutdown();
    
    /**
     * @brief Get current configuration
     */
    const StructuredLoggerConfig& config() const { return config_; }

private:
    StructuredLogger();
    ~StructuredLogger();
    
    void async_worker();
    
    StructuredLoggerConfig config_;
    std::vector<std::shared_ptr<IStructuredSink>> sinks_;
    std::mutex sinks_mutex_;
    
    // Async logging
    std::unique_ptr<LockFreeQueue<LogEntry>> async_queue_;
    std::atomic<bool> running_{false};
    std::thread async_thread_;
};

// ============================================================================
// CORRELATION CONTEXT
// ============================================================================

/**
 * @brief Thread-local correlation context for request tracking
 */
class CorrelationContext {
public:
    /**
     * @brief Get or create correlation ID for current context
     */
    static std::string get_correlation_id();
    
    /**
     * @brief Set correlation ID for current context
     */
    static void set_correlation_id(std::string_view id);
    
    /**
     * @brief Clear correlation ID for current context
     */
    static void clear_correlation_id();
    
    /**
     * @brief Generate a new correlation ID
     */
    static std::string generate_correlation_id();
    
    /**
     * @brief RAII scope for correlation context
     */
    class Scope {
    public:
        explicit Scope(std::string_view correlation_id);
        Scope(); // Generates new ID
        ~Scope();
        
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        
        const std::string& correlation_id() const { return correlation_id_; }
        
    private:
        std::string correlation_id_;
        std::string previous_id_;
    };
};

// ============================================================================
// REQUEST CONTEXT
// ============================================================================

/**
 * @brief Full request context with all tracing information
 */
struct RequestContext {
    std::string correlation_id;
    TraceId trace_id;
    SpanId span_id;
    SpanId parent_span_id;
    
    std::string service_name;
    std::string operation_name;
    
    std::chrono::system_clock::time_point start_time;
    std::unordered_map<std::string, std::string> baggage;
    
    /**
     * @brief Create a new request context
     */
    static RequestContext create(std::string_view operation);
    
    /**
     * @brief Create a child context for sub-operation
     */
    RequestContext create_child(std::string_view operation) const;
    
    /**
     * @brief Serialize to W3C Trace Context header format
     */
    std::string to_traceparent() const;
    
    /**
     * @brief Parse from W3C Trace Context header
     */
    static std::optional<RequestContext> from_traceparent(std::string_view header);
};

/**
 * @brief RAII scope for request context
 */
class RequestScope {
public:
    explicit RequestScope(RequestContext ctx);
    RequestScope(std::string_view operation);
    ~RequestScope();
    
    RequestScope(const RequestScope&) = delete;
    RequestScope& operator=(const RequestScope&) = delete;
    
    const RequestContext& context() const { return context_; }
    
    /**
     * @brief Get current request context (if any)
     */
    static const RequestContext* current();
    
private:
    RequestContext context_;
    const RequestContext* previous_;
};

// ============================================================================
// LOGGING MACROS
// ============================================================================

/**
 * @brief Create a structured log entry
 * Usage: SLOG_INFO("component").msg("message").field("key", value).emit();
 */
#define SLOG_TRACE(component) \
    ::ipb::common::logging::StructuredLogger::instance().trace(component).location(IPB_CURRENT_LOCATION)

#define SLOG_DEBUG(component) \
    ::ipb::common::logging::StructuredLogger::instance().debug(component).location(IPB_CURRENT_LOCATION)

#define SLOG_INFO(component) \
    ::ipb::common::logging::StructuredLogger::instance().info(component).location(IPB_CURRENT_LOCATION)

#define SLOG_WARN(component) \
    ::ipb::common::logging::StructuredLogger::instance().warn(component).location(IPB_CURRENT_LOCATION)

#define SLOG_ERROR(component) \
    ::ipb::common::logging::StructuredLogger::instance().error(component).location(IPB_CURRENT_LOCATION)

#define SLOG_FATAL(component) \
    ::ipb::common::logging::StructuredLogger::instance().fatal(component).location(IPB_CURRENT_LOCATION)

// ============================================================================
// TIMING MACROS
// ============================================================================

/**
 * @brief RAII timer that logs duration on scope exit
 */
class ScopedTimer {
public:
    ScopedTimer(std::string_view component, std::string_view operation)
        : component_(component)
        , operation_(operation)
        , start_(std::chrono::steady_clock::now())
    {}
    
    ~ScopedTimer() {
        auto elapsed = std::chrono::steady_clock::now() - start_;
        SLOG_DEBUG(component_)
            .msg(std::string(operation_) + " completed")
            .duration(elapsed)
            .field("operation", operation_)
            .emit();
    }
    
private:
    std::string_view component_;
    std::string_view operation_;
    std::chrono::steady_clock::time_point start_;
};

#define SLOG_TIMED(component, operation) \
    ::ipb::common::logging::ScopedTimer _slog_timer_##__LINE__(component, operation)

}  // namespace ipb::common::logging
