#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#if defined(IPB_OS_POSIX)
#include <pthread.h>
#include <unistd.h>  // For isatty, fileno
#elif defined(IPB_OS_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// Undefine Windows macros that conflict with our code
#ifdef ERROR
#undef ERROR
#endif
#endif

namespace ipb::common::debug {

// ============================================================================
// Thread-local storage for tracing
// ============================================================================

namespace {

struct ThreadContext {
    TraceId trace_id;
    SpanId span_id;
    std::string thread_name;
};

IPB_THREAD_LOCAL ThreadContext* tls_context = nullptr;

ThreadContext& get_thread_context() {
    if (!tls_context) {
        tls_context = new ThreadContext();
    }
    return *tls_context;
}

// Platform-safe localtime conversion
inline std::tm safe_localtime(const std::time_t* time) {
    std::tm result{};
#if defined(IPB_OS_WINDOWS)
    localtime_s(&result, time);
#else
    localtime_r(time, &result);
#endif
    return result;
}

// Random number generator for IDs
std::mt19937_64& get_rng() {
    static thread_local std::mt19937_64 rng(
        std::random_device{}() ^
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
        static_cast<uint64_t>(platform::get_thread_id()));
    return rng;
}

}  // anonymous namespace

// ============================================================================
// Log Level Parsing
// ============================================================================

LogLevel parse_log_level(std::string_view name) noexcept {
    // Convert to uppercase for comparison
    std::string upper;
    upper.reserve(name.size());
    for (char c : name) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    if (upper == "TRACE")
        return LogLevel::TRACE;
    if (upper == "DEBUG")
        return LogLevel::DEBUG;
    if (upper == "INFO")
        return LogLevel::INFO;
    if (upper == "WARN" || upper == "WARNING")
        return LogLevel::WARN;
    if (upper == "ERROR" || upper == "ERR")
        return LogLevel::ERROR;
    if (upper == "FATAL" || upper == "CRITICAL")
        return LogLevel::FATAL;
    if (upper == "OFF" || upper == "NONE")
        return LogLevel::OFF;

    return LogLevel::INFO;  // Default
}

// ============================================================================
// TraceId Implementation
// ============================================================================

TraceId TraceId::generate() noexcept {
    return TraceId(get_rng()());
}

TraceId TraceId::from_string(std::string_view str) noexcept {
    if (str.size() != 16)
        return TraceId();

    uint64_t id = 0;
    for (char c : str) {
        id <<= 4;
        if (c >= '0' && c <= '9') {
            id |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            id |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            id |= (c - 'A' + 10);
        } else {
            return TraceId();  // Invalid character
        }
    }
    return TraceId(id);
}

std::string TraceId::to_string() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << id_;
    return oss.str();
}

// ============================================================================
// SpanId Implementation
// ============================================================================

SpanId SpanId::generate() noexcept {
    return SpanId(get_rng()());
}

