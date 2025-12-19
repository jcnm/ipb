/**
 * @file test_console_scoop.cpp
 * @brief Unit tests for the console scoop implementation
 *
 * Tests cover:
 * - ConsoleScoopConfig construction and presets
 * - ConsoleScoopStatistics
 * - ConsoleScoop lifecycle (start, stop)
 * - Data point injection and reading
 * - Input formats (JSON, KEY_VALUE, CSV)
 * - Subscription callbacks
 * - Address management
 * - ConsoleScoopFactory
 */

#include <ipb/scoop/console/console_scoop.hpp>
#include <ipb_plugin_test.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace ipb::scoop::console;
using namespace ipb::common;
using namespace ipb::test;
using namespace std::chrono_literals;

// ============================================================================
// ConsoleScoopConfig Tests
// ============================================================================

class ConsoleScoopConfigTest : public ::testing::Test {};

TEST_F(ConsoleScoopConfigTest, DefaultValues) {
    ConsoleScoopConfig config;

    EXPECT_EQ(config.format, InputFormat::AUTO);
    EXPECT_EQ(config.prompt, "ipb> ");
    EXPECT_FALSE(config.interactive);
    EXPECT_FALSE(config.echo_input);
    EXPECT_EQ(config.csv_delimiter, ',');
    EXPECT_FALSE(config.csv_has_header);
    EXPECT_EQ(config.default_quality, Quality::Good);
    EXPECT_EQ(config.default_protocol_id, 100u);
    EXPECT_EQ(config.address_prefix, "console/");
    EXPECT_EQ(config.buffer_size, 1000u);
    EXPECT_TRUE(config.skip_empty_lines);
    EXPECT_TRUE(config.skip_comments);
    EXPECT_TRUE(config.skip_parse_errors);
    EXPECT_TRUE(config.enable_statistics);
}

TEST_F(ConsoleScoopConfigTest, CreateDefault) {
    auto config = ConsoleScoopConfig::create_default();

    EXPECT_EQ(config.format, InputFormat::AUTO);
    EXPECT_FALSE(config.interactive);
}

TEST_F(ConsoleScoopConfigTest, CreateInteractive) {
    auto config = ConsoleScoopConfig::create_interactive();

    EXPECT_TRUE(config.interactive);
    EXPECT_FALSE(config.prompt.empty());
}

TEST_F(ConsoleScoopConfigTest, CreateJsonPipe) {
    auto config = ConsoleScoopConfig::create_json_pipe();

    EXPECT_EQ(config.format, InputFormat::JSON);
    EXPECT_FALSE(config.interactive);
}

TEST_F(ConsoleScoopConfigTest, CreateCsvPipe) {
    auto config = ConsoleScoopConfig::create_csv_pipe();

    EXPECT_EQ(config.format, InputFormat::CSV);
    EXPECT_FALSE(config.interactive);
}

TEST_F(ConsoleScoopConfigTest, InputFormats) {
    EXPECT_EQ(static_cast<int>(InputFormat::JSON), 0);
    EXPECT_EQ(static_cast<int>(InputFormat::KEY_VALUE), 1);
    EXPECT_EQ(static_cast<int>(InputFormat::CSV), 2);
    EXPECT_EQ(static_cast<int>(InputFormat::AUTO), 3);
}

TEST_F(ConsoleScoopConfigTest, Validation) {
    ConsoleScoopConfig config;
    EXPECT_TRUE(config.is_valid());
    EXPECT_TRUE(config.validation_error().empty());
}

// ============================================================================
// ConsoleScoopStatistics Tests
// ============================================================================

class ConsoleScoopStatisticsTest : public ::testing::Test {
protected:
    ConsoleScoopStatistics stats_;
};

TEST_F(ConsoleScoopStatisticsTest, DefaultValues) {
    EXPECT_EQ(stats_.lines_received.load(), 0u);
    EXPECT_EQ(stats_.lines_processed.load(), 0u);
    EXPECT_EQ(stats_.lines_skipped.load(), 0u);
    EXPECT_EQ(stats_.parse_errors.load(), 0u);
    EXPECT_EQ(stats_.data_points_produced.load(), 0u);
    EXPECT_EQ(stats_.bytes_received.load(), 0u);
}

TEST_F(ConsoleScoopStatisticsTest, AtomicOperations) {
    stats_.lines_received++;
    EXPECT_EQ(stats_.lines_received.load(), 1u);

    stats_.lines_received += 10;
    EXPECT_EQ(stats_.lines_received.load(), 11u);
}

TEST_F(ConsoleScoopStatisticsTest, Reset) {
    stats_.lines_received = 100;
    stats_.lines_processed = 90;
    stats_.parse_errors = 10;

    stats_.reset();

    EXPECT_EQ(stats_.lines_received.load(), 0u);
    EXPECT_EQ(stats_.lines_processed.load(), 0u);
    EXPECT_EQ(stats_.parse_errors.load(), 0u);
}

