#pragma once

/**
 * @file tracing.hpp
 * @brief Distributed tracing abstraction with optional OpenTelemetry support
 *
 * Provides:
 * - W3C Trace Context compatible trace/span IDs
 * - Span creation and management
 * - Context propagation
 * - Exporters for different backends (console, OTLP, Jaeger)
 *
 * Can be used standalone or with OpenTelemetry SDK when available.
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "debug.hpp"
#include "error.hpp"
#include "platform.hpp"

namespace ipb::common::tracing {

using debug::SpanId;
using debug::TraceId;

// ============================================================================
// SPAN STATUS
// ============================================================================

/**
 * @brief Span status codes (compatible with OpenTelemetry)
 */
enum class SpanStatus : uint8_t {
    UNSET = 0,  ///< Default, not explicitly set
    OK = 1,     ///< Operation completed successfully
    ERROR = 2   ///< Operation failed
};

constexpr std::string_view span_status_name(SpanStatus status) noexcept {
    switch (status) {
        case SpanStatus::OK: return "OK";
        case SpanStatus::ERROR: return "ERROR";
        default: return "UNSET";
    }
}

/**
 * @brief Span kind (compatible with OpenTelemetry)
 */
enum class SpanKind : uint8_t {
    INTERNAL = 0,  ///< Internal operation
    SERVER = 1,    ///< Server-side of RPC
    CLIENT = 2,    ///< Client-side of RPC
    PRODUCER = 3,  ///< Message producer
    CONSUMER = 4   ///< Message consumer
};

constexpr std::string_view span_kind_name(SpanKind kind) noexcept {
    switch (kind) {
        case SpanKind::SERVER: return "SERVER";
        case SpanKind::CLIENT: return "CLIENT";
        case SpanKind::PRODUCER: return "PRODUCER";
        case SpanKind::CONSUMER: return "CONSUMER";
        default: return "INTERNAL";
    }
}

// ============================================================================
// SPAN ATTRIBUTES
// ============================================================================

/**
 * @brief Attribute value types
 */
using AttributeValue = std::variant<
    bool,
    int64_t,
    double,
    std::string,
    std::vector<bool>,
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>
>;

/**
 * @brief Span attribute
 */
struct Attribute {
    std::string key;
    AttributeValue value;
    
    Attribute(std::string k, bool v) : key(std::move(k)), value(v) {}
    Attribute(std::string k, int v) : key(std::move(k)), value(static_cast<int64_t>(v)) {}
    Attribute(std::string k, int64_t v) : key(std::move(k)), value(v) {}
    Attribute(std::string k, double v) : key(std::move(k)), value(v) {}
    Attribute(std::string k, const char* v) : key(std::move(k)), value(std::string(v)) {}
    Attribute(std::string k, std::string v) : key(std::move(k)), value(std::move(v)) {}
    Attribute(std::string k, std::string_view v) : key(std::move(k)), value(std::string(v)) {}
};

// ============================================================================
// SPAN EVENT
// ============================================================================

/**
 * @brief Event recorded during a span
 */
struct SpanEvent {
    std::string name;
    std::chrono::system_clock::time_point timestamp;
    std::vector<Attribute> attributes;
    
    SpanEvent(std::string n) 
        : name(std::move(n))
        , timestamp(std::chrono::system_clock::now())
    {}
    
    SpanEvent& add(std::string key, auto value) {
        attributes.emplace_back(std::move(key), std::move(value));
        return *this;
    }
};

// ============================================================================
// SPAN DATA
// ============================================================================

/**
 * @brief Complete span data for export
 */
struct SpanData {
    std::string name;
    TraceId trace_id;
    SpanId span_id;
    SpanId parent_span_id;
    
    SpanKind kind = SpanKind::INTERNAL;
    SpanStatus status = SpanStatus::UNSET;
    std::string status_message;
    
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    
    std::vector<Attribute> attributes;
    std::vector<SpanEvent> events;
    
    // Resource attributes
    std::string service_name;
    std::string service_version;
    
    // Computed duration
    std::chrono::nanoseconds duration() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    }
};

// ============================================================================
// SPAN EXPORTER INTERFACE
// ============================================================================

