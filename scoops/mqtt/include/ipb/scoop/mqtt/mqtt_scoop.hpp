#pragma once

/**
 * @file mqtt_scoop.hpp
 * @brief Generic MQTT protocol scoop (data collector)
 *
 * This scoop subscribes to MQTT topics and converts incoming messages
 * to IPB DataPoints for routing through the system.
 *
 * Unlike SparkplugScoop which handles the Sparkplug B protocol specifically,
 * this scoop handles generic MQTT messages with configurable payload parsing.
 *
 * Uses the shared MQTT transport layer to avoid duplicating the MQTT client.
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <regex>
#include <string>
#include <vector>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"

namespace ipb::scoop::mqtt {

//=============================================================================
// Payload Format
//=============================================================================

/**
 * @brief Supported payload formats for incoming MQTT messages
 */
enum class PayloadFormat {
    RAW,            ///< Raw bytes/string - value is the payload as-is
    JSON,           ///< JSON format - extract value from JSON field
    JSON_ARRAY,     ///< JSON array - each element becomes a DataPoint
    CSV,            ///< CSV format - parse as comma-separated values
    INFLUX_LINE,    ///< InfluxDB line protocol
    BINARY_FLOAT,   ///< Binary 32-bit float (little-endian)
    BINARY_DOUBLE,  ///< Binary 64-bit double (little-endian)
    BINARY_INT32,   ///< Binary 32-bit integer (little-endian)
    BINARY_INT64,   ///< Binary 64-bit integer (little-endian)
    CUSTOM          ///< Custom parser via callback
};

//=============================================================================
// Topic Mapping
//=============================================================================

/**
 * @brief Mapping rule from MQTT topic to IPB address
 */
struct TopicMapping {
    std::string topic_pattern;     ///< MQTT topic filter (supports + and # wildcards)
    std::string address_template;  ///< IPB address template (e.g., "mqtt/{topic}")
    PayloadFormat format = PayloadFormat::RAW;

    // JSON-specific options
    std::string json_value_path     = "value";  ///< JSON path to value field
    std::string json_timestamp_path = "";  ///< JSON path to timestamp (empty = use receive time)
    std::string json_quality_path   = "";  ///< JSON path to quality field

    // Metadata
    uint16_t protocol_id = 0;  ///< Override protocol ID (0 = use default)

    /**
     * @brief Check if a topic matches this mapping
     */
    bool matches(const std::string& topic) const;

    /**
     * @brief Generate IPB address from MQTT topic
     */
    std::string generate_address(const std::string& topic) const;
};

//=============================================================================
// MQTT Scoop Configuration
//=============================================================================

/**
 * @brief Subscription configuration
 */
struct SubscriptionConfig {
    std::vector<TopicMapping> mappings;  ///< Topic-to-address mappings
    transport::mqtt::QoS default_qos = transport::mqtt::QoS::AT_LEAST_ONCE;

    // Filtering
    size_t max_payload_size = 1024 * 1024;  ///< Max payload size (1MB default)
    bool ignore_retained    = false;        ///< Ignore retained messages
};

/**
 * @brief Processing configuration
 */
struct ProcessingConfig {
    // Timestamps
    bool use_broker_timestamp  = false;  ///< Use broker timestamp if available
    bool use_payload_timestamp = true;   ///< Extract timestamp from payload if possible

    // Quality
    common::Quality default_quality = common::Quality::Good;

    // Buffering
    bool enable_buffering = true;
    size_t buffer_size    = 10000;  ///< Max buffered DataPoints
    std::chrono::milliseconds flush_interval{100};

    // Error handling
    bool skip_parse_errors  = true;  ///< Skip messages that fail to parse
    size_t max_parse_errors = 100;   ///< Max errors before unhealthy
};

/**
 * @brief Complete MQTT Scoop configuration
 */
struct MQTTScoopConfig {
    // MQTT connection (uses shared transport)
    std::string connection_id = "mqtt_scoop_default";
    transport::mqtt::ConnectionConfig mqtt_config;

    // Subscription
    SubscriptionConfig subscription;

    // Processing
    ProcessingConfig processing;

    // Monitoring
    bool enable_statistics = true;
    std::chrono::seconds statistics_interval{30};

    // Validation
    bool is_valid() const;
    std::string validation_error() const;

    // Presets
    static MQTTScoopConfig create_default();
    static MQTTScoopConfig create_high_throughput();
    static MQTTScoopConfig create_json_topics(const std::vector<std::string>& topics);
};

//=============================================================================
// MQTT Scoop Statistics
//=============================================================================

/**
 * @brief MQTT Scoop statistics
 */
struct MQTTScoopStatistics {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> parse_errors{0};
    std::atomic<uint64_t> data_points_produced{0};
    std::atomic<uint64_t> bytes_received{0};

    std::atomic<uint64_t> subscriptions_active{0};

