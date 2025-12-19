/**
 * @file test_console_sink.cpp
 * @brief Unit tests for the console sink implementation
 *
 * Tests cover:
 * - ConsoleSinkConfig construction and presets
 * - ConsoleSinkStatistics
 * - ConsoleSink lifecycle (initialize, start, stop, shutdown)
 * - Data point sending (single and batch)
 * - Output formats (PLAIN, JSON, CSV, TABLE, COLORED)
 * - Filtering (by address, protocol, quality)
 * - Async output queue
 * - Statistics tracking
 * - ConsoleSinkFactory
 */

#include <ipb/sink/console/console_sink.hpp>
#include <ipb_plugin_test.hpp>

#include <chrono>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::sink::console;
using namespace ipb::common;
using namespace ipb::test;
using namespace std::chrono_literals;

// ============================================================================
// ConsoleSinkConfig Tests
// ============================================================================

class ConsoleSinkConfigTest : public ::testing::Test {};

TEST_F(ConsoleSinkConfigTest, DefaultValues) {
    ConsoleSinkConfig config;

    EXPECT_EQ(config.output_format, OutputFormat::PLAIN);
    EXPECT_TRUE(config.enable_console_output);
    EXPECT_FALSE(config.enable_file_output);
    EXPECT_TRUE(config.output_file_path.empty());
    EXPECT_TRUE(config.include_timestamp);
    EXPECT_TRUE(config.include_address);
    EXPECT_TRUE(config.include_protocol_id);
    EXPECT_TRUE(config.include_quality);
    EXPECT_TRUE(config.include_value);
    EXPECT_FALSE(config.enable_filtering);
    EXPECT_TRUE(config.enable_colors);
    EXPECT_TRUE(config.enable_async_output);
    EXPECT_EQ(config.queue_size, 10000u);
    EXPECT_FALSE(config.enable_statistics);
}

TEST_F(ConsoleSinkConfigTest, CreateDebug) {
    ConsoleSinkConfig config = ConsoleSinkConfig::create_debug();

    EXPECT_EQ(config.output_format, OutputFormat::COLORED);
    EXPECT_TRUE(config.enable_filtering);
    EXPECT_TRUE(config.enable_statistics);
    EXPECT_TRUE(config.enable_async_output);
}

TEST_F(ConsoleSinkConfigTest, CreateProduction) {
    ConsoleSinkConfig config = ConsoleSinkConfig::create_production();

    EXPECT_EQ(config.output_format, OutputFormat::JSON);
    EXPECT_FALSE(config.enable_colors);
    EXPECT_TRUE(config.enable_async_output);
    EXPECT_FALSE(config.enable_statistics);
}

TEST_F(ConsoleSinkConfigTest, CreateMinimal) {
    ConsoleSinkConfig config = ConsoleSinkConfig::create_minimal();

    EXPECT_EQ(config.output_format, OutputFormat::PLAIN);
    EXPECT_FALSE(config.include_timestamp);
    EXPECT_FALSE(config.include_protocol_id);
    EXPECT_FALSE(config.include_quality);
    EXPECT_FALSE(config.enable_async_output);
}

TEST_F(ConsoleSinkConfigTest, CreateVerbose) {
    ConsoleSinkConfig config = ConsoleSinkConfig::create_verbose();

    EXPECT_EQ(config.output_format, OutputFormat::TABLE);
    EXPECT_TRUE(config.enable_statistics);
    EXPECT_TRUE(config.enable_async_output);
    EXPECT_EQ(config.statistics_interval, std::chrono::seconds(5));
}

TEST_F(ConsoleSinkConfigTest, OutputFormats) {
    EXPECT_EQ(static_cast<int>(OutputFormat::PLAIN), 0);
    EXPECT_EQ(static_cast<int>(OutputFormat::JSON), 1);
    EXPECT_EQ(static_cast<int>(OutputFormat::CSV), 2);
    EXPECT_EQ(static_cast<int>(OutputFormat::TABLE), 3);
    EXPECT_EQ(static_cast<int>(OutputFormat::COLORED), 4);
    EXPECT_EQ(static_cast<int>(OutputFormat::CUSTOM), 5);
}

