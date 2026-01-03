/**
 * @file test_structured_logger.cpp
 * @brief Comprehensive unit tests for ipb::common::logging structured logger
 *
 * Tests cover:
 * - Field and FieldValue types
 * - LogEntry creation and fluent API
 * - LogEntry serialization (JSON, string)
 * - StructuredLoggerConfig
 * - OutputFormat enum
 * - IStructuredSink interface
 * - StructuredConsoleSink
 * - StructuredLogger singleton and configuration
 * - CorrelationContext
 * - RequestContext
 * - RequestScope
 * - ScopedTimer
 */

#include <ipb/common/structured_logger.hpp>

#include <chrono>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::common::logging;
using namespace ipb::common;
using namespace std::chrono_literals;

// ============================================================================
// Test Helpers
// ============================================================================

// Mock sink to capture log entries for testing
class MockStructuredSink : public IStructuredSink {
public:
    void write(const LogEntry& entry) override {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(entry.to_json());
        last_entry_level_ = entry.level();
        last_entry_component_ = std::string(entry.component());
        last_entry_message_ = entry.message();
        write_count_++;
    }

    void flush() override {
        flush_count_++;
    }

    size_t write_count() const { return write_count_; }
    size_t flush_count() const { return flush_count_; }
    const std::vector<std::string>& entries() const { return entries_; }
    LogLevel last_level() const { return last_entry_level_; }
    std::string last_component() const { return last_entry_component_; }
    std::string last_message() const { return last_entry_message_; }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        write_count_ = 0;
        flush_count_ = 0;
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> entries_;
    std::atomic<size_t> write_count_{0};
    std::atomic<size_t> flush_count_{0};
    LogLevel last_entry_level_ = LogLevel::INFO;
    std::string last_entry_component_;
    std::string last_entry_message_;
};

// ============================================================================
// Field Tests
// ============================================================================

class FieldTest : public ::testing::Test {};

TEST_F(FieldTest, NullField) {
    Field f("null_field", nullptr);
    EXPECT_EQ(f.key, "null_field");
    EXPECT_TRUE(std::holds_alternative<std::nullptr_t>(f.value));
}

TEST_F(FieldTest, BoolField) {
    Field f("bool_field", true);
    EXPECT_EQ(f.key, "bool_field");
    EXPECT_TRUE(std::holds_alternative<bool>(f.value));
    EXPECT_TRUE(std::get<bool>(f.value));
}

TEST_F(FieldTest, IntField) {
    Field f("int_field", 42);
    EXPECT_EQ(f.key, "int_field");
    EXPECT_TRUE(std::holds_alternative<int64_t>(f.value));
    EXPECT_EQ(std::get<int64_t>(f.value), 42);
}

TEST_F(FieldTest, Int64Field) {
    Field f("int64_field", int64_t{9999999999});
    EXPECT_TRUE(std::holds_alternative<int64_t>(f.value));
    EXPECT_EQ(std::get<int64_t>(f.value), 9999999999);
}

TEST_F(FieldTest, Uint64Field) {
    Field f("uint64_field", uint64_t{18446744073709551615ULL});
    EXPECT_TRUE(std::holds_alternative<uint64_t>(f.value));
}

TEST_F(FieldTest, DoubleField) {
    Field f("double_field", 3.14159);
    EXPECT_TRUE(std::holds_alternative<double>(f.value));
    EXPECT_DOUBLE_EQ(std::get<double>(f.value), 3.14159);
}

TEST_F(FieldTest, CStringField) {
    Field f("cstring_field", "hello");
    EXPECT_TRUE(std::holds_alternative<std::string>(f.value));
    EXPECT_EQ(std::get<std::string>(f.value), "hello");
}

TEST_F(FieldTest, StringField) {
    Field f("string_field", std::string("world"));
    EXPECT_TRUE(std::holds_alternative<std::string>(f.value));
    EXPECT_EQ(std::get<std::string>(f.value), "world");
}

TEST_F(FieldTest, StringViewField) {
    std::string_view sv = "view";
    Field f("sv_field", sv);
    EXPECT_TRUE(std::holds_alternative<std::string>(f.value));
    EXPECT_EQ(std::get<std::string>(f.value), "view");
}