    void reset() {
        messages_received    = 0;
        messages_processed   = 0;
        messages_dropped     = 0;
        parse_errors         = 0;
        data_points_produced = 0;
        bytes_received       = 0;
    }
};

//=============================================================================
// Custom Parser Callback
//=============================================================================

/**
 * @brief Custom payload parser callback type
 *
 * @param topic The MQTT topic
 * @param payload The raw payload bytes
 * @return Vector of DataPoints extracted from payload, or empty on error
 */
using CustomParserCallback = std::function<std::vector<common::DataPoint>(
    const std::string& topic, const std::vector<uint8_t>& payload)>;

//=============================================================================
// MQTT Scoop
//=============================================================================

/**
 * @brief Generic MQTT Protocol Scoop
 *
 * Subscribes to MQTT topics and converts incoming messages to IPB DataPoints.
 * Uses the shared MQTT transport layer to avoid duplicating the MQTT client.
 *
 * Features:
 * - Flexible topic-to-address mapping
 * - Multiple payload format support (JSON, CSV, binary, etc.)
 * - Custom parser support for proprietary formats
 * - Shared MQTT connection via MQTTConnectionManager
 * - Buffered async delivery
 */
class MQTTScoop : public common::IProtocolSourceBase {
public:
    static constexpr uint16_t PROTOCOL_ID               = 1;
    static constexpr std::string_view PROTOCOL_NAME     = "MQTT";
    static constexpr std::string_view COMPONENT_NAME    = "MQTTScoop";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";

    /**
     * @brief Construct MQTTScoop with configuration
     */
    explicit MQTTScoop(const MQTTScoopConfig& config = MQTTScoopConfig::create_default());

    ~MQTTScoop() override;

    // Non-copyable
    MQTTScoop(const MQTTScoop&)            = delete;
    MQTTScoop& operator=(const MQTTScoop&) = delete;

    //=========================================================================
    // IProtocolSourceBase Implementation
    //=========================================================================

    common::Result<common::DataSet> read() override;
    common::Result<common::DataSet> read_async() override;

    common::Result<> subscribe(DataCallback data_cb, ErrorCallback error_cb) override;
    common::Result<> unsubscribe() override;

    common::Result<> add_address(std::string_view address) override;
    common::Result<> remove_address(std::string_view address) override;
    std::vector<std::string> get_addresses() const override;

    common::Result<> connect() override;
    common::Result<> disconnect() override;
    bool is_connected() const noexcept override;

    uint16_t protocol_id() const noexcept override { return PROTOCOL_ID; }
    std::string_view protocol_name() const noexcept override { return PROTOCOL_NAME; }

    //=========================================================================
    // IIPBComponent Implementation
    //=========================================================================

    common::Result<> start() override;
    common::Result<> stop() override;
    bool is_running() const noexcept override;

    common::Result<> configure(const common::ConfigurationBase& config) override;
    std::unique_ptr<common::ConfigurationBase> get_configuration() const override;

    common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;

    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }

    //=========================================================================
    // MQTT-Specific Methods
    //=========================================================================

    /**
     * @brief Add a topic mapping dynamically
     */
    common::Result<> add_topic_mapping(const TopicMapping& mapping);

    /**
     * @brief Remove a topic mapping
     */
    common::Result<> remove_topic_mapping(const std::string& topic_pattern);

    /**
     * @brief Get active topic mappings
     */
    std::vector<TopicMapping> get_topic_mappings() const;

    /**
     * @brief Set custom parser for CUSTOM format
     */
    void set_custom_parser(CustomParserCallback parser);

    /**
     * @brief Get MQTT-specific statistics
     */
    MQTTScoopStatistics get_mqtt_statistics() const;

    /**
     * @brief Get list of subscribed topics
     */
    std::vector<std::string> get_subscribed_topics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// MQTT Scoop Factory
//=============================================================================

/**
 * @brief Factory for creating MQTTScoop instances
 */
class MQTTScoopFactory {
public:
    /**
     * @brief Create basic MQTTScoop
     */
    static std::unique_ptr<MQTTScoop> create(const std::string& broker_url);

    /**
     * @brief Create MQTTScoop for specific topics
     */
    static std::unique_ptr<MQTTScoop> create_for_topics(const std::string& broker_url,
                                                        const std::vector<std::string>& topics);

    /**
     * @brief Create MQTTScoop with JSON parsing
     */
    static std::unique_ptr<MQTTScoop> create_json(const std::string& broker_url,
                                                  const std::vector<std::string>& topics,
                                                  const std::string& value_path = "value");

    /**
     * @brief Create MQTTScoop with full configuration
     */
    static std::unique_ptr<MQTTScoop> create(const MQTTScoopConfig& config);

    /**
     * @brief Create high-throughput MQTTScoop
     */
    static std::unique_ptr<MQTTScoop> create_high_throughput(const std::string& broker_url);
};

}  // namespace ipb::scoop::mqtt
