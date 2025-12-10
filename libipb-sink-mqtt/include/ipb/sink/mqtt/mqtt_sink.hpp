#pragma once

/**
 * @file mqtt_sink.hpp
 * @brief Generic MQTT sink for publishing IPB DataPoints
 *
 * Uses the shared MQTT transport layer to avoid duplicating the MQTT client.
 */

#include "ipb/common/interfaces.hpp"
#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/endpoint.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"

#include <json/json.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace ipb::sink::mqtt {

// Use shared transport QoS
using QoS = transport::mqtt::QoS;

// MQTT message format types
enum class MQTTMessageFormat {
    JSON,           // Standard JSON format
    JSON_COMPACT,   // Compact JSON without whitespace
    BINARY,         // Binary protobuf format
    CSV,            // Comma-separated values
    INFLUX_LINE,    // InfluxDB line protocol
    CUSTOM          // Custom format via callback
};

// Use shared transport security mode
using SecurityMode = transport::mqtt::SecurityMode;

// MQTT topic strategy
enum class MQTTTopicStrategy {
    SINGLE_TOPIC,       // All messages to one topic
    PROTOCOL_BASED,     // Topic per protocol
    ADDRESS_BASED,      // Topic per address
    HIERARCHICAL,       // Hierarchical topic structure
    CUSTOM              // Custom topic via callback
};

// Use shared transport connection configuration
using ConnectionConfig = transport::mqtt::ConnectionConfig;

// MQTT message configuration
struct MQTTMessageConfig {
    MQTTMessageFormat format = MQTTMessageFormat::JSON;
    QoS qos = QoS::AT_LEAST_ONCE;
    bool retain = false;
    bool enable_compression = false;
    std::string compression_algorithm = "gzip";
    
    // Topic configuration
    MQTTTopicStrategy topic_strategy = MQTTTopicStrategy::SINGLE_TOPIC;
    std::string base_topic = "ipb/data";
    std::string topic_separator = "/";
    
    // Message content
    bool include_timestamp = true;
    bool include_quality = true;
    bool include_protocol_info = true;
    bool include_metadata = false;
    
    // Custom formatters (optional)
    std::function<std::string(const common::DataPoint&)> custom_formatter;
    std::function<std::string(const common::DataPoint&)> custom_topic_generator;
};

// MQTT performance configuration
struct MQTTPerformanceConfig {
    // Batching
    bool enable_batching = true;
    size_t batch_size = 100;
    std::chrono::milliseconds batch_timeout{1000};
    size_t max_batch_size = 1000;
    
    // Async processing
    bool enable_async = true;
    size_t queue_size = 10000;
    size_t thread_pool_size = 2;
    std::chrono::milliseconds flush_interval{100};
    
    // Memory management
    bool enable_memory_pool = true;
    size_t memory_pool_size = 1024 * 1024; // 1MB
    bool enable_zero_copy = true;
    
    // Flow control
    size_t max_inflight_messages = 1000;
    std::chrono::milliseconds publish_timeout{30000};
    bool enable_backpressure = true;
    size_t backpressure_threshold = 8000; // 80% of queue
};

// MQTT monitoring configuration
struct MQTTMonitoringConfig {
    bool enable_statistics = true;
    std::chrono::seconds statistics_interval{30};
    bool enable_health_checks = true;
    std::chrono::seconds health_check_interval{10};
    
    // Metrics
    bool track_message_rates = true;
    bool track_latency = true;
    bool track_errors = true;
    bool track_connection_status = true;
    
    // Alerting
    bool enable_alerting = false;
    std::string alert_topic = "ipb/alerts";
    double max_error_rate = 0.05; // 5%
    std::chrono::milliseconds max_latency{1000};
};

// Complete MQTT sink configuration
struct MQTTSinkConfig {
    // Shared transport connection (uses MQTTConnectionManager)
    std::string connection_id = "mqtt_sink_default";
    ConnectionConfig connection;

    MQTTMessageConfig messages;
    MQTTPerformanceConfig performance;
    MQTTMonitoringConfig monitoring;

    // Sink identification
    std::string sink_id = "mqtt_sink";
    std::string description = "MQTT Sink for IPB";

    // Presets
    static MQTTSinkConfig create_high_throughput();
    static MQTTSinkConfig create_low_latency();
    static MQTTSinkConfig create_reliable();
    static MQTTSinkConfig create_minimal();
};

// MQTT sink statistics
struct MQTTSinkStatistics {
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_failed{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> connection_attempts{0};
    std::atomic<uint64_t> connection_failures{0};
    std::atomic<uint64_t> reconnections{0};
    
    // Timing statistics
    mutable std::mutex timing_mutex;
    std::vector<std::chrono::nanoseconds> publish_times;
    
    // Connection status
    std::atomic<bool> is_connected{false};
    std::atomic<std::chrono::system_clock::time_point> last_connection_time;
    std::atomic<std::chrono::system_clock::time_point> last_message_time;
    
    void reset();
    void update_publish_time(std::chrono::nanoseconds time);
    std::chrono::nanoseconds get_average_publish_time() const;
    std::chrono::nanoseconds get_p95_publish_time() const;
    std::chrono::nanoseconds get_p99_publish_time() const;
    double get_message_rate() const;
    double get_error_rate() const;
};

// MQTT Sink implementation
class MQTTSink : public common::IIPBSink {
public:
    explicit MQTTSink(const MQTTSinkConfig& config = MQTTSinkConfig{});
    virtual ~MQTTSink();