SpanId SpanId::from_string(std::string_view str) noexcept {
    if (str.size() != 16)
        return SpanId();

    uint64_t id = 0;
    for (char c : str) {
        id <<= 4;
        if (c >= '0' && c <= '9') {
            id |= (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            id |= (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            id |= (c - 'A' + 10);
        } else {
            return SpanId();
        }
    }
    return SpanId(id);
}

std::string SpanId::to_string() const {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << id_;
    return oss.str();
}

// ============================================================================
// LogFilter Implementation
// ============================================================================

void LogFilter::set_category_level(std::string_view category, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    category_levels_[std::string(category)] = level;
}

bool LogFilter::should_log(LogLevel level, std::string_view category) const noexcept {
    // First check global level
    if (level < global_level_.load(std::memory_order_relaxed)) {
        return false;
    }

    // Then check category-specific level
    if (!category.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = category_levels_.find(std::string(category));
        if (it != category_levels_.end()) {
            return level >= it->second;
        }
    }

    return true;
}

void LogFilter::reset() noexcept {
    global_level_.store(LogLevel::INFO, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    category_levels_.clear();
}

// ============================================================================
// ConsoleSink Implementation
// ============================================================================

namespace {

// ANSI color codes
constexpr const char* RESET = "\033[0m";
constexpr const char* BOLD  = "\033[1m";
constexpr const char* DIM   = "\033[2m";

constexpr const char* color_for_level(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::TRACE:
            return "\033[90m";  // Dark gray
        case LogLevel::DEBUG:
            return "\033[36m";  // Cyan
        case LogLevel::INFO:
            return "\033[32m";  // Green
        case LogLevel::WARN:
            return "\033[33m";  // Yellow
        case LogLevel::ERROR:
            return "\033[31m";  // Red
        case LogLevel::FATAL:
            return "\033[35m";  // Magenta
        default:
            return "";
    }
}

bool should_use_colors() noexcept {
#if defined(IPB_OS_WINDOWS)
    // Check if running in Windows Terminal or ConEmu
    return platform::get_env("WT_SESSION").length() > 0 || platform::get_env("ConEmuANSI") == "ON";
#else
    // Check if stdout is a TTY
    return isatty(fileno(stdout));
#endif
}

}  // anonymous namespace

ConsoleSink::ConsoleSink() : config_() {
    if (config_.use_colors) {
        config_.use_colors = should_use_colors();
    }
}

ConsoleSink::ConsoleSink(const Config& config) : config_(config) {
    if (config_.use_colors) {
        config_.use_colors = should_use_colors();
    }
}

ConsoleSink::~ConsoleSink() {
    flush();
}

void ConsoleSink::write(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostream& out =
        (config_.use_stderr && record.level >= LogLevel::ERROR) ? std::cerr : std::cout;

    // Timestamp
    if (config_.include_timestamp) {
        auto time = std::chrono::system_clock::to_time_t(record.timestamp);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                      record.timestamp.time_since_epoch()) %
                  1000;
        auto tm_result = safe_localtime(&time);

        if (config_.use_colors)
            out << DIM;
        out << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
            << std::setw(3) << ms.count();
        if (config_.use_colors)
            out << RESET;
        out << ' ';
    }

    // Level
    if (config_.use_colors) {
        out << color_for_level(record.level) << BOLD;
    }
    out << '[' << level_char(record.level) << ']';
    if (config_.use_colors)
        out << RESET;
    out << ' ';

    // Category
    if (!record.category.empty()) {
        if (config_.use_colors)
            out << "\033[34m";  // Blue
        out << '[' << record.category << ']';
        if (config_.use_colors)
            out << RESET;
        out << ' ';
    }

    // Thread ID
    if (config_.include_thread_id) {
        if (config_.use_colors)
            out << DIM;
        out << "[T:" << std::hex << record.thread_id << std::dec << ']';
        if (config_.use_colors)
            out << RESET;
        out << ' ';
    }

    // Trace ID
    if (config_.include_trace_id && record.trace_id.is_valid()) {
        if (config_.use_colors)
            out << DIM;
        out << "[trace:" << record.trace_id.to_string().substr(0, 8) << ']';
        if (config_.use_colors)
            out << RESET;
        out << ' ';
    }

    // Message
    out << record.message;

    // Location
    if (config_.include_location && record.location.is_valid()) {
        if (config_.use_colors)
            out << DIM;
        out << " (" << record.location.file << ':' << record.location.line << ')';
        if (config_.use_colors)
            out << RESET;
    }

    out << '\n';
}

void ConsoleSink::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout.flush();
    std::cerr.flush();
}

// ============================================================================
// FileSink Implementation
// ============================================================================

struct FileSink::Impl {
    Config config;
    std::ofstream file;
    std::mutex mutex;
    size_t current_size = 0;

    bool open() {
        file.open(config.file_path, std::ios::app);
        if (file.is_open()) {
            file.seekp(0, std::ios::end);
            current_size = static_cast<size_t>(file.tellp());
            return true;
        }
        return false;
    }

    void rotate() {
        file.close();

        // Rename existing files
        for (int i = static_cast<int>(config.max_files) - 1; i >= 0; --i) {
            std::string old_name = config.file_path;
            if (i > 0) {
                old_name += "." + std::to_string(i);
            }

            std::string new_name = config.file_path + "." + std::to_string(i + 1);

            std::rename(old_name.c_str(), new_name.c_str());
        }

        // Remove oldest file if over limit
        std::string oldest = config.file_path + "." + std::to_string(config.max_files);
        std::remove(oldest.c_str());

        // Open new file
        current_size = 0;
        open();
    }
};

