#pragma once

/**
 * @file sparkplug_sink.hpp
 * @brief Sparkplug B protocol sink (data publisher)
 *
 * This sink publishes IPB DataPoints to an MQTT broker using the Sparkplug B
 * protocol specification. It acts as an Edge Node, publishing NBIRTH/NDEATH/NDATA
 * messages and optionally managing virtual devices with DBIRTH/DDEATH/DDATA.
 *
 * Key responsibilities:
 * - Manage Edge Node lifecycle (NBIRTH on connect, NDEATH via LWT)
 * - Convert IPB DataPoints to Sparkplug B metrics
 * - Batch metrics into efficient NDATA/DDATA messages
 * - Track sequence numbers per specification
 * - Handle rebirth requests from Host Applications
 *
 * Uses the shared MQTT transport layer to avoid duplicating the MQTT client.
 *
 * @see https://sparkplug.eclipse.org/
 */

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "ipb/common/data_point.hpp"
#include "ipb/common/dataset.hpp"
#include "ipb/common/interfaces.hpp"
#include "ipb/transport/mqtt/mqtt_connection.hpp"

namespace ipb::sink::sparkplug {

// Forward declarations from scoop (shared types)
namespace types {

/**
 * @brief Sparkplug B data types
 */
enum class DataType : uint32_t {
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
    Template = 19
};

}  // namespace types

//=============================================================================
// Metric Definition
//=============================================================================

/**
 * @brief Sparkplug metric definition for birth certificates
 *
 * Defines the schema of a metric that will be published. Used in NBIRTH/DBIRTH
 * to declare what metrics the node/device will publish.
 */
struct MetricDefinition {
    std::string name;          ///< Metric name (e.g., "Temperature/Zone1")
    types::DataType datatype;  ///< Data type
    uint64_t alias = 0;        ///< Alias (auto-assigned if 0)

    // Optional metadata
    std::optional<std::string> description;
    std::optional<std::string> unit;
    std::optional<double> min_value;
    std::optional<double> max_value;

    // Properties
    bool is_transient  = false;  ///< Not persisted by host
    bool is_historical = false;  ///< Can be historical data

    /**
     * @brief Create from IPB DataPoint (infer type)
     */
    static MetricDefinition from_data_point(const common::DataPoint& dp);
};

//=============================================================================
// Device Configuration
//=============================================================================

/**
 * @brief Virtual device configuration
 *
 * Represents a logical device under the Edge Node. Each device has its own
 * birth/death/data lifecycle within the Sparkplug namespace.
 */
struct DeviceConfig {
    std::string device_id;                  ///< Unique device identifier
    std::vector<MetricDefinition> metrics;  ///< Metrics this device publishes

    // Filtering - which DataPoints belong to this device
    std::string address_prefix;          ///< Address prefix to match
    std::vector<std::string> protocols;  ///< Protocol IDs to match (empty = all)
};

//=============================================================================
// Edge Node Configuration
//=============================================================================

/**
 * @brief Sparkplug Edge Node configuration
 */
struct EdgeNodeConfig {
    std::string group_id;      ///< Sparkplug group ID
    std::string edge_node_id;  ///< Edge node identifier

    // Node metrics (published in NBIRTH)
    std::vector<MetricDefinition> node_metrics;

    // Virtual devices under this node
    std::vector<DeviceConfig> devices;

    // Behavior
    bool auto_discover_metrics = true;  ///< Auto-discover metrics from DataPoints
    bool publish_bdseq         = true;  ///< Include bdSeq metric
    bool publish_node_control  = true;  ///< Include Node Control/* metrics
};

//=============================================================================
// Sparkplug Sink Configuration
//=============================================================================

/**
 * @brief Publishing behavior configuration
 */
struct PublishConfig {
    // QoS settings
    transport::mqtt::QoS data_qos  = transport::mqtt::QoS::AT_MOST_ONCE;
    transport::mqtt::QoS birth_qos = transport::mqtt::QoS::AT_LEAST_ONCE;
    transport::mqtt::QoS death_qos = transport::mqtt::QoS::AT_LEAST_ONCE;

    // Batching
    bool enable_batching = true;
    size_t batch_size    = 100;  ///< Max metrics per NDATA/DDATA
    std::chrono::milliseconds batch_timeout{1000};

    // Alias usage
    bool use_aliases_in_data = true;  ///< Use aliases instead of names in DATA messages