TEST_F(FieldTest, VectorField) {
    std::vector<std::string> vec = {"a", "b", "c"};
    Field f("vec_field", vec);
    EXPECT_TRUE(std::holds_alternative<std::vector<std::string>>(f.value));
    auto& stored = std::get<std::vector<std::string>>(f.value);
    EXPECT_EQ(stored.size(), 3u);
}

// ============================================================================
// LogEntry Tests
// ============================================================================

class LogEntryTest : public ::testing::Test {};

TEST_F(LogEntryTest, Construction) {
    LogEntry entry(LogLevel::INFO, "TestComponent");

    EXPECT_EQ(entry.level(), LogLevel::INFO);
    EXPECT_EQ(entry.component(), "TestComponent");
    EXPECT_TRUE(entry.message().empty());
    EXPECT_TRUE(entry.fields().empty());
}

TEST_F(LogEntryTest, SetMessage) {
    LogEntry entry(LogLevel::INFO, "Test");
    entry.msg("Test message");

    EXPECT_EQ(entry.message(), "Test message");
}

TEST_F(LogEntryTest, FluentAPI) {
    LogEntry entry(LogLevel::WARN, "Component");
    entry.msg("Warning occurred")
         .field("count", 42)
         .field("active", true)
         .field("rate", 3.14);

    EXPECT_EQ(entry.message(), "Warning occurred");
    EXPECT_EQ(entry.fields().size(), 3u);
}

TEST_F(LogEntryTest, FieldTypes) {
    LogEntry entry(LogLevel::DEBUG, "Test");
    entry.field("null_val", nullptr)
         .field("bool_val", false)
         .field("int_val", 100)
         .field("int64_val", int64_t{1000000000000})
         .field("uint64_val", uint64_t{2000000000000})
         .field("double_val", 2.718)
         .field("cstr_val", "c-string")
         .field("str_val", std::string("std-string"))
         .field("sv_val", std::string_view("string-view"));

    EXPECT_EQ(entry.fields().size(), 9u);
}

TEST_F(LogEntryTest, ErrorInfo) {
    LogEntry entry(LogLevel::ERROR, "ErrorComponent");
    entry.error(ErrorCode::CONNECTION_FAILED)
         .error(ErrorCode::OPERATION_TIMEOUT, "Connection timed out");

    EXPECT_TRUE(entry.error_code().has_value());
    EXPECT_EQ(*entry.error_code(), ErrorCode::OPERATION_TIMEOUT);
    EXPECT_TRUE(entry.error_message().has_value());
    EXPECT_EQ(*entry.error_message(), "Connection timed out");
}

TEST_F(LogEntryTest, Duration) {
    LogEntry entry(LogLevel::INFO, "Perf");
    entry.duration(std::chrono::milliseconds(150));

    EXPECT_TRUE(entry.duration().has_value());
    EXPECT_EQ(entry.duration()->count(), 150000000);  // nanoseconds
}

TEST_F(LogEntryTest, DurationNanoseconds) {
    LogEntry entry(LogLevel::INFO, "Perf");
    entry.duration(std::chrono::nanoseconds(12345));

    EXPECT_TRUE(entry.duration().has_value());
    EXPECT_EQ(entry.duration()->count(), 12345);
}

TEST_F(LogEntryTest, SourceLocation) {
    LogEntry entry(LogLevel::DEBUG, "Debug");
    common::SourceLocation loc("test.cpp", "testFunction", 42);
    entry.location(loc);

    EXPECT_TRUE(entry.source_location().has_value());
    EXPECT_EQ(entry.source_location()->file, "test.cpp");
    EXPECT_EQ(entry.source_location()->line, 42);
}

TEST_F(LogEntryTest, TraceContext) {
    LogEntry entry(LogLevel::INFO, "Trace");
    debug::TraceId tid;
    debug::SpanId sid;

    entry.trace(tid).span(sid);

    EXPECT_EQ(entry.trace_id(), tid);
    EXPECT_EQ(entry.span_id(), sid);
}

TEST_F(LogEntryTest, Timestamp) {
    auto before = std::chrono::system_clock::now();
    LogEntry entry(LogLevel::INFO, "Time");
    auto after = std::chrono::system_clock::now();

    EXPECT_GE(entry.timestamp(), before);
    EXPECT_LE(entry.timestamp(), after);
}

TEST_F(LogEntryTest, ThreadId) {
    LogEntry entry(LogLevel::INFO, "Thread");
    EXPECT_NE(entry.thread_id(), 0u);
}