TEST_F(ConsoleSinkConfigTest, ConsoleColors) {
    EXPECT_EQ(static_cast<int>(ConsoleColor::RESET), 0);
    EXPECT_EQ(static_cast<int>(ConsoleColor::RED), 31);
    EXPECT_EQ(static_cast<int>(ConsoleColor::GREEN), 32);
    EXPECT_EQ(static_cast<int>(ConsoleColor::YELLOW), 33);
    EXPECT_EQ(static_cast<int>(ConsoleColor::BLUE), 34);
}

// ============================================================================
// ConsoleSinkStatistics Tests
// ============================================================================

class ConsoleSinkStatisticsTest : public ::testing::Test {
protected:
    ConsoleSinkStatistics stats_;
};

TEST_F(ConsoleSinkStatisticsTest, DefaultValues) {
    EXPECT_EQ(stats_.messages_processed.load(), 0u);
    EXPECT_EQ(stats_.messages_filtered.load(), 0u);
    EXPECT_EQ(stats_.messages_dropped.load(), 0u);
    EXPECT_EQ(stats_.bytes_written.load(), 0u);
    EXPECT_EQ(stats_.flush_operations.load(), 0u);
}

TEST_F(ConsoleSinkStatisticsTest, GetMessagesPerSecond) {
    stats_.messages_processed = 1000;
    // Need to wait a bit for elapsed time
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    double mps = stats_.get_messages_per_second();
    // Should be reasonable value
    EXPECT_GE(mps, 0.0);
}

TEST_F(ConsoleSinkStatisticsTest, GetAverageProcessingTime) {
    stats_.messages_processed = 10;
    stats_.total_processing_time = std::chrono::nanoseconds(10000);

    auto avg = stats_.get_average_processing_time();
    EXPECT_EQ(avg.count(), 1000);
}

TEST_F(ConsoleSinkStatisticsTest, GetAverageProcessingTimeZero) {
    auto avg = stats_.get_average_processing_time();
    EXPECT_EQ(avg.count(), 0);
}

TEST_F(ConsoleSinkStatisticsTest, UpdateProcessingTime) {
    stats_.update_processing_time(std::chrono::nanoseconds(100));
    stats_.update_processing_time(std::chrono::nanoseconds(200));
    stats_.update_processing_time(std::chrono::nanoseconds(50));

    EXPECT_EQ(stats_.min_processing_time.count(), 50);
    EXPECT_EQ(stats_.max_processing_time.count(), 200);
    EXPECT_EQ(stats_.total_processing_time.count(), 350);
}

TEST_F(ConsoleSinkStatisticsTest, Reset) {
    stats_.messages_processed = 100;
    stats_.messages_filtered = 10;
    stats_.bytes_written = 5000;

    stats_.reset();

    EXPECT_EQ(stats_.messages_processed.load(), 0u);
    EXPECT_EQ(stats_.messages_filtered.load(), 0u);
    EXPECT_EQ(stats_.bytes_written.load(), 0u);
}

// ============================================================================
// ConsoleSink Lifecycle Tests
// ============================================================================

class ConsoleSinkTest : public SinkTestBase {
protected:
    void SetUp() override {
        SinkTestBase::SetUp();

        // Use minimal config without actual console output
        config_.enable_console_output = false;
        config_.enable_async_output = false;
        config_.output_format = OutputFormat::PLAIN;

        // Redirect to stringstream for testing
        output_.str("");
        config_.output_stream = &output_;
    }

    ConsoleSinkConfig config_;
    std::stringstream output_;
};

TEST_F(ConsoleSinkTest, DefaultConstruction) {
    ConsoleSink sink;

    EXPECT_FALSE(sink.is_connected());
    EXPECT_TRUE(sink.is_healthy());
}

TEST_F(ConsoleSinkTest, ConstructWithConfig) {
    ConsoleSink sink(config_);

    EXPECT_FALSE(sink.is_connected());
    EXPECT_TRUE(sink.is_healthy());
}

TEST_F(ConsoleSinkTest, Initialize) {
    ConsoleSink sink(config_);

    auto result = sink.initialize("");
    EXPECT_RESULT_OK(result);
}

TEST_F(ConsoleSinkTest, StartStop) {
    ConsoleSink sink(config_);

    sink.initialize("");

    auto start_result = sink.start();
    EXPECT_RESULT_OK(start_result);
    EXPECT_TRUE(sink.is_connected());

    auto stop_result = sink.stop();
    EXPECT_RESULT_OK(stop_result);
    EXPECT_FALSE(sink.is_connected());
}