    // Compression
    bool enable_compression = false;  ///< Compress payload (non-standard extension)

    // Timing
    bool include_timestamps       = true;  ///< Include timestamps in metrics
    bool use_datapoint_timestamps = true;  ///< Use DataPoint timestamps (vs current time)
};

/**
 * @brief Host Application awareness
 */
struct HostConfig {
    bool enable_host_awareness = false;     ///< Listen for STATE messages
    std::string primary_host_id;            ///< Expected primary host application ID
    std::chrono::seconds host_timeout{30};  ///< Time to wait for host STATE

    // Rebirth handling
    bool auto_rebirth_on_request = true;  ///< Respond to rebirth requests
};

/**
 * @brief Complete Sparkplug Sink configuration
 */
struct SparkplugSinkConfig {
    // MQTT connection (uses shared transport)
    std::string connection_id = "sparkplug_sink_default";
    transport::mqtt::ConnectionConfig mqtt_config;

    // Edge Node configuration
    EdgeNodeConfig edge_node;

    // Publishing behavior
    PublishConfig publishing;

    // Host awareness
    HostConfig host;

    // Performance
    size_t message_queue_size = 10000;
    size_t worker_threads     = 2;

    // Monitoring
    bool enable_statistics = true;
    std::chrono::seconds statistics_interval{30};

    // Validation
    bool is_valid() const;
    std::string validation_error() const;

    // Presets
    static SparkplugSinkConfig create_default(const std::string& group_id,
                                              const std::string& edge_node_id);
    static SparkplugSinkConfig create_high_throughput(const std::string& group_id,
                                                      const std::string& edge_node_id);
    static SparkplugSinkConfig create_reliable(const std::string& group_id,
                                               const std::string& edge_node_id);
};

//=============================================================================
// Sparkplug Sink Statistics
//=============================================================================

/**
 * @brief Sparkplug Sink statistics (internal atomic counters)
 */
struct SparkplugSinkStatisticsInternal {
    // Message counts
    std::atomic<uint64_t> births_sent{0};
    std::atomic<uint64_t> deaths_sent{0};
    std::atomic<uint64_t> data_messages_sent{0};
    std::atomic<uint64_t> metrics_published{0};

    // Errors
    std::atomic<uint64_t> publish_failures{0};
    std::atomic<uint64_t> encode_failures{0};

    // Sequence tracking
    std::atomic<uint64_t> sequence_number{0};
    std::atomic<uint64_t> birth_death_sequence{0};

    // Performance
    std::atomic<uint64_t> bytes_sent{0};

    void reset() {
        births_sent        = 0;
        deaths_sent        = 0;
        data_messages_sent = 0;
        metrics_published  = 0;
        publish_failures   = 0;
        encode_failures    = 0;
        bytes_sent         = 0;
    }
};

/**
 * @brief Sparkplug Sink statistics (copyable snapshot)
 */
struct SparkplugSinkStatistics {
    uint64_t births_sent         = 0;
    uint64_t deaths_sent         = 0;
    uint64_t data_messages_sent  = 0;
    uint64_t metrics_published   = 0;
    uint64_t publish_failures    = 0;
    uint64_t encode_failures     = 0;
    uint64_t sequence_number     = 0;
    uint64_t birth_death_sequence = 0;
    uint64_t bytes_sent          = 0;

    static SparkplugSinkStatistics from_internal(const SparkplugSinkStatisticsInternal& internal) {
        SparkplugSinkStatistics stats;
        stats.births_sent         = internal.births_sent.load();
        stats.deaths_sent         = internal.deaths_sent.load();
        stats.data_messages_sent  = internal.data_messages_sent.load();
        stats.metrics_published   = internal.metrics_published.load();
        stats.publish_failures    = internal.publish_failures.load();
        stats.encode_failures     = internal.encode_failures.load();
        stats.sequence_number     = internal.sequence_number.load();
        stats.birth_death_sequence = internal.birth_death_sequence.load();
        stats.bytes_sent          = internal.bytes_sent.load();
        return stats;
    }
};

//=============================================================================
// Sparkplug Sink
//=============================================================================

/**
 * @brief Sparkplug B Protocol Sink
 *
 * Publishes IPB DataPoints as Sparkplug B messages to an MQTT broker.
 * Acts as an Edge Node in the Sparkplug topology.
 *
 * Lifecycle:
 * 1. start() -> Connects to broker, publishes NBIRTH (and DBIRTH for devices)
 * 2. send_data_point() -> Accumulates metrics, publishes NDATA/DDATA
 * 3. stop() -> NDEATH is published via MQTT Last Will (set up at connect)
 *
 * Features:
 * - Sparkplug B v3.0 compliant
 * - Protocol Buffers encoding
 * - Automatic sequence number management
 * - Metric aliasing for bandwidth efficiency
 * - Virtual device support
 * - Batching for high throughput
 * - Host Application awareness (optional)
 */
class SparkplugSink : public common::IIPBSinkBase {
public:
    static constexpr uint16_t PROTOCOL_ID               = 10;
    static constexpr std::string_view PROTOCOL_NAME     = "SparkplugB";
    static constexpr std::string_view COMPONENT_NAME    = "SparkplugSink";
    static constexpr std::string_view COMPONENT_VERSION = "1.0.0";

