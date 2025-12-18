/**
 * @file structured_logger.cpp
 * @brief Implementation of structured logging with JSON output
 */

#include "ipb/common/structured_logger.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace ipb::common::logging {

// ============================================================================
// Thread-local storage
// ============================================================================

namespace {
thread_local std::string tl_correlation_id;
thread_local const RequestContext* tl_request_context = nullptr;

std::string generate_uuid() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937_64 gen(rd());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t a = dis(gen);
    uint64_t b = dis(gen);
    
    char buf[37];
    snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<uint32_t>(a >> 32),
        static_cast<uint16_t>((a >> 16) & 0xFFFF),
        static_cast<uint16_t>(a & 0x0FFF) | 0x4000,
        static_cast<uint16_t>((b >> 48) & 0x3FFF) | 0x8000,
        static_cast<unsigned long long>(b & 0xFFFFFFFFFFFF));
    return std::string(buf);
}

std::string escape_json_string(std::string_view str) {
    std::string result;
    result.reserve(str.size() + 10);
    
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif
    
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    
    char result[40];
    snprintf(result, sizeof(result), "%s.%03dZ", buf, static_cast<int>(ms.count()));
    return std::string(result);
}

std::string field_value_to_json(const FieldValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss;
            oss << std::setprecision(15) << arg;
            return oss.str();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + escape_json_string(arg) + "\"";
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            std::string result = "[";
            for (size_t i = 0; i < arg.size(); ++i) {
                if (i > 0) result += ",";
                result += "\"" + escape_json_string(arg[i]) + "\"";
            }
            result += "]";
            return result;
        }
        return "null";
    }, value);
}

}  // namespace

// ============================================================================
// LogEntry Implementation
// ============================================================================

void LogEntry::emit() {
    StructuredLogger::instance().emit(*this);
}

std::string LogEntry::to_json() const {
    std::ostringstream oss;
    oss << "{";
    
    // Timestamp
    oss << "\"timestamp\":\"" << format_timestamp(timestamp_) << "\"";
    
    // Level
    oss << ",\"level\":\"" << debug::level_name(level_) << "\"";
    
    // Component
    oss << ",\"component\":\"" << escape_json_string(component_) << "\"";
    
    // Message
    if (!message_.empty()) {
        oss << ",\"message\":\"" << escape_json_string(message_) << "\"";
    }
    
    // Thread ID
    oss << ",\"thread_id\":" << thread_id_;
    
    // Trace context
    if (trace_id_.is_valid()) {
        oss << ",\"trace_id\":\"" << trace_id_.to_string() << "\"";
    }
    if (span_id_.is_valid()) {
        oss << ",\"span_id\":\"" << span_id_.to_string() << "\"";
    }
    
    // Correlation ID
    if (!tl_correlation_id.empty()) {
        oss << ",\"correlation_id\":\"" << escape_json_string(tl_correlation_id) << "\"";
    }
    
    // Error info
    if (error_code_) {
        oss << ",\"error_code\":" << static_cast<uint32_t>(*error_code_);
        oss << ",\"error_name\":\"" << error_name(*error_code_) << "\"";
    }
    if (error_message_) {
        oss << ",\"error_message\":\"" << escape_json_string(*error_message_) << "\"";
    }
    
    // Duration
    if (duration_) {
        oss << ",\"duration_ns\":" << duration_->count();
        double ms = static_cast<double>(duration_->count()) / 1e6;
        oss << ",\"duration_ms\":" << std::fixed << std::setprecision(3) << ms;
    }
    
    // Source location
    if (location_) {
        oss << ",\"source\":{";
        oss << "\"file\":\"" << escape_json_string(location_->file_name()) << "\"";
        oss << ",\"line\":" << location_->line();
        oss << ",\"function\":\"" << escape_json_string(location_->function_name()) << "\"";
        oss << "}";
    }
    
    // Custom fields
    if (!fields_.empty()) {
        oss << ",\"fields\":{";
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << escape_json_string(fields_[i].key) << "\":";
            oss << field_value_to_json(fields_[i].value);
        }
        oss << "}";
    }
    
    oss << "}";
    return oss.str();
}

