/**
 * @file test_debug.cpp
 * @brief Unit tests for IPB debug and logging utilities
 *
 * Tests coverage for:
 * - LogLevel parsing
 * - TraceId: Generation, parsing, conversion
 * - SpanId: Generation, parsing, conversion
 * - LogFilter: Level filtering, category filtering
 * - LogRecord: Record construction
 * - ConsoleSink: Console output
 * - FileSink: File output with rotation
 * - Logger: Singleton, sinks, logging
 * - TraceScope: Trace context management
 * - Span: Timing and context
 * - Assertion handlers
 * - Initialization functions
 */

#include <gtest/gtest.h>
#include <ipb/common/debug.hpp>
#include <ipb/common/platform.hpp>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace ipb::common::debug;
using namespace ipb::common;

// ============================================================================
// LogLevel Parsing Tests
// ============================================================================

class LogLevelParseTest : public ::testing::Test {};

TEST_F(LogLevelParseTest, ParseTrace) {
    EXPECT_EQ(parse_log_level("TRACE"), LogLevel::TRACE);
    EXPECT_EQ(parse_log_level("trace"), LogLevel::TRACE);
    EXPECT_EQ(parse_log_level("Trace"), LogLevel::TRACE);
}

TEST_F(LogLevelParseTest, ParseDebug) {
    EXPECT_EQ(parse_log_level("DEBUG"), LogLevel::DEBUG);
    EXPECT_EQ(parse_log_level("debug"), LogLevel::DEBUG);
}

TEST_F(LogLevelParseTest, ParseInfo) {
    EXPECT_EQ(parse_log_level("INFO"), LogLevel::INFO);
    EXPECT_EQ(parse_log_level("info"), LogLevel::INFO);
}

TEST_F(LogLevelParseTest, ParseWarn) {
    EXPECT_EQ(parse_log_level("WARN"), LogLevel::WARN);
    EXPECT_EQ(parse_log_level("WARNING"), LogLevel::WARN);
    EXPECT_EQ(parse_log_level("warn"), LogLevel::WARN);
    EXPECT_EQ(parse_log_level("warning"), LogLevel::WARN);
}

TEST_F(LogLevelParseTest, ParseError) {
    EXPECT_EQ(parse_log_level("ERROR"), LogLevel::ERROR);
    EXPECT_EQ(parse_log_level("ERR"), LogLevel::ERROR);
    EXPECT_EQ(parse_log_level("error"), LogLevel::ERROR);
    EXPECT_EQ(parse_log_level("err"), LogLevel::ERROR);
}

TEST_F(LogLevelParseTest, ParseFatal) {
    EXPECT_EQ(parse_log_level("FATAL"), LogLevel::FATAL);
    EXPECT_EQ(parse_log_level("CRITICAL"), LogLevel::FATAL);
    EXPECT_EQ(parse_log_level("fatal"), LogLevel::FATAL);
}

TEST_F(LogLevelParseTest, ParseOff) {
    EXPECT_EQ(parse_log_level("OFF"), LogLevel::OFF);
    EXPECT_EQ(parse_log_level("NONE"), LogLevel::OFF);
    EXPECT_EQ(parse_log_level("off"), LogLevel::OFF);
}

TEST_F(LogLevelParseTest, ParseUnknown) {
    EXPECT_EQ(parse_log_level("UNKNOWN"), LogLevel::INFO);
    EXPECT_EQ(parse_log_level("invalid"), LogLevel::INFO);
    EXPECT_EQ(parse_log_level(""), LogLevel::INFO);
}

// ============================================================================
// TraceId Tests
// ============================================================================

class TraceIdTest : public ::testing::Test {};

TEST_F(TraceIdTest, DefaultConstruction) {
    TraceId trace;
    EXPECT_FALSE(trace.is_valid());
    EXPECT_EQ(trace.value(), 0u);
}

TEST_F(TraceIdTest, ValueConstruction) {
    TraceId trace(0x123456789ABCDEF0);
    EXPECT_TRUE(trace.is_valid());
    EXPECT_EQ(trace.value(), 0x123456789ABCDEF0);
}

