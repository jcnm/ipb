#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <regex>
#include <sstream>
#include <thread>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"

namespace ipb::sink::console {

/**
 * @brief Output format options for console sink
 */
enum class OutputFormat {
    PLAIN,    ///< Simple text format
    JSON,     ///< JSON format
    CSV,      ///< Comma-separated values
    TABLE,    ///< Tabular format
    COLORED,  ///< Colored text format
    CUSTOM    ///< Custom format using callback
};

/**
 * @brief Console color codes for colored output
 */
enum class ConsoleColor {
    RESET          = 0,
    BLACK          = 30,
    RED            = 31,
    GREEN          = 32,
    YELLOW         = 33,
    BLUE           = 34,
    MAGENTA        = 35,
    CYAN           = 36,
    WHITE          = 37,
    BRIGHT_BLACK   = 90,
    BRIGHT_RED     = 91,
    BRIGHT_GREEN   = 92,
    BRIGHT_YELLOW  = 93,
    BRIGHT_BLUE    = 94,
    BRIGHT_MAGENTA = 95,
    BRIGHT_CYAN    = 96,
    BRIGHT_WHITE   = 97
};

/**
 * @brief Configuration for console sink
 */
struct ConsoleSinkConfig {
    // Output settings
    OutputFormat output_format  = OutputFormat::PLAIN;
    std::ostream* output_stream = &std::cout;
    std::string output_file_path;
    bool enable_file_output    = false;
    bool enable_console_output = true;

    // Formatting settings
    std::string timestamp_format = "%Y-%m-%d %H:%M:%S.%f";
    std::string field_separator  = " | ";
    std::string line_prefix;
    std::string line_suffix  = "\n";
    bool include_timestamp   = true;
    bool include_address     = true;
    bool include_protocol_id = true;
    bool include_quality     = true;
    bool include_value       = true;

    // Filtering settings
    bool enable_filtering = false;
    std::vector<std::string> address_filters;  // Regex patterns
    std::vector<uint16_t> protocol_id_filters;
    std::vector<common::Quality> quality_filters;

    // Color settings (for COLORED format)
    bool enable_colors                   = true;
    ConsoleColor timestamp_color         = ConsoleColor::CYAN;
    ConsoleColor address_color           = ConsoleColor::GREEN;
    ConsoleColor protocol_color          = ConsoleColor::BLUE;
    ConsoleColor quality_good_color      = ConsoleColor::GREEN;
    ConsoleColor quality_uncertain_color = ConsoleColor::YELLOW;
    ConsoleColor quality_bad_color       = ConsoleColor::RED;
    ConsoleColor value_color             = ConsoleColor::WHITE;

    // Performance settings
    bool enable_async_output = true;
    size_t queue_size        = 10000;
    std::chrono::milliseconds flush_interval{100};
    size_t batch_size = 100;

    // Custom formatting callback
    std::function<std::string(const common::DataPoint&)> custom_formatter;

    // Statistics settings
    bool enable_statistics = false;
    std::chrono::seconds statistics_interval{10};

    /**
     * @brief Create configuration for debug mode
     */
    static ConsoleSinkConfig create_debug() {
        ConsoleSinkConfig config;
        config.output_format       = OutputFormat::COLORED;
        config.enable_filtering    = true;
        config.enable_statistics   = true;
        config.enable_async_output = true;
        return config;
    }

    /**
     * @brief Create configuration for production mode
     */
    static ConsoleSinkConfig create_production() {
        ConsoleSinkConfig config;
        config.output_format       = OutputFormat::JSON;
        config.enable_colors       = false;
        config.enable_async_output = true;
        config.enable_statistics   = false;
        return config;
    }

    /**
     * @brief Create configuration for minimal output
     */
    static ConsoleSinkConfig create_minimal() {
        ConsoleSinkConfig config;
        config.output_format       = OutputFormat::PLAIN;
        config.include_timestamp   = false;
        config.include_protocol_id = false;
        config.include_quality     = false;
        config.enable_async_output = false;
        return config;
    }

    /**
     * @brief Create configuration for verbose output
     */
    static ConsoleSinkConfig create_verbose() {
        ConsoleSinkConfig config;
        config.output_format       = OutputFormat::TABLE;
        config.enable_statistics   = true;
        config.enable_async_output = true;
        config.statistics_interval = std::chrono::seconds{5};
        return config;
    }
};

/**
 * @brief Statistics for console sink performance monitoring
 */
struct ConsoleSinkStatistics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_filtered{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> bytes_written{0};
    std::atomic<uint64_t> flush_operations{0};

    std::chrono::steady_clock::time_point start_time;
    std::chrono::nanoseconds total_processing_time{0};
    std::chrono::nanoseconds min_processing_time{std::chrono::nanoseconds::max()};
    std::chrono::nanoseconds max_processing_time{0};

    mutable std::mutex stats_mutex;

    ConsoleSinkStatistics() : start_time(std::chrono::steady_clock::now()) {}

    /**
     * @brief Get messages per second
     */
    double get_messages_per_second() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return seconds > 0 ? static_cast<double>(messages_processed.load()) / seconds : 0.0;
    }

