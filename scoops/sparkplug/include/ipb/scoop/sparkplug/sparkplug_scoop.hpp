#pragma once

/**
 * @file sparkplug_scoop.hpp
 * @brief Sparkplug B protocol scoop (data collector)
 *
 * Sparkplug B is an industrial IoT protocol built on top of MQTT that provides:
 * - Standardized topic namespace: spBv1.0/{group_id}/{message_type}/{edge_node_id}/{device_id}
 * - Protocol Buffers encoded payloads for efficient serialization
 * - Birth/Death certificates for online/offline state management
 * - Metric definitions with datatypes, timestamps, and metadata
 *
 * This scoop subscribes to Sparkplug B topics and converts incoming data
 * to IPB DataPoints for routing through the system.
 *
 * @see https://sparkplug.eclipse.org/
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"

namespace ipb::scoop::sparkplug {

//=============================================================================
// Sparkplug B Constants
//=============================================================================

/// Sparkplug B topic prefix
constexpr std::string_view SPARKPLUG_NAMESPACE = "spBv1.0";

/// Sparkplug B protocol version
constexpr uint8_t SPARKPLUG_VERSION = 3;

//=============================================================================
// Sparkplug B Message Types
//=============================================================================

/**
 * @brief Sparkplug B message types
 */
enum class MessageType {
    // Node messages
    NBIRTH,  ///< Node Birth Certificate - sent when a node comes online
    NDEATH,  ///< Node Death Certificate - sent when a node goes offline
    NDATA,   ///< Node Data - periodic/sporadic data from node
    NCMD,    ///< Node Command - command sent to node

    // Device messages
    DBIRTH,  ///< Device Birth Certificate - sent when a device comes online
    DDEATH,  ///< Device Death Certificate - sent when a device goes offline
    DDATA,   ///< Device Data - periodic/sporadic data from device
    DCMD,    ///< Device Command - command sent to device

    // State message
    STATE,  ///< Host application state

    UNKNOWN  ///< Unknown message type
};

/**
 * @brief Convert MessageType to string
 */
std::string_view message_type_to_string(MessageType type);

/**
 * @brief Parse MessageType from string
 */
MessageType string_to_message_type(std::string_view str);

//=============================================================================
// Sparkplug B Data Types
//=============================================================================

/**
 * @brief Sparkplug B metric data types
 */
enum class SparkplugDataType : uint32_t {
    Unknown  = 0,
    Int8     = 1,
    Int16    = 2,
    Int32    = 3,
    Int64    = 4,
    UInt8    = 5,
    UInt16   = 6,
    UInt32   = 7,
    UInt64   = 8,
    Float    = 9,
    Double   = 10,
    Boolean  = 11,
    String   = 12,
    DateTime = 13,
    Text     = 14,
    UUID     = 15,
    DataSet  = 16,
    Bytes    = 17,
    File     = 18,
    Template = 19,

    // Arrays (starting at 20)
    Int8Array     = 20,
    Int16Array    = 21,
    Int32Array    = 22,
    Int64Array    = 23,
    UInt8Array    = 24,
    UInt16Array   = 25,
    UInt32Array   = 26,
    UInt64Array   = 27,
    FloatArray    = 28,
    DoubleArray   = 29,
    BooleanArray  = 30,
    StringArray   = 31,
    DateTimeArray = 32
};

//=============================================================================
// Sparkplug B Metric
//=============================================================================

/**
 * @brief Sparkplug B Metric representation
 */
struct SparkplugMetric {
    std::string name;                ///< Metric name
    uint64_t alias             = 0;  ///< Metric alias (for efficient referencing)
    uint64_t timestamp         = 0;  ///< Timestamp in milliseconds since epoch
    SparkplugDataType datatype = SparkplugDataType::Unknown;
    bool is_historical         = false;  ///< Is this historical data?
    bool is_transient          = false;  ///< Is this transient (not persisted)?
    bool is_null               = false;  ///< Is value null?

    // Value (stored based on datatype)
    std::variant<bool, int8_t, int16_t, int32_t, int64_t, uint8_t, uint16_t, uint32_t, uint64_t,
                 float, double, std::string, std::vector<uint8_t>>
        value;

    // Metadata (optional)
    std::optional<std::string> description;
    std::optional<std::string> unit;
    std::optional<std::pair<double, double>> range;  // min, max