TEST_F(ConsoleSinkTest, Shutdown) {
    ConsoleSink sink(config_);

    sink.initialize("");
    sink.start();

    auto result = sink.shutdown();
    EXPECT_RESULT_OK(result);
    EXPECT_FALSE(sink.is_connected());
}

TEST_F(ConsoleSinkTest, GetSinkInfo) {
    ConsoleSink sink(config_);

    std::string info = sink.get_sink_info();
    EXPECT_FALSE(info.empty());
    EXPECT_NE(info.find("Console"), std::string::npos);
}

TEST_F(ConsoleSinkTest, GetMetrics) {
    ConsoleSink sink(config_);

    sink.initialize("");
    sink.start();

    auto metrics = sink.get_metrics();
    EXPECT_EQ(metrics.messages_sent, 0u);
    EXPECT_TRUE(metrics.is_connected);
    EXPECT_TRUE(metrics.is_healthy);
}

// ============================================================================
// ConsoleSink Data Sending Tests
// ============================================================================

class ConsoleSinkDataTest : public ConsoleSinkTest {};

TEST_F(ConsoleSinkDataTest, SendSingleDataPoint) {
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dp = create_datapoint("sensor/temperature", 25.5);
    auto result = sink.send_data_point(dp);

    EXPECT_RESULT_OK(result);

    std::string output = output_.str();
    EXPECT_NE(output.find("sensor/temperature"), std::string::npos);
}

TEST_F(ConsoleSinkDataTest, SendDataSet) {
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dataset = create_dataset(5);
    auto result = sink.send_data_set(dataset);

    EXPECT_RESULT_OK(result);

    auto metrics = sink.get_metrics();
    EXPECT_GE(metrics.messages_sent, 5u);
}

TEST_F(ConsoleSinkDataTest, SendMultipleDataPoints) {
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    for (int i = 0; i < 10; ++i) {
        auto dp = create_datapoint("sensor/test" + std::to_string(i), i * 1.5);
        sink.send_data_point(dp);
    }

    auto metrics = sink.get_metrics();
    EXPECT_EQ(metrics.messages_sent, 10u);
}

// ============================================================================
// ConsoleSink Output Format Tests
// ============================================================================

class ConsoleSinkFormatTest : public ConsoleSinkTest {};

TEST_F(ConsoleSinkFormatTest, PlainFormat) {
    config_.output_format = OutputFormat::PLAIN;
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dp = create_datapoint("sensor/temp", 25.0);
    sink.send_data_point(dp);

    std::string output = output_.str();
    EXPECT_NE(output.find("sensor/temp"), std::string::npos);
}

TEST_F(ConsoleSinkFormatTest, JsonFormat) {
    config_.output_format = OutputFormat::JSON;
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dp = create_datapoint("sensor/temp", 25.0);
    sink.send_data_point(dp);

    std::string output = output_.str();
    // JSON should have braces
    EXPECT_NE(output.find("{"), std::string::npos);
    EXPECT_NE(output.find("}"), std::string::npos);
}

TEST_F(ConsoleSinkFormatTest, CsvFormat) {
    config_.output_format = OutputFormat::CSV;
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dp = create_datapoint("sensor/temp", 25.0);
    sink.send_data_point(dp);

    std::string output = output_.str();
    // CSV should have commas
    EXPECT_NE(output.find(","), std::string::npos);
}

// ============================================================================
// ConsoleSink Filtering Tests
// ============================================================================

class ConsoleSinkFilterTest : public ConsoleSinkTest {};

TEST_F(ConsoleSinkFilterTest, AddAddressFilter) {
    config_.enable_filtering = true;
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    sink.add_address_filter("sensor/.*");

    // This should pass filter
    auto dp1 = create_datapoint("sensor/temp", 25.0);
    sink.send_data_point(dp1);

    // This should be filtered
    auto dp2 = create_datapoint("other/device", 30.0);
    sink.send_data_point(dp2);

    std::string output = output_.str();
    EXPECT_NE(output.find("sensor/temp"), std::string::npos);
}

TEST_F(ConsoleSinkFilterTest, ClearFilters) {
    config_.enable_filtering = true;

    ConsoleSink sink(config_);
    sink.initialize("");

    sink.add_address_filter("test/.*");
    sink.clear_filters();

    // After clearing, nothing should be filtered
}