TEST_F(LogEntryTest, ToJson) {
    LogEntry entry(LogLevel::ERROR, "JsonTest");
    entry.msg("Test message")
         .field("key1", "value1")
         .field("key2", 42);

    std::string json = entry.to_json();

    // Basic JSON structure checks
    EXPECT_NE(json.find("\"level\""), std::string::npos);
    EXPECT_NE(json.find("\"component\":\"JsonTest\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Test message\""), std::string::npos);
}

TEST_F(LogEntryTest, ToString) {
    LogEntry entry(LogLevel::WARN, "StringTest");
    entry.msg("Warning message");

    std::string str = entry.to_string();

    EXPECT_NE(str.find("WARN"), std::string::npos);
    EXPECT_NE(str.find("StringTest"), std::string::npos);
    EXPECT_NE(str.find("Warning message"), std::string::npos);
}

// ============================================================================
// StructuredLoggerConfig Tests
// ============================================================================

class StructuredLoggerConfigTest : public ::testing::Test {};

TEST_F(StructuredLoggerConfigTest, DefaultValues) {
    StructuredLoggerConfig config;

    EXPECT_EQ(config.format, OutputFormat::JSON);
    EXPECT_EQ(config.min_level, LogLevel::INFO);
    EXPECT_FALSE(config.async_logging);
    EXPECT_EQ(config.async_queue_size, 10000u);
    EXPECT_TRUE(config.include_timestamp);
    EXPECT_TRUE(config.include_thread_id);
    EXPECT_TRUE(config.include_trace_id);
    EXPECT_FALSE(config.include_source_location);
    EXPECT_EQ(config.service_name, "ipb");
}

TEST_F(StructuredLoggerConfigTest, CustomValues) {
    StructuredLoggerConfig config;
    config.format = OutputFormat::LOGFMT;
    config.min_level = LogLevel::DEBUG;
    config.async_logging = true;
    config.service_name = "custom_service";

    EXPECT_EQ(config.format, OutputFormat::LOGFMT);
    EXPECT_EQ(config.min_level, LogLevel::DEBUG);
    EXPECT_TRUE(config.async_logging);
    EXPECT_EQ(config.service_name, "custom_service");
}

// ============================================================================
// OutputFormat Tests
// ============================================================================

class OutputFormatTest : public ::testing::Test {};

TEST_F(OutputFormatTest, EnumValues) {
    EXPECT_EQ(static_cast<int>(OutputFormat::JSON), 0);
    EXPECT_EQ(static_cast<int>(OutputFormat::JSON_PRETTY), 1);
    EXPECT_EQ(static_cast<int>(OutputFormat::LOGFMT), 2);
    EXPECT_EQ(static_cast<int>(OutputFormat::TEXT), 3);
}

// ============================================================================
// StructuredConsoleSink Tests
// ============================================================================

class StructuredConsoleSinkTest : public ::testing::Test {};

TEST_F(StructuredConsoleSinkTest, DefaultConstruction) {
    // Just verify it doesn't crash
    StructuredConsoleSink sink;
}

TEST_F(StructuredConsoleSinkTest, ConstructWithFormat) {
    StructuredConsoleSink sink(OutputFormat::JSON);
    StructuredConsoleSink sink2(OutputFormat::TEXT);
    StructuredConsoleSink sink3(OutputFormat::LOGFMT);
}

TEST_F(StructuredConsoleSinkTest, WriteAndFlush) {
    // Redirect stdout temporarily
    std::stringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());

    {
        StructuredConsoleSink sink(OutputFormat::TEXT);
        LogEntry entry(LogLevel::INFO, "ConsoleSinkTest");
        entry.msg("Test output");

        sink.write(entry);
        sink.flush();
    }

    std::cout.rdbuf(old);

    std::string output = buffer.str();
    EXPECT_NE(output.find("ConsoleSinkTest"), std::string::npos);
}

// ============================================================================
// StructuredLogger Tests
// ============================================================================

class StructuredLoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_sink_ = std::make_shared<MockStructuredSink>();
        StructuredLogger::instance().clear_sinks();
        StructuredLogger::instance().add_sink(mock_sink_);
    }

    void TearDown() override {
        StructuredLogger::instance().clear_sinks();
    }

    std::shared_ptr<MockStructuredSink> mock_sink_;
};