TEST_F(TraceIdTest, Generate) {
    TraceId trace = TraceId::generate();
    EXPECT_TRUE(trace.is_valid());
    EXPECT_NE(trace.value(), 0u);
}

TEST_F(TraceIdTest, GenerateUniqueness) {
    TraceId trace1 = TraceId::generate();
    TraceId trace2 = TraceId::generate();
    EXPECT_NE(trace1.value(), trace2.value());
}

TEST_F(TraceIdTest, ToString) {
    TraceId trace(0x0123456789ABCDEF);
    std::string str = trace.to_string();
    EXPECT_EQ(str.size(), 16u);
    EXPECT_EQ(str, "0123456789abcdef");
}

TEST_F(TraceIdTest, FromStringValid) {
    TraceId trace = TraceId::from_string("0123456789abcdef");
    EXPECT_TRUE(trace.is_valid());
    EXPECT_EQ(trace.value(), 0x0123456789ABCDEF);
}

TEST_F(TraceIdTest, FromStringUppercase) {
    TraceId trace = TraceId::from_string("0123456789ABCDEF");
    EXPECT_TRUE(trace.is_valid());
    EXPECT_EQ(trace.value(), 0x0123456789ABCDEF);
}

TEST_F(TraceIdTest, FromStringMixedCase) {
    TraceId trace = TraceId::from_string("0123456789AbCdEf");
    EXPECT_TRUE(trace.is_valid());
}

TEST_F(TraceIdTest, FromStringInvalidLength) {
    TraceId trace = TraceId::from_string("0123456789");  // Too short
    EXPECT_FALSE(trace.is_valid());
}

TEST_F(TraceIdTest, FromStringInvalidChars) {
    TraceId trace = TraceId::from_string("012345678GHIJKLM");  // Invalid chars
    EXPECT_FALSE(trace.is_valid());
}

TEST_F(TraceIdTest, Equality) {
    TraceId trace1(0x12345);
    TraceId trace2(0x12345);
    TraceId trace3(0x67890);

    EXPECT_EQ(trace1, trace2);
    EXPECT_NE(trace1, trace3);
}

TEST_F(TraceIdTest, BoolConversion) {
    TraceId valid(0x12345);
    TraceId invalid;

    EXPECT_TRUE(valid.is_valid());
    EXPECT_FALSE(invalid.is_valid());
}

// ============================================================================
// SpanId Tests
// ============================================================================

class SpanIdTest : public ::testing::Test {};

TEST_F(SpanIdTest, DefaultConstruction) {
    SpanId span;
    EXPECT_FALSE(span.is_valid());
    EXPECT_EQ(span.value(), 0u);
}

TEST_F(SpanIdTest, ValueConstruction) {
    SpanId span(0x123456789ABCDEF0);
    EXPECT_TRUE(span.is_valid());
    EXPECT_EQ(span.value(), 0x123456789ABCDEF0);
}

TEST_F(SpanIdTest, Generate) {
    SpanId span = SpanId::generate();
    EXPECT_TRUE(span.is_valid());
    EXPECT_NE(span.value(), 0u);
}

TEST_F(SpanIdTest, GenerateUniqueness) {
    SpanId span1 = SpanId::generate();
    SpanId span2 = SpanId::generate();
    EXPECT_NE(span1.value(), span2.value());
}

TEST_F(SpanIdTest, ToString) {
    SpanId span(0x0123456789ABCDEF);
    std::string str = span.to_string();
    EXPECT_EQ(str.size(), 16u);
}

TEST_F(SpanIdTest, FromStringValid) {
    SpanId span = SpanId::from_string("0123456789abcdef");
    EXPECT_TRUE(span.is_valid());
}

TEST_F(SpanIdTest, FromStringInvalidLength) {
    SpanId span = SpanId::from_string("0123456789");
    EXPECT_FALSE(span.is_valid());
}

TEST_F(SpanIdTest, FromStringInvalidChars) {
    SpanId span = SpanId::from_string("012345678GHIJKLM");
    EXPECT_FALSE(span.is_valid());
}

// ============================================================================
// LogFilter Tests
// ============================================================================

class LogFilterTest : public ::testing::Test {
protected:
    void SetUp() override {
        filter_.reset();
    }

    LogFilter filter_;
};