/**
 * @brief Interface for exporting spans to backends
 */
class ISpanExporter {
public:
    virtual ~ISpanExporter() = default;
    
    /**
     * @brief Export a batch of spans
     * @return true if export succeeded
     */
    virtual bool export_spans(const std::vector<SpanData>& spans) = 0;
    
    /**
     * @brief Force flush pending exports
     */
    virtual void flush() = 0;
    
    /**
     * @brief Shutdown the exporter
     */
    virtual void shutdown() = 0;
};

// ============================================================================
// CONSOLE EXPORTER
// ============================================================================

/**
 * @brief Exports spans to console (for debugging)
 */
class ConsoleExporter : public ISpanExporter {
public:
    struct Config {
        bool pretty_print = true;
        bool include_attributes = true;
        bool include_events = true;
    };
    
    explicit ConsoleExporter(Config config = {});
    
    bool export_spans(const std::vector<SpanData>& spans) override;
    void flush() override;
    void shutdown() override;
    
private:
    Config config_;
    std::mutex mutex_;
};

// ============================================================================
// SPAN CLASS
// ============================================================================

class Tracer;

/**
 * @brief Represents a unit of work in distributed tracing
 *
 * RAII-style span that automatically ends when destroyed.
 *
 * Example:
 * @code
 * auto span = tracer.start_span("process_request");
 * span.set_attribute("user.id", user_id);
 * span.add_event("validation_complete");
 * // ... do work ...
 * span.set_status(SpanStatus::OK);
 * // span automatically ends when it goes out of scope
 * @endcode
 */
class Span {
public:
    Span(Tracer& tracer, std::string name, SpanKind kind = SpanKind::INTERNAL);
    Span(Tracer& tracer, std::string name, const Span& parent, SpanKind kind = SpanKind::INTERNAL);
    ~Span();
    
    // Non-copyable, movable
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
    Span(Span&& other) noexcept;
    Span& operator=(Span&& other) noexcept;
    
    // Identification
    TraceId trace_id() const { return data_.trace_id; }
    SpanId span_id() const { return data_.span_id; }
    SpanId parent_span_id() const { return data_.parent_span_id; }
    const std::string& name() const { return data_.name; }
    
    // Attributes
    Span& set_attribute(std::string key, bool value);
    Span& set_attribute(std::string key, int value);
    Span& set_attribute(std::string key, int64_t value);
    Span& set_attribute(std::string key, double value);
    Span& set_attribute(std::string key, const char* value);
    Span& set_attribute(std::string key, std::string value);
    Span& set_attribute(std::string key, std::string_view value);
    
    // Events
    Span& add_event(std::string name);
    Span& add_event(SpanEvent event);
    
    // Status
    Span& set_status(SpanStatus status, std::string message = "");
    Span& set_error(ErrorCode code, std::string message = "");
    
    // Record exception
    Span& record_exception(const std::exception& e);
    Span& record_exception(ErrorCode code, std::string_view message);
    
    // Check if span is recording
    bool is_recording() const { return recording_; }
    
    // End span manually (also called by destructor)
    void end();
    
    // Get span data (for export)
    const SpanData& data() const { return data_; }
    
    // Context propagation (W3C Trace Context)
    std::string traceparent() const;
    std::string tracestate() const;

private:
    Tracer* tracer_;
    SpanData data_;
    bool recording_ = true;
    bool ended_ = false;
};

// ============================================================================
// TRACER CLASS
// ============================================================================

/**
 * @brief Configuration for tracer
 */
struct TracerConfig {
    std::string service_name = "ipb";
    std::string service_version = "1.0.0";
    std::string environment = "production";
    
    // Sampling
    double sample_rate = 1.0;  // 1.0 = trace everything
    
    // Batching
    size_t max_batch_size = 512;
    std::chrono::milliseconds batch_timeout{5000};
    
    // Export
    bool async_export = true;
    size_t export_queue_size = 2048;
};

/**
 * @brief Creates and manages spans
 */
class Tracer {
public:
    explicit Tracer(TracerConfig config = {});
    ~Tracer();
    
    // Non-copyable
    Tracer(const Tracer&) = delete;
    Tracer& operator=(const Tracer&) = delete;
    