    /**
     * @brief Convert to IPB DataPoint
     */
    common::DataPoint to_data_point(const std::string& edge_node_id,
                                    const std::string& device_id = "") const;
};

//=============================================================================
// Sparkplug B Payload
//=============================================================================

/**
 * @brief Sparkplug B Payload (decoded)
 */
struct SparkplugPayload {
    uint64_t timestamp = 0;  ///< Payload timestamp
    uint64_t seq       = 0;  ///< Sequence number (0-255, wrapping)
    std::string uuid;        ///< Optional UUID
    std::vector<SparkplugMetric> metrics;

    /**
     * @brief Decode payload from protobuf binary
     */
    static std::optional<SparkplugPayload> decode(const std::vector<uint8_t>& data);

    /**
     * @brief Encode payload to protobuf binary
     */
    std::vector<uint8_t> encode() const;
};

//=============================================================================
// Sparkplug B Topic Parser
//=============================================================================

/**
 * @brief Parsed Sparkplug B topic
 */
struct SparkplugTopic {
    std::string group_id;
    MessageType message_type = MessageType::UNKNOWN;
    std::string edge_node_id;
    std::string device_id;  ///< Empty for node-level messages

    /**
     * @brief Parse a Sparkplug B topic string
     */
    static std::optional<SparkplugTopic> parse(const std::string& topic);

    /**
     * @brief Build a Sparkplug B topic string
     */
    std::string to_string() const;

    /**
     * @brief Check if this is a node-level message
     */
    bool is_node_message() const;

    /**
     * @brief Check if this is a device-level message
     */
    bool is_device_message() const;

    /**
     * @brief Check if this is a birth message
     */
    bool is_birth() const;

    /**
     * @brief Check if this is a death message
     */
    bool is_death() const;

    /**
     * @brief Check if this is a data message
     */
    bool is_data() const;

    /**
     * @brief Check if this is a command message
     */
    bool is_command() const;
};

//=============================================================================
// Sparkplug Scoop Configuration
//=============================================================================

/**
 * @brief Subscription filter for Sparkplug topics
 */
struct SubscriptionFilter {
    std::string group_id_pattern  = "#";  ///< Group ID filter (supports wildcards)
    std::string edge_node_pattern = "#";  ///< Edge node filter
    std::string device_pattern    = "#";  ///< Device filter (empty = node only)

    std::vector<MessageType> message_types;  ///< Message types to receive (empty = all)

    /**
     * @brief Build MQTT topic filters from this configuration
     */
    std::vector<std::string> to_mqtt_topics() const;
};

/**
 * @brief Sparkplug Scoop configuration
 */
struct SparkplugScoopConfig {
    // MQTT connection (uses shared transport)
    std::string connection_id = "sparkplug_default";
    transport::mqtt::ConnectionConfig mqtt_config;

    // Subscription filters
    std::vector<SubscriptionFilter> filters;

    // Processing options
    bool process_births  = true;  ///< Process birth certificates
    bool process_deaths  = true;  ///< Process death certificates
    bool process_data    = true;  ///< Process data messages
    bool ignore_commands = true;  ///< Ignore CMD messages (we're a scoop, not a host)

    // State tracking
    bool track_node_state     = true;  ///< Track online/offline state of nodes
    bool track_device_state   = true;  ///< Track online/offline state of devices
    bool track_metric_aliases = true;  ///< Track metric aliases from births

    // Data conversion
    bool include_metadata          = true;  ///< Include Sparkplug metadata in DataPoints
    bool use_fully_qualified_names = true;  ///< Use group/node/device/metric naming

    // Performance
    size_t message_queue_size = 10000;
    bool enable_statistics    = true;
    std::chrono::seconds statistics_interval{30};

    // Presets
    static SparkplugScoopConfig create_default();
    static SparkplugScoopConfig create_high_throughput();
    static SparkplugScoopConfig create_selective(const std::string& group_id);
};

//=============================================================================
// Sparkplug Scoop
//=============================================================================