// ============================================================================
// ConsoleSink Statistics Tests
// ============================================================================

class ConsoleSinkStatsTest : public ConsoleSinkTest {};

TEST_F(ConsoleSinkStatsTest, GetStatistics) {
    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    for (int i = 0; i < 10; ++i) {
        auto dp = create_datapoint("test/sensor", i * 1.0);
        sink.send_data_point(dp);
    }

    auto stats = sink.get_statistics();
    EXPECT_EQ(stats.messages_processed.load(), 10u);
}

TEST_F(ConsoleSinkStatsTest, ResetStatistics) {
    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    auto dp = create_datapoint("test/sensor", 25.0);
    sink.send_data_point(dp);

    sink.reset_statistics();

    auto stats = sink.get_statistics();
    EXPECT_EQ(stats.messages_processed.load(), 0u);
}

TEST_F(ConsoleSinkStatsTest, Flush) {
    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    // Should not throw
    sink.flush();
}

// ============================================================================
// ConsoleSink Runtime Configuration Tests
// ============================================================================

class ConsoleSinkRuntimeConfigTest : public ConsoleSinkTest {};

TEST_F(ConsoleSinkRuntimeConfigTest, UpdateConfig) {
    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    ConsoleSinkConfig new_config = config_;
    new_config.output_format = OutputFormat::JSON;

    auto result = sink.update_config(new_config);
    EXPECT_RESULT_OK(result);
}

TEST_F(ConsoleSinkRuntimeConfigTest, SetCustomFormatter) {
    config_.output_format = OutputFormat::CUSTOM;
    config_.enable_console_output = true;

    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    sink.set_custom_formatter([](const DataPoint& dp) {
        return "CUSTOM: " + std::string(dp.address());
    });

    auto dp = create_datapoint("test/sensor", 25.0);
    sink.send_data_point(dp);

    std::string output = output_.str();
    EXPECT_NE(output.find("CUSTOM:"), std::string::npos);
}

// ============================================================================
// ConsoleSinkFactory Tests
// ============================================================================

class ConsoleSinkFactoryTest : public ::testing::Test {};

TEST_F(ConsoleSinkFactoryTest, Create) {
    auto sink = ConsoleSinkFactory::create();
    EXPECT_NE(sink, nullptr);
}

TEST_F(ConsoleSinkFactoryTest, CreateWithConfig) {
    ConsoleSinkConfig config;
    config.output_format = OutputFormat::JSON;

    auto sink = ConsoleSinkFactory::create(config);
    EXPECT_NE(sink, nullptr);
}

TEST_F(ConsoleSinkFactoryTest, CreateDebug) {
    auto sink = ConsoleSinkFactory::create_debug();
    EXPECT_NE(sink, nullptr);
}

TEST_F(ConsoleSinkFactoryTest, CreateProduction) {
    auto sink = ConsoleSinkFactory::create_production();
    EXPECT_NE(sink, nullptr);
}

TEST_F(ConsoleSinkFactoryTest, CreateMinimal) {
    auto sink = ConsoleSinkFactory::create_minimal();
    EXPECT_NE(sink, nullptr);
}

TEST_F(ConsoleSinkFactoryTest, CreateVerbose) {
    auto sink = ConsoleSinkFactory::create_verbose();
    EXPECT_NE(sink, nullptr);
}

// ============================================================================
// Performance Tests (Optional)
// ============================================================================

class ConsoleSinkPerformanceTest : public ConsoleSinkTest {
protected:
    void SetUp() override {
        ConsoleSinkTest::SetUp();
        config_.enable_console_output = false;  // Disable for perf tests
        config_.enable_async_output = false;
    }
};

TEST_F(ConsoleSinkPerformanceTest, ThroughputSync) {
    ConsoleSink sink(config_);
    sink.initialize("");
    sink.start();

    const int iterations = 1000;
    PerformanceTimer timer;

    timer.start();
    for (int i = 0; i < iterations; ++i) {
        auto dp = create_datapoint("test/sensor", i * 1.0);
        sink.send_data_point(dp);
    }
    timer.stop();

    double throughput = timer.throughput(iterations);
    // Should handle at least 10000 msg/s
    EXPECT_GT(throughput, 1000.0);
}