    /**
     * @brief Add an exporter
     */
    void add_exporter(std::shared_ptr<ISpanExporter> exporter);
    
    /**
     * @brief Start a new root span
     */
    Span start_span(std::string name, SpanKind kind = SpanKind::INTERNAL);
    
    /**
     * @brief Start a child span
     */
    Span start_span(std::string name, const Span& parent, SpanKind kind = SpanKind::INTERNAL);
    
    /**
     * @brief Start a span from W3C traceparent header
     */
    Span start_span_from_context(std::string name, std::string_view traceparent,
                                  SpanKind kind = SpanKind::INTERNAL);
    
    /**
     * @brief Get current span (if any) for this thread
     */
    Span* current_span();
    
    /**
     * @brief Force flush all pending spans
     */
    void flush();
    
    /**
     * @brief Shutdown tracer
     */
    void shutdown();
    
    /**
     * @brief Get configuration
     */
    const TracerConfig& config() const { return config_; }
    
    // Internal: called when span ends
    void on_span_end(SpanData data);

private:
    void export_batch(std::vector<SpanData> batch);
    void async_export_worker();
    bool should_sample() const;
    
    TracerConfig config_;
    std::vector<std::shared_ptr<ISpanExporter>> exporters_;
    std::mutex exporters_mutex_;
    
    // Batching
    std::vector<SpanData> pending_spans_;
    std::mutex pending_mutex_;
    
    // Async export
    std::atomic<bool> running_{false};
    std::thread export_thread_;
    std::condition_variable export_cv_;
};

// ============================================================================
// GLOBAL TRACER
// ============================================================================

/**
 * @brief Get the global tracer instance
 */
Tracer& get_tracer();

/**
 * @brief Set the global tracer instance
 */
void set_tracer(std::unique_ptr<Tracer> tracer);

/**
 * @brief Initialize global tracer with config
 */
void init_tracing(TracerConfig config = {});

/**
 * @brief Shutdown global tracer
 */
void shutdown_tracing();

// ============================================================================
// SCOPED SPAN
// ============================================================================

/**
 * @brief RAII wrapper that ensures span is current for the scope
 */
class ScopedSpan {
public:
    ScopedSpan(std::string name, SpanKind kind = SpanKind::INTERNAL);
    ScopedSpan(std::string name, const Span& parent, SpanKind kind = SpanKind::INTERNAL);
    ~ScopedSpan();
    
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;
    
    Span& span() { return span_; }
    const Span& span() const { return span_; }
    
    // Delegate to inner span
    ScopedSpan& set_attribute(std::string key, auto value) {
        span_.set_attribute(std::move(key), std::move(value));
        return *this;
    }
    
    ScopedSpan& add_event(std::string name) {
        span_.add_event(std::move(name));
        return *this;
    }
    
    ScopedSpan& set_status(SpanStatus status, std::string message = "") {
        span_.set_status(status, std::move(message));
        return *this;
    }
    
    ScopedSpan& set_error(ErrorCode code, std::string message = "") {
        span_.set_error(code, std::move(message));
        return *this;
    }

private:
    Span span_;
    Span* previous_span_;
};

// ============================================================================
// MACROS
// ============================================================================

/**
 * @brief Start a scoped span with automatic naming
 */
#define IPB_TRACE_SPAN(name) \
    ::ipb::common::tracing::ScopedSpan _ipb_span_##__LINE__(name)

#define IPB_TRACE_SPAN_KIND(name, kind) \
    ::ipb::common::tracing::ScopedSpan _ipb_span_##__LINE__(name, kind)

/**
 * @brief Add attribute to current span (if any)
 */
#define IPB_TRACE_ATTR(key, value) \
    do { \
        if (auto* _span = ::ipb::common::tracing::get_tracer().current_span()) { \
            _span->set_attribute(key, value); \
        } \
    } while (0)

/**
 * @brief Add event to current span (if any)
 */
#define IPB_TRACE_EVENT(name) \
    do { \
        if (auto* _span = ::ipb::common::tracing::get_tracer().current_span()) { \
            _span->add_event(name); \
        } \
    } while (0)

}  // namespace ipb::common::tracing