FileSink::FileSink(Config config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->open();
}

FileSink::~FileSink() {
    flush();
}

void FileSink::write(const LogRecord& record) {
    if (!impl_->file.is_open())
        return;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::ostringstream oss;

    // Timestamp
    auto time = std::chrono::system_clock::to_time_t(record.timestamp);
    auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(record.timestamp.time_since_epoch()) %
        1000;
    auto tm_result = safe_localtime(&time);
    oss << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
        << std::setw(3) << ms.count() << ' ';

    // Level and category
    oss << level_name(record.level) << ' ';
    if (!record.category.empty()) {
        oss << '[' << record.category << "] ";
    }

    // Thread ID
    oss << "[T:" << std::hex << record.thread_id << std::dec << "] ";

    // Trace context
    if (record.trace_id.is_valid()) {
        oss << "[trace:" << record.trace_id.to_string() << "] ";
    }
    if (record.span_id.is_valid()) {
        oss << "[span:" << record.span_id.to_string() << "] ";
    }

    // Message
    oss << record.message;

    // Location
    if (record.location.is_valid()) {
        oss << " (" << record.location.file << ':' << record.location.line << ')';
    }

    oss << '\n';

    std::string line = oss.str();
    impl_->file << line;
    impl_->current_size += line.size();

    // Check if rotation needed
    if (impl_->config.max_file_size > 0 && impl_->current_size >= impl_->config.max_file_size) {
        impl_->rotate();
    }
}

void FileSink::flush() {
    if (impl_->file.is_open()) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->file.flush();
    }
}

bool FileSink::is_ready() const noexcept {
    return impl_->file.is_open();
}

// ============================================================================
// Logger Implementation
// ============================================================================

Logger& Logger::instance() noexcept {
    static Logger logger;
    return logger;
}

Logger::Logger() {
    // Add default console sink
    add_sink(std::make_shared<ConsoleSink>());
}

Logger::~Logger() {
    flush();
}

void Logger::add_sink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clear_sinks() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    sinks_.clear();
}

void Logger::log(LogLevel level, std::string_view category, std::string message,
                 SourceLocation loc) {
    log(level, category, std::move(message), TraceScope::current_trace_id(),
        TraceScope::current_span_id(), loc);
}

void Logger::log(LogLevel level, std::string_view category, std::string message, TraceId trace_id,
                 SpanId span_id, SourceLocation loc) {
    if (!filter_.should_log(level, category)) {
        return;
    }

    LogRecord record;
    record.level          = level;
    record.category       = category;
    record.message        = std::move(message);
    record.location       = loc;
    record.timestamp      = std::chrono::system_clock::now();
    record.monotonic_time = std::chrono::steady_clock::now();
    record.trace_id       = trace_id;
    record.span_id        = span_id;
    record.thread_id      = platform::get_thread_id();
    record.thread_name    = get_thread_name();

    dispatch(std::move(record));
}

void Logger::dispatch(LogRecord record) {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
        if (sink && sink->is_ready()) {
            sink->write(record);
        }
    }
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(sinks_mutex_);
    for (auto& sink : sinks_) {
        if (sink)
            sink->flush();
    }
}

void Logger::set_thread_name(std::string_view name) {
    get_thread_context().thread_name = std::string(name);

    // Also set OS thread name if possible
#if defined(IPB_OS_LINUX)
    pthread_setname_np(pthread_self(), std::string(name).c_str());
#elif defined(IPB_OS_MACOS)
    pthread_setname_np(std::string(name).c_str());
#endif
}

std::string_view Logger::get_thread_name() noexcept {
    return get_thread_context().thread_name;
}

// ============================================================================
// TraceScope Implementation
// ============================================================================

TraceScope::TraceScope(TraceId trace_id) : TraceScope(trace_id, SpanId::generate()) {}