TEST_F(StructuredLoggerTest, Singleton) {
    auto& logger1 = StructuredLogger::instance();
    auto& logger2 = StructuredLogger::instance();

    EXPECT_EQ(&logger1, &logger2);
}

TEST_F(StructuredLoggerTest, Configure) {
    StructuredLoggerConfig config;
    config.min_level = LogLevel::DEBUG;

    StructuredLogger::instance().configure(config);

    EXPECT_EQ(StructuredLogger::instance().config().min_level, LogLevel::DEBUG);
}

TEST_F(StructuredLoggerTest, SetLevel) {
    StructuredLogger::instance().set_level(LogLevel::WARN);

    EXPECT_EQ(StructuredLogger::instance().config().min_level, LogLevel::WARN);
}

TEST_F(StructuredLoggerTest, IsEnabled) {
    StructuredLogger::instance().set_level(LogLevel::WARN);

    EXPECT_FALSE(StructuredLogger::instance().is_enabled(LogLevel::DEBUG));
    EXPECT_FALSE(StructuredLogger::instance().is_enabled(LogLevel::INFO));
    EXPECT_TRUE(StructuredLogger::instance().is_enabled(LogLevel::WARN));
    EXPECT_TRUE(StructuredLogger::instance().is_enabled(LogLevel::ERROR));
    EXPECT_TRUE(StructuredLogger::instance().is_enabled(LogLevel::FATAL));
}

TEST_F(StructuredLoggerTest, LogLevels) {
    StructuredLogger::instance().set_level(LogLevel::TRACE);

    auto& logger = StructuredLogger::instance();

    // Test all convenience methods
    logger.trace("comp").msg("trace msg").emit();
    logger.debug("comp").msg("debug msg").emit();
    logger.info("comp").msg("info msg").emit();
    logger.warn("comp").msg("warn msg").emit();
    logger.error("comp").msg("error msg").emit();
    logger.fatal("comp").msg("fatal msg").emit();

    EXPECT_EQ(mock_sink_->write_count(), 6u);
}

TEST_F(StructuredLoggerTest, Emit) {
    StructuredLogger::instance().set_level(LogLevel::INFO);

    LogEntry entry(LogLevel::ERROR, "TestComp");
    entry.msg("Test emit");

    StructuredLogger::instance().emit(entry);

    EXPECT_EQ(mock_sink_->write_count(), 1u);
    EXPECT_EQ(mock_sink_->last_level(), LogLevel::ERROR);
    EXPECT_EQ(mock_sink_->last_component(), "TestComp");
    EXPECT_EQ(mock_sink_->last_message(), "Test emit");
}

TEST_F(StructuredLoggerTest, Flush) {
    StructuredLogger::instance().flush();

    EXPECT_EQ(mock_sink_->flush_count(), 1u);
}

TEST_F(StructuredLoggerTest, FilterByLevel) {
    StructuredLogger::instance().set_level(LogLevel::ERROR);

    auto& logger = StructuredLogger::instance();
    logger.debug("comp").msg("should not appear").emit();
    logger.info("comp").msg("should not appear").emit();
    logger.warn("comp").msg("should not appear").emit();
    logger.error("comp").msg("should appear").emit();

    // Only ERROR level should pass through
    EXPECT_EQ(mock_sink_->write_count(), 1u);
}

TEST_F(StructuredLoggerTest, MultipleSinks) {
    auto second_sink = std::make_shared<MockStructuredSink>();
    StructuredLogger::instance().add_sink(second_sink);

    StructuredLogger::instance().set_level(LogLevel::INFO);
    StructuredLogger::instance().info("multi").msg("test").emit();

    EXPECT_EQ(mock_sink_->write_count(), 1u);
    EXPECT_EQ(second_sink->write_count(), 1u);
}

// ============================================================================
// CorrelationContext Tests
// ============================================================================

class CorrelationContextTest : public ::testing::Test {
protected:
    void TearDown() override {
        CorrelationContext::clear_correlation_id();
    }
};

TEST_F(CorrelationContextTest, GenerateCorrelationId) {
    std::string id = CorrelationContext::generate_correlation_id();

    EXPECT_FALSE(id.empty());
    EXPECT_GT(id.length(), 8u);
}

TEST_F(CorrelationContextTest, UniqueIds) {
    std::set<std::string> ids;
    for (int i = 0; i < 100; ++i) {
        ids.insert(CorrelationContext::generate_correlation_id());
    }

    EXPECT_EQ(ids.size(), 100u);  // All unique
}