    // IIPBSink interface implementation
    common::Result<void> initialize(const std::string& config_path = "") override;
    common::Result<void> start() override;
    common::Result<void> stop() override;
    common::Result<void> shutdown() override;
    
    bool is_connected() const override;
    bool is_healthy() const override;
    
    common::Result<void> send_data_point(const common::DataPoint& data_point) override;
    common::Result<void> send_data_set(const common::DataSet& data_set) override;
    
    common::SinkMetrics get_metrics() const override;
    std::string get_sink_info() const override;

    // MQTT-specific methods
    common::Result<void> configure(const MQTTSinkConfig& config);
    common::Result<void> publish_message(const std::string& topic,
                                        const std::string& payload,
                                        QoS qos = QoS::AT_LEAST_ONCE,
                                        bool retain = false);
    
    // Statistics and monitoring
    MQTTSinkStatistics get_statistics() const;
    void reset_statistics();
    void print_statistics() const;
    
    // Configuration management
    MQTTSinkConfig get_configuration() const { return config_; }
    common::Result<void> update_configuration(const MQTTSinkConfig& config);
    
    // Topic management
    std::string generate_topic(const common::DataPoint& data_point) const;
    std::string format_message(const common::DataPoint& data_point) const;
    std::string format_batch_message(const common::DataSet& data_set) const;

private:
    // Configuration
    MQTTSinkConfig config_;

    // Shared MQTT connection (via MQTTConnectionManager)
    std::shared_ptr<transport::mqtt::MQTTConnection> connection_;

    // State management
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> connected_{false};

    // Async processing
    std::queue<common::DataPoint> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> worker_threads_;

    // Batching
    std::vector<common::DataPoint> current_batch_;
    std::mutex batch_mutex_;
    std::thread batch_thread_;
    std::chrono::steady_clock::time_point last_batch_time_;

    // Statistics
    mutable MQTTSinkStatistics statistics_;
    std::thread statistics_thread_;

    // Memory management
    std::unique_ptr<char[]> memory_pool_;
    std::atomic<size_t> memory_pool_offset_{0};

    // Internal methods
    void worker_loop();
    void batch_loop();
    void statistics_loop();

    common::Result<void> connect_to_broker();
    common::Result<void> disconnect_from_broker();

    void handle_connection_state(transport::mqtt::ConnectionState state, const std::string& reason);
    void handle_delivery_complete(int token, bool success, const std::string& error);
    
    common::Result<void> publish_data_point_internal(const common::DataPoint& data_point);
    common::Result<void> publish_batch_internal(const std::vector<common::DataPoint>& batch);
    
    void flush_current_batch();
    bool should_flush_batch() const;
    
    // Formatting helpers
    Json::Value data_point_to_json(const common::DataPoint& data_point) const;
    std::string data_point_to_csv(const common::DataPoint& data_point) const;
    std::string data_point_to_influx_line(const common::DataPoint& data_point) const;
    
    // Topic generation helpers
    std::string generate_single_topic() const;
    std::string generate_protocol_topic(const common::DataPoint& data_point) const;
    std::string generate_address_topic(const common::DataPoint& data_point) const;
    std::string generate_hierarchical_topic(const common::DataPoint& data_point) const;
    
    // Utility methods
    std::string sanitize_topic_component(const std::string& component) const;
    std::vector<uint8_t> compress_data(const std::string& data) const;
    std::string decompress_data(const std::vector<uint8_t>& compressed_data) const;
    
    // Health check
    bool perform_health_check() const;
    void update_health_status();
};

// MQTT Sink Factory
class MQTTSinkFactory {
public:
    static std::unique_ptr<MQTTSink> create_high_throughput(
        const std::string& broker_url,
        const std::string& base_topic = "ipb/data"
    );
    
    static std::unique_ptr<MQTTSink> create_low_latency(
        const std::string& broker_url,
        const std::string& base_topic = "ipb/data"
    );
    
    static std::unique_ptr<MQTTSink> create_reliable(
        const std::string& broker_url,
        const std::string& base_topic = "ipb/data"
    );
    
    static std::unique_ptr<MQTTSink> create_secure(
        const std::string& broker_url,
        const std::string& ca_cert_path,
        const std::string& client_cert_path,
        const std::string& client_key_path,
        const std::string& base_topic = "ipb/data"
    );
    
    static std::unique_ptr<MQTTSink> create(const MQTTSinkConfig& config);
};

} // namespace ipb::sink::mqtt