TEST_F(LogFilterTest, DefaultLevel) {
    // Default level is INFO
    EXPECT_FALSE(filter_.should_log(LogLevel::DEBUG, ""));
    EXPECT_TRUE(filter_.should_log(LogLevel::INFO, ""));
}

TEST_F(LogFilterTest, SetLevel) {
    filter_.set_level(LogLevel::DEBUG);
    // Now DEBUG should be allowed
    EXPECT_TRUE(filter_.should_log(LogLevel::DEBUG, ""));
}

TEST_F(LogFilterTest, ShouldLogBasedOnLevel) {
    filter_.set_level(LogLevel::WARN);

    EXPECT_FALSE(filter_.should_log(LogLevel::TRACE, ""));
    EXPECT_FALSE(filter_.should_log(LogLevel::DEBUG, ""));
    EXPECT_FALSE(filter_.should_log(LogLevel::INFO, ""));
    EXPECT_TRUE(filter_.should_log(LogLevel::WARN, ""));
    EXPECT_TRUE(filter_.should_log(LogLevel::ERROR, ""));
    EXPECT_TRUE(filter_.should_log(LogLevel::FATAL, ""));
}

TEST_F(LogFilterTest, CategorySpecificLevel) {
    // Global level allows INFO and above
    filter_.set_level(LogLevel::INFO);

    // Category-specific level can FURTHER restrict logging
    // (not expand it below global level)
    filter_.set_category_level("restricted_module", LogLevel::ERROR);

    // Other modules follow global level (INFO and above allowed)
    EXPECT_FALSE(filter_.should_log(LogLevel::DEBUG, "other_module"));
    EXPECT_TRUE(filter_.should_log(LogLevel::INFO, "other_module"));

    // restricted_module only allows ERROR and above
    EXPECT_FALSE(filter_.should_log(LogLevel::INFO, "restricted_module"));
    EXPECT_TRUE(filter_.should_log(LogLevel::ERROR, "restricted_module"));
}

TEST_F(LogFilterTest, Reset) {
    filter_.set_level(LogLevel::DEBUG);
    filter_.set_category_level("test", LogLevel::TRACE);

    filter_.reset();

    // After reset, default level (INFO) should be restored
    EXPECT_FALSE(filter_.should_log(LogLevel::DEBUG, ""));
    EXPECT_TRUE(filter_.should_log(LogLevel::INFO, ""));
}

// ============================================================================
// LogRecord Tests
// ============================================================================

class LogRecordTest : public ::testing::Test {};

TEST_F(LogRecordTest, DefaultConstruction) {
    LogRecord record;
    EXPECT_EQ(record.level, LogLevel::INFO);
    EXPECT_TRUE(record.category.empty());
    EXPECT_TRUE(record.message.empty());
}

TEST_F(LogRecordTest, FieldAssignment) {
    LogRecord record;
    record.level = LogLevel::ERROR;
    record.category = "test";
    record.message = "Test message";
    record.thread_id = 12345;

    EXPECT_EQ(record.level, LogLevel::ERROR);
    EXPECT_EQ(record.category, "test");
    EXPECT_EQ(record.message, "Test message");
    EXPECT_EQ(record.thread_id, 12345u);
}

// ============================================================================
// ConsoleSink Tests
// ============================================================================

class ConsoleSinkTest : public ::testing::Test {};

TEST_F(ConsoleSinkTest, DefaultConstruction) {
    ConsoleSink sink;
    EXPECT_TRUE(sink.is_ready());
}

TEST_F(ConsoleSinkTest, ConfiguredConstruction) {
    ConsoleSink::Config config;
    config.use_colors = false;
    config.include_timestamp = true;
    config.include_thread_id = true;

    ConsoleSink sink(config);
    EXPECT_TRUE(sink.is_ready());
}

TEST_F(ConsoleSinkTest, WriteRecord) {
    ConsoleSink::Config config;
    config.use_colors = false;
    config.include_timestamp = true;

    ConsoleSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.category = "test";
    record.message = "Test message";
    record.timestamp = std::chrono::system_clock::now();
    record.thread_id = platform::get_thread_id();

    // Should not throw
    EXPECT_NO_THROW(sink.write(record));
}