TraceScope::TraceScope(TraceId trace_id, SpanId span_id) : trace_id_(trace_id), span_id_(span_id) {
    auto& ctx          = get_thread_context();
    previous_trace_id_ = ctx.trace_id;
    previous_span_id_  = ctx.span_id;
    ctx.trace_id       = trace_id_;
    ctx.span_id        = span_id_;
}

TraceScope::~TraceScope() {
    auto& ctx    = get_thread_context();
    ctx.trace_id = previous_trace_id_;
    ctx.span_id  = previous_span_id_;
}

TraceId TraceScope::current_trace_id() noexcept {
    return get_thread_context().trace_id;
}

SpanId TraceScope::current_span_id() noexcept {
    return get_thread_context().span_id;
}

// ============================================================================
// Span Implementation
// ============================================================================

Span::Span(std::string_view name, std::string_view category, SourceLocation loc)
    : name_(name), category_(category), location_(loc), trace_id_(TraceScope::current_trace_id()),
      span_id_(SpanId::generate()), parent_span_id_(TraceScope::current_span_id()),
      start_time_(std::chrono::steady_clock::now()) {
    // Log span start
    IPB_LOG_TRACE(category_, "Span started: " << name_);
}

Span::Span(std::string_view name, const Span& parent, SourceLocation loc)
    : name_(name), category_(parent.category_), location_(loc), trace_id_(parent.trace_id_),
      span_id_(SpanId::generate()), parent_span_id_(parent.span_id_),
      start_time_(std::chrono::steady_clock::now()) {}

Span::~Span() {
    auto duration = elapsed();
    auto us       = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();

    if (has_error_) {
        IPB_LOG_ERROR(category_, "Span completed with error: "
                                     << name_ << " duration=" << us << "us"
                                     << " error=" << error_name(error_code_)
                                     << (error_message_.empty() ? "" : " msg=") << error_message_);
    } else {
        IPB_LOG_DEBUG(category_, "Span completed: " << name_ << " duration=" << us << "us");
    }
}

Span& Span::add_context(std::string_view key, std::string_view value) {
    context_.emplace_back(std::string(key), std::string(value));
    return *this;
}

Span& Span::add_context(std::string_view key, int64_t value) {
    context_.emplace_back(std::string(key), std::to_string(value));
    return *this;
}

Span& Span::add_context(std::string_view key, double value) {
    context_.emplace_back(std::string(key), std::to_string(value));
    return *this;
}

void Span::set_error(ErrorCode code, std::string_view message) {
    has_error_     = true;
    error_code_    = code;
    error_message_ = std::string(message);
}

std::chrono::nanoseconds Span::elapsed() const noexcept {
    return std::chrono::steady_clock::now() - start_time_;
}

// ============================================================================
// Assertion Handling
// ============================================================================

namespace {
AssertHandler g_assert_handler = default_assert_handler;
}  // namespace

void set_assert_handler(AssertHandler handler) {
    g_assert_handler = handler ? handler : default_assert_handler;
}

AssertHandler get_assert_handler() noexcept {
    return g_assert_handler;
}

void default_assert_handler(const char* expr, const char* msg, const SourceLocation& loc) {
    std::ostringstream oss;
    oss << "Assertion failed: " << expr;
    if (msg) {
        oss << " - " << msg;
    }
    oss << " at " << loc.file << ":" << loc.line;
    if (loc.function[0] != '\0') {
        oss << " in " << loc.function;
    }

    IPB_FATAL(oss.str());
    Logger::instance().flush();

#ifdef IPB_BUILD_DEBUG
    std::abort();
#endif
}

void assert_fail(const char* expr, const char* msg, const SourceLocation& loc) {
    if (g_assert_handler) {
        g_assert_handler(expr, msg, loc);
    }
}

// ============================================================================
// Initialization
// ============================================================================

void init_logging(LogLevel level) {
    Logger::instance().set_level(level);

    // Check for environment variable override
    std::string env_level = platform::get_env("IPB_LOG_LEVEL");
    if (!env_level.empty()) {
        Logger::instance().set_level(parse_log_level(env_level));
    }
}

void shutdown_logging() {
    Logger::instance().flush();
    Logger::instance().clear_sinks();
}

}  // namespace ipb::common::debug