TEST_F(CorrelationContextTest, SetAndGet) {
    CorrelationContext::set_correlation_id("test-correlation-id");

    std::string id = CorrelationContext::get_correlation_id();
    EXPECT_EQ(id, "test-correlation-id");
}

TEST_F(CorrelationContextTest, Clear) {
    CorrelationContext::set_correlation_id("to-be-cleared");
    CorrelationContext::clear_correlation_id();

    // After clear, should get empty or new generated id
    std::string id = CorrelationContext::get_correlation_id();
    EXPECT_NE(id, "to-be-cleared");
}

TEST_F(CorrelationContextTest, ScopeWithExplicitId) {
    std::string outer_id = "outer-id";
    CorrelationContext::set_correlation_id(outer_id);

    {
        CorrelationContext::Scope scope("inner-scope-id");
        EXPECT_EQ(CorrelationContext::get_correlation_id(), "inner-scope-id");
        EXPECT_EQ(scope.correlation_id(), "inner-scope-id");
    }

    // Should restore outer id
    EXPECT_EQ(CorrelationContext::get_correlation_id(), outer_id);
}

TEST_F(CorrelationContextTest, ScopeWithGeneratedId) {
    CorrelationContext::Scope scope;

    std::string id = scope.correlation_id();
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(CorrelationContext::get_correlation_id(), id);
}

// ============================================================================
// RequestContext Tests
// ============================================================================

class RequestContextTest : public ::testing::Test {};

TEST_F(RequestContextTest, Create) {
    RequestContext ctx = RequestContext::create("test-operation");

    EXPECT_FALSE(ctx.correlation_id.empty());
    EXPECT_EQ(ctx.operation_name, "test-operation");
}

TEST_F(RequestContextTest, CreateChild) {
    RequestContext parent = RequestContext::create("parent-op");
    RequestContext child = parent.create_child("child-op");

    EXPECT_EQ(child.correlation_id, parent.correlation_id);
    EXPECT_EQ(child.trace_id, parent.trace_id);
    EXPECT_EQ(child.parent_span_id, parent.span_id);
    EXPECT_NE(child.span_id, parent.span_id);
    EXPECT_EQ(child.operation_name, "child-op");
}

TEST_F(RequestContextTest, ToTraceparent) {
    RequestContext ctx = RequestContext::create("op");
    std::string traceparent = ctx.to_traceparent();

    // W3C Trace Context format: version-trace_id-parent_id-flags
    EXPECT_FALSE(traceparent.empty());
    EXPECT_NE(traceparent.find("-"), std::string::npos);
}

TEST_F(RequestContextTest, FromTraceparent) {
    RequestContext original = RequestContext::create("op");
    std::string traceparent = original.to_traceparent();

    auto parsed = RequestContext::from_traceparent(traceparent);
    EXPECT_TRUE(parsed.has_value());
    if (parsed) {
        EXPECT_EQ(parsed->trace_id, original.trace_id);
    }
}

TEST_F(RequestContextTest, FromTraceparentInvalid) {
    auto result = RequestContext::from_traceparent("invalid-format");
    // May or may not parse depending on implementation
    // Just verify no crash
}

TEST_F(RequestContextTest, Baggage) {
    RequestContext ctx = RequestContext::create("op");
    ctx.baggage["user_id"] = "12345";
    ctx.baggage["request_id"] = "req-abc";

    EXPECT_EQ(ctx.baggage.size(), 2u);
    EXPECT_EQ(ctx.baggage["user_id"], "12345");
}

// ============================================================================
// RequestScope Tests
// ============================================================================

class RequestScopeTest : public ::testing::Test {};

TEST_F(RequestScopeTest, ConstructWithContext) {
    RequestContext ctx = RequestContext::create("ctx-op");

    {
        RequestScope scope(ctx);
        EXPECT_NE(RequestScope::current(), nullptr);
        EXPECT_EQ(RequestScope::current()->operation_name, "ctx-op");
    }

    // After scope, current should be nullptr or previous
    EXPECT_EQ(RequestScope::current(), nullptr);
}

TEST_F(RequestScopeTest, ConstructWithOperation) {
    {
        RequestScope scope("scope-operation");
        EXPECT_NE(RequestScope::current(), nullptr);
        EXPECT_EQ(RequestScope::current()->operation_name, "scope-operation");
    }

    EXPECT_EQ(RequestScope::current(), nullptr);
}