TEST_F(ConsoleSinkTest, WriteAllLevels) {
    ConsoleSink::Config config;
    config.use_colors = false;
    ConsoleSink sink(config);

    std::vector<LogLevel> levels = {
        LogLevel::TRACE, LogLevel::DEBUG, LogLevel::INFO,
        LogLevel::WARN, LogLevel::ERROR, LogLevel::FATAL
    };

    for (auto level : levels) {
        LogRecord record;
        record.level = level;
        record.message = "Test at level";
        record.timestamp = std::chrono::system_clock::now();

        EXPECT_NO_THROW(sink.write(record));
    }
}

TEST_F(ConsoleSinkTest, WriteWithTraceId) {
    ConsoleSink::Config config;
    config.use_colors = false;
    config.include_trace_id = true;

    ConsoleSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.message = "Test with trace";
    record.timestamp = std::chrono::system_clock::now();
    record.trace_id = TraceId::generate();

    EXPECT_NO_THROW(sink.write(record));
}

TEST_F(ConsoleSinkTest, WriteWithLocation) {
    ConsoleSink::Config config;
    config.use_colors = false;
    config.include_location = true;

    ConsoleSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.message = "Test with location";
    record.timestamp = std::chrono::system_clock::now();
    record.location = SourceLocation(__FILE__, __func__, __LINE__);

    EXPECT_NO_THROW(sink.write(record));
}

TEST_F(ConsoleSinkTest, Flush) {
    ConsoleSink sink;
    EXPECT_NO_THROW(sink.flush());
}

// ============================================================================
// FileSink Tests
// ============================================================================

class FileSinkTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = "/tmp/ipb_test_log_" + std::to_string(getpid()) + ".log";
    }

    void TearDown() override {
        // Clean up test files
        std::filesystem::remove(test_file_);
        for (int i = 1; i <= 5; ++i) {
            std::filesystem::remove(test_file_ + "." + std::to_string(i));
        }
    }

    std::string test_file_;
};

TEST_F(FileSinkTest, Construction) {
    FileSink::Config config;
    config.file_path = test_file_;

    FileSink sink(config);
    EXPECT_TRUE(sink.is_ready());
}

TEST_F(FileSinkTest, WriteRecord) {
    FileSink::Config config;
    config.file_path = test_file_;

    FileSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.category = "test";
    record.message = "Test message";
    record.timestamp = std::chrono::system_clock::now();
    record.thread_id = platform::get_thread_id();

    sink.write(record);
    sink.flush();

    // Verify file was written
    std::ifstream file(test_file_);
    EXPECT_TRUE(file.good());

    std::string content;
    std::getline(file, content);
    EXPECT_FALSE(content.empty());
    EXPECT_NE(content.find("Test message"), std::string::npos);
}

TEST_F(FileSinkTest, WriteWithTraceContext) {
    FileSink::Config config;
    config.file_path = test_file_;

    FileSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.message = "Traced message";
    record.timestamp = std::chrono::system_clock::now();
    record.trace_id = TraceId::generate();
    record.span_id = SpanId::generate();

    sink.write(record);
    sink.flush();

    std::ifstream file(test_file_);
    std::string content;
    std::getline(file, content);
    EXPECT_NE(content.find("trace:"), std::string::npos);
}

TEST_F(FileSinkTest, FileRotation) {
    FileSink::Config config;
    config.file_path = test_file_;
    config.max_file_size = 100;  // Very small to trigger rotation
    config.max_files = 3;

    FileSink sink(config);

    // Write enough data to trigger rotation
    for (int i = 0; i < 20; ++i) {
        LogRecord record;
        record.level = LogLevel::INFO;
        record.message = "Message " + std::to_string(i) + " with some extra content to fill space";
        record.timestamp = std::chrono::system_clock::now();

        sink.write(record);
    }
    sink.flush();

    // Check that rotated files exist
    EXPECT_TRUE(std::filesystem::exists(test_file_));
}

TEST_F(FileSinkTest, Flush) {
    FileSink::Config config;
    config.file_path = test_file_;

    FileSink sink(config);

    LogRecord record;
    record.level = LogLevel::INFO;
    record.message = "Flush test";
    record.timestamp = std::chrono::system_clock::now();

    sink.write(record);
    EXPECT_NO_THROW(sink.flush());
}