std::string LogEntry::to_string() const {
    std::ostringstream oss;
    
    // Timestamp
    oss << format_timestamp(timestamp_);
    
    // Level
    oss << " [" << debug::level_name(level_) << "]";
    
    // Component
    oss << " [" << component_ << "]";
    
    // Correlation ID
    if (!tl_correlation_id.empty()) {
        oss << " [" << tl_correlation_id.substr(0, 8) << "]";
    }
    
    // Message
    oss << " " << message_;
    
    // Fields
    for (const auto& field : fields_) {
        oss << " " << field.key << "=";
        std::visit([&oss](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                oss << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                oss << (arg ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::string>) {
                oss << "\"" << arg << "\"";
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                oss << "[...]";
            } else {
                oss << arg;
            }
        }, field.value);
    }
    
    // Duration
    if (duration_) {
        double ms = static_cast<double>(duration_->count()) / 1e6;
        oss << " duration=" << std::fixed << std::setprecision(2) << ms << "ms";
    }
    
    // Error
    if (error_code_) {
        oss << " error=" << error_name(*error_code_);
    }
    
    return oss.str();
}

// ============================================================================
// StructuredConsoleSink Implementation
// ============================================================================

StructuredConsoleSink::StructuredConsoleSink(OutputFormat format)
    : format_(format)
{}

void StructuredConsoleSink::write(const LogEntry& entry) {
    std::string output;
    
    switch (format_) {
        case OutputFormat::JSON:
        case OutputFormat::JSON_PRETTY:
            output = entry.to_json();
            break;
        case OutputFormat::TEXT:
        case OutputFormat::LOGFMT:
        default:
            output = entry.to_string();
            break;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << output << std::endl;
}

void StructuredConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout.flush();
}

// ============================================================================
// StructuredFileSink Implementation
// ============================================================================

struct StructuredFileSink::Impl {
    Config config;
    std::ofstream file;
    std::mutex mutex;
    size_t current_size = 0;
    
    explicit Impl(Config cfg) : config(std::move(cfg)) {
        open_file();
    }
    
    void open_file() {
        file.open(config.path, std::ios::app);
        if (file) {
            file.seekp(0, std::ios::end);
            current_size = file.tellp();
        }
    }
    
    void rotate() {
        file.close();
        
        // Rename existing files
        for (uint32_t i = config.max_files - 1; i > 0; --i) {
            std::string old_name = config.path + "." + std::to_string(i);
            std::string new_name = config.path + "." + std::to_string(i + 1);
            std::rename(old_name.c_str(), new_name.c_str());
        }
        
        // Rename current file
        std::rename(config.path.c_str(), (config.path + ".1").c_str());
        
        // Open new file
        current_size = 0;
        open_file();
    }
};

StructuredFileSink::StructuredFileSink(Config config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{}

StructuredFileSink::~StructuredFileSink() = default;

void StructuredFileSink::write(const LogEntry& entry) {
    std::string output;
    
    switch (impl_->config.format) {
        case OutputFormat::JSON:
        case OutputFormat::JSON_PRETTY:
            output = entry.to_json();
            break;
        default:
            output = entry.to_string();
            break;
    }
    output += "\n";
    
    std::lock_guard<std::mutex> lock(impl_->mutex);
    
    if (impl_->current_size + output.size() > impl_->config.max_size) {
        impl_->rotate();
    }
    
    impl_->file << output;
    impl_->current_size += output.size();
}

void StructuredFileSink::flush() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->file.flush();
}

// ============================================================================
// StructuredLogger Implementation
// ============================================================================

StructuredLogger& StructuredLogger::instance() {
    static StructuredLogger logger;
    return logger;
}

StructuredLogger::StructuredLogger() = default;

StructuredLogger::~StructuredLogger() {
    shutdown();
}

void StructuredLogger::configure(const StructuredLoggerConfig& config) {
    config_ = config;
    
    if (config.async_logging && !running_) {
        async_queue_ = std::make_unique<LockFreeQueue<LogEntry>>(config.async_queue_size);
        running_ = true;
        async_thread_ = std::thread(&StructuredLogger::async_worker, this);
    }
}

void StructuredLogger::add_sink(std::shared_ptr<IStructuredSink> sink) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back(std::move(sink));
}

void StructuredLogger::clear_sinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();
}

void StructuredLogger::emit(const LogEntry& entry) {
    if (!is_enabled(entry.level())) {
        return;
    }
    
    if (config_.async_logging && async_queue_) {
        async_queue_->push(entry);
    } else {
        std::lock_guard<std::mutex> lock(sinks_mutex_);
        for (auto& sink : sinks_) {
            sink->write(entry);
        }
    }
}