/**
 * @brief Sparkplug B Protocol Scoop
 *
 * Subscribes to Sparkplug B topics on an MQTT broker and converts
 * incoming metrics to IPB DataPoints. Uses the shared MQTT transport
 * layer to avoid duplicating the MQTT client.
 *
 * Features:
 * - Sparkplug B v3.0 compliant
 * - Protocol Buffers decoding
 * - Birth/Death certificate tracking
 * - Metric alias resolution
 * - Node/Device state management
 * - Automatic reconnection via shared transport
 */
class SparkplugScoop : public common::IProtocolSourceBase {
public:
    static constexpr uint16_t PROTOCOL_ID               = 10;
    static constexpr std::string_view PROTOCOL_NAME     = "SparkplugB";
    static constexpr std::string_view COMPONENT_NAME    = "SparkplugScoop";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";

    /**
     * @brief Construct SparkplugScoop with configuration
     */
    explicit SparkplugScoop(
        const SparkplugScoopConfig& config = SparkplugScoopConfig::create_default());

    ~SparkplugScoop() override;

    // Non-copyable
    SparkplugScoop(const SparkplugScoop&)            = delete;
    SparkplugScoop& operator=(const SparkplugScoop&) = delete;

    //=========================================================================
    // IProtocolSourceBase Implementation
    //=========================================================================

    common::Result<common::DataSet> read() override;
    common::Result<common::DataSet> read_async() override;

    common::Result<void> subscribe(DataCallback data_cb, ErrorCallback error_cb) override;
    common::Result<void> unsubscribe() override;

    common::Result<void> add_address(std::string_view address) override;
    common::Result<void> remove_address(std::string_view address) override;
    std::vector<std::string> get_addresses() const override;

    common::Result<void> connect() override;
    common::Result<void> disconnect() override;
    bool is_connected() const noexcept override;

    uint16_t protocol_id() const noexcept override { return PROTOCOL_ID; }
    std::string_view protocol_name() const noexcept override { return PROTOCOL_NAME; }

    //=========================================================================
    // IIPBComponent Implementation
    //=========================================================================

    common::Result<void> start() override;
    common::Result<void> stop() override;
    bool is_running() const noexcept override;

    common::Result<void> configure(const common::ConfigurationBase& config) override;
    std::unique_ptr<common::ConfigurationBase> get_configuration() const override;

    common::Statistics get_statistics() const noexcept override;
    void reset_statistics() noexcept override;

    bool is_healthy() const noexcept override;
    std::string get_health_status() const override;

    std::string_view component_name() const noexcept override { return COMPONENT_NAME; }
    std::string_view component_version() const noexcept override { return COMPONENT_VERSION; }

    //=========================================================================
    // Sparkplug-Specific Methods
    //=========================================================================

    /**
     * @brief Get list of known online nodes
     */
    std::vector<std::string> get_online_nodes() const;

    /**
     * @brief Get list of known online devices for a node
     */
    std::vector<std::string> get_online_devices(const std::string& edge_node_id) const;

    /**
     * @brief Check if a node is online
     */
    bool is_node_online(const std::string& edge_node_id) const;

    /**
     * @brief Check if a device is online
     */
    bool is_device_online(const std::string& edge_node_id, const std::string& device_id) const;

    /**
     * @brief Get metrics for a node (from last birth)
     */
    std::vector<std::string> get_node_metrics(const std::string& edge_node_id) const;

    /**
     * @brief Get metrics for a device (from last birth)
     */
    std::vector<std::string> get_device_metrics(const std::string& edge_node_id,
                                                const std::string& device_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// Sparkplug Scoop Factory
//=============================================================================

/**
 * @brief Factory for creating SparkplugScoop instances
 */
class SparkplugScoopFactory {
public:
    /**
     * @brief Create default SparkplugScoop
     */
    static std::unique_ptr<SparkplugScoop> create(const std::string& broker_url);

    /**
     * @brief Create SparkplugScoop for specific group
     */
    static std::unique_ptr<SparkplugScoop> create_for_group(const std::string& broker_url,
                                                            const std::string& group_id);

    /**
     * @brief Create SparkplugScoop with full configuration
     */
    static std::unique_ptr<SparkplugScoop> create(const SparkplugScoopConfig& config);

    /**
     * @brief Create high-throughput SparkplugScoop
     */
    static std::unique_ptr<SparkplugScoop> create_high_throughput(const std::string& broker_url);
};

}  // namespace ipb::scoop::sparkplug