// ============================================================================
// Logger Tests
// ============================================================================

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Save original state - we'll assume INFO is the default
    }

    void TearDown() override {
        // Restore original state
        Logger::instance().set_level(LogLevel::INFO);
    }
};

TEST_F(LoggerTest, Singleton) {
    Logger& instance1 = Logger::instance();
    Logger& instance2 = Logger::instance();
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(LoggerTest, SetLevel) {
    Logger::instance().set_level(LogLevel::DEBUG);
    // Verify by checking is_enabled
    EXPECT_TRUE(Logger::instance().is_enabled(LogLevel::DEBUG));
}

TEST_F(LoggerTest, AddSink) {
    auto sink = std::make_shared<ConsoleSink>();
    EXPECT_NO_THROW(Logger::instance().add_sink(sink));
}

TEST_F(LoggerTest, ClearSinks) {
    EXPECT_NO_THROW(Logger::instance().clear_sinks());

    // Re-add a sink so logging doesn't break
    Logger::instance().add_sink(std::make_shared<ConsoleSink>());
}

TEST_F(LoggerTest, LogAtLevel) {
    Logger::instance().set_level(LogLevel::TRACE);

    // These should not throw
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::TRACE, "test", "Trace message"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::DEBUG, "test", "Debug message"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::INFO, "test", "Info message"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::WARN, "test", "Warn message"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::ERROR, "test", "Error message"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::FATAL, "test", "Fatal message"));
}

TEST_F(LoggerTest, LogFiltering) {
    Logger::instance().set_level(LogLevel::ERROR);

    // These should be filtered out (level too low)
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::DEBUG, "test", "Should be filtered"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::INFO, "test", "Should be filtered"));

    // These should pass
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::ERROR, "test", "Should pass"));
    EXPECT_NO_THROW(Logger::instance().log(LogLevel::FATAL, "test", "Should pass"));
}

TEST_F(LoggerTest, Flush) {
    EXPECT_NO_THROW(Logger::instance().flush());
}

TEST_F(LoggerTest, ThreadName) {
    Logger::set_thread_name("TestThread");
    auto name = Logger::get_thread_name();
    EXPECT_EQ(name, "TestThread");
}

TEST_F(LoggerTest, IsEnabled) {
    Logger::instance().set_level(LogLevel::WARN);

    EXPECT_FALSE(Logger::instance().is_enabled(LogLevel::DEBUG));
    EXPECT_FALSE(Logger::instance().is_enabled(LogLevel::INFO));
    EXPECT_TRUE(Logger::instance().is_enabled(LogLevel::WARN));
    EXPECT_TRUE(Logger::instance().is_enabled(LogLevel::ERROR));
}

// ============================================================================
// TraceScope Tests
// ============================================================================

class TraceScopeTest : public ::testing::Test {};

TEST_F(TraceScopeTest, SetsCurrentIds) {
    TraceId trace = TraceId::generate();
    SpanId span = SpanId::generate();

    {
        TraceScope scope(trace, span);
        EXPECT_EQ(TraceScope::current_trace_id(), trace);
        EXPECT_EQ(TraceScope::current_span_id().value(), span.value());
    }

    // After scope ends, IDs should be reset
    EXPECT_NE(TraceScope::current_trace_id(), trace);
}

TEST_F(TraceScopeTest, NestedScopes) {
    TraceId outer_trace = TraceId::generate();
    TraceId inner_trace = TraceId::generate();

    {
        TraceScope outer(outer_trace);
        EXPECT_EQ(TraceScope::current_trace_id(), outer_trace);

        {
            TraceScope inner(inner_trace);
            EXPECT_EQ(TraceScope::current_trace_id(), inner_trace);
        }

        // Should restore outer
        EXPECT_EQ(TraceScope::current_trace_id(), outer_trace);
    }
}

TEST_F(TraceScopeTest, AutoGenerateSpan) {
    TraceId trace = TraceId::generate();

    {
        TraceScope scope(trace);  // Should auto-generate span
        EXPECT_EQ(TraceScope::current_trace_id(), trace);
        EXPECT_TRUE(TraceScope::current_span_id().is_valid());
    }
}