void StructuredLogger::flush() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
        sink->flush();
    }
}

void StructuredLogger::shutdown() {
    if (running_) {
        running_ = false;
        if (async_thread_.joinable()) {
            async_thread_.join();
        }
    }
    flush();
}

void StructuredLogger::async_worker() {
    while (running_ || (async_queue_ && !async_queue_->empty())) {
        LogEntry entry(LogLevel::INFO, "");
        if (async_queue_ && async_queue_->try_pop(entry)) {
            std::lock_guard<std::mutex> lock(sinks_mutex_);
            for (auto& sink : sinks_) {
                sink->write(entry);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

// ============================================================================
// CorrelationContext Implementation
// ============================================================================

std::string CorrelationContext::get_correlation_id() {
    if (tl_correlation_id.empty()) {
        tl_correlation_id = generate_correlation_id();
    }
    return tl_correlation_id;
}

void CorrelationContext::set_correlation_id(std::string_view id) {
    tl_correlation_id = std::string(id);
}

void CorrelationContext::clear_correlation_id() {
    tl_correlation_id.clear();
}

std::string CorrelationContext::generate_correlation_id() {
    return generate_uuid();
}

CorrelationContext::Scope::Scope(std::string_view correlation_id)
    : correlation_id_(correlation_id)
    , previous_id_(tl_correlation_id)
{
    tl_correlation_id = correlation_id_;
}

CorrelationContext::Scope::Scope()
    : correlation_id_(generate_correlation_id())
    , previous_id_(tl_correlation_id)
{
    tl_correlation_id = correlation_id_;
}

CorrelationContext::Scope::~Scope() {
    tl_correlation_id = previous_id_;
}

// ============================================================================
// RequestContext Implementation
// ============================================================================

RequestContext RequestContext::create(std::string_view operation) {
    RequestContext ctx;
    ctx.correlation_id = CorrelationContext::generate_correlation_id();
    ctx.trace_id = TraceId::generate();
    ctx.span_id = SpanId::generate();
    ctx.operation_name = std::string(operation);
    ctx.start_time = std::chrono::system_clock::now();
    return ctx;
}

RequestContext RequestContext::create_child(std::string_view operation) const {
    RequestContext child;
    child.correlation_id = correlation_id;
    child.trace_id = trace_id;
    child.span_id = SpanId::generate();
    child.parent_span_id = span_id;
    child.service_name = service_name;
    child.operation_name = std::string(operation);
    child.start_time = std::chrono::system_clock::now();
    child.baggage = baggage;
    return child;
}

std::string RequestContext::to_traceparent() const {
    char buf[64];
    snprintf(buf, sizeof(buf), "00-%s-%s-01",
        trace_id.to_string().c_str(),
        span_id.to_string().c_str());
    return std::string(buf);
}

std::optional<RequestContext> RequestContext::from_traceparent(std::string_view header) {
    // Parse W3C Trace Context format: version-trace_id-parent_id-flags
    if (header.size() < 55) return std::nullopt;
    
    RequestContext ctx;
    // Simplified parsing - real implementation would be more robust
    ctx.trace_id = TraceId::from_string(header.substr(3, 32));
    ctx.span_id = SpanId::from_string(header.substr(36, 16));
    ctx.start_time = std::chrono::system_clock::now();
    
    return ctx;
}

// ============================================================================
// RequestScope Implementation
// ============================================================================

RequestScope::RequestScope(RequestContext ctx)
    : context_(std::move(ctx))
    , previous_(tl_request_context)
{
    tl_request_context = &context_;
    CorrelationContext::set_correlation_id(context_.correlation_id);
}

RequestScope::RequestScope(std::string_view operation)
    : context_(RequestContext::create(operation))
    , previous_(tl_request_context)
{
    tl_request_context = &context_;
    CorrelationContext::set_correlation_id(context_.correlation_id);
}

RequestScope::~RequestScope() {
    tl_request_context = previous_;
    if (previous_) {
        CorrelationContext::set_correlation_id(previous_->correlation_id);
    } else {
        CorrelationContext::clear_correlation_id();
    }
}

const RequestContext* RequestScope::current() {
    return tl_request_context;
}

}  // namespace ipb::common::logging