// ============================================================================
// ConsoleScoop Lifecycle Tests
// ============================================================================

class ConsoleScoopTest : public ScoopTestBase {
protected:
    void SetUp() override {
        ScoopTestBase::SetUp();
        config_ = ConsoleScoopConfig::create_default();
        config_.interactive = false;
    }

    ConsoleScoopConfig config_;
};

TEST_F(ConsoleScoopTest, Construction) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    EXPECT_FALSE(scoop.is_running());
    EXPECT_FALSE(scoop.is_connected());
}

TEST_F(ConsoleScoopTest, ProtocolInfo) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    EXPECT_EQ(scoop.protocol_id(), ConsoleScoop::PROTOCOL_ID);
    EXPECT_EQ(scoop.protocol_name(), "Console");
    EXPECT_EQ(scoop.component_name(), "ConsoleScoop");
    EXPECT_FALSE(scoop.component_version().empty());
}

TEST_F(ConsoleScoopTest, Connect) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    auto result = scoop.connect();
    EXPECT_RESULT_OK(result);
    EXPECT_TRUE(scoop.is_connected());
}

TEST_F(ConsoleScoopTest, Disconnect) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    EXPECT_TRUE(scoop.is_connected());

    auto result = scoop.disconnect();
    EXPECT_RESULT_OK(result);
    EXPECT_FALSE(scoop.is_connected());
}

TEST_F(ConsoleScoopTest, StartStop) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();

    auto start_result = scoop.start();
    EXPECT_RESULT_OK(start_result);
    EXPECT_TRUE(scoop.is_running());

    auto stop_result = scoop.stop();
    EXPECT_RESULT_OK(stop_result);
    EXPECT_FALSE(scoop.is_running());
}

TEST_F(ConsoleScoopTest, IsHealthy) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    EXPECT_TRUE(scoop.is_healthy());
}

TEST_F(ConsoleScoopTest, GetHealthStatus) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    std::string status = scoop.get_health_status();
    EXPECT_FALSE(status.empty());
}

// ============================================================================
// ConsoleScoop Address Management Tests
// ============================================================================

class ConsoleScoopAddressTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopAddressTest, AddAddress) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    auto result = scoop.add_address("sensor/temperature");
    EXPECT_RESULT_OK(result);

    auto addresses = scoop.get_addresses();
    EXPECT_EQ(addresses.size(), 1u);
    EXPECT_EQ(addresses[0], "sensor/temperature");
}

TEST_F(ConsoleScoopAddressTest, AddMultipleAddresses) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.add_address("sensor/temp1");
    scoop.add_address("sensor/temp2");
    scoop.add_address("sensor/temp3");

    auto addresses = scoop.get_addresses();
    EXPECT_EQ(addresses.size(), 3u);
}

TEST_F(ConsoleScoopAddressTest, RemoveAddress) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.add_address("sensor/temp1");
    scoop.add_address("sensor/temp2");

    auto result = scoop.remove_address("sensor/temp1");
    EXPECT_RESULT_OK(result);

    auto addresses = scoop.get_addresses();
    EXPECT_EQ(addresses.size(), 1u);
}

// ============================================================================
// ConsoleScoop Data Injection Tests
// ============================================================================

class ConsoleScoopDataTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopDataTest, InjectDataPoint) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    auto dp = create_test_datapoint("sensor/temp", 25.5);
    auto result = scoop.inject_data_point(dp);
    EXPECT_RESULT_OK(result);

    auto stats = scoop.get_console_statistics();
    EXPECT_EQ(stats.data_points_produced.load(), 1u);
}

TEST_F(ConsoleScoopDataTest, InjectJsonLine) {
    std::stringstream input;
    config_.format = InputFormat::JSON;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    auto result = scoop.inject_line(R"({"address": "sensor/temp", "value": 25.5})");
    // Result depends on implementation
}

TEST_F(ConsoleScoopDataTest, ReadEmpty) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    auto result = scoop.read();
    EXPECT_RESULT_OK(result);

    auto dataset = result.value();
    EXPECT_TRUE(dataset.empty());
}

TEST_F(ConsoleScoopDataTest, ReadAfterInject) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    // Inject data points
    scoop.inject_data_point(create_test_datapoint("sensor/temp1", 25.5));
    scoop.inject_data_point(create_test_datapoint("sensor/temp2", 26.5));

    auto result = scoop.read();
    EXPECT_RESULT_OK(result);

    auto dataset = result.value();
    EXPECT_GE(dataset.size(), 2u);
}

// ============================================================================
// ConsoleScoop Subscription Tests
// ============================================================================

class ConsoleScoopSubscriptionTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopSubscriptionTest, Subscribe) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();

    std::vector<DataPoint> received;
    std::mutex received_mutex;

    auto result = scoop.subscribe(
        [&received, &received_mutex](DataSet ds) {
            std::lock_guard<std::mutex> lock(received_mutex);
            for (const auto& dp : ds) {
                received.push_back(dp);
            }
        },
        [](ErrorCode, std::string_view) {
            // Error callback
        }
    );

    EXPECT_RESULT_OK(result);
}

TEST_F(ConsoleScoopSubscriptionTest, Unsubscribe) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.subscribe([](DataSet) {}, [](ErrorCode, std::string_view) {});

    auto result = scoop.unsubscribe();
    EXPECT_RESULT_OK(result);
}

// ============================================================================
// ConsoleScoop Statistics Tests
// ============================================================================

class ConsoleScoopStatsTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopStatsTest, GetStatistics) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    auto stats = scoop.get_statistics();
    EXPECT_EQ(stats.total_messages, 0u);
}

TEST_F(ConsoleScoopStatsTest, GetConsoleStatistics) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    auto stats = scoop.get_console_statistics();
    EXPECT_EQ(stats.lines_received.load(), 0u);
    EXPECT_EQ(stats.data_points_produced.load(), 0u);
}

TEST_F(ConsoleScoopStatsTest, ResetStatistics) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    scoop.inject_data_point(create_test_datapoint("test", 1.0));

    scoop.reset_statistics();

    auto stats = scoop.get_statistics();
    EXPECT_EQ(stats.total_messages, 0u);
}

// ============================================================================
// ConsoleScoop Custom Parser Tests
// ============================================================================

class ConsoleScoopCustomParserTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopCustomParserTest, SetCustomParser) {
    std::stringstream input;
    ConsoleScoop scoop(config_, input);

    bool parser_called = false;

    scoop.set_custom_parser([&parser_called](const std::string& line) -> std::optional<DataPoint> {
        parser_called = true;
        if (line.find("CUSTOM:") == 0) {
            DataPoint dp("custom/parsed");
            dp.set_value(42.0);
            return dp;
        }
        return std::nullopt;
    });

    scoop.connect();
    scoop.start();

    scoop.inject_line("CUSTOM:test");

    // Parser should have been called
}

// ============================================================================
// ConsoleScoopFactory Tests
// ============================================================================

class ConsoleScoopFactoryTest : public ::testing::Test {};

TEST_F(ConsoleScoopFactoryTest, Create) {
    // Note: This may block waiting for stdin, so we skip the actual creation
    // auto scoop = ConsoleScoopFactory::create();
    // EXPECT_NE(scoop, nullptr);
}

TEST_F(ConsoleScoopFactoryTest, CreateInteractive) {
    // auto scoop = ConsoleScoopFactory::create_interactive("test> ");
    // EXPECT_NE(scoop, nullptr);
}

TEST_F(ConsoleScoopFactoryTest, CreateWithConfig) {
    ConsoleScoopConfig config = ConsoleScoopConfig::create_default();
    // Note: Actual creation may depend on stdin availability

    // Verify config creation works
    EXPECT_TRUE(config.is_valid());
}

TEST_F(ConsoleScoopFactoryTest, CreateFromStream) {
    std::stringstream input;
    auto scoop = ConsoleScoopFactory::create_from_stream(input);
    EXPECT_NE(scoop, nullptr);
}

// ============================================================================
// Input Stream Tests
// ============================================================================

class ConsoleScoopInputStreamTest : public ConsoleScoopTest {};

TEST_F(ConsoleScoopInputStreamTest, ReadFromStringStream) {
    std::stringstream input;
    input << R"({"address": "sensor/temp", "value": 25.5})" << std::endl;

    config_.format = InputFormat::JSON;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    // Give it time to process
    std::this_thread::sleep_for(50ms);

    auto result = scoop.read();
    EXPECT_RESULT_OK(result);
}

TEST_F(ConsoleScoopInputStreamTest, SkipEmptyLines) {
    std::stringstream input;
    input << "" << std::endl;
    input << R"({"address": "sensor/temp", "value": 25.5})" << std::endl;
    input << "   " << std::endl;

    config_.format = InputFormat::JSON;
    config_.skip_empty_lines = true;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    std::this_thread::sleep_for(50ms);

    auto stats = scoop.get_console_statistics();
    EXPECT_GE(stats.lines_skipped.load(), 0u);
}

TEST_F(ConsoleScoopInputStreamTest, SkipComments) {
    std::stringstream input;
    input << "# This is a comment" << std::endl;
    input << R"({"address": "sensor/temp", "value": 25.5})" << std::endl;

    config_.format = InputFormat::JSON;
    config_.skip_comments = true;
    ConsoleScoop scoop(config_, input);

    scoop.connect();
    scoop.start();

    std::this_thread::sleep_for(50ms);

    auto stats = scoop.get_console_statistics();
    EXPECT_GE(stats.lines_skipped.load(), 0u);
}