    /**
     * @brief Get average processing time
     */
    std::chrono::nanoseconds get_average_processing_time() const {
        auto processed = messages_processed.load();
        if (processed > 0) {
            return std::chrono::nanoseconds{total_processing_time.count() /
                                            static_cast<int64_t>(processed)};
        }
        return std::chrono::nanoseconds{0};
    }

    /**
     * @brief Update processing time statistics
     */
    void update_processing_time(std::chrono::nanoseconds processing_time) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        total_processing_time += processing_time;
        min_processing_time = std::min(min_processing_time, processing_time);
        max_processing_time = std::max(max_processing_time, processing_time);
    }

    /**
     * @brief Reset all statistics
     */
    void reset() {
        messages_processed = 0;
        messages_filtered  = 0;
        messages_dropped   = 0;
        bytes_written      = 0;
        flush_operations   = 0;
        start_time         = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(stats_mutex);
        total_processing_time = std::chrono::nanoseconds{0};
        min_processing_time   = std::chrono::nanoseconds::max();
        max_processing_time   = std::chrono::nanoseconds{0};
    }
};

/**
 * @brief High-performance console sink for debugging and monitoring
 */
class ConsoleSink : public common::ISink {
public:
    /**
     * @brief Constructor with configuration
     */
    explicit ConsoleSink(const ConsoleSinkConfig& config = ConsoleSinkConfig{});

    /**
     * @brief Destructor
     */
    ~ConsoleSink() override;

    // IIPBSink interface implementation
    common::Result<void> initialize(const std::string& config_path) override;
    common::Result<void> start() override;
    common::Result<void> stop() override;
    common::Result<void> shutdown() override;

    common::Result<void> send_data_point(const common::DataPoint& data_point) override;
    common::Result<void> send_data_set(const common::DataSet& data_set) override;

    bool is_connected() const override;
    bool is_healthy() const override;

    common::SinkMetrics get_metrics() const override;
    std::string get_sink_info() const override;

    // Console-specific methods

    /**
     * @brief Update configuration at runtime
     */
    common::Result<void> update_config(const ConsoleSinkConfig& new_config);

    /**
     * @brief Add address filter pattern
     */
    void add_address_filter(const std::string& pattern);

    /**
     * @brief Remove address filter pattern
     */
    void remove_address_filter(const std::string& pattern);

    /**
     * @brief Clear all filters
     */
    void clear_filters();

    /**
     * @brief Get current statistics
     */
    ConsoleSinkStatistics get_statistics() const;

    /**
     * @brief Reset statistics
     */
    void reset_statistics();

    /**
     * @brief Flush output immediately
     */
    void flush();

    /**
     * @brief Set custom formatter
     */
    void set_custom_formatter(std::function<std::string(const common::DataPoint&)> formatter);

private:
    ConsoleSinkConfig config_;
    ConsoleSinkStatistics statistics_;

    // Threading
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::thread worker_thread_;
    std::thread statistics_thread_;

    // Async processing
    std::queue<common::DataPoint> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_condition_;

    // Output streams
    std::unique_ptr<std::ofstream> file_stream_;
    mutable std::mutex output_mutex_;

    // Filtering
    std::vector<std::regex> address_regex_filters_;
    mutable std::mutex filter_mutex_;

    // Internal methods
    void worker_loop();
    void statistics_loop();

    bool should_filter_message(const common::DataPoint& data_point) const;
    std::string format_message(const common::DataPoint& data_point) const;

    std::string format_plain(const common::DataPoint& data_point) const;
    std::string format_json(const common::DataPoint& data_point) const;
    std::string format_csv(const common::DataPoint& data_point) const;
    std::string format_table(const common::DataPoint& data_point) const;
    std::string format_colored(const common::DataPoint& data_point) const;

    std::string format_timestamp(const common::Timestamp& timestamp) const;
    std::string format_value(const common::Value& value) const;
    std::string format_quality(common::Quality quality) const;

    std::string apply_color(const std::string& text, ConsoleColor color) const;

    void write_output(const std::string& message);
    void process_message_batch(const std::vector<common::DataPoint>& messages);

    void compile_address_filters();
    void print_statistics();
};

/**
 * @brief Factory for creating console sinks
 */
class ConsoleSinkFactory {
public:
    /**
     * @brief Create console sink with configuration
     */
    static std::unique_ptr<ConsoleSink> create(
        const ConsoleSinkConfig& config = ConsoleSinkConfig{});

    /**
     * @brief Create console sink from configuration file
     */
    static std::unique_ptr<ConsoleSink> create_from_file(const std::string& config_file);

    /**
     * @brief Create debug console sink
     */
    static std::unique_ptr<ConsoleSink> create_debug();

    /**
     * @brief Create production console sink
     */
    static std::unique_ptr<ConsoleSink> create_production();

    /**
     * @brief Create minimal console sink
     */
    static std::unique_ptr<ConsoleSink> create_minimal();

    /**
     * @brief Create verbose console sink
     */
    static std::unique_ptr<ConsoleSink> create_verbose();
};

}  // namespace ipb::sink::console