    /**
     * @brief Construct SparkplugSink with configuration
     */
    explicit SparkplugSink(const SparkplugSinkConfig& config);

    ~SparkplugSink() override;

    // Non-copyable
    SparkplugSink(const SparkplugSink&)            = delete;
    SparkplugSink& operator=(const SparkplugSink&) = delete;

    //=========================================================================
    // IIPBSinkBase Implementation
    //=========================================================================

    common::Result<void> write(const common::DataPoint& data_point) override;
    common::Result<void> write_batch(std::span<const common::DataPoint> data_points) override;
    common::Result<void> write_dataset(const common::DataSet& dataset) override;

    std::future<common::Result<void>> write_async(const common::DataPoint& data_point) override;
    std::future<common::Result<void>> write_batch_async(std::span<const common::DataPoint> data_points) override;

    common::Result<void> flush() override;
    size_t pending_count() const noexcept override;
    bool can_accept_data() const noexcept override;

    std::string_view sink_type() const noexcept override { return PROTOCOL_NAME; }
    size_t max_batch_size() const noexcept override;

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
     * @brief Manually trigger a rebirth (NBIRTH + all DBIRTHs)
     */
    common::Result<void> rebirth();

    /**
     * @brief Add a new metric definition to the node
     *
     * Note: Requires rebirth to take effect
     */
    void add_node_metric(const MetricDefinition& metric);

    /**
     * @brief Add a new virtual device
     *
     * Note: Will trigger DBIRTH for the new device
     */
    common::Result<void> add_device(const DeviceConfig& device);

    /**
     * @brief Remove a virtual device
     *
     * Note: Will trigger DDEATH for the device
     */
    common::Result<void> remove_device(const std::string& device_id);

    /**
     * @brief Get current sequence number
     */
    uint64_t get_sequence_number() const noexcept;

    /**
     * @brief Get current bdSeq (birth/death sequence)
     */
    uint64_t get_bdseq() const noexcept;

    /**
     * @brief Check if connected to broker
     */
    bool is_connected() const noexcept;

    /**
     * @brief Get Sparkplug-specific statistics
     */
    SparkplugSinkStatistics get_sparkplug_statistics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

//=============================================================================
// Sparkplug Sink Factory
//=============================================================================

/**
 * @brief Factory for creating SparkplugSink instances
 */
class SparkplugSinkFactory {
public:
    /**
     * @brief Create basic SparkplugSink
     */
    static std::unique_ptr<SparkplugSink> create(const std::string& broker_url,
                                                 const std::string& group_id,
                                                 const std::string& edge_node_id);

    /**
     * @brief Create SparkplugSink with devices
     */
    static std::unique_ptr<SparkplugSink> create_with_devices(
        const std::string& broker_url, const std::string& group_id, const std::string& edge_node_id,
        const std::vector<DeviceConfig>& devices);

    /**
     * @brief Create SparkplugSink with full configuration
     */
    static std::unique_ptr<SparkplugSink> create(const SparkplugSinkConfig& config);

    /**
     * @brief Create high-throughput SparkplugSink
     */
    static std::unique_ptr<SparkplugSink> create_high_throughput(const std::string& broker_url,
                                                                 const std::string& group_id,
                                                                 const std::string& edge_node_id);

    /**
     * @brief Create reliable SparkplugSink (QoS 1 for all)
     */
    static std::unique_ptr<SparkplugSink> create_reliable(const std::string& broker_url,
                                                          const std::string& group_id,
                                                          const std::string& edge_node_id);
};

}  // namespace ipb::sink::sparkplug