TEST_F(RequestScopeTest, NestedScopes) {
    {
        RequestScope outer("outer-op");
        EXPECT_EQ(RequestScope::current()->operation_name, "outer-op");

        {
            RequestScope inner("inner-op");
            EXPECT_EQ(RequestScope::current()->operation_name, "inner-op");
        }

        EXPECT_EQ(RequestScope::current()->operation_name, "outer-op");
    }

    EXPECT_EQ(RequestScope::current(), nullptr);
}

TEST_F(RequestScopeTest, AccessContext) {
    RequestScope scope("test-access");

    const RequestContext& ctx = scope.context();
    EXPECT_EQ(ctx.operation_name, "test-access");
    EXPECT_FALSE(ctx.correlation_id.empty());
}

// ============================================================================
// ScopedTimer Tests
// ============================================================================

class ScopedTimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_sink_ = std::make_shared<MockStructuredSink>();
        StructuredLogger::instance().clear_sinks();
        StructuredLogger::instance().add_sink(mock_sink_);
        StructuredLogger::instance().set_level(LogLevel::DEBUG);
    }

    void TearDown() override {
        StructuredLogger::instance().clear_sinks();
    }

    std::shared_ptr<MockStructuredSink> mock_sink_;
};

TEST_F(ScopedTimerTest, LogsOnDestruction) {
    {
        ScopedTimer timer("TimerComponent", "test_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Timer should have logged on destruction
    EXPECT_GE(mock_sink_->write_count(), 1u);
}

TEST_F(ScopedTimerTest, LogsCorrectComponent) {
    {
        ScopedTimer timer("MyComponent", "my_operation");
    }

    EXPECT_EQ(mock_sink_->last_component(), "MyComponent");
}

// ============================================================================
// Integration Tests
// ============================================================================

class StructuredLoggerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_sink_ = std::make_shared<MockStructuredSink>();
        StructuredLogger::instance().clear_sinks();
        StructuredLogger::instance().add_sink(mock_sink_);
        StructuredLogger::instance().set_level(LogLevel::TRACE);
    }

    void TearDown() override {
        StructuredLogger::instance().clear_sinks();
    }

    std::shared_ptr<MockStructuredSink> mock_sink_;
};

TEST_F(StructuredLoggerIntegrationTest, CompleteWorkflow) {
    // Create request scope
    RequestScope scope("integration-test");

    // Log with fields
    StructuredLogger::instance().info("Integration")
        .msg("Processing request")
        .field("user_id", "user123")
        .field("request_size", 1024)
        .field("authenticated", true)
        .emit();

    // Log error
    StructuredLogger::instance().error("Integration")
        .msg("Error occurred")
        .error(ErrorCode::INVALID_ARGUMENT, "Invalid input")
        .field("input", "bad-data")
        .emit();

    // Log with timing
    {
        ScopedTimer timer("Integration", "sub_operation");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Verify all logs were captured
    EXPECT_GE(mock_sink_->write_count(), 3u);
}

TEST_F(StructuredLoggerIntegrationTest, ConcurrentLogging) {
    const int num_threads = 4;
    const int logs_per_thread = 50;

    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, logs_per_thread]() {
            for (int i = 0; i < logs_per_thread; ++i) {
                StructuredLogger::instance().info("Thread" + std::to_string(t))
                    .msg("Log entry " + std::to_string(i))
                    .field("thread", t)
                    .field("iteration", i)
                    .emit();
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(mock_sink_->write_count(), num_threads * logs_per_thread);
}

TEST_F(StructuredLoggerIntegrationTest, JsonOutput) {
    StructuredLogger::instance().info("JsonTest")
        .msg("JSON format test")
        .field("string_field", "value")
        .field("int_field", 42)
        .field("double_field", 3.14)
        .field("bool_field", true)
        .emit();

    ASSERT_EQ(mock_sink_->entries().size(), 1u);

    const std::string& json = mock_sink_->entries()[0];

    // Verify JSON contains expected fields
    EXPECT_NE(json.find("\"level\""), std::string::npos);
    EXPECT_NE(json.find("\"component\":\"JsonTest\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"JSON format test\""), std::string::npos);
    EXPECT_NE(json.find("\"string_field\":\"value\""), std::string::npos);
    EXPECT_NE(json.find("\"int_field\":42"), std::string::npos);
}