TEST_F(TraceScopeTest, GetTraceAndSpanIds) {
    TraceId trace = TraceId::generate();
    SpanId span = SpanId::generate();

    TraceScope scope(trace, span);
    EXPECT_EQ(scope.trace_id(), trace);
    EXPECT_EQ(scope.span_id().value(), span.value());
}

// ============================================================================
// Span Tests
// ============================================================================

class SpanTest : public ::testing::Test {};

TEST_F(SpanTest, BasicConstruction) {
    Span span("test_operation", "test_category");

    // May or may not have trace depending on context
    EXPECT_TRUE(span.id().is_valid());
}

TEST_F(SpanTest, AddContextString) {
    Span span("test_operation", "test");

    span.add_context("key", "value");

    // Should not throw
}

TEST_F(SpanTest, AddContextInt) {
    Span span("test_operation", "test");

    span.add_context("count", int64_t(42));

    // Should not throw
}

TEST_F(SpanTest, AddContextDouble) {
    Span span("test_operation", "test");

    span.add_context("temperature", 98.6);

    // Should not throw
}

TEST_F(SpanTest, SetError) {
    Span span("test_operation", "test");

    span.set_error(ErrorCode::UNKNOWN_ERROR, "Something went wrong");

    // Error should be recorded (verified via destructor logging)
}

TEST_F(SpanTest, Elapsed) {
    Span span("test_operation", "test");

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto elapsed = span.elapsed();
    EXPECT_GE(elapsed.count(), 10'000'000);  // At least 10ms in nanoseconds
}

TEST_F(SpanTest, ChildSpan) {
    Span parent("parent_operation", "test");

    Span child("child_operation", parent);

    EXPECT_EQ(child.trace_id(), parent.trace_id());
    EXPECT_NE(child.id().value(), parent.id().value());
}

TEST_F(SpanTest, ChainedAddContext) {
    Span span("test", "test");

    span.add_context("key1", "value1")
        .add_context("key2", int64_t(42))
        .add_context("key3", 3.14);

    // Chaining should work
}

TEST_F(SpanTest, GetTraceId) {
    TraceId trace = TraceId::generate();
    TraceScope scope(trace);

    Span span("test", "test");
    EXPECT_EQ(span.trace_id(), trace);
}

// ============================================================================
// Assertion Handler Tests
// ============================================================================

// Global flag for custom handler test
static bool g_custom_handler_called = false;

static void custom_test_handler(const char* expr, const char* msg, const SourceLocation& loc) {
    g_custom_handler_called = true;
    (void)expr;
    (void)msg;
    (void)loc;
}

class AssertionHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        original_handler_ = get_assert_handler();
        g_custom_handler_called = false;
    }

    void TearDown() override {
        set_assert_handler(original_handler_);
    }

    AssertHandler original_handler_;
};

TEST_F(AssertionHandlerTest, DefaultHandler) {
    AssertHandler handler = get_assert_handler();
    EXPECT_NE(handler, nullptr);
}

TEST_F(AssertionHandlerTest, SetCustomHandler) {
    set_assert_handler(custom_test_handler);

    SourceLocation loc(__FILE__, __func__, __LINE__);
    assert_fail("test_expr", "test_msg", loc);

    EXPECT_TRUE(g_custom_handler_called);
}

TEST_F(AssertionHandlerTest, SetNullHandler) {
    set_assert_handler(nullptr);

    // Should reset to default handler
    AssertHandler handler = get_assert_handler();
    EXPECT_NE(handler, nullptr);
}

TEST_F(AssertionHandlerTest, DefaultHandlerFunction) {
    // Test that default_assert_handler exists and can be called
    // (Don't actually call it as it may abort in debug mode)
    AssertHandler handler = default_assert_handler;
    EXPECT_NE(handler, nullptr);
}

// ============================================================================
// Initialization Tests
// ============================================================================

class InitializationTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Reset to default state
        init_logging(LogLevel::INFO);
    }
};

TEST_F(InitializationTest, InitLogging) {
    init_logging(LogLevel::DEBUG);
    // Verify by checking is_enabled
    EXPECT_TRUE(Logger::instance().is_enabled(LogLevel::DEBUG));
}

TEST_F(InitializationTest, ShutdownLogging) {
    EXPECT_NO_THROW(shutdown_logging());

    // Re-initialize for other tests
    init_logging(LogLevel::INFO);
    Logger::instance().add_sink(std::make_shared<ConsoleSink>());
}

// ============================================================================
// Helper Function Tests
// ============================================================================

class HelperFunctionTest : public ::testing::Test {};

TEST_F(HelperFunctionTest, LevelChar) {
    EXPECT_EQ(level_char(LogLevel::TRACE), 'T');
    EXPECT_EQ(level_char(LogLevel::DEBUG), 'D');
    EXPECT_EQ(level_char(LogLevel::INFO), 'I');
    EXPECT_EQ(level_char(LogLevel::WARN), 'W');
    EXPECT_EQ(level_char(LogLevel::ERROR), 'E');
    EXPECT_EQ(level_char(LogLevel::FATAL), 'F');
}

TEST_F(HelperFunctionTest, LevelName) {
    EXPECT_EQ(std::string(level_name(LogLevel::TRACE)), "TRACE");
    EXPECT_EQ(std::string(level_name(LogLevel::DEBUG)), "DEBUG");
    EXPECT_EQ(std::string(level_name(LogLevel::INFO)), "INFO");
    EXPECT_EQ(std::string(level_name(LogLevel::WARN)), "WARN");
    EXPECT_EQ(std::string(level_name(LogLevel::ERROR)), "ERROR");
    EXPECT_EQ(std::string(level_name(LogLevel::FATAL)), "FATAL");
}

TEST_F(HelperFunctionTest, LevelCharUnknown) {
    EXPECT_EQ(level_char(LogLevel::OFF), '?');
}

TEST_F(HelperFunctionTest, LevelNameUnknown) {
    EXPECT_EQ(std::string(level_name(LogLevel::OFF)), "OFF");
}

// ============================================================================
// SourceLocation Tests
// ============================================================================

class SourceLocationTest : public ::testing::Test {};

TEST_F(SourceLocationTest, DefaultConstruction) {
    SourceLocation loc;
    EXPECT_FALSE(loc.is_valid());
}

TEST_F(SourceLocationTest, Construction) {
    SourceLocation loc("test.cpp", "test_func", 42);
    EXPECT_TRUE(loc.is_valid());
    EXPECT_STREQ(loc.file, "test.cpp");
    EXPECT_STREQ(loc.function, "test_func");
    EXPECT_EQ(loc.line, 42u);
}

TEST_F(SourceLocationTest, ConstructionWithColumn) {
    SourceLocation loc("test.cpp", "test_func", 42, 10);
    EXPECT_TRUE(loc.is_valid());
    EXPECT_EQ(loc.column, 10u);
}

TEST_F(SourceLocationTest, Current) {
    auto loc = IPB_CURRENT_LOCATION;
    EXPECT_TRUE(loc.is_valid());
    EXPECT_NE(loc.line, 0u);
}

TEST_F(SourceLocationTest, IsValidWithEmptyFile) {
    SourceLocation loc("", "func", 1);
    EXPECT_FALSE(loc.is_valid());
}

TEST_F(SourceLocationTest, IsValidWithZeroLine) {
    SourceLocation loc("file.cpp", "func", 0);
    EXPECT_FALSE(loc.is_valid());
}

// ============================================================================
// Category Tests
// ============================================================================

class CategoryTest : public ::testing::Test {};

TEST_F(CategoryTest, PredefinedCategories) {
    EXPECT_EQ(category::GENERAL, "general");
    EXPECT_EQ(category::ROUTER, "router");
    EXPECT_EQ(category::SCHEDULER, "scheduler");
    EXPECT_EQ(category::MESSAGING, "messaging");
    EXPECT_EQ(category::PROTOCOL, "protocol");
    EXPECT_EQ(category::TRANSPORT, "transport");
    EXPECT_EQ(category::CONFIG, "config");
    EXPECT_EQ(category::SECURITY, "security");
    EXPECT_EQ(category::METRICS, "metrics");
    EXPECT_EQ(category::LIFECYCLE, "lifecycle");
}
